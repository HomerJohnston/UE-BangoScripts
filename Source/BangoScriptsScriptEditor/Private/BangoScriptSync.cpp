#include "BangoScriptSync.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "BangoScriptAsset.h"
#include "ContentBrowserMenuContexts.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "SourceControlHelpers.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "BangoScriptSync"

namespace BangoScriptSyncPrivate
{
	const TCHAR* Extension = TEXT(".bango");
	/** How long a changed file must be quiet before it's handled: editors often save in steps (a temporary file, then a rename). */
	constexpr double QuietSeconds = 1.0;

	/** Saves waiting for Play in Editor to end. */
	TArray<TWeakObjectPtr<UBangoScriptAsset>> SavesAfterPIE;

	void Notify(const FText& Message, bool bSuccess = true)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	bool IsBangoFile(const FString& Path)
	{
		return Path.EndsWith(Extension, ESearchCase::IgnoreCase);
	}

	/** What's at a package: its script asset (loaded if bLoad), or whether something else is there. */
	struct FFoundAsset
	{
		FAssetData Data;
		bool bScript = false;
		bool bOther = false;
	};

	FFoundAsset FindAsset(const FString& PackageName)
	{
		FFoundAsset Found;
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPackageName(FName(*PackageName), Assets, /*bIncludeOnlyOnDiskAssets*/ false);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.IsInstanceOf(UBangoScriptAsset::StaticClass()))
			{
				Found.Data = Asset;
				Found.bScript = true;
			}
			else if (!Asset.IsRedirector())
			{
				Found.bOther = true;
			}
		}
		return Found;
	}

	bool IsLinked(const FAssetData& Data)
	{
		FString Value;
		return Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UBangoScriptAsset, bLinked), Value) && Value.Equals(TEXT("True"), ESearchCase::IgnoreCase);
	}

	FString GetHashTag(const FAssetData& Data)
	{
		FString Value;
		Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UBangoScriptAsset, SourceFileHash), Value);
		return Value;
	}

	/** Every .bango file under Folder, skipping World Partition's per-actor folders (which can hold huge numbers of files). */
	void FindBangoFiles(const FString& Folder, TArray<FString>& OutFiles)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TArray<FString> Folders;
		Folders.Add(Folder);
		while (Folders.Num() > 0)
		{
			const FString Current = Folders.Pop();
			PlatformFile.IterateDirectory(*Current, [&Folders, &OutFiles](const TCHAR* Path, bool bIsDirectory)
			{
				const FString Item(Path);
				if (bIsDirectory)
				{
					if (!FPaths::GetCleanFilename(Item).StartsWith(TEXT("__External")))
					{
						Folders.Add(Item);
					}
				}
				else if (IsBangoFile(Item))
				{
					OutFiles.Add(Item);
				}
				return true;
			});
		}
	}
}

// ---- Files and assets ----

FString FBangoScriptSync::GetLinkedFilePath(const FString& PackageName)
{
	return FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(PackageName, BangoScriptSyncPrivate::Extension));
}

bool FBangoScriptSync::GetPackageNameForFile(const FString& FilePath, FString& OutPackageName)
{
	FString Path = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(Path);
	if (!BangoScriptSyncPrivate::IsBangoFile(Path))
	{
		return false;
	}
	Path = Path.LeftChop(FCString::Strlen(BangoScriptSyncPrivate::Extension));
	FString Reason;
	return FPackageName::TryConvertFilenameToLongPackageName(Path, OutPackageName, &Reason);
}

FString FBangoScriptSync::HashFile(const FString& FilePath)
{
	const FMD5Hash Hash = FMD5Hash::HashFile(*FilePath);
	return Hash.IsValid() ? LexToString(Hash) : FString();
}

