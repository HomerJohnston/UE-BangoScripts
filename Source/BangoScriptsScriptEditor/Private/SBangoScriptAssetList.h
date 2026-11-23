// SBangoScriptAssetList.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FAssetData;
class ITableRow;
class STableViewBase;

/** A script was picked from the list: its name, the asset path ("/Game/Quests/Intro"). */
DECLARE_DELEGATE_OneParam(FOnBangoScriptPicked, const FString& /*ScriptName*/);
/** How a script's path would be written where it's picked for: "Intro" for "/Game/Quests/Intro", say. */
DECLARE_DELEGATE_RetVal_OneParam(FString, FOnGetBangoWrittenPath, const FString& /*ScriptName*/);

/**
 * Every Bango Script asset in the project, by folder, with a search box: the script editor's Script Assets pane, for
 * writing one script's path into another (Exec). It lists the Asset Registry's assets and follows them as they're
 * added, renamed and deleted. Double-clicking a script reports it (OnScriptPicked); what's done with it, and how its
 * path is written from the open script, is the owner's.
 */
class SBangoScriptAssetList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoScriptAssetList) {}
		/** The script the owner has open (its name), shown greyed: a script rarely names itself. Empty for none. */
		SLATE_ATTRIBUTE(FString, OpenScript)
		/** How the owner would write a script's path (for the rows' tooltips). Without it, the full path. */
		SLATE_EVENT(FOnGetBangoWrittenPath, OnGetWrittenPath)
		/** What double-clicking does, for the rows' tooltips: "write its path at the text cursor". */
		SLATE_ATTRIBUTE(FText, PickHint)
		SLATE_EVENT(FOnBangoScriptPicked, OnScriptPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBangoScriptAssetList() override;

	/** Reads the assets again (the pane does when it's pulled out). */
	void Refresh();

private:
	/** A row: a folder's heading, or a script in the folder above it. */
	struct FRow
	{
		bool bFolder = false;
		/** "/Game/Quests" for a folder; "/Game/Quests/Intro" for a script. */
		FString Path;
		/** What's shown: the folder's path, or the script's own name ("Intro"). */
		FString Label;
	};

	void OnAssetChanged(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void QueueRefresh();
	/** Fills Rows from Scripts and the search text. */
	void RebuildRows();
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner);

	TAttribute<FString> OpenScript;
	TAttribute<FText> PickHint;
	FOnGetBangoWrittenPath OnGetWrittenPath;
	FOnBangoScriptPicked OnScriptPicked;

	/** Every script's name, sorted. */
	TArray<FString> Scripts;
	FString SearchText;
	TArray<TSharedPtr<FRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FRow>>> ListView;
	bool bRefreshQueued = false;
};
