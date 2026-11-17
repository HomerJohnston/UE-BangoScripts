#include "BangoLevelStartScriptMenu.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BangoEditorStyle.h"
#include "BangoLevelStartScript.h"
#include "BangoScriptAsset.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/WorldSettings.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "BangoLevelStartScript"

namespace BangoLevelStartScriptMenuPrivate
{
	/** The level being edited (not a Play in Editor copy of it). */
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UBangoScriptAsset* GetStartScript()
	{
		const UBangoLevelStartScript* Data = UBangoLevelStartScript::Find(GetEditorWorld());
		return Data ? Data->Script.Get() : nullptr;
	}

	void OpenScript(UBangoScriptAsset* Script)
	{
		if (Script && GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Script);
		}
	}

	/** Makes Script the level's start script, as one undoable change to its World Settings. */
	void SetStartScript(UBangoScriptAsset* Script)
	{
		UWorld* World = GetEditorWorld();
		AWorldSettings* Settings = World ? World->GetWorldSettings(false, false) : nullptr;
		if (!Settings || !Script)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("SetTransaction", "Set Bango Level Start Script"));
		Settings->Modify();
		UBangoLevelStartScript* Data = UBangoLevelStartScript::Find(World);
		if (Data)
		{
			Data->Modify();
		}
		else
		{
			Data = NewObject<UBangoLevelStartScript>(Settings, NAME_None, RF_Transactional);
			Settings->AddAssetUserData(Data);
		}
		Data->Script = Script;
	}

	void RemoveStartScript()
	{
		UWorld* World = GetEditorWorld();
		AWorldSettings* Settings = World ? World->GetWorldSettings(false, false) : nullptr;
		if (!Settings || !UBangoLevelStartScript::Find(World))
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveTransaction", "Remove Bango Level Start Script"));
		Settings->Modify();
		Settings->RemoveUserDataOfClass(UBangoLevelStartScript::StaticClass());
	}

	/** Makes a new script asset, `<Level>_Start`, next to the level (in /Game if it isn't saved yet), and makes it the start script. */
	void CreateStartScript()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return;
		}
		const FString LevelPackage = World->GetPackage()->GetName();
		FString Folder = FPackageName::GetLongPackagePath(LevelPackage);
		if (!Folder.StartsWith(TEXT("/Game")))
		{
			Folder = TEXT("/Game"); // an unsaved level is in /Temp
		}
		const FString BaseName = World->GetMapName() + TEXT("_Start");
		FString AssetName = BaseName;
		for (int32 Number = 2; FPackageName::DoesPackageExist(Folder / AssetName) || FindPackage(nullptr, *(Folder / AssetName)); ++Number)
		{
			AssetName = FString::Printf(TEXT("%s%d"), *BaseName, Number);
		}

		UPackage* Package = CreatePackage(*(Folder / AssetName));
		UBangoScriptAsset* Script = NewObject<UBangoScriptAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		Script->Source = TEXT("// The level's start script: it runs when the level starts, before anything else can start a script.\n")
			TEXT("// It must finish straight away: put anything that waits (Sleep, ...) in a spawn block. It has no self.\n");
		FAssetRegistryModule::AssetCreated(Script);
		Package->MarkPackageDirty();
		SetStartScript(Script);
		OpenScript(Script);
	}

	/** A list of the project's script assets: picking one makes it the start script. */
	TSharedRef<SWidget> MakeScriptPicker()
	{
		FAssetPickerConfig Config;
		Config.Filter.ClassPaths.Add(UBangoScriptAsset::StaticClass()->GetClassPathName());
		Config.SelectionMode = ESelectionMode::Single;
		Config.InitialAssetViewType = EAssetViewType::List;
		Config.bFocusSearchBoxWhenOpened = true;
		Config.bAllowNullSelection = false;
		if (UBangoScriptAsset* Current = GetStartScript())
		{
			Config.InitialAssetSelection = FAssetData(Current);
		}
		Config.OnAssetSelected = FOnAssetSelected::CreateLambda([](const FAssetData& Asset)
		{
			FSlateApplication::Get().DismissAllMenus();
			SetStartScript(Cast<UBangoScriptAsset>(Asset.GetAsset()));
		});
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		return SNew(SBox)
			.WidthOverride(320.0f)
			.HeightOverride(400.0f)
			[
				ContentBrowser.Get().CreateAssetPicker(Config)
			];
	}

	TSharedRef<SWidget> MakeMenu()
	{
		FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
		UWorld* World = GetEditorWorld();
		const UBangoLevelStartScript* Data = UBangoLevelStartScript::Find(World);
		UBangoScriptAsset* Script = Data ? Data->Script.Get() : nullptr;

		Menu.BeginSection(TEXT("LevelStartScript"), LOCTEXT("LevelStartScriptSection", "Level Start Script"));
		if (Script)
		{
			Menu.AddMenuEntry(FText::Format(LOCTEXT("Open", "Open {0}"), FText::FromString(Script->GetName())),
				LOCTEXT("OpenTooltip", "Open this level's start script in the script editor. It runs when the level starts, before anything else can start a script."),
				FSlateIcon(FBangoEditorStyle::GetStyleSetName(), FBangoEditorStyle::GetScriptIconName()),
				FUIAction(FExecuteAction::CreateLambda([Script]() { OpenScript(Script); })));
		}
		else
		{
			Menu.AddMenuEntry(LOCTEXT("Create", "Create Level Start Script"),
				FText::Format(LOCTEXT("CreateTooltip", "Make a new script, {0}_Start, next to this level, and make it the level's start script: it runs when the level starts, before anything else can start a script."),
					FText::FromString(World ? World->GetMapName() : FString())),
				FSlateIcon(FBangoEditorStyle::GetStyleSetName(), FBangoEditorStyle::GetScriptIconName()),
				FUIAction(FExecuteAction::CreateStatic(&CreateStartScript), FCanExecuteAction::CreateLambda([World]() { return World != nullptr; })));
		}
		Menu.AddSubMenu(Script ? LOCTEXT("ChooseOther", "Choose Another Script") : LOCTEXT("Choose", "Choose Level Start Script"),
			LOCTEXT("ChooseTooltip", "Pick a script asset to run when the level starts."),
			FNewMenuDelegate::CreateLambda([](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(MakeScriptPicker(), FText::GetEmpty(), /*bNoIndent*/ true);
			}));
		if (Data)
		{
			Menu.AddMenuEntry(LOCTEXT("Remove", "Remove Level Start Script"),
				LOCTEXT("RemoveTooltip", "The level won't run a start script. The script asset itself is kept."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&RemoveStartScript)));
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}
}

void FBangoLevelStartScriptMenu::Startup()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBangoLevelStartScriptMenu::RegisterMenus));
}

void FBangoLevelStartScriptMenu::Shutdown()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FBangoLevelStartScriptMenu::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	// Beside the level toolbar's Blueprints and Cinematics dropdowns.
	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"));
	FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("Content"));
	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		TEXT("BangoMenu"),
		FUIAction(),
		FOnGetContent::CreateStatic(&BangoLevelStartScriptMenuPrivate::MakeMenu),
		LOCTEXT("ToolbarLabel", "Bango"),
		LOCTEXT("ToolbarTooltip", "Bango scripts for this level: its start script, which runs when the level starts."),
		FSlateIcon(FBangoEditorStyle::GetStyleSetName(), FBangoEditorStyle::GetScriptIconName()));
	Entry.StyleNameOverride = TEXT("AssetEditorToolbar");
	Section.AddEntry(Entry);
}

#undef LOCTEXT_NAMESPACE