bool FBangoScriptSync::WriteScriptFile(const FString& FilePath, const FString& Text, FString& OutError)
{
	const bool bSourceControl = ISourceControlModule::Get().IsEnabled();
	const bool bExisted = FPaths::FileExists(FilePath);
	if (bSourceControl && bExisted)
	{
		USourceControlHelpers::CheckOutOrAddFile(FilePath, /*bSilent*/ true);
	}
	if (bExisted && IFileManager::Get().IsReadOnly(*FilePath))
	{
		OutError = FString::Printf(TEXT("%s is read-only (check it out, or make it writable)."), *FilePath);
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Couldn't write %s."), *FilePath);
		return false;
	}
	if (bSourceControl && !bExisted)
	{
		USourceControlHelpers::CheckOutOrAddFile(FilePath, /*bSilent*/ true);
	}
	return true;
}

bool FBangoScriptSync::RefreshFromFile(UBangoScriptAsset& Asset)
{
	const FString FilePath = GetLinkedFilePath(Asset.GetScriptName());
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		return false;
	}
	Asset.Modify();
	Asset.Source = Text; // the runtime compiles the new text on the script's next spawn, or hot reloads it
	Asset.bLinked = true;
	Asset.SourceFileHash = HashFile(FilePath);
	Asset.MarkPackageDirty();
	return true;
}

bool FBangoScriptSync::ExportToFile(UBangoScriptAsset& Asset)
{
	const FString FilePath = GetLinkedFilePath(Asset.GetScriptName());
	FString Existing;
	if (FFileHelper::LoadFileToString(Existing, *FilePath) && !Existing.Equals(Asset.Source, ESearchCase::CaseSensitive))
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("ExportOverwrite", "{0} is already there, with different text. Replace it with this script's text?\n\n(No keeps the file, and links the script to it: the file's text becomes the script.)"),
			FText::FromString(FilePath)));
		if (Answer != EAppReturnType::Yes)
		{
			return RefreshFromFile(Asset);
		}
	}
	FString Error;
	if (!WriteScriptFile(FilePath, Asset.Source, Error))
	{
		BangoScriptSyncPrivate::Notify(FText::FromString(Error), false);
		return false;
	}
	Asset.Modify();
	Asset.bLinked = true;
	Asset.SourceFileHash = HashFile(FilePath);
	Asset.MarkPackageDirty();
	return true;
}

void FBangoScriptSync::Unlink(UBangoScriptAsset& Asset)
{
	Asset.Modify();
	Asset.bLinked = false;
	Asset.SourceFileHash.Reset();
	Asset.MarkPackageDirty();
}

UBangoScriptAsset* FBangoScriptSync::ImportFile(const FString& FilePath, const FString& PackageName)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		return nullptr;
	}
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}
	const FName AssetName(*FPackageName::GetLongPackageAssetName(PackageName));
	UBangoScriptAsset* Asset = NewObject<UBangoScriptAsset>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Asset->Source = Text;
	Asset->bLinked = true;
	Asset->SourceFileHash = HashFile(FilePath);
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	return Asset;
}

void FBangoScriptSync::SaveAssets(const TArray<UBangoScriptAsset*>& Assets)
{
	TArray<UPackage*> Packages;
	for (UBangoScriptAsset* Asset : Assets)
	{
		if (!Asset)
		{
			continue;
		}
		if (GEditor && GEditor->PlayWorld)
		{
			BangoScriptSyncPrivate::SavesAfterPIE.AddUnique(Asset); // packages aren't saved while playing
			continue;
		}
		Packages.AddUnique(Asset->GetOutermost());
	}
	if (Packages.Num() > 0)
	{
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
	}
}

// ---- Starting up ----

void FBangoScriptSync::Startup()
{
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.OnAssetRenamed().AddRaw(this, &FBangoScriptSync::OnAssetRenamed);
	Registry.OnInMemoryAssetCreated().AddRaw(this, &FBangoScriptSync::OnInMemoryAssetCreated);
	Registry.OnAssetRemoved().AddRaw(this, &FBangoScriptSync::OnAssetRemoved);
	FEditorDelegates::OnAssetsPreDelete.AddRaw(this, &FBangoScriptSync::OnAssetsPreDelete);
	FEditorDelegates::EndPIE.AddRaw(this, &FBangoScriptSync::OnEndPIE);

	// Watch Content for .bango files being added, changed or deleted.
	WatchedFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FDirectoryWatcherModule& Watchers = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if (IDirectoryWatcher* Watcher = Watchers.Get())
	{
		Watcher->RegisterDirectoryChangedCallback_Handle(WatchedFolder,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FBangoScriptSync::OnDirectoryChanged), WatcherHandle);
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FBangoScriptSync::OnTick), 0.25f);

	// Once every asset is known, look for .bango files that are new or changed since the editor last ran.
	if (Registry.IsLoadingAssets())
	{
		Registry.OnFilesLoaded().AddRaw(this, &FBangoScriptSync::OnFilesLoaded);
	}
	else
	{
		OnFilesLoaded();
	}

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBangoScriptSync::RegisterMenus));
	bStarted = true;
}

