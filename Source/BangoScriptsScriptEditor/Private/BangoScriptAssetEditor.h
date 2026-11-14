#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SBangoScriptEditor;
class UBangoScriptAsset;

/**
 * The editor a Bango Script asset opens in (double-clicking it in the Content Browser): a Script tab with the graph
 * editor (SBangoScriptEditor), in front, and a Details tab behind it in the same tab stack.
 */
class FBangoScriptAssetEditor : public FAssetEditorToolkit
{
public:
	void Init(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UBangoScriptAsset* InAsset);

	//~ Begin IToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	//~ End IToolkit

protected:
	//~ Begin FAssetEditorToolkit
	/** Closing asks about the graph editor's unsaved changes. */
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
	/** The window's Save (File menu, Ctrl+S) saves through the graph editor, so the script's text is what's saved. */
	virtual void SaveAsset_Execute() override;
	//~ End FAssetEditorToolkit

private:
	TSharedRef<SDockTab> SpawnScriptTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);

	TWeakObjectPtr<UBangoScriptAsset> Asset;
	TSharedPtr<SBangoScriptEditor> ScriptEditor;
	TSharedPtr<IDetailsView> DetailsView;
};
