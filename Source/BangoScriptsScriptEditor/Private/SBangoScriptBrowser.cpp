#include "SBangoScriptBrowser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BangoEditorStyle.h"
#include "BangoScriptAsset.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "Framework/Docking/TabManager.h"
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
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SBangoScriptBrowser"

namespace BangoScriptBrowserPrivate
{
	static const TCHAR* ConfigSection = TEXT("BangoScripts.ScriptBrowser");
	/** The script tabs' tab stack holds this (closed) tab, so new script tabs know where to go. */
	static const FName ScriptDocumentTabId(TEXT("BangoScriptDocument"));
}

void SBangoScriptBrowser::Construct(const FArguments& InArgs)
{
	using namespace BangoScriptBrowserPrivate;

	// The script tabs: document tabs in a tab manager of the Bango Scripts tab's own, as an asset editor has.
	check(InArgs._OwnerTab.IsValid());
	TabManager = FGlobalTabmanager::Get()->NewTabManager(InArgs._OwnerTab.ToSharedRef());
	const TSharedRef<FTabManager::FLayout> TabLayout = FTabManager::NewLayout(TEXT("BangoScriptBrowser_Documents_v1"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(ScriptDocumentTabId, ETabState::ClosedTab)
				->SetHideTabWell(false)
			)
		);
	const TSharedPtr<SWidget> TabArea = TabManager->RestoreFrom(TabLayout, InArgs._OwnerWindow);

	GConfig->GetBool(BangoScriptBrowserPrivate::ConfigSection, TEXT("bTreeView"), bTreeView, GEditorPerProjectIni);
	GConfig->GetFloat(BangoScriptBrowserPrivate::ConfigSection, TEXT("ListWidth"), ListWidth, GEditorPerProjectIni);
	ListWidth = FMath::Max(ListWidth, 120.0f);
	ExpandedFolders.Add(TEXT("/Game")); // Content starts open

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Left: the scripts
		+ SSplitter::Slot()
		// Sized to its content, a width it keeps when the window is resized; dragging the splitter changes it (ListWidth, remembered).
		.SizeRule(SSplitter::SizeToContent)
		.Resizable(true) // a sized-to-content slot is only dragged if it says so (and has OnSlotResized)
		.MinSize(120.0f)
		.OnSlotResized_Lambda([this](float NewWidth)
		{
			ListWidth = FMath::Max(NewWidth, 120.0f);
			GConfig->SetFloat(BangoScriptBrowserPrivate::ConfigSection, TEXT("ListWidth"), ListWidth, GEditorPerProjectIni);
		})
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]() { return ListWidth; })
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
						.ToolTipText(LOCTEXT("TreeViewTooltip", "Show the scripts in their folders, like the Content Browser, instead of as one list."))
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
						.ToolTipText(LOCTEXT("RefreshTooltip", "List the Bango Script assets again (the list also follows them as they're added, renamed and deleted). To read an open script again from its file, use its Reload button."))
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
		]

		// Right: a document tab per open script, each showing its editor
		+ SSplitter::Slot()
		.Value(1.0f) // takes up any change in the window's width
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				TabArea.IsValid() ? TabArea.ToSharedRef() : SNullWidget::NullWidget
			]
			// Shown while no script is open.
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(16, 48, 16, 16)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return Tabs.Num() == 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.Text(LOCTEXT("PickScript", "Pick a script on the left to open it. Each open script gets a tab here. (Bango Script assets can also be opened from the Content Browser.)"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
		]
	];

	// The list follows script assets as they're added, renamed and deleted.
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.OnAssetAdded().AddSP(this, &SBangoScriptBrowser::OnAssetChanged);
	Registry.OnAssetRemoved().AddSP(this, &SBangoScriptBrowser::OnAssetChanged);
	Registry.OnAssetRenamed().AddSP(this, &SBangoScriptBrowser::OnAssetRenamed);

	RefreshScriptList();
}

SBangoScriptBrowser::~SBangoScriptBrowser()
{
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		Registry->OnAssetAdded().RemoveAll(this);
		Registry->OnAssetRemoved().RemoveAll(this);
		Registry->OnAssetRenamed().RemoveAll(this);
	}
}

void SBangoScriptBrowser::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	OnAssetChanged(AssetData);
}

// ---- Script list ----