void FBangoScriptSync::Shutdown()
{
	if (!bStarted)
	{
		return;
	}
	bStarted = false;
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		Registry->OnAssetRenamed().RemoveAll(this);
		Registry->OnInMemoryAssetCreated().RemoveAll(this);
		Registry->OnAssetRemoved().RemoveAll(this);
		Registry->OnFilesLoaded().RemoveAll(this);
	}
	FEditorDelegates::OnAssetsPreDelete.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	if (FDirectoryWatcherModule* Watchers = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Watchers->Get())
		{
			Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedFolder, WatcherHandle);
		}
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FBangoScriptSync::OnFilesLoaded()
{
	IAssetRegistry::GetChecked().OnFilesLoaded().RemoveAll(this);
	StartScan();
}

void FBangoScriptSync::StartScan()
{
	// Listing Content and hashing the .bango files happens on another thread, so a big project doesn't stall the editor.
	const FString Folder = WatchedFolder;
	Async(EAsyncExecution::ThreadPool, [this, Folder]()
	{
		TArray<FString> Files;
		BangoScriptSyncPrivate::FindBangoFiles(Folder, Files);
		TArray<TPair<FString, FString>> Hashed;
		for (const FString& File : Files)
		{
			Hashed.Emplace(File, HashFile(File));
		}
		AsyncTask(ENamedThreads::GameThread, [this, Hashed = MoveTemp(Hashed)]() mutable
		{
			if (bStarted)
			{
				OnScanFinished(MoveTemp(Hashed));
			}
		});
	});
}

void FBangoScriptSync::OnScanFinished(TArray<TPair<FString, FString>> Files)
{
	using namespace BangoScriptSyncPrivate;
	TArray<UBangoScriptAsset*> ToSave;
	TArray<FString> Imported;
	TArray<FString> Refreshed;
	TSet<FString> PackagesWithFiles;
	for (const TPair<FString, FString>& File : Files)
	{
		FString PackageName;
		if (!GetPackageNameForFile(File.Key, PackageName))
		{
			continue;
		}
		PackagesWithFiles.Add(PackageName);
		const FFoundAsset Found = FindAsset(PackageName);
		if (!Found.bScript)
		{
			if (Found.bOther)
			{
				UE_LOG(LogTemp, Warning, TEXT("Bango: %s isn't imported: another kind of asset has its name (%s)."), *File.Key, *PackageName);
			}
			else if (UBangoScriptAsset* Asset = ImportFile(File.Key, PackageName))
			{
				ToSave.Add(Asset);
				Imported.Add(PackageName);
			}
			continue;
		}
		// A linked script whose file changed while the editor was closed (an embedded one ignores the file).
		if (IsLinked(Found.Data) && !GetHashTag(Found.Data).Equals(File.Value))
		{
			if (UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Found.Data.GetAsset()))
			{
				if (RefreshFromFile(*Asset))
				{
					ToSave.Add(Asset);
					Refreshed.Add(PackageName);
				}
			}
		}
	}

	// Linked scripts whose file is gone become embedded.
	TArray<FString> Unlinked;
	TArray<FAssetData> Scripts;
	IAssetRegistry::GetChecked().GetAssetsByClass(UBangoScriptAsset::StaticClass()->GetClassPathName(), Scripts);
	for (const FAssetData& Script : Scripts)
	{
		const FString PackageName = Script.PackageName.ToString();
		if (IsLinked(Script) && !PackagesWithFiles.Contains(PackageName) && !FPaths::FileExists(GetLinkedFilePath(PackageName)))
		{
			if (UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Script.GetAsset()))
			{
				Unlink(*Asset);
				ToSave.Add(Asset);
				Unlinked.Add(PackageName);
			}
		}
	}

	SaveAssets(ToSave);
	if (Imported.Num() > 0)
	{
		Notify(Imported.Num() == 1
			? FText::Format(LOCTEXT("ImportedOne", "Imported {0}.bango as a Bango Script."), FText::FromString(Imported[0]))
			: FText::Format(LOCTEXT("ImportedMany", "Imported {0} .bango files as Bango Scripts."), FText::AsNumber(Imported.Num())));
	}
	for (const FString& PackageName : Refreshed)
	{
		UE_LOG(LogTemp, Log, TEXT("Bango: %s was refreshed from its .bango file, which changed while the editor was closed."), *PackageName);
	}
	for (const FString& PackageName : Unlinked)
	{
		Notify(FText::Format(LOCTEXT("UnlinkedMissing", "{0}.bango is gone, so {0} is now an embedded script (its text is kept in the asset)."), FText::FromString(PackageName)), false);
	}
}

