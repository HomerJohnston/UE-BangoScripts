#include "BangoScriptFactories.h"

#include "BangoScriptAsset.h"
#include "BangoScriptAssetEditor.h"
#include "BangoScriptSync.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "BangoScriptFactories"

// ---- Importing a .bango file ----

UBangoScriptImportFactory::UBangoScriptImportFactory()
{
	SupportedClass = UBangoScriptAsset::StaticClass();
	Formats.Add(TEXT("bango;Bango Script"));
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
}

UObject* UBangoScriptImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
	const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Filename))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("Couldn't read %s."), *Filename);
		return nullptr;
	}

	UBangoScriptAsset* Asset = NewObject<UBangoScriptAsset>(InParent, InClass, InName, Flags);
	Asset->Source = Text;

	// The script's file is the one next to it, with its name: put a copy there if this one came from elsewhere.
	const FString LinkedFile = FBangoScriptSync::GetLinkedFilePath(Asset->GetScriptName());
	FString Source = FPaths::ConvertRelativePathToFull(Filename);
	FPaths::NormalizeFilename(Source);
	if (!FPaths::IsSamePath(Source, LinkedFile))
	{
		FString Error;
		if (!FBangoScriptSync::WriteScriptFile(LinkedFile, Text, Error))
		{
			// Still made, as an embedded script: its text is in the asset.
			Warn->Logf(ELogVerbosity::Warning, TEXT("%s Imported as an embedded script instead."), *Error);
			return Asset;
		}
	}
	Asset->bLinked = true;
	Asset->SourceFileHash = FBangoScriptSync::HashFile(LinkedFile);
	return Asset;
}

// ---- Add > Bango Script ----

UBangoScriptNewFactory::UBangoScriptNewFactory()
{
	SupportedClass = UBangoScriptAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;
}

UObject* UBangoScriptNewFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UBangoScriptAsset>(InParent, InClass, InName, Flags); // embedded, with no text yet
}

// ---- In the Content Browser ----

FText UAssetDefinition_BangoScript::GetAssetDisplayName() const
{
	return LOCTEXT("BangoScript", "Bango Script");
}

TSoftClassPtr<UObject> UAssetDefinition_BangoScript::GetAssetClass() const
{
	return UBangoScriptAsset::StaticClass();
}

FLinearColor UAssetDefinition_BangoScript::GetAssetColor() const
{
	return FLinearColor(FColor(255, 170, 40));
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_BangoScript::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Misc };
	return Categories;
}

EAssetCommandResult UAssetDefinition_BangoScript::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UBangoScriptAsset* Asset : OpenArgs.LoadObjects<UBangoScriptAsset>())
	{
		const TSharedRef<FBangoScriptAssetEditor> Editor = MakeShared<FBangoScriptAssetEditor>();
		Editor->Init(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
