#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class FTabManager;
class ITableRow;
class SBangoScriptEditor;
class SDockTab;
class STableViewBase;
class SWindow;

/**
 * The content of the "Bango Scripts" tab: every script in the script folder on the left, and the open scripts on
 * the right, one tab each. Each tab is a script editor of its own (SBangoScriptEditor), kept while the tab is open,
 * so switching tabs keeps each script's view, selection and unsaved changes. The list only picks which script to
 * show; everything about an open script is its editor's.
 *
 * The script tabs are the editor's own document tabs (SDockTab, in a tab well of a tab manager owned by the Bango
 * Scripts tab), as an asset editor's are: they look and behave like Blueprint tabs, and can be dragged out.
 */
class SBangoScriptBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoScriptBrowser) {}
		/** The Bango Scripts tab this is the content of: it owns the tab manager the script tabs are in. */
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, OwnerTab)
		/** The window that tab is in, when it's made. */
		SLATE_ARGUMENT(TSharedPtr<SWindow>, OwnerWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** An open script and its editor. */
	struct FScriptTab
	{
		FString Script;
		TSharedPtr<SBangoScriptEditor> Editor;
		/** The script's document tab (its tab manager keeps it alive while it's open). */
		TWeakPtr<SDockTab> DockTab;
	};

	TArray<TSharedPtr<FString>> ScriptItems;
	TSharedPtr<SListView<TSharedPtr<FString>>> ScriptList;

	/** A folder or a script in the folder tree. */
	struct FScriptTreeItem
	{
		/** Shown: the folder's or the script's name. */
		FString Name;
		/** A script's name as the list shows it (its path under the script folder, without .bango), or a folder's path under it ("" for the script folder itself). */
		FString Path;
		bool bFolder = false;
		TArray<TSharedPtr<FScriptTreeItem>> Children;
	};

	/** The folder tree's one root: the script folder (Content/Bango). */
	TArray<TSharedPtr<FScriptTreeItem>> TreeRoots;
	TSharedPtr<STreeView<TSharedPtr<FScriptTreeItem>>> ScriptTree;
	/** The folders open in the tree (by Path), kept when the tree is made again. The script folder starts open. */
	TSet<FString> ExpandedFolders;
	/** Showing the folder tree instead of the list. Remembered between sessions. */
	bool bTreeView = false;
	/** The script list's width: kept when the window is resized, set by dragging the splitter, and remembered between sessions. */
	float ListWidth = 240.0f;
	/** Set while the list's selection is changed in code, so it doesn't count as picking a script. */
	bool bIgnoreScriptSelection = false;

	/** The open scripts, in the order they were opened. */
	TArray<FScriptTab> Tabs;
	/** Which of Tabs was shown last (its tab activated last), or INDEX_NONE when none is open. */
	int32 ActiveTab = INDEX_NONE;

	/** Owned by the Bango Scripts tab; the script tabs are document tabs in its one tab stack. */
	TSharedPtr<FTabManager> TabManager;

	/** Finds every .bango file in the script folder again, keeping the shown script selected. */
	void RefreshScriptList();
	FReply OnRefreshClicked();

	/** Makes the folder tree again from the script folder's folders and Files (paths under it, without .bango). */
	void RebuildScriptTree(const TArray<FString>& Scripts, const TArray<FString>& Folders);
	TSharedRef<ITableRow> OnGenerateTreeRow(TSharedPtr<FScriptTreeItem> Item, const TSharedRef<STableViewBase>& Owner);
	void OnGetTreeChildren(TSharedPtr<FScriptTreeItem> Item, TArray<TSharedPtr<FScriptTreeItem>>& OutChildren);
	void OnTreeSelectionChanged(TSharedPtr<FScriptTreeItem> Item, ESelectInfo::Type SelectInfo);
	void OnTreeExpansionChanged(TSharedPtr<FScriptTreeItem> Item, bool bExpanded);
	/** The tree item with this Path (a script or a folder), or null. Fills OutParents with the folders above it. */
	TSharedPtr<FScriptTreeItem> FindTreeItem(const FString& Path, bool bFolder, TArray<TSharedPtr<FScriptTreeItem>>* OutParents = nullptr) const;
	void SetTreeView(bool bInTreeView);

	TSharedRef<ITableRow> OnGenerateScriptRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner);
	void OnScriptSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	/** Selects the shown script's row in the list (or nothing), without opening anything. */
	void SelectShownScript();

	/** Shows Script's tab, opening a new one if it isn't open yet. */
	void OpenScriptTab(const FString& Script);
	/** Brings a tab to the front of its tab well. */
	void ActivateTab(int32 Index);
	int32 FindTab(const FString& Script) const;
	int32 FindTab(const TSharedRef<SDockTab>& DockTab) const;

	/** A script's document tab, showing its editor. */
	TSharedRef<SDockTab> MakeScriptDockTab(const FString& Script, const TSharedRef<SBangoScriptEditor>& Editor);
	void OnScriptTabActivated(TSharedRef<SDockTab> DockTab, ETabActivationCause Cause);
	void OnScriptTabClosed(TSharedRef<SDockTab> DockTab);
};