// ---- Watching Content ----

void FBangoScriptSync::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	const double Now = FPlatformTime::Seconds();
	for (const FFileChangeData& Change : Changes)
	{
		if (Change.Action == FFileChangeData::FCA_RescanRequired)
		{
			StartScan(); // the watcher lost track: look at everything again
			continue;
		}
		if (BangoScriptSyncPrivate::IsBangoFile(Change.Filename))
		{
			FString Path = FPaths::ConvertRelativePathToFull(Change.Filename);
			FPaths::NormalizeFilename(Path);
			PendingChanges.Add(Path, Now);
		}
	}
}

bool FBangoScriptSync::OnTick(float DeltaTime)
{
	if (PendingChanges.Num() == 0)
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	TArray<FString> Ready;
	for (const TPair<FString, double>& Pending : PendingChanges)
	{
		if (Now - Pending.Value >= BangoScriptSyncPrivate::QuietSeconds)
		{
			Ready.Add(Pending.Key);
		}
	}
	for (const FString& File : Ready)
	{
		PendingChanges.Remove(File);
		HandleFileChange(File);
	}
	return true;
}

void FBangoScriptSync::HandleFileChange(const FString& FilePath)
{
	using namespace BangoScriptSyncPrivate;
	FString PackageName;
	if (!GetPackageNameForFile(FilePath, PackageName))
	{
		return;
	}
	const FFoundAsset Found = FindAsset(PackageName);
	if (FPaths::FileExists(FilePath))
	{
		if (!Found.bScript)
		{
			// New: import it (unless the engine's own auto import already did, or another kind of asset has the name).
			if (!Found.bOther)
			{
				if (UBangoScriptAsset* Asset = ImportFile(FilePath, PackageName))
				{
					SaveAssets({ Asset });
					Notify(FText::Format(LOCTEXT("ImportedNew", "Imported {0}.bango as a Bango Script."), FText::FromString(PackageName)));
				}
			}
			return;
		}
		UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Found.Data.GetAsset());
		if (Asset && Asset->bLinked && !HashFile(FilePath).Equals(Asset->SourceFileHash))
		{
			// Changed by something else (another editor): the file wins.
			if (RefreshFromFile(*Asset))
			{
				SaveAssets({ Asset });
			}
		}
		return;
	}

	// Deleted: a linked script keeps its text and becomes embedded.
	if (Found.bScript)
	{
		UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Found.Data.GetAsset());
		if (Asset && Asset->bLinked)
		{
			Unlink(*Asset);
			SaveAssets({ Asset });
			Notify(FText::Format(LOCTEXT("UnlinkedDeleted", "{0}.bango was deleted, so {0} is now an embedded script (its text is kept in the asset)."), FText::FromString(PackageName)), false);
		}
	}
}

// ---- Moving, copying and deleting assets ----

