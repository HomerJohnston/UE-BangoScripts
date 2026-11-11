#include "SBangoScriptBrowser.h"

#include "BangoEditorStyle.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "SBangoScriptEditor.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SBangoScriptBrowser"

void SBangoScriptBrowser::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Left: the scripts
		+ SSplitter::Slot()
		.Value(0.15f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ScriptsHeading", "SCRIPTS"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.ToolTipText(LOCTEXT("RefreshTooltip", "Look for .bango files again. (To read an open script again from its file, use its Reload button.)"))
					.OnClicked(this, &SBangoScriptBrowser::OnRefreshClicked)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ScriptList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&ScriptItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SBangoScriptBrowser::OnGenerateScriptRow)
				.OnSelectionChanged(this, &SBangoScriptBrowser::OnScriptSelected)
			]
		]

		// Right: a tab per open script, and the shown one's editor
		+ SSplitter::Slot()
		.Value(0.85f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				.ScrollBarVisibility(EVisibility::Collapsed)
				+ SScrollBox::Slot()
				[
					SAssignNew(TabStrip, SHorizontalBox)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(EditorHost, SBox)
			]
		]
	];

	RefreshScriptList();
	ActivateTab(INDEX_NONE);
}

// ---- Script list ----

void SBangoScriptBrowser::RefreshScriptList()
{
	FString Folder = SBangoScriptEditor::GetScriptFolder();
	FPaths::NormalizeDirectoryName(Folder);

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.bango"), /*Files*/ true, /*Directories*/ false);
	Files.Sort();

	ScriptItems.Reset();
	const FString Prefix = Folder + TEXT("/");
	for (FString File : Files)
	{
		File = FPaths::ConvertRelativePathToFull(File);
		FPaths::NormalizeFilename(File);
		if (File.StartsWith(Prefix))
		{
			File = File.RightChop(Prefix.Len());
		}
		ScriptItems.Add(MakeShared<FString>(FPaths::GetBaseFilename(File, /*bRemovePath*/ false)));
	}
	ScriptList->RequestListRefresh();
	SelectShownScript();
}

FReply SBangoScriptBrowser::OnRefreshClicked()
{
	// Only the list: open scripts are reloaded with their own Reload button.
	RefreshScriptList();
	return FReply::Handled();
}

TSharedRef<ITableRow> SBangoScriptBrowser::OnGenerateScriptRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
{
	const FString Script = Item.IsValid() ? *Item : FString();
	return SNew(STableRow<TSharedPtr<FString>>, Owner)
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Script))
			// Open scripts are bold.
			.Font_Lambda([this, Script]() { return FindTab(Script) != INDEX_NONE ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont"); })
		];
}

void SBangoScriptBrowser::OnScriptSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	if (bIgnoreScriptSelection || !Item.IsValid())
	{
		return;
	}
	OpenScriptTab(*Item);
}

void SBangoScriptBrowser::SelectShownScript()
{
	const FString Shown = Tabs.IsValidIndex(ActiveTab) ? Tabs[ActiveTab].Script : FString();
	const TSharedPtr<FString>* Item = ScriptItems.FindByPredicate([&Shown](const TSharedPtr<FString>& Each) { return Each.IsValid() && Each->Equals(Shown); });
	bIgnoreScriptSelection = true;
	if (Item && !Shown.IsEmpty())
	{
		ScriptList->SetSelection(*Item, ESelectInfo::Direct);
	}
	else
	{
		ScriptList->ClearSelection();
	}
	bIgnoreScriptSelection = false;
}

// ---- Tabs ----

int32 SBangoScriptBrowser::FindTab(const FString& Script) const
{
	return Tabs.IndexOfByPredicate([&Script](const FScriptTab& Tab) { return Tab.Script.Equals(Script); });
}

void SBangoScriptBrowser::OpenScriptTab(const FString& Script)
{
	int32 Index = FindTab(Script);
	if (Index == INDEX_NONE)
	{
		FScriptTab& Tab = Tabs.AddDefaulted_GetRef();
		Tab.Script = Script;
		SAssignNew(Tab.Editor, SBangoScriptEditor).Script(Script);
		Index = Tabs.Num() - 1;
	}
	ActivateTab(Index);
}

