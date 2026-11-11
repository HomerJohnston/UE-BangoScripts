#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SBangoScriptEditor;
class SBox;
class SHorizontalBox;
class STableViewBase;

/**
 * The content of the "Bango Scripts" tab: every script in the script folder on the left, and the open scripts on
 * the right, one tab each. Each tab is a script editor of its own (SBangoScriptEditor), kept while the tab is open,
 * so switching tabs keeps each script's view, selection and unsaved changes. The list only picks which script to
 * show; everything about an open script is its editor's.
 */
class SBangoScriptBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoScriptBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** An open script and its editor. */
	struct FScriptTab
	{
		FString Script;
		TSharedPtr<SBangoScriptEditor> Editor;
	};

	TArray<TSharedPtr<FString>> ScriptItems;
	TSharedPtr<SListView<TSharedPtr<FString>>> ScriptList;
	/** Set while the list's selection is changed in code, so it doesn't count as picking a script. */
	bool bIgnoreScriptSelection = false;

	/** The open scripts, in tab order. */
	TArray<FScriptTab> Tabs;
	/** Which of Tabs is shown, or INDEX_NONE when none is open. */
	int32 ActiveTab = INDEX_NONE;

	TSharedPtr<SHorizontalBox> TabStrip;
	TSharedPtr<SBox> EditorHost;

	/** Finds every .bango file in the script folder again, keeping the shown script selected. */
	void RefreshScriptList();
	FReply OnRefreshClicked();

	TSharedRef<ITableRow> OnGenerateScriptRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner);
	void OnScriptSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	/** Selects the shown script's row in the list (or nothing), without opening anything. */
	void SelectShownScript();

	/** Shows Script's tab, opening a new one at the end if it isn't open yet. */
	void OpenScriptTab(const FString& Script);
	void ActivateTab(int32 Index);
	/** Closes a tab, asking about unsaved changes first. Returns false if the user cancelled. */
	bool CloseTab(int32 Index);
	int32 FindTab(const FString& Script) const;

	/** Rebuilds the row of tabs, after one is opened, closed or shown. */
	void RebuildTabStrip();
	TSharedRef<SWidget> MakeTab(const FString& Script);
};
