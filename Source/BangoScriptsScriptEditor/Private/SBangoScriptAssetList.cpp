// SBangoScriptAssetList.cpp

#include "SBangoScriptAssetList.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BangoEditorStyle.h"
#include "BangoScriptAsset.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SBangoScriptAssetList"

void SBangoScriptAssetList::Construct(const FArguments& InArgs)
{
	OpenScript = InArgs._OpenScript;
	PickHint = InArgs._PickHint;
	OnGetWrittenPath = InArgs._OnGetWrittenPath;
	OnScriptPicked = InArgs._OnScriptPicked;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 4.0f)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search scripts"))
			.ToolTipText(LOCTEXT("SearchTooltip", "Shows only the scripts whose path contains this."))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				SearchText = NewText.ToString().TrimStartAndEnd();
				RebuildRows();
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FRow>>)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SBangoScriptAssetList::OnGenerateRow)
			.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FRow> Row)
			{
				if (Row.IsValid() && !Row->bFolder)
				{
					OnScriptPicked.ExecuteIfBound(Row->Path);
				}
			})
		]
	];

	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.OnAssetAdded().AddSP(this, &SBangoScriptAssetList::OnAssetChanged);
	Registry.OnAssetRemoved().AddSP(this, &SBangoScriptAssetList::OnAssetChanged);
	Registry.OnAssetRenamed().AddSP(this, &SBangoScriptAssetList::OnAssetRenamed);

	Refresh();
}

SBangoScriptAssetList::~SBangoScriptAssetList()
{
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		Registry->OnAssetAdded().RemoveAll(this);
		Registry->OnAssetRemoved().RemoveAll(this);
		Registry->OnAssetRenamed().RemoveAll(this);
	}
}

void SBangoScriptAssetList::OnAssetChanged(const FAssetData& AssetData)
{
	if (AssetData.IsInstanceOf(UBangoScriptAsset::StaticClass()))
	{
		QueueRefresh();
	}
}

void SBangoScriptAssetList::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	OnAssetChanged(AssetData);
}

void SBangoScriptAssetList::QueueRefresh()
{
	// Once, on the next frame, however many assets changed (the registry reports them one at a time).
	if (bRefreshQueued)
	{
		return;
	}
	bRefreshQueued = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Refresh();
		return EActiveTimerReturnType::Stop;
	}));
}

void SBangoScriptAssetList::Refresh()
{
	bRefreshQueued = false;

	// Every Bango Script asset, by its asset path ("/Game/Quests/Intro").
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByClass(UBangoScriptAsset::StaticClass()->GetClassPathName(), Assets);
	Scripts.Reset();
	for (const FAssetData& Asset : Assets)
	{
		Scripts.Add(Asset.PackageName.ToString());
	}
	// By folder, then by name: a folder's own scripts before its subfolders' is what sorting by folder path gives.
	Scripts.Sort([](const FString& A, const FString& B)
	{
		const FString FolderA = FPaths::GetPath(A);
		const FString FolderB = FPaths::GetPath(B);
		const int32 ByFolder = FolderA.Compare(FolderB, ESearchCase::IgnoreCase);
		return ByFolder != 0 ? ByFolder < 0 : A.Compare(B, ESearchCase::IgnoreCase) < 0;
	});
	RebuildRows();
}

void SBangoScriptAssetList::RebuildRows()
{
	Rows.Reset();
	FString LastFolder;
	bool bAnyFolder = false;
	for (const FString& Script : Scripts)
	{
		if (!SearchText.IsEmpty() && !Script.Contains(SearchText, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString Folder = FPaths::GetPath(Script);
		if (!bAnyFolder || !Folder.Equals(LastFolder, ESearchCase::CaseSensitive))
		{
			TSharedPtr<FRow> Heading = MakeShared<FRow>();
			Heading->bFolder = true;
			Heading->Path = Folder;
			Heading->Label = Folder;
			Rows.Add(Heading);
			LastFolder = Folder;
			bAnyFolder = true;
		}
		TSharedPtr<FRow> Row = MakeShared<FRow>();
		Row->Path = Script;
		Row->Label = FPaths::GetCleanFilename(Script);
		Rows.Add(Row);
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SBangoScriptAssetList::OnGenerateRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	if (Row->bFolder)
	{
		// A heading, not something to pick.
		return SNew(STableRow<TSharedPtr<FRow>>, Owner)
			.Padding(FMargin(6.0f, 6.0f, 6.0f, 2.0f))
			.ShowSelection(false)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row->Label))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	const FString Script = Row->Path;
	const auto IsOpenScript = [this, Script]() { return OpenScript.IsSet() && OpenScript.Get().Equals(Script, ESearchCase::CaseSensitive); };
	return SNew(STableRow<TSharedPtr<FRow>>, Owner)
		.Padding(FMargin(14.0f, 2.0f, 6.0f, 2.0f))
		.ToolTipText_Lambda([this, Script, IsOpenScript]()
		{
			const FString Written = OnGetWrittenPath.IsBound() ? OnGetWrittenPath.Execute(Script) : Script;
			FText Hint = PickHint.IsSet() ? PickHint.Get() : FText::GetEmpty();
			if (Hint.IsEmpty())
			{
				Hint = LOCTEXT("DefaultPickHint", "Double-click to use it.");
			}
			return IsOpenScript()
				? FText::Format(LOCTEXT("OpenScriptRowTooltip", "{0}\nThis is the open script.\n\n{1}\nWritten here as \"{2}\"."), FText::FromString(Script), Hint, FText::FromString(Written))
				: FText::Format(LOCTEXT("ScriptRowTooltip", "{0}\n\n{1}\nWritten here as \"{2}\"."), FText::FromString(Script), Hint, FText::FromString(Written));
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FBangoEditorStyle::GetScriptIcon())
				.ColorAndOpacity_Lambda([IsOpenScript]() { return IsOpenScript() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row->Label))
				.ColorAndOpacity_Lambda([IsOpenScript]() { return IsOpenScript() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground(); })
			]
		];
}

#undef LOCTEXT_NAMESPACE
