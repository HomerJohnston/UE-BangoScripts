#include "SBangoScriptBrowser.h"

#include "BangoEditorStyle.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "SBangoScriptEditor.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SBangoScriptBrowser"

namespace BangoScriptBrowserPrivate
{
	static const TCHAR* ConfigSection = TEXT("BangoScripts.ScriptBrowser");
}

void SBangoScriptBrowser::Construct(const FArguments& InArgs)
{
	GConfig->GetBool(BangoScriptBrowserPrivate::ConfigSection, TEXT("bTreeView"), bTreeView, GEditorPerProjectIni);
	ExpandedFolders.Add(FString()); // the script folder starts open

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
				// Shows the scripts in their folders, like the Content Browser, instead of as one list.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("TreeViewTooltip", "Show the scripts in their folders, with the Bango script folder (Content/Bango) at the top, instead of as one list."))
					.IsChecked_Lambda([this]() { return bTreeView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetTreeView(NewState == ECheckBoxState::Checked); })
					.Padding(FMargin(6.0f, 2.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("TreeView", "Folders"))
						]
					]
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
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return bTreeView ? 1 : 0; })
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(ScriptList, SListView<TSharedPtr<FString>>)
					.ListItemsSource(&ScriptItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SBangoScriptBrowser::OnGenerateScriptRow)
					.OnSelectionChanged(this, &SBangoScriptBrowser::OnScriptSelected)
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(ScriptTree, STreeView<TSharedPtr<FScriptTreeItem>>)
					.TreeItemsSource(&TreeRoots)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SBangoScriptBrowser::OnGenerateTreeRow)
					.OnGetChildren(this, &SBangoScriptBrowser::OnGetTreeChildren)
					.OnSelectionChanged(this, &SBangoScriptBrowser::OnTreeSelectionChanged)
					.OnExpansionChanged(this, &SBangoScriptBrowser::OnTreeExpansionChanged)
					.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FScriptTreeItem> Item)
					{
						if (Item.IsValid() && Item->bFolder)
						{
							ScriptTree->SetItemExpansion(Item, !ScriptTree->IsItemExpanded(Item)); // double-click opens or closes a folder
						}
					})
				]
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

	// Paths under the script folder, with '/'.
	const FString Prefix = FPaths::ConvertRelativePathToFull(Folder) + TEXT("/");
	auto UnderFolder = [&Prefix](FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
		return Path.StartsWith(Prefix) ? Path.RightChop(Prefix.Len()) : Path;
	};

	ScriptItems.Reset();
	TArray<FString> Scripts;
	for (const FString& File : Files)
	{
		Scripts.Add(FPaths::GetBaseFilename(UnderFolder(File), /*bRemovePath*/ false));
		ScriptItems.Add(MakeShared<FString>(Scripts.Last()));
	}
	ScriptList->RequestListRefresh();

	// The tree shows empty folders too, as the Content Browser does.
	TArray<FString> Directories;
	IFileManager::Get().FindFilesRecursive(Directories, *Folder, TEXT("*"), /*Files*/ false, /*Directories*/ true);
	TArray<FString> Folders;
	for (const FString& Directory : Directories)
	{
		Folders.Add(UnderFolder(Directory));
	}
	RebuildScriptTree(Scripts, Folders);

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
	TArray<TSharedPtr<FScriptTreeItem>> Parents;
	const TSharedPtr<FScriptTreeItem> TreeItem = Shown.IsEmpty() ? nullptr : FindTreeItem(Shown, false, &Parents);
	bIgnoreScriptSelection = true;
	if (Item && !Shown.IsEmpty())
	{
		ScriptList->SetSelection(*Item, ESelectInfo::Direct);
		ScriptList->RequestScrollIntoView(*Item);
	}
	else
	{
		ScriptList->ClearSelection();
	}
	if (ScriptTree.IsValid())
	{
		if (TreeItem.IsValid())
		{
			for (const TSharedPtr<FScriptTreeItem>& Parent : Parents)
			{
				ScriptTree->SetItemExpansion(Parent, true); // the shown script's folders open, so it can be seen
			}
			ScriptTree->SetSelection(TreeItem, ESelectInfo::Direct);
			ScriptTree->RequestScrollIntoView(TreeItem);
		}
		else
		{
			ScriptTree->ClearSelection();
		}
	}
	bIgnoreScriptSelection = false;
}

// ---- Folder tree ----