void FBangoScriptSync::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	if (!AssetData.IsInstanceOf(UBangoScriptAsset::StaticClass()))
	{
		return;
	}
	const UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(AssetData.GetAsset());
	if (!Asset || !Asset->bLinked)
	{
		return;
	}
	// Its file moves with it, so they stay side by side.
	const FString OldFile = GetLinkedFilePath(FPackageName::ObjectPathToPackageName(OldObjectPath));
	const FString NewFile = GetLinkedFilePath(AssetData.PackageName.ToString());
	if (OldFile.Equals(NewFile, ESearchCase::IgnoreCase) || !FPaths::FileExists(OldFile))
	{
		return;
	}
	if (FPaths::FileExists(NewFile))
	{
		BangoScriptSyncPrivate::Notify(FText::Format(LOCTEXT("MoveBlocked", "{0} couldn't be moved with its script: {1} is already there."),
			FText::FromString(OldFile), FText::FromString(NewFile)), false);
		return;
	}
	if (!IFileManager::Get().Move(*NewFile, *OldFile, /*bReplace*/ false))
	{
		BangoScriptSyncPrivate::Notify(FText::Format(LOCTEXT("MoveFailed", "Couldn't move {0} to {1}."), FText::FromString(OldFile), FText::FromString(NewFile)), false);
		return;
	}
	if (ISourceControlModule::Get().IsEnabled())
	{
		USourceControlHelpers::MarkFileForDelete(OldFile, /*bSilent*/ true);
		USourceControlHelpers::CheckOutOrAddFile(NewFile, /*bSilent*/ true);
	}
}

void FBangoScriptSync::OnInMemoryAssetCreated(UObject* Object)
{
	// A copy of a linked script (Duplicate) needs its own file: written once the editor's done with it, so a move
	// (which may also report the asset as created) has moved the file first.
	TWeakObjectPtr<UBangoScriptAsset> Created = Cast<UBangoScriptAsset>(Object);
	if (!Created.IsValid() || !Created->bLinked)
	{
		return;
	}
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Created](float)
	{
		if (UBangoScriptAsset* Asset = Created.Get())
		{
			const FString FilePath = GetLinkedFilePath(Asset->GetScriptName());
			if (Asset->bLinked && !FPaths::FileExists(FilePath))
			{
				FString Error;
				if (WriteScriptFile(FilePath, Asset->Source, Error))
				{
					Asset->SourceFileHash = HashFile(FilePath);
					Asset->MarkPackageDirty();
				}
				else
				{
					BangoScriptSyncPrivate::Notify(FText::FromString(Error), false);
				}
			}
		}
		return false; // once
	}), 0.5f);
}

void FBangoScriptSync::OnAssetsPreDelete(const TArray<UObject*>& Objects)
{
	FilesToDeleteWithAssets.Reset();
	TMap<FString, FString> Linked;
	for (const UObject* Object : Objects)
	{
		const UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Object);
		if (Asset && Asset->bLinked)
		{
			const FString FilePath = GetLinkedFilePath(Asset->GetScriptName());
			if (FPaths::FileExists(FilePath))
			{
				Linked.Add(Asset->GetScriptName(), FilePath);
			}
		}
	}
	if (Linked.Num() == 0)
	{
		return;
	}
	TArray<FString> Names;
	Linked.GenerateValueArray(Names);
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
		LOCTEXT("DeleteLinkedFiles", "Also delete the .bango file of each linked script being deleted?\n\n{0}\n\nA .bango file that's kept will be imported again, as a new script, the next time the editor starts."),
		FText::FromString(FString::Join(Names, TEXT("\n")))));
	if (Answer == EAppReturnType::Yes)
	{
		FilesToDeleteWithAssets = MoveTemp(Linked); // deleted once the assets really are (the delete can still be cancelled)
	}
}