void SBangoScriptBrowser::RefreshScriptList()
{
	bRefreshQueued = false;

	// Every Bango Script asset, by its asset path ("/Game/Quests/Intro").
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByClass(UBangoScriptAsset::StaticClass()->GetClassPathName(), Assets);
	TArray<FString> Scripts;
	for (const FAssetData& Asset : Assets)
	{
		Scripts.Add(Asset.PackageName.ToString());
	}
	Scripts.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::IgnoreCase) < 0; });

	ScriptItems.Reset();
	for (const FString& Script : Scripts)
	{
		ScriptItems.Add(MakeShared<FString>(Script));
	}
	ScriptList->RequestListRefresh();
	RebuildScriptTree(Scripts);
	SelectShownScript();
}

void SBangoScriptBrowser::QueueRefresh()
{
	// Once, on the next frame, however many assets changed (the registry reports them one at a time).
	if (bRefreshQueued)
	{
		return;
	}
	bRefreshQueued = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		RefreshScriptList();
		return EActiveTimerReturnType::Stop;
	}));
}

void SBangoScriptBrowser::OnAssetChanged(const FAssetData& AssetData)
{
	if (AssetData.IsInstanceOf(UBangoScriptAsset::StaticClass()))
	{
		QueueRefresh();
	}
}

FString SBangoScriptBrowser::GetDisplayName(const FString& Script)
{
	// "/Game/Quests/Intro" is shown as "Quests/Intro"; a plugin's ("/MyPlugin/Intro") keeps its root.
	FString Name = Script;
	if (!Name.RemoveFromStart(TEXT("/Game/")))
	{
		Name.RemoveFromStart(TEXT("/"));
	}
	return Name;
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
		.ToolTipText(FText::FromString(Script))
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetDisplayName(Script)))
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

void SBangoScriptBrowser::RebuildScriptTree(const TArray<FString>& Scripts)
{
	// A root per content root ("/Game" shown as Content, a plugin's by its name), then its folders, made along with
	// their parents as they're needed.
	TArray<TSharedPtr<FScriptTreeItem>> Roots;
	TMap<FString, TSharedPtr<FScriptTreeItem>> FolderItems;
	TFunction<TSharedPtr<FScriptTreeItem>(const FString&)> GetFolder = [&FolderItems, &Roots, &GetFolder](const FString& Path) -> TSharedPtr<FScriptTreeItem>
	{
		if (const TSharedPtr<FScriptTreeItem>* Found = FolderItems.Find(Path))
		{
			return *Found;
		}
		FString ParentPath;
		FString Name = Path;
		Path.Split(TEXT("/"), &ParentPath, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		TSharedPtr<FScriptTreeItem> Item = MakeShared<FScriptTreeItem>();
		Item->Path = Path;
		Item->bFolder = true;
		if (ParentPath.IsEmpty())
		{
			Item->Name = Name == TEXT("Game") ? FString(TEXT("Content")) : Name; // a content root
			Roots.Add(Item);
		}
		else
		{
			Item->Name = Name;
			GetFolder(ParentPath)->Children.Add(Item);
		}
		FolderItems.Add(Path, Item);
		return Item;
	};

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

	Roots.Sort([](const TSharedPtr<FScriptTreeItem>& A, const TSharedPtr<FScriptTreeItem>& B)
	{
		const bool bAContent = A->Path == TEXT("/Game");
		const bool bBContent = B->Path == TEXT("/Game");
		if (bAContent != bBContent)
		{
			return bAContent; // Content first
		}
		return A->Name.Compare(B->Name, ESearchCase::IgnoreCase) < 0;
	});
	TreeRoots = Roots;
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
		.ToolTipText(FText::FromString(Path))
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
						return FBangoEditorStyle::GetScriptIcon();
					}
					const TSharedPtr<FScriptTreeItem> Pinned = WeakItem.Pin();
					return FAppStyle::GetBrush(Pinned.IsValid() && ScriptTree.IsValid() && ScriptTree->IsItemExpanded(Pinned)
						? "ContentBrowser.AssetTreeFolderOpen" : "ContentBrowser.AssetTreeFolderClosed");
				})
				.ColorAndOpacity(bFolder ? FSlateColor(FLinearColor(0.84f, 0.71f, 0.42f)) : FSlateColor(FLinearColor::White)) // folders in the Content Browser's tan; scripts in the icon's own colours
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

