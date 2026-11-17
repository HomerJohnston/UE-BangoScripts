#include "BangoScriptAssetEditor.h"

#include "BangoEditorStyle.h"
#include "BangoScriptAsset.h"
#include "SBangoScriptEditor.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "BangoScriptAssetEditor"

namespace BangoScriptAssetEditorPrivate
{
	const FName AppIdentifier(TEXT("BangoScriptAssetEditorApp"));
	const FName ScriptTabId(TEXT("BangoScriptAssetEditor_Script"));
}

void FBangoScriptAssetEditor::Init(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UBangoScriptAsset* InAsset)
{
	using namespace BangoScriptAssetEditorPrivate;
	Asset = InAsset;

	ScriptEditor = SNew(SBangoScriptEditor);
	ScriptEditor->OpenScriptAsset(InAsset);

	// Just the graph editor: one tab, with its tab well hidden, so it fills the window without a tab above it.
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("Standalone_BangoScriptAssetEditor_v2"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(ScriptTabId, ETabState::OpenedTab)
				->SetHideTabWell(true)
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
}

void FBangoScriptAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	using namespace BangoScriptAssetEditorPrivate;
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ScriptTabId);
}

TSharedRef<SDockTab> FBangoScriptAssetEditor::SpawnScriptTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ScriptTab", "Script"))
		[
			ScriptEditor.ToSharedRef()
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