void SBangoScriptBrowser::RebuildScriptTree(const TArray<FString>& Scripts, const TArray<FString>& Folders)
{
	FString Folder = SBangoScriptEditor::GetScriptFolder();
	FPaths::NormalizeDirectoryName(Folder);

	TSharedPtr<FScriptTreeItem> Root = MakeShared<FScriptTreeItem>();
	Root->Name = FPaths::GetCleanFilename(Folder); // "Bango"
	Root->bFolder = true;

	// The folder for a path ("" is the root), made along with its parents if they aren't there yet.
	TMap<FString, TSharedPtr<FScriptTreeItem>> FolderItems;
	FolderItems.Add(FString(), Root);
	TFunction<TSharedPtr<FScriptTreeItem>(const FString&)> GetFolder = [&FolderItems, &GetFolder](const FString& Path) -> TSharedPtr<FScriptTreeItem>
	{
		if (const TSharedPtr<FScriptTreeItem>* Found = FolderItems.Find(Path))
		{
			return *Found;
		}
		FString ParentPath;
		FString Name = Path;
		Path.Split(TEXT("/"), &ParentPath, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		const TSharedPtr<FScriptTreeItem> Parent = GetFolder(ParentPath);
		TSharedPtr<FScriptTreeItem> Item = MakeShared<FScriptTreeItem>();
		Item->Name = Name;
		Item->Path = Path;
		Item->bFolder = true;
		Parent->Children.Add(Item);
		FolderItems.Add(Path, Item);
		return Item;
	};

	for (const FString& Path : Folders)
	{
		GetFolder(Path);
	}
	for (const FString& Script : Scripts)
	{
		FString FolderPath;
		FString Name = Script;
		Script.Split(TEXT("/"), &FolderPath, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		TSharedPtr<FScriptTreeItem> Item = MakeShared<FScriptTreeItem>();
		Item->Name = Name;
		Item->Path = Script;
		GetFolder(FolderPath)->Children.Add(Item);
	}

	// Folders first, then scripts, each by name (as the Content Browser does).
	for (const TPair<FString, TSharedPtr<FScriptTreeItem>>& Pair : FolderItems)
	{
		Pair.Value->Children.Sort([](const TSharedPtr<FScriptTreeItem>& A, const TSharedPtr<FScriptTreeItem>& B)
		{
			if (A->bFolder != B->bFolder)
			{
				return A->bFolder;
			}
			return A->Name.Compare(B->Name, ESearchCase::IgnoreCase) < 0;
		});
	}

	TreeRoots.Reset();
	TreeRoots.Add(Root);
	ScriptTree->RequestTreeRefresh();

	// The same folders open as before.
	const TSet<FString> WereExpanded = ExpandedFolders;
	for (const TPair<FString, TSharedPtr<FScriptTreeItem>>& Pair : FolderItems)
	{
		ScriptTree->SetItemExpansion(Pair.Value, WereExpanded.Contains(Pair.Key));
	}
	ExpandedFolders = WereExpanded;
}

TSharedRef<ITableRow> SBangoScriptBrowser::OnGenerateTreeRow(TSharedPtr<FScriptTreeItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	const TWeakPtr<FScriptTreeItem> WeakItem = Item;
	const FString Path = Item.IsValid() ? Item->Path : FString();
	const bool bFolder = Item.IsValid() && Item->bFolder;
	return SNew(STableRow<TSharedPtr<FScriptTreeItem>>, Owner)
		.Padding(FMargin(2.0f, 2.0f))
		.ToolTipText(FText::FromString(bFolder ? FString::Printf(TEXT("Content/Bango/%s"), *Path) : Path + TEXT(".bango")))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image_Lambda([this, WeakItem, bFolder]()
				{
					if (!bFolder)
					{
						return FAppStyle::GetBrush("GraphEditor.EventGraph_16x"); // a script is a graph
					}
					const TSharedPtr<FScriptTreeItem> Pinned = WeakItem.Pin();
					return FAppStyle::GetBrush(Pinned.IsValid() && ScriptTree.IsValid() && ScriptTree->IsItemExpanded(Pinned)
						? "ContentBrowser.AssetTreeFolderOpen" : "ContentBrowser.AssetTreeFolderClosed");
				})
				.ColorAndOpacity(bFolder ? FSlateColor(FLinearColor(0.84f, 0.71f, 0.42f)) : FSlateColor::UseForeground()) // folders in the Content Browser's tan
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item.IsValid() ? Item->Name : FString()))
				// Open scripts are bold, as in the list.
				.Font_Lambda([this, Path, bFolder]() { return !bFolder && FindTab(Path) != INDEX_NONE ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont"); })
			]
		];
}

void SBangoScriptBrowser::OnGetTreeChildren(TSharedPtr<FScriptTreeItem> Item, TArray<TSharedPtr<FScriptTreeItem>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SBangoScriptBrowser::OnTreeSelectionChanged(TSharedPtr<FScriptTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	if (bIgnoreScriptSelection || !Item.IsValid() || Item->bFolder)
	{
		return; // picking a folder only selects it
	}
	OpenScriptTab(Item->Path);
}

void SBangoScriptBrowser::OnTreeExpansionChanged(TSharedPtr<FScriptTreeItem> Item, bool bExpanded)
{
	if (!Item.IsValid() || !Item->bFolder)
	{
		return;
	}
	if (bExpanded)
	{
		ExpandedFolders.Add(Item->Path);
	}
	else
	{
		ExpandedFolders.Remove(Item->Path);
	}
}

TSharedPtr<SBangoScriptBrowser::FScriptTreeItem> SBangoScriptBrowser::FindTreeItem(const FString& Path, bool bFolder, TArray<TSharedPtr<FScriptTreeItem>>* OutParents) const
{
	TFunction<TSharedPtr<FScriptTreeItem>(const TSharedPtr<FScriptTreeItem>&)> Search = [&](const TSharedPtr<FScriptTreeItem>& Item) -> TSharedPtr<FScriptTreeItem>
	{
		if (Item->bFolder == bFolder && Item->Path.Equals(Path))
		{
			return Item;
		}
		for (const TSharedPtr<FScriptTreeItem>& Child : Item->Children)
		{
			if (const TSharedPtr<FScriptTreeItem> Found = Search(Child))
			{
				if (OutParents)
				{
					OutParents->Insert(Item, 0);
				}
				return Found;
			}
		}
		return nullptr;
	};
	for (const TSharedPtr<FScriptTreeItem>& Root : TreeRoots)
	{
		if (const TSharedPtr<FScriptTreeItem> Found = Search(Root))
		{
			return Found;
		}
	}
	return nullptr;
}

void SBangoScriptBrowser::SetTreeView(bool bInTreeView)
{
	bTreeView = bInTreeView;
	GConfig->SetBool(BangoScriptBrowserPrivate::ConfigSection, TEXT("bTreeView"), bTreeView, GEditorPerProjectIni);
	SelectShownScript(); // the shown script, selected in the view now shown
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