void FBangoScriptSync::OnAssetRemoved(const FAssetData& AssetData)
{
	FString FilePath;
	if (!FilesToDeleteWithAssets.RemoveAndCopyValue(AssetData.PackageName.ToString(), FilePath))
	{
		return;
	}
	if (ISourceControlModule::Get().IsEnabled())
	{
		USourceControlHelpers::MarkFileForDelete(FilePath, /*bSilent*/ true);
	}
	if (FPaths::FileExists(FilePath))
	{
		IFileManager::Get().Delete(*FilePath, /*bRequireExists*/ false, /*bEvenReadOnly*/ true);
	}
}

void FBangoScriptSync::OnEndPIE(bool bIsSimulating)
{
	// After the editor's done ending the session.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		TArray<UBangoScriptAsset*> Assets;
		for (const TWeakObjectPtr<UBangoScriptAsset>& Asset : BangoScriptSyncPrivate::SavesAfterPIE)
		{
			if (Asset.IsValid())
			{
				Assets.Add(Asset.Get());
			}
		}
		BangoScriptSyncPrivate::SavesAfterPIE.Reset();
		SaveAssets(Assets);
		return false;
	}), 0.5f);
}

// ---- The asset's right-click commands ----

void FBangoScriptSync::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UBangoScriptAsset::StaticClass());
	if (!Menu)
	{
		return;
	}
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
	Section.AddDynamicEntry(TEXT("BangoScriptLinking"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context)
		{
			return;
		}
		TArray<FAssetData> Linked;
		TArray<FAssetData> Embedded;
		for (const FAssetData& Asset : Context->SelectedAssets)
		{
			(BangoScriptSyncPrivate::IsLinked(Asset) ? Linked : Embedded).Add(Asset);
		}
		const FSlateIcon NoIcon;

		if (Embedded.Num() > 0)
		{
			InSection.AddMenuEntry(TEXT("BangoExportToFile"), LOCTEXT("ExportToFile", "Export to .bango"),
				LOCTEXT("ExportToFileTooltip", "Write the script's text to a .bango file next to it, and link the script to the file: from then on the file is the script, and can be edited in another editor too."),
				NoIcon, FUIAction(FExecuteAction::CreateLambda([Embedded]()
				{
					TArray<UBangoScriptAsset*> ToSave;
					for (const FAssetData& Data : Embedded)
					{
						UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Data.GetAsset());
						if (Asset && ExportToFile(*Asset))
						{
							ToSave.Add(Asset);
						}
					}
					SaveAssets(ToSave);
				})));
		}
		if (Linked.Num() > 0)
		{
			InSection.AddMenuEntry(TEXT("BangoUnlink"), LOCTEXT("Unlink", "Unlink from .bango"),
				LOCTEXT("UnlinkTooltip", "Keep the script's text in the asset only. The .bango file is left where it is, but no longer used for this script."),
				NoIcon, FUIAction(FExecuteAction::CreateLambda([Linked]()
				{
					TArray<UBangoScriptAsset*> ToSave;
					for (const FAssetData& Data : Linked)
					{
						if (UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Data.GetAsset()))
						{
							Unlink(*Asset);
							ToSave.Add(Asset);
						}
					}
					SaveAssets(ToSave);
				})));
		}
		if (Linked.Num() == 1)
		{
			const FString FilePath = GetLinkedFilePath(Linked[0].PackageName.ToString());
			InSection.AddMenuEntry(TEXT("BangoShowFile"), LOCTEXT("ShowFile", "Show .bango File"),
				LOCTEXT("ShowFileTooltip", "Show the script's .bango file in the file browser."),
				NoIcon, FUIAction(FExecuteAction::CreateLambda([FilePath]() { FPlatformProcess::ExploreFolder(*FilePath); })));
			InSection.AddMenuEntry(TEXT("BangoEditExternally"), LOCTEXT("EditExternally", "Edit in External Editor"),
				LOCTEXT("EditExternallyTooltip", "Open the script's .bango file with the program set up for .bango files (like VS Code). Saving it there updates the script."),
				NoIcon, FUIAction(FExecuteAction::CreateLambda([FilePath]() { FPlatformProcess::LaunchFileInDefaultExternalApplication(*FilePath); })));
		}
	}));
}

#undef LOCTEXT_NAMESPACE
