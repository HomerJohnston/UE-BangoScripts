#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class SBangoScriptEditor;
class UBangoScriptAsset;

/**
 * The editor a Bango Script asset opens in (double-clicking it in the Content Browser): the graph editor
 * (SBangoScriptEditor), filling the window. It's in one tab, whose tab well is hidden, so no tabs are shown. (The
 * asset's one property, whether it's linked, is shown in the graph editor's toolbar.)
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

	TWeakObjectPtr<UBangoScriptAsset> Asset;
	TSharedPtr<SBangoScriptEditor> ScriptEditor;
};
