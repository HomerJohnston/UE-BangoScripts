#include "BangoScriptAssetEditor.h"

#include "BangoEditorStyle.h"
#include "BangoScriptAsset.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "SBangoScriptEditor.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "BangoScriptAssetEditor"

namespace BangoScriptAssetEditorPrivate
{
	const FName AppIdentifier(TEXT("BangoScriptAssetEditorApp"));
	const FName ScriptTabId(TEXT("BangoScriptAssetEditor_Script"));
	const FName DetailsTabId(TEXT("BangoScriptAssetEditor_Details"));
}

void FBangoScriptAssetEditor::Init(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UBangoScriptAsset* InAsset)
{
	using namespace BangoScriptAssetEditorPrivate;
	Asset = InAsset;

	ScriptEditor = SNew(SBangoScriptEditor);
	ScriptEditor->OpenScriptAsset(InAsset);

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(InAsset);

	// The Script tab in front, and Details behind it in the same stack (a tab away).
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("Standalone_BangoScriptAssetEditor_v1"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(ScriptTabId, ETabState::OpenedTab)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
				->SetForegroundTab(ScriptTabId)
			)
		);

	// No default toolbar: the graph editor has its own (Save, Reload, Compile Test, Debug).
	InitAssetEditor(Mode, InToolkitHost, AppIdentifier, Layout, /*bCreateDefaultStandaloneMenu*/ true, /*bCreateDefaultToolbar*/ false, InAsset);
}

FName FBangoScriptAssetEditor::GetToolkitFName() const
{
	return TEXT("BangoScriptAssetEditor");
}

FText FBangoScriptAssetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Bango Script Editor");
}

FString FBangoScriptAssetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("Bango Script ");
}

FLinearColor FBangoScriptAssetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(1.0f, 0.67f, 0.16f, 0.5f);
}

void FBangoScriptAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	using namespace BangoScriptAssetEditorPrivate;
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Bango Script Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ScriptTabId, FOnSpawnTab::CreateSP(this, &FBangoScriptAssetEditor::SpawnScriptTab))
		.SetDisplayName(LOCTEXT("ScriptTab", "Script"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FBangoEditorStyle::GetStyleSetName(), FBangoEditorStyle::GetScriptIconName()));
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FBangoScriptAssetEditor::SpawnDetailsTab))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FBangoScriptAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	using namespace BangoScriptAssetEditorPrivate;
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ScriptTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
}

TSharedRef<SDockTab> FBangoScriptAssetEditor::SpawnScriptTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ScriptTab", "Script"))
		[
			ScriptEditor.ToSharedRef()
		];
}

TSharedRef<SDockTab> FBangoScriptAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTab", "Details"))
		[
			DetailsView.ToSharedRef()
		];
}

bool FBangoScriptAssetEditor::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
	if (ScriptEditor.IsValid() && !ScriptEditor->ConfirmCloseScript())
	{
		return false;
	}
	return FAssetEditorToolkit::OnRequestClose(InCloseReason);
}

void FBangoScriptAssetEditor::SaveAsset_Execute()
{
	if (ScriptEditor.IsValid())
	{
		ScriptEditor->SaveScript();
	}
}

#undef LOCTEXT_NAMESPACE