int32 SBangoScriptBrowser::FindTab(const TSharedRef<SDockTab>& DockTab) const
{
	return Tabs.IndexOfByPredicate([&DockTab](const FScriptTab& Tab) { return Tab.DockTab.Pin() == DockTab; });
}

void SBangoScriptBrowser::OpenScriptTab(const FString& Script)
{
	int32 Index = FindTab(Script);
	if (Index == INDEX_NONE)
	{
		const FSoftObjectPath ObjectPath(Script + TEXT(".") + FPackageName::GetLongPackageAssetName(Script));
		UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(ObjectPath.TryLoad());
		if (!Asset)
		{
			UE_LOG(LogTemp, Warning, TEXT("Bango: couldn't open %s: there's no Bango Script asset there any more."), *Script);
			QueueRefresh();
			return;
		}
		FScriptTab& Tab = Tabs.AddDefaulted_GetRef();
		Tab.Script = Script;
		SAssignNew(Tab.Editor, SBangoScriptEditor);
		Tab.Editor->OpenScriptAsset(Asset);
		const TSharedRef<SDockTab> DockTab = MakeScriptDockTab(Script, Tab.Editor.ToSharedRef());
		Tab.DockTab = DockTab;
		Index = Tabs.Num() - 1;
		// Beside the open script tabs, or where the last one was if none is open.
		TabManager->InsertNewDocumentTab(BangoScriptBrowserPrivate::ScriptDocumentTabId, FTabManager::ESearchPreference::PreferLiveTab, DockTab);
	}
	ActivateTab(Index);
}

void SBangoScriptBrowser::ActivateTab(int32 Index)
{
	if (!Tabs.IsValidIndex(Index))
	{
		return;
	}
	if (const TSharedPtr<SDockTab> DockTab = Tabs[Index].DockTab.Pin())
	{
		DockTab->ActivateInParent(ETabActivationCause::SetDirectly); // OnScriptTabActivated follows
	}
	ActiveTab = Index;
	SelectShownScript();
}

TSharedRef<SDockTab> SBangoScriptBrowser::MakeScriptDockTab(const FString& Script, const TSharedRef<SBangoScriptEditor>& Editor)
{
	const TWeakPtr<SBangoScriptEditor> WeakEditor = Editor;
	TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::DocumentTab)
		// The script's name, with a * while it has unsaved changes (as an asset's tab).
		.Label_Lambda([Script, WeakEditor]()
		{
			const TSharedPtr<SBangoScriptEditor> Pinned = WeakEditor.Pin();
			const FString Name = FPackageName::GetLongPackageAssetName(Script);
			return FText::FromString(Pinned.IsValid() && Pinned->HasUnsavedChanges() ? Name + TEXT("*") : Name);
		})
		.ToolTipText(FText::FromString(Script))
		// Closing (the tab's x, middle-click, or its menu) asks about unsaved changes first.
		.OnCanCloseTab_Lambda([WeakEditor]()
		{
			const TSharedPtr<SBangoScriptEditor> Pinned = WeakEditor.Pin();
			return !Pinned.IsValid() || Pinned->ConfirmCloseScript();
		})
		.OnTabClosed(this, &SBangoScriptBrowser::OnScriptTabClosed)
		.OnTabActivated(this, &SBangoScriptBrowser::OnScriptTabActivated)
		[
			Editor
		];
	DockTab->SetTabIcon(FBangoEditorStyle::GetScriptIcon());
	return DockTab;
}

void SBangoScriptBrowser::OnScriptTabActivated(TSharedRef<SDockTab> DockTab, ETabActivationCause Cause)
{
	const int32 Index = FindTab(DockTab);
	if (Index != INDEX_NONE && Index != ActiveTab)
	{
		ActiveTab = Index;
		SelectShownScript(); // the script picked by its tab is selected on the left
	}
}

void SBangoScriptBrowser::OnScriptTabClosed(TSharedRef<SDockTab> DockTab)
{
	const int32 Index = FindTab(DockTab);
	if (Index == INDEX_NONE)
	{
		return;
	}
	Tabs.RemoveAt(Index);
	// The shown script is now whichever tab is in front (the tab well shows another one when one closes).
	ActiveTab = Tabs.IndexOfByPredicate([](const FScriptTab& Tab)
	{
		const TSharedPtr<SDockTab> Pinned = Tab.DockTab.Pin();
		return Pinned.IsValid() && Pinned->IsForeground();
	});
	SelectShownScript();
}

#undef LOCTEXT_NAMESPACE