void SBangoScriptBrowser::ActivateTab(int32 Index)
{
	ActiveTab = Tabs.IsValidIndex(Index) ? Index : INDEX_NONE;
	if (ActiveTab != INDEX_NONE)
	{
		// The other editors stay alive (keeping their view and changes) while they aren't shown.
		EditorHost->SetContent(Tabs[ActiveTab].Editor.ToSharedRef());
	}
	else
	{
		EditorHost->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(16.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PickScript", "Pick a script on the left to open it. Each open script gets a tab here."))
				.AutoWrapText(true)
			]);
	}
	RebuildTabStrip();
	SelectShownScript();
}

bool SBangoScriptBrowser::CloseTab(int32 Index)
{
	if (!Tabs.IsValidIndex(Index) || !Tabs[Index].Editor->ConfirmCloseScript())
	{
		return false;
	}
	const FString Shown = Tabs.IsValidIndex(ActiveTab) ? Tabs[ActiveTab].Script : FString();
	Tabs.RemoveAt(Index);
	if (Index == ActiveTab)
	{
		// Show the tab that took its place, or the one before it if it was the last.
		ActivateTab(FMath::Min(Index, Tabs.Num() - 1));
	}
	else
	{
		ActivateTab(FindTab(Shown));
	}
	return true;
}

void SBangoScriptBrowser::RebuildTabStrip()
{
	TabStrip->ClearChildren();
	for (const FScriptTab& Tab : Tabs)
	{
		TabStrip->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 1.0f, 0.0f)
			[
				MakeTab(Tab.Script)
			];
	}
}

TSharedRef<SWidget> SBangoScriptBrowser::MakeTab(const FString& Script)
{
	// Looked up by name each time, so the tab still finds its editor after other tabs close.
	auto IsShown = [this, Script]() { return Tabs.IsValidIndex(ActiveTab) && Tabs[ActiveTab].Script.Equals(Script); };
	auto Label = [this, Script]()
	{
		const int32 Index = FindTab(Script);
		const bool bDirty = Index != INDEX_NONE && Tabs[Index].Editor->HasUnsavedChanges();
		return FText::FromString(bDirty ? Script + TEXT(" *") : Script);
	};

	return SNew(SBorder)
		.BorderImage(FBangoEditorStyle::GetWhiteBrush())
		.BorderBackgroundColor_Lambda([IsShown]() { return IsShown() ? FSlateColor(FStyleColors::Panel) : FSlateColor(FStyleColors::Recessed); })
		.Padding(0.0f)
		.ToolTipText(FText::FromString(Script + TEXT(".bango")))
		.OnMouseButtonDown_Lambda([this, Script](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
			{
				CloseTab(FindTab(Script)); // middle-click closes, as in a browser
				return FReply::Handled();
			}
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				ActivateTab(FindTab(Script));
				return FReply::Handled();
			}
			return FReply::Unhandled();
		})
		[
			SNew(SVerticalBox)
			// The shown tab is marked with a line along its top.
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FBangoEditorStyle::GetWhiteBrush())
				.BorderBackgroundColor_Lambda([IsShown]() { return IsShown() ? FSlateColor(FStyleColors::Primary) : FSlateColor(FStyleColors::Transparent); })
				.Padding(0.0f)
				[
					SNew(SBox).HeightOverride(2.0f)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(10.0f, 4.0f, 4.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text_Lambda(Label)
					.ColorAndOpacity_Lambda([IsShown]() { return IsShown() ? FSlateColor(FStyleColors::ForegroundHover) : FSlateColor(FStyleColors::Foreground); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(3.0f, 0.0f))
					.ToolTipText(LOCTEXT("CloseTab", "Close (asks first if there are unsaved changes). Middle-clicking the tab closes it too."))
					.OnClicked_Lambda([this, Script]()
					{
						CloseTab(FindTab(Script));
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("×"))) // a multiplication sign, as the close cross
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
