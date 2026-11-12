#include "SBangoScriptEditor.h"

#include "Bango.h"
#include "BangoCompiler.h"
#include "BangoEdGraphBuilder.h"
#include "BangoEdGraphNode.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoParser.h"
#include "BangoProgram.h"
#include "BangoSettings.h"
#include "BangoTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "GraphEditorDragDropAction.h"
#include "BangoEdGraphSchema.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "SBangoTextEditor.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/StyleColors.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "BangoGraphMenu.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "BangoEditorStyle.h"
#include "BangoCommand.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SBangoScriptEditor"

DEFINE_LOG_CATEGORY_STATIC(LogBangoScriptEditor, Log, All);

namespace BangoScriptEditorPrivate
{
	/** The height of a FUNCTIONS, INPUTS, GLOBALS or LOCALS row's content, and the padding the row adds above and below it. */
	static constexpr float VariableRowContentHeight = 24.0f;
	static constexpr float VariableRowPadding = 4.0f;
	/** The space above the script text pane's first line, the same for the text and its line numbers. */
	static constexpr float ScriptTextTopPadding = 2.0f;
}

FOptionalSize SBangoScriptEditor::VariableListHeight(int32 NumRows) const
{
	// The lists don't scroll on their own: the pane they're in does.
	return NumRows * (BangoScriptEditorPrivate::VariableRowContentHeight + BangoScriptEditorPrivate::VariableRowPadding);
}

void SBangoScriptEditor::Construct(const FArguments& InArgs)
{
	auto Heading = [](const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FAppStyle::GetFontStyle("BoldFont"));
	};

	GraphCommands = MakeShared<FUICommandList>();
	GraphCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanDeleteSelectedNodes));
	GraphCommands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanCopySelectedNodes));
	GraphCommands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanCutSelectedNodes));
	GraphCommands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::PasteNodes),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanPasteNodes));
	GraphCommands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::DuplicateSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanCopySelectedNodes));
	GraphCommands->MapAction(FGenericCommands::Get().Undo, FExecuteAction::CreateSP(this, &SBangoScriptEditor::Undo));
	GraphCommands->MapAction(FGenericCommands::Get().Redo, FExecuteAction::CreateSP(this, &SBangoScriptEditor::Redo));
	GraphCommands->MapAction(FGenericCommands::Get().SelectAll, FExecuteAction::CreateLambda([this]()
	{
		if (GraphEditor.IsValid())
		{
			GraphEditor->SelectAllNodes();
		}
	}));

	EmptyMessage = InArgs._EmptyMessage;

	auto SectionHeader = [&Heading](const FText& Text, TSharedRef<SWidget> Extra)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Heading(Text)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Extra
			];
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		// Top: the toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Reload", "Reload"))
					.ToolTipText(LOCTEXT("ReloadTooltip", "Read the script again from its .bango file, to pick up changes made to the text outside the graph editor. Asks first if there are unsaved changes."))
					.IsEnabled_Lambda([this]() { return !OpenScript.IsEmpty(); }) // also when the text didn't read, so it can be fixed and reloaded
					.OnClicked(this, &SBangoScriptEditor::OnReloadClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Compile", "Compile"))
					.ToolTipText(LOCTEXT("CompileTooltip", "Check the script for errors. The first error is shown on its node. Text typed in the script text pane is applied first."))
					.IsEnabled_Lambda([this]() { return EdGraphs.Num() > 0 || bTextEdited; })
					.OnClicked(this, &SBangoScriptEditor::OnCompileClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Save", "Save"))
					.ToolTipText(LOCTEXT("SaveTooltip", "Write the script to its .bango file (Ctrl+S). Text typed in the script text pane is applied first."))
					.IsEnabled_Lambda([this]() { return EdGraphs.Num() > 0 || bTextEdited; })
					.OnClicked(this, &SBangoScriptEditor::OnSaveClicked)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(TitleText, STextBlock)
					.Font(FAppStyle::GetFontStyle("BoldFont"))
					.Text(this, &SBangoScriptEditor::GetTitle)
				]
			]
		]

		// Below: the functions and variables, then the graph
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.18f)
			[
				// FUNCTIONS, INPUTS, GLOBALS and LOCALS, one after another in a single scrolling pane. Each list is as
				// tall as its rows, so only the pane scrolls.
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)

					// SCRIPT DESCRIPTION: the comment lines at the top of the file. Not shown in the graph.
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 4.0f)
					[
						Heading(LOCTEXT("DescriptionHeading", "SCRIPT DESCRIPTION"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 0.0f, 6.0f, 4.0f)
					[
						SAssignNew(DescriptionBox, SMultiLineEditableTextBox)
						.AutoWrapText(true)
						.HintText(LOCTEXT("DescriptionHint", "Describe what this script does."))
						.ToolTipText(LOCTEXT("DescriptionTooltip", "A description of the script, for designers. It's saved as the comment lines at the very top of the file, and isn't shown in the graph. Enter starts a new line; clicking elsewhere applies it. (A line starting with ==== would be read as a section heading, and lost.)"))
						.IsReadOnly_Lambda([this]() { return EdGraphs.Num() == 0; })
						.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
						{
							SetScriptDescription(NewText.ToString());
						})
					]

					// FUNCTIONS
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 10.0f, 6.0f, 4.0f)
					[
						SectionHeader(LOCTEXT("FunctionsHeading", "FUNCTIONS"),
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.0f, 0.0f))
							.ToolTipText(LOCTEXT("AddFunctionTooltip", "Add a function to this script."))
							.IsEnabled_Lambda([this]() { return EdGraphs.Num() > 0; })
							.OnClicked(this, &SBangoScriptEditor::OnAddFunctionClicked)
							[
								SNew(STextBlock).Text(LOCTEXT("AddFunction", "+ Add"))
							])
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride_Lambda([this]() { return VariableListHeight(FunctionItems.Num()); })
						[
							SAssignNew(FunctionList, SListView<TSharedPtr<FGraphEntry>>)
							.ListItemsSource(&FunctionItems)
							.SelectionMode(ESelectionMode::Single)
							.ScrollbarVisibility(EVisibility::Collapsed)
							.ConsumeMouseWheel(EConsumeMouseWheel::Never)
							.AllowOverscroll(EAllowOverscroll::No)
							.OnGenerateRow(this, &SBangoScriptEditor::OnGenerateFunctionRow)
							.OnSelectionChanged(this, &SBangoScriptEditor::OnFunctionSelected)
							.OnMouseButtonClick(this, &SBangoScriptEditor::OnFunctionClicked)
							.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FGraphEntry> Item)
							{
								if (TSharedPtr<SInlineEditableTextBlock> Name = Item.IsValid() ? Item->NameWidget.Pin() : nullptr)
								{
									Name->EnterEditingMode(); // double-click renames
								}
							})
						]
					]

					// INPUTS
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 10.0f, 6.0f, 4.0f)
					[
						SectionHeader(LOCTEXT("InputsHeading", "INPUTS"),
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.0f, 0.0f))
							.ToolTipText_Lambda([this]()
							{
								return ShownGraph > 0
									? LOCTEXT("AddFunctionInputTooltip", "Add a parameter to this function. Calls to it need a value for it.")
									: LOCTEXT("AddScriptInputTooltip", "Add a parameter to this script (#define params): a value it's spawned with.");
							})
							.IsEnabled_Lambda([this]() { return GetShownEntryNode() != nullptr; })
							.OnClicked(this, &SBangoScriptEditor::OnAddInputClicked)
							[
								SNew(STextBlock).Text(LOCTEXT("AddInput", "+ Add"))
							])
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride_Lambda([this]() { return VariableListHeight(InputItems.Num()); })
						[
							SAssignNew(InputList, SListView<TSharedPtr<FInputEntry>>)
							.ListItemsSource(&InputItems)
							.SelectionMode(ESelectionMode::Single)
							.ScrollbarVisibility(EVisibility::Collapsed)
							.ConsumeMouseWheel(EConsumeMouseWheel::Never)
							.AllowOverscroll(EAllowOverscroll::No)
							.OnGenerateRow(this, &SBangoScriptEditor::OnGenerateInputRow)
							.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FInputEntry> Item)
							{
								if (TSharedPtr<SInlineEditableTextBlock> Name = Item.IsValid() ? Item->NameWidget.Pin() : nullptr)
								{
									Name->EnterEditingMode(); // double-click renames
								}
							})
						]
					]

					// GLOBALS
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 10.0f, 6.0f, 4.0f)
					[
						SectionHeader(LOCTEXT("GlobalsHeading", "GLOBALS"),
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.0f, 0.0f))
							.ToolTipText(LOCTEXT("GlobalsHeadingAddTooltip", "Declare a global: a variable shared by every script, which keeps its value between levels. It starts as nil until something sets it."))
							.IsEnabled_Lambda([this]() { return GetShownEntryNode() != nullptr; })
							.OnClicked_Lambda([this]() { return OnAddDeclClicked(true); })
							[
								SNew(STextBlock).Text(LOCTEXT("GlobalsHeadingAdd", "+ Add"))
							])
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride_Lambda([this]() { return VariableListHeight(GlobalItems.Num()); })
						[
							SAssignNew(GlobalList, SListView<TSharedPtr<FDeclEntry>>)
							.ListItemsSource(&GlobalItems)
							.SelectionMode(ESelectionMode::Single)
							.ScrollbarVisibility(EVisibility::Collapsed)
							.ConsumeMouseWheel(EConsumeMouseWheel::Never)
							.AllowOverscroll(EAllowOverscroll::No)
							.OnGenerateRow(this, &SBangoScriptEditor::OnGenerateDeclRow)
							.OnMouseButtonClick(this, &SBangoScriptEditor::OnDeclClicked)
							.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FDeclEntry> Item)
							{
								if (TSharedPtr<SInlineEditableTextBlock> Name = Item.IsValid() ? Item->NameWidget.Pin() : nullptr)
								{
									Name->EnterEditingMode(); // double-click renames
								}
							})
						]
					]

					// LOCALS
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 10.0f, 6.0f, 4.0f)
					[
						SectionHeader(LOCTEXT("LocalsHeading", "LOCALS"),
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.0f, 0.0f))
							.ToolTipText(LOCTEXT("LocalsHeadingAddTooltip", "Add a local to this graph, with the value it starts with."))
							.IsEnabled_Lambda([this]() { return GetShownEntryNode() != nullptr; })
							.OnClicked_Lambda([this]() { return OnAddDeclClicked(false); })
							[
								SNew(STextBlock).Text(LOCTEXT("LocalsHeadingAdd", "+ Add"))
							])
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SBox)
						.HeightOverride_Lambda([this]() { return VariableListHeight(LocalItems.Num()); })
						[
							SAssignNew(LocalList, SListView<TSharedPtr<FDeclEntry>>)
							.ListItemsSource(&LocalItems)
							.SelectionMode(ESelectionMode::Single)
							.ScrollbarVisibility(EVisibility::Collapsed)
							.ConsumeMouseWheel(EConsumeMouseWheel::Never)
							.AllowOverscroll(EAllowOverscroll::No)
							.OnGenerateRow(this, &SBangoScriptEditor::OnGenerateDeclRow)
							.OnMouseButtonClick(this, &SBangoScriptEditor::OnDeclClicked)
							.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FDeclEntry> Item)
							{
								if (TSharedPtr<SInlineEditableTextBlock> Name = Item.IsValid() ? Item->NameWidget.Pin() : nullptr)
								{
									Name->EnterEditingMode(); // double-click renames
								}
							})
						]
					]
				]
			]

			+ SSplitter::Slot()
			.Value(0.82f)
			[
				SNew(SHorizontalBox)

				// The tab on the left edge that shows or hides the script text pane.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.0f, 8.0f))
							.ToolTipText_Lambda([this]()
							{
								return bTextPaneOpen
									? LOCTEXT("HideTextTooltip", "Hide the script's text.")
									: LOCTEXT("ShowTextTooltip", "Show the script's text beside the graph, where it can be edited too.");
							})
							.OnClicked_Lambda([this]()
							{
								bTextPaneOpen = !bTextPaneOpen;
								if (bTextPaneOpen)
								{
									UpdateScriptText(nullptr);
								}
								return FReply::Handled();
							})
							[
								// Written down the tab, a letter a line.
								SNew(STextBlock)
								.Text(LOCTEXT("TextTab", "T\nE\nX\nT"))
								.Justification(ETextJustify::Center)
								.Font(FAppStyle::GetFontStyle("BoldFont"))
								.ColorAndOpacity_Lambda([this]()
								{
									return bTextPaneOpen ? FSlateColor(FStyleColors::Primary) : FSlateColor::UseForeground();
								})
							]
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)

					// The script text pane. While it's hidden, the graph has the whole width.
					+ SSplitter::Slot()
					.Value(0.35f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() { return bTextPaneOpen ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							MakeTextPane()
						]
					]

					+ SSplitter::Slot()
					.Value(0.65f)
					[
						SNew(SVerticalBox)
						// Shown when the file changed outside the editor while there are unsaved changes here.
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBorder)
							.Visibility_Lambda([this]() { return bChangedOnDisk ? EVisibility::Visible : EVisibility::Collapsed; })
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							.BorderBackgroundColor(FLinearColor(1.0f, 0.75f, 0.2f))
							.Padding(FMargin(8.0f, 4.0f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return FText::Format(LOCTEXT("ChangedOnDiskBar", "{0} was changed outside the graph editor, and you have unsaved changes here."),
											FText::FromString(OpenScript + TEXT(".bango")));
									})
									.AutoWrapText(true)
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(8.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(SButton)
									.Text(LOCTEXT("ReloadFromDisk", "Reload"))
									.ToolTipText(LOCTEXT("ReloadFromDiskTooltip", "Read the changed file, throwing away your unsaved changes here."))
									.OnClicked_Lambda([this]()
									{
										ReloadKeepingView();
										return FReply::Handled();
									})
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SButton)
									.Text(LOCTEXT("KeepMine", "Keep Mine"))
									.ToolTipText(LOCTEXT("KeepMineTooltip", "Keep your changes. Saving will then overwrite the changed file."))
									.OnClicked_Lambda([this]()
									{
										bChangedOnDisk = false;
										KeptFileTime = IFileManager::Get().GetTimeStamp(*OpenPath);
										return FReply::Handled();
									})
								]
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(GraphHost, SBox)
						]
					]
				]
			]
		]
	];

	if (InArgs._Script.IsEmpty())
	{
		ShowMessage(EmptyMessage);
	}
	else
	{
		LoadScript(InArgs._Script);
	}
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::OnPoll));
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::SyncLineNumbers)); // every frame
}

SBangoScriptEditor::~SBangoScriptEditor()
{
	if (bDirty || bTextEdited)
	{
		WriteRecoveryCopy();
	}
}

FReply SBangoScriptEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::S && InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown())
	{
		if (EdGraphs.Num() > 0 || bTextEdited)
		{
			Save();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FString SBangoScriptEditor::GetScriptFolder()
{
	const FString Folder = FPaths::Combine(FPaths::ProjectContentDir(), GetDefault<UBangoSettings>()->ScriptDirectory);
	return FPaths::ConvertRelativePathToFull(Folder);
}

// ---- FUNCTIONS ----

void SBangoScriptEditor::RebuildFunctionItems()
{
	FunctionItems.Reset();
	for (int32 i = 1; i < GraphItems.Num(); ++i)
	{
		GraphItems[i]->FunctionIndex = i - 1; // EdGraphs[i] is function i - 1
		FunctionItems.Add(GraphItems[i]);
	}
	FunctionList->RequestListRefresh();
}

TSharedRef<ITableRow> SBangoScriptEditor::OnGenerateFunctionRow(TSharedPtr<FGraphEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	TWeakPtr<FGraphEntry> WeakItem = Item;
	TSharedRef<STableRow<TSharedPtr<FGraphEntry>>> Row = SNew(STableRow<TSharedPtr<FGraphEntry>>, Owner)
		.Padding(FMargin(6.0f, 2.0f))
		// Dragged onto a graph, a function makes a call to it (as the add-node menu's "This Script" entries do);
		// dropped on a pin, the call is wired to it.
		.OnDragDetected_Lambda([this, WeakItem](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			TSharedPtr<FGraphEntry> Entry = WeakItem.Pin();
			if (!Entry.IsValid() || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Unhandled();
			}
			// The press that started the drag selected this row; the list goes back to showing which graph is open.
			SelectShownFunction();
			TSharedPtr<FEdGraphSchemaAction> Action = MakeShared<FBangoSchemaAction_NewNode>(
				FText::GetEmpty(),
				FText::FromString(Entry->Label + TEXT("()")),
				FText::Format(LOCTEXT("CallFunctionTooltip", "Calls this script's function {0}."), FText::FromString(Entry->Label)),
				FText::GetEmpty(),
				(uint8)EBangoGraphNodeKind::Call,
				Entry->Label + TEXT("()"));
			return FReply::Handled().BeginDragDrop(FGraphSchemaActionDragDropAction::New(Action));
		});
	TSharedPtr<SInlineEditableTextBlock> Name;
	Row->SetContent(
		SNew(SBox)
		.HeightOverride(BangoScriptEditorPrivate::VariableRowContentHeight) // a fixed height, so the list can be as tall as its rows
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Name, SInlineEditableTextBlock)
				.Text_Lambda([WeakItem]() { TSharedPtr<FGraphEntry> Entry = WeakItem.Pin(); return FText::FromString(Entry.IsValid() ? Entry->Label : FString()); })
				.ToolTipText(LOCTEXT("FunctionRowTooltip", "Click to show this function. Double-click (or click it again) to rename it; calls to it are renamed too. Drag it onto a graph to add a call to it."))
				// Clicking the selected row again renames, as in Blueprint's My Blueprint panel.
				.IsSelected(Row, &STableRow<TSharedPtr<FGraphEntry>>::IsSelectedExclusively)
				.OnVerifyTextChanged_Lambda([this, WeakItem](const FText& NewText, FText& OutError)
				{
					TSharedPtr<FGraphEntry> Entry = WeakItem.Pin();
					const FString Problem = Entry.IsValid() ? CheckFunctionName(Entry->Label, NewText.ToString().TrimStartAndEnd()) : FString();
					OutError = FText::FromString(Problem);
					return Problem.IsEmpty();
				})
				.OnTextCommitted_Lambda([this, WeakItem](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnCleared)
					{
						RenameFunction(WeakItem.Pin(), NewText.ToString().TrimStartAndEnd());
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.0f, 0.0f))
				.ToolTipText(LOCTEXT("DeleteFunctionTooltip", "Delete this function (asks first)."))
				.OnClicked_Lambda([this, WeakItem]()
				{
					DeleteFunction(WeakItem.Pin());
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("×")))
				]
			]
		]);
	if (Item.IsValid())
	{
		Item->NameWidget = Name;
	}
	return Row;
}

void SBangoScriptEditor::OnFunctionSelected(TSharedPtr<FGraphEntry> Item, ESelectInfo::Type SelectInfo)
{
	// Only the keyboard shows a graph here. A mouse press selects the row before anyone knows whether it's a click
	// or the start of a drag, so clicks show their graph in OnFunctionClicked, which a drag never reaches. Selections
	// made in code (ShowGraph) already show their graph.
	if (Item.IsValid() && (SelectInfo == ESelectInfo::OnKeyPress || SelectInfo == ESelectInfo::OnNavigation))
	{
		ShowGraph(Item->FunctionIndex + 1);
	}
}

void SBangoScriptEditor::OnFunctionClicked(TSharedPtr<FGraphEntry> Item)
{
	const int32 GraphIndex = Item.IsValid() ? Item->FunctionIndex + 1 : INDEX_NONE;
	// Clicking the shown function again is how it's renamed, so it leaves the view alone.
	if (GraphIndex > 0 && GraphIndex != ShownGraph && !FSlateApplication::Get().IsDragDropping())
	{
		ShowGraph(GraphIndex);
	}
}

void SBangoScriptEditor::SelectShownFunction()
{
	if (ShownGraph > 0 && GraphItems.IsValidIndex(ShownGraph))
	{
		FunctionList->SetSelection(GraphItems[ShownGraph], ESelectInfo::Direct);
	}
	else
	{
		FunctionList->ClearSelection();
	}
}

FString SBangoScriptEditor::CheckFunctionName(const FString& OldName, const FString& NewName) const
{
	if (NewName.Equals(OldName, ESearchCase::CaseSensitive))
	{
		return FString();
	}
	if (NewName.IsEmpty())
	{
		return TEXT("A function needs a name.");
	}
	for (const TSharedPtr<FGraphEntry>& Entry : FunctionItems)
	{
		if (Entry.IsValid() && Entry->Label.Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("This script already has a function called %s."), *NewName);
		}
	}
	if (Bango::IsBuiltinCommandName(NewName) || FBangoModule::Get().GetCommands().FindCommand(NewName))
	{
		return FString::Printf(TEXT("%s is a command, so a function can't have that name."), *NewName);
	}
	// The rest (letters, digits and _, not a keyword) is what the signature must read as.
	const TSharedPtr<FGraphEntry>* Item = FunctionItems.FindByPredicate([&OldName](const TSharedPtr<FGraphEntry>& Entry) { return Entry.IsValid() && Entry->Label == OldName; });
	const int32 GraphIndex = Item ? (*Item)->FunctionIndex + 1 : INDEX_NONE;
	const UBangoEdGraphNode* FunctionEntry = EdGraphs.IsValidIndex(GraphIndex) && EdGraphs[GraphIndex]->Nodes.Num() > 0 ? Cast<UBangoEdGraphNode>(EdGraphs[GraphIndex]->Nodes[0]) : nullptr;
	if (FunctionEntry)
	{
		FString Signature = FunctionEntry->Text;
		FString Problem;
		if (!Bango::RenameFunctionInText(Signature, OldName, NewName) || !Bango::ValidateNodeText(EBangoGraphNodeKind::Entry, /*bInFunctionGraph*/ true, Signature, Problem))
		{
			return FString::Printf(TEXT("%s can't be a function's name: use letters, digits and _, not starting with a digit, and not a keyword."), *NewName);
		}
	}
	return FString();
}

void SBangoScriptEditor::RenameFunction(TSharedPtr<FGraphEntry> Item, const FString& NewName)
{
	if (!Item.IsValid() || NewName.Equals(Item->Label, ESearchCase::CaseSensitive) || !CheckFunctionName(Item->Label, NewName).IsEmpty())
	{
		return;
	}
	// One undoable edit: the function's signature, and every call to it in every graph.
	const FScopedTransaction Transaction(LOCTEXT("RenameFunction", "Rename Bango Function"));
	const FString OldName = Item->Label;
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
			FString Text = BangoNode ? BangoNode->Text : FString();
			if (BangoNode && Bango::RenameFunctionInText(Text, OldName, NewName))
			{
				BangoNode->Modify();
				BangoNode->Text = Text;
				BangoNode->UpdateLayout();
				Graph->NotifyNodeChanged(BangoNode);
			}
		}
	}
	Item->Label = NewName; // the poll would catch up too; this shows it straight away
}

FReply SBangoScriptEditor::OnAddFunctionClicked()
{
	if (EdGraphs.Num() == 0)
	{
		return FReply::Handled();
	}
	TSet<FString> Used;
	for (const TSharedPtr<FGraphEntry>& Entry : FunctionItems)
	{
		Used.Add(Entry->Label);
	}
	FString Name = TEXT("NewFunction");
	for (int32 Suffix = 2; Used.Contains(Name) || Bango::IsBuiltinCommandName(Name) || FBangoModule::Get().GetCommands().FindCommand(Name); ++Suffix)
	{
		Name = FString::Printf(TEXT("NewFunction%d"), Suffix);
	}

	FBangoGraph Function;
	Function.bIsFunction = true;
	Function.Name = Name;
	FBangoGraphNode& Entry = Function.Nodes.AddDefaulted_GetRef();
	Entry.Kind = EBangoGraphNodeKind::Entry;
	Entry.Text = FString::Printf(TEXT("void %s()"), *Name);
	EdGraphs.Add(TStrongObjectPtr<UEdGraph>(BangoScriptEditor::CreateEdGraph(Function, LayoutContext)));
	TSharedPtr<FGraphEntry> Item = MakeShared<FGraphEntry>();
	Item->Label = Name;
	GraphItems.Add(Item);
	RebuildFunctionItems();
	ShowGraph(EdGraphs.Num() - 1);

	// Rename it straight away, once its row exists (next frame).
	TWeakPtr<FGraphEntry> WeakItem = Item;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakItem](double, float)
	{
		TSharedPtr<FGraphEntry> Pinned = WeakItem.Pin();
		if (TSharedPtr<SInlineEditableTextBlock> NameWidget = Pinned.IsValid() ? Pinned->NameWidget.Pin() : nullptr)
		{
			NameWidget->EnterEditingMode();
		}
		return EActiveTimerReturnType::Stop;
	}));
	return FReply::Handled();
}

void SBangoScriptEditor::DeleteFunction(TSharedPtr<FGraphEntry> Item)
{
	const int32 GraphIndex = Item.IsValid() ? Item->FunctionIndex + 1 : INDEX_NONE;
	if (GraphIndex <= 0 || !EdGraphs.IsValidIndex(GraphIndex))
	{
		return;
	}
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
		LOCTEXT("DeleteFunctionConfirm", "Delete the function {0}, and everything in its graph?\n\nCalls to it stay where they are, and won't compile until they're removed. This can't be undone with Undo (until you save, Reload brings it back)."),
		FText::FromString(Item->Label)));
	if (Answer != EAppReturnType::Yes)
	{
		return;
	}
	const bool bWasShown = ShownGraph == GraphIndex;
	EdGraphs.RemoveAt(GraphIndex);
	GraphItems.RemoveAt(GraphIndex);
	RebuildFunctionItems();
	if (bWasShown)
	{
		ShowGraph(0);
	}
	else if (ShownGraph > GraphIndex)
	{
		--ShownGraph; // the same graph, one place earlier
	}
}

// ---- INPUTS ----

namespace BangoScriptEditorPrivate
{
	/** The types the INPUTS type menu offers (and arrays of each). */
	static const TCHAR* const InputTypes[] = { TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("string"), TEXT("entity"), TEXT("text"), TEXT("vector2"), TEXT("vector3") };

	/** The icon of a type, as the nodes' fields show it; the plain variable icon when the type isn't known. */
	static const FSlateBrush* GetTypeIcon(const FString& Type, FLinearColor& OutTint)
	{
		const FSlateBrush* Icon = Type.IsEmpty() ? nullptr : FBangoEditorStyle::GetTypeIcon(Type, OutTint);
		return Icon ? Icon : FBangoEditorStyle::GetVariableIcon(Type, OutTint);
	}

	/**
	 * Dragging a variable from the VARIABLES or INPUTS list onto the graph: dropping it opens a menu of the nodes
	 * that work on it (Set, Add To, Increment, Push, ...; see Bango::GetVariableNodeEntries), and the one picked is
	 * placed where it was dropped, with the variable filled in. Dropped on a pin, the node is wired to it.
	 */
	class FBangoVariableDragDrop : public FGraphEditorDragDropAction
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FBangoVariableDragDrop, FGraphEditorDragDropAction)

		static TSharedRef<FBangoVariableDragDrop> New(const TSharedRef<SWidget>& InEditor, const FString& InName, const FString& InType, const TSet<FString>& InUsed)
		{
			TSharedRef<FBangoVariableDragDrop> Op = MakeShared<FBangoVariableDragDrop>();
			Op->Editor = InEditor;
			Op->VarName = InName;
			Op->VarType = InType;
			Op->Used = InUsed;
			Op->Construct(); // makes the cursor window, then calls HoverTargetChanged
			return Op;
		}

		virtual void HoverTargetChanged() override
		{
			// The feedback goes in the cursor window, which only exists once Construct has made it (and is gone
			// again after the drop).
			if (!CursorDecoratorWindow.IsValid())
			{
				return;
			}
			FLinearColor Tint = FLinearColor::White;
			const FSlateBrush* Icon = GetTypeIcon(VarType, Tint);
			SetSimpleFeedbackMessage(Icon, Tint, GetHoveredGraph()
				? FText::Format(LOCTEXT("DropVariable", "Drop to add a node for {0}"), FText::FromString(VarName))
				: FText::FromString(VarName));
		}

		virtual FReply DroppedOnPanel(const TSharedRef<SWidget>& Panel, const FVector2f& ScreenPosition, const FVector2f& GraphPosition, UEdGraph& Graph) override
		{
			ShowMenu(Panel, ScreenPosition, GraphPosition, &Graph, nullptr);
			return FReply::Handled();
		}

		virtual FReply DroppedOnPin(const FVector2f& ScreenPosition, const FVector2f& GraphPosition) override
		{
			return DroppedOnGraphItem(ScreenPosition, GraphPosition, GetHoveredPin());
		}

		virtual FReply DroppedOnNode(const FVector2f& ScreenPosition, const FVector2f& GraphPosition) override
		{
			return DroppedOnGraphItem(ScreenPosition, GraphPosition, nullptr);
		}

	private:
		TWeakPtr<SWidget> Editor;
		FString VarName;
		FString VarType;
		/** Names in the graph, so a For Each's loop variable doesn't clash. */
		TSet<FString> Used;

		FReply DroppedOnGraphItem(const FVector2f& ScreenPosition, const FVector2f& GraphPosition, UEdGraphPin* Pin)
		{
			UEdGraph* Graph = GetHoveredGraph();
			TSharedPtr<SWidget> Parent = Editor.Pin();
			if (!Graph || !Parent.IsValid())
			{
				return FReply::Unhandled();
			}
			ShowMenu(Parent.ToSharedRef(), ScreenPosition, GraphPosition, Graph, Pin);
			return FReply::Handled();
		}

		void ShowMenu(const TSharedRef<SWidget>& Parent, const FVector2f& ScreenPosition, const FVector2f& GraphPosition, UEdGraph* Graph, UEdGraphPin* Pin) const
		{
			TArray<FBangoNodeMenuEntry> Entries;
			Bango::GetVariableNodeEntries(VarName, VarType, Used, Entries);
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
			Menu.BeginSection(NAME_None, FText::FromString(VarType.IsEmpty() ? VarName : FString::Printf(TEXT("%s (%s)"), *VarName, *VarType)));
			const TWeakObjectPtr<UEdGraph> WeakGraph = Graph;
			FEdGraphPinReference PinRef;
			if (Pin)
			{
				PinRef = FEdGraphPinReference(Pin);
			}
			for (const FBangoNodeMenuEntry& Entry : Entries)
			{
				const uint8 Kind = (uint8)Entry.Kind;
				const FString Text = Entry.Text;
				const FText Title = FText::FromString(Entry.Title);
				const FText Description = FText::FromString(Entry.Description);
				Menu.AddMenuEntry(Title, Description, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakGraph, PinRef, GraphPosition, Kind, Text, Title, Description]()
				{
					if (UEdGraph* Target = WeakGraph.Get())
					{
						FBangoSchemaAction_NewNode Action(FText::GetEmpty(), Title, Description, FText::GetEmpty(), Kind, Text);
						Action.PerformAction(Target, PinRef.Get(), GraphPosition);
					}
				})));
			}
			Menu.EndSection();
			FSlateApplication::Get().PushMenu(Parent, FWidgetPath(), Menu.MakeWidget(), ScreenPosition, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
	};

	/** Dragging an input by its handle, to move it up or down the list. */
	class FBangoInputDragDrop : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FBangoInputDragDrop, FDecoratedDragDropOp)

		/** The script editor it came from: rows only move within their own list. */
		TWeakPtr<SWidget> Editor;
		/** Which list: 0 Inputs, 1 Globals, 2 Locals. */
		int32 List = 0;
		int32 Index = INDEX_NONE;

		static TSharedRef<FBangoInputDragDrop> New(const TSharedRef<SWidget>& InEditor, int32 InList, int32 InIndex, const FString& Name)
		{
			TSharedRef<FBangoInputDragDrop> Op = MakeShared<FBangoInputDragDrop>();
			Op->Editor = InEditor;
			Op->List = InList;
			Op->Index = InIndex;
			Op->MouseCursor = EMouseCursor::GrabHandClosed;
			Op->CurrentHoverText = FText::FromString(Name);
			Op->SetupDefaults();
			Op->Construct();
			return Op;
		}
	};

	/** The grab handle on the left of an input's row, as on an array element in the details panel. */
	class SBangoInputHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBangoInputHandle) {}
			SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			DragDetected = InArgs._OnDragDetected;
			SetCursor(EMouseCursor::GrabHand);
			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
				? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton)
				: FReply::Unhandled();
		}

		virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			return DragDetected.IsBound() ? DragDetected.Execute(MyGeometry, MouseEvent) : FReply::Unhandled();
		}

	private:
		FOnDragDetected DragDetected;
	};

	/** A variable's name (a parameter, local or global): letters, digits and `_`, not starting with a digit, and not a keyword. A leading `_` is allowed, not needed. */
	static bool IsVariableName(const FString& Name)
	{
		return Bango::IsValidIdentifier(Name) && !Bango::IsReservedWord(Name);
	}
}

TSet<FString> SBangoScriptEditor::GetShownGraphNames() const
{
	TSet<FString> Names;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Names.Add(Item->Param.Name);
	}
	for (const TSharedPtr<FBangoGraphVariable>& Variable : VariableItems)
	{
		Names.Add(Variable->Name);
	}
	return Names;
}

UBangoEdGraphNode* SBangoScriptEditor::GetShownEntryNode() const
{
	if (!EdGraphs.IsValidIndex(ShownGraph))
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : EdGraphs[ShownGraph]->Nodes)
	{
		UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
		if (BangoNode && BangoNode->IsEntry())
		{
			return BangoNode;
		}
	}
	return nullptr;
}

void SBangoScriptEditor::UpdateInputs()
{
	const UBangoEdGraphNode* Entry = GetShownEntryNode();
	const FString Text = Entry ? Entry->Text : FString();
	if (InputsGraph == ShownGraph && InputsText == Text && (Entry != nullptr || InputItems.Num() == 0))
	{
		return;
	}
	InputsGraph = ShownGraph;
	InputsText = Text;

	TArray<FBangoEntryParam> Params;
	if (Entry)
	{
		Bango::ReadEntryParams(Text, ShownGraph > 0, Params);
	}
	InputItems.Reset();
	for (int32 i = 0; i < Params.Num(); ++i)
	{
		TSharedPtr<FInputEntry> Item = MakeShared<FInputEntry>();
		Item->Index = i;
		Item->Param = Params[i];
		InputItems.Add(Item);
	}
	if (InputList.IsValid())
	{
		InputList->RequestListRefresh();
	}

	// A new input is renamed straight away, once its row exists (next frame).
	if (InputItems.IsValidIndex(PendingInputRename))
	{
		TWeakPtr<FInputEntry> WeakItem = InputItems[PendingInputRename];
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakItem](double, float)
		{
			TSharedPtr<FInputEntry> Pinned = WeakItem.Pin();
			if (TSharedPtr<SInlineEditableTextBlock> NameWidget = Pinned.IsValid() ? Pinned->NameWidget.Pin() : nullptr)
			{
				NameWidget->EnterEditingMode();
			}
			return EActiveTimerReturnType::Stop;
		}));
	}
	PendingInputRename = INDEX_NONE;
}

TSharedRef<ITableRow> SBangoScriptEditor::OnGenerateInputRow(TSharedPtr<FInputEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	using namespace BangoScriptEditorPrivate;
	const int32 Index = Item.IsValid() ? Item->Index : INDEX_NONE;
	const FBangoEntryParam Param = Item.IsValid() ? Item->Param : FBangoEntryParam();
	FLinearColor Tint = FLinearColor::White;
	const FSlateBrush* Icon = BangoScriptEditorPrivate::GetTypeIcon(Param.Type, Tint);

	// Dropping another input on this row's top half puts it above this one; on the bottom half, below.
	typedef STableRow<TSharedPtr<FInputEntry>> FInputRow;
	TSharedRef<TWeakPtr<FInputRow>> RowHolder = MakeShared<TWeakPtr<FInputRow>>();
	auto GetDropZone = [RowHolder](const FDragDropEvent& Event, EItemDropZone Zone)
	{
		if (TSharedPtr<FInputRow> Pinned = RowHolder->Pin())
		{
			const FGeometry& Geometry = Pinned->GetTickSpaceGeometry();
			return Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y < Geometry.GetLocalSize().Y * 0.5f ? EItemDropZone::AboveItem : EItemDropZone::BelowItem;
		}
		return Zone == EItemDropZone::AboveItem ? EItemDropZone::AboveItem : EItemDropZone::BelowItem;
	};
	TSharedRef<FInputRow> Row = SNew(FInputRow, Owner)
		.Padding(FMargin(2.0f, 2.0f, 6.0f, 2.0f))
		// Dragged by anything but its handle, a parameter goes onto the graph like a variable from VARIABLES.
		.OnDragDetected_Lambda([this, Param](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Unhandled();
			}
			return FReply::Handled().BeginDragDrop(FBangoVariableDragDrop::New(AsShared(), Param.Name, Param.Type, GetShownGraphNames()));
		})
		.OnCanAcceptDrop_Lambda([this, Index, GetDropZone](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FInputEntry>) -> TOptional<EItemDropZone>
		{
			TSharedPtr<FBangoInputDragDrop> Op = Event.GetOperationAs<FBangoInputDragDrop>();
			if (!Op.IsValid() || Op->Editor.Pin().Get() != this || Op->List != 0)
			{
				return TOptional<EItemDropZone>();
			}
			const EItemDropZone Half = GetDropZone(Event, Zone);
			const int32 To = Half == EItemDropZone::AboveItem ? Index : Index + 1;
			if (To == Op->Index || To == Op->Index + 1)
			{
				return TOptional<EItemDropZone>(); // where it already is
			}
			return Half;
		})
		.OnAcceptDrop_Lambda([this, Index, GetDropZone](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FInputEntry>)
		{
			TSharedPtr<FBangoInputDragDrop> Op = Event.GetOperationAs<FBangoInputDragDrop>();
			if (!Op.IsValid() || Op->Editor.Pin().Get() != this || Op->List != 0)
			{
				return FReply::Unhandled();
			}
			MoveInput(Op->Index, GetDropZone(Event, Zone) == EItemDropZone::AboveItem ? Index : Index + 1);
			return FReply::Handled();
		});
	*RowHolder = Row;

	// The main graph's parameters can have defaults, edited only here. The box works like a node's field: a string
	// is typed without quotes. Required parameters must come before optional ones, so a required parameter can only
	// get a default if every parameter after it already has one (it's the last required one); otherwise its box is
	// locked. (Clearing an optional one's default can still be refused, if an optional one comes before it.)
	TSharedRef<SWidget> DefaultBox = SNullWidget::NullWidget;
	if (ShownGraph == 0)
	{
		bool bCanEditDefault = Param.bHasDefault;
		if (!bCanEditDefault)
		{
			bCanEditDefault = true;
			for (int32 Later = Index + 1; Later < InputItems.Num(); ++Later)
			{
				bCanEditDefault = bCanEditDefault && InputItems[Later]->Param.bHasDefault;
			}
		}
		const UBangoEdGraphNode* Entry = GetShownEntryNode();
		const FBangoNodeField* Field = Entry ? Entry->Layout.Fields.FindByPredicate([&Param](const FBangoNodeField& Each)
		{
			return !Each.bLocNamespace && Each.Label == Param.Name;
		}) : nullptr;
		DefaultBox = SNew(SEditableTextBox)
				.Text(FText::FromString(Field ? Bango::GetFieldEditText(*Field) : Param.Default))
				.HintText(LOCTEXT("InputRequiredHint", "required"))
				.IsEnabled(bCanEditDefault)
				.ToolTipText(bCanEditDefault
					? LOCTEXT("InputDefaultTooltip", "The default, used when the script is spawned without this parameter. Leave it empty to make the parameter required; nil makes it optional with no particular value. Required parameters must come first.")
					: LOCTEXT("InputDefaultLockedTooltip", "Required: a parameter after this one is required too, and required parameters must come before optional ones. Give the later ones defaults first, or drag this one below them."))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.OnTextCommitted_Lambda([this, Index](const FText& NewText, ETextCommit::Type CommitType)
				{
					SetInputDefault(Index, NewText.ToString());
				});
	}

	TSharedPtr<SInlineEditableTextBlock> Name;
	Row->SetContent(
		SNew(SBox)
		.HeightOverride(VariableRowContentHeight) // a fixed height, so the list can be as tall as its rows
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// The grab handle: drag it up or down to reorder.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SBangoInputHandle)
				.ToolTipText(LOCTEXT("InputHandleTooltip", "Drag up or down to reorder."))
				.OnDragDetected_Lambda([this, Index, ParamName = Param.Name](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						return FReply::Unhandled();
					}
					return FReply::Handled().BeginDragDrop(FBangoInputDragDrop::New(AsShared(), 0, Index, ParamName));
				})
			]
			// The type's icon is a button: its menu changes the type.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.HasDownArrow(false)
				.ContentPadding(FMargin(2.0f, 1.0f))
				.ToolTipText(FText::Format(LOCTEXT("InputTypeTooltip", "{0}. Click to change the type."), FText::FromString(Param.Type)))
				.OnGetMenuContent_Lambda([this, Index]() { return MakeInputTypeMenu(Index); })
				.ButtonContent()
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(Tint)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Name, SInlineEditableTextBlock)
				.Text(FText::FromString(Param.Name))
				.ToolTipText(LOCTEXT("InputNameTooltip", "Double-click (or click it again) to rename it; its uses in this graph are renamed too."))
				.IsSelected(Row, &FInputRow::IsSelectedExclusively)
				.OnVerifyTextChanged_Lambda([this, Index](const FText& NewText, FText& OutError)
				{
					const FString Problem = CheckInputName(Index, NewText.ToString().TrimStartAndEnd());
					OutError = FText::FromString(Problem);
					return Problem.IsEmpty();
				})
				.OnTextCommitted_Lambda([this, Index](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnCleared)
					{
						RenameInput(Index, NewText.ToString().TrimStartAndEnd());
					}
				})
			]
			// The type and default columns have fixed widths, so they line up from row to row.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Param.Type))
					.ToolTipText(FText::FromString(Param.Type))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(ShownGraph == 0 ? 96.0f : 0.0f)
				[
					DefaultBox
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.0f, 0.0f))
				.ToolTipText(LOCTEXT("DeleteInputTooltip", "Remove this parameter. Its uses in the graph are left as they are."))
				.OnClicked_Lambda([this, Index]()
				{
					DeleteInput(Index);
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("×")))
				]
			]
		]);
	if (Item.IsValid())
	{
		Item->NameWidget = Name;
	}
	return Row;
}

TSharedRef<SWidget> SBangoScriptEditor::MakeInputTypeMenu(int32 Index)
{
	const FString Current = InputItems.IsValidIndex(Index) ? InputItems[Index]->Param.Type : FString();
	return MakeTypeMenu(Current, [this, Index](const FString& Type) { SetInputType(Index, Type); });
}

TSharedRef<SWidget> SBangoScriptEditor::MakeTypeMenu(const FString& Current, TFunction<void(const FString&)> OnPick)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	auto AddType = [&Menu, &Current, &OnPick](const FString& Type)
	{
		FLinearColor Tint = FLinearColor::White;
		const FSlateBrush* TypeIcon = BangoScriptEditorPrivate::GetTypeIcon(Type, Tint);
		Menu.AddMenuEntry(
			FUIAction(
				FExecuteAction::CreateLambda([OnPick, Type]() { OnPick(Type); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Current, Type]() { return Current == Type; })),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(TypeIcon)
				.ColorAndOpacity(Tint)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Type))
			],
			NAME_None, TAttribute<FText>(), EUserInterfaceActionType::RadioButton);
	};
	Menu.BeginSection(NAME_None, LOCTEXT("InputTypes", "Type"));
	for (const TCHAR* Type : BangoScriptEditorPrivate::InputTypes)
	{
		AddType(Type);
	}
	Menu.EndSection();
	Menu.BeginSection(NAME_None, LOCTEXT("InputArrayTypes", "Array of"));
	for (const TCHAR* Type : BangoScriptEditorPrivate::InputTypes)
	{
		AddType(FString(Type) + TEXT("[]"));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

FReply SBangoScriptEditor::OnAddInputClicked()
{
	if (!GetShownEntryNode())
	{
		return FReply::Handled();
	}
	TArray<FBangoEntryParam> Params;
	TSet<FString> Used;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
		Used.Add(Item->Param.Name);
	}
	for (const TSharedPtr<FBangoGraphVariable>& Variable : VariableItems)
	{
		Used.Add(Variable->Name);
	}
	FBangoEntryParam& Added = Params.AddDefaulted_GetRef();
	Added.Type = TEXT("bool");
	Added.Name = Bango::MakeUniqueName(TEXT("_newParam"), Used);
	// Required parameters must come first, so one added after an optional one is optional too (= nil).
	if (ShownGraph == 0 && Params.Num() > 1 && Params[Params.Num() - 2].bHasDefault)
	{
		Added.bHasDefault = true;
		Added.Default = TEXT("nil");
	}
	PendingInputRename = Params.Num() - 1;
	if (!ApplyInputs(Params, LOCTEXT("AddInputTransaction", "Add Bango Parameter")))
	{
		PendingInputRename = INDEX_NONE;
	}
	return FReply::Handled();
}

void SBangoScriptEditor::DeleteInput(int32 Index)
{
	if (!InputItems.IsValidIndex(Index))
	{
		return;
	}
	TArray<FBangoEntryParam> Params;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
	}
	Params.RemoveAt(Index);
	ApplyInputs(Params, LOCTEXT("DeleteInputTransaction", "Remove Bango Parameter"));
}

void SBangoScriptEditor::SetInputType(int32 Index, const FString& Type)
{
	if (!InputItems.IsValidIndex(Index) || InputItems[Index]->Param.Type == Type)
	{
		return;
	}
	TArray<FBangoEntryParam> Params;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
	}
	Params[Index].Type = Type;
	// A default made for the old type rarely fits the new one; nil fits every type and keeps it optional.
	if (Params[Index].bHasDefault)
	{
		Params[Index].Default = TEXT("nil");
	}
	ApplyInputs(Params, LOCTEXT("SetInputTypeTransaction", "Change Bango Parameter Type"));
}

void SBangoScriptEditor::MoveInput(int32 From, int32 To)
{
	if (!InputItems.IsValidIndex(From) || To < 0 || To > InputItems.Num())
	{
		return;
	}
	TArray<FBangoEntryParam> Params;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
	}
	const FBangoEntryParam Moved = Params[From];
	Params.RemoveAt(From);
	const int32 Insert = From < To ? To - 1 : To; // To counted the moved one, which is gone now
	if (Insert == From)
	{
		return;
	}
	Params.Insert(Moved, Insert);
	ApplyInputs(Params, LOCTEXT("MoveInputTransaction", "Reorder Bango Parameters"));
}

void SBangoScriptEditor::SetInputDefault(int32 Index, const FString& Typed)
{
	const UBangoEdGraphNode* Entry = GetShownEntryNode();
	if (!Entry || ShownGraph != 0 || !InputItems.IsValidIndex(Index))
	{
		return;
	}
	TArray<FBangoEntryParam> Params;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
	}
	FBangoEntryParam& Param = Params[Index];
	const FBangoNodeField* Field = Entry->Layout.Fields.FindByPredicate([&Param](const FBangoNodeField& Each)
	{
		return !Each.bLocNamespace && Each.Label == Param.Name;
	});
	// As the node's field did: a string or text is typed as plain text (or =expression), anything else as code.
	const FString Value = (Field ? Bango::GetFieldValueFromTyped(*Field, Typed) : Typed).TrimStartAndEnd();
	const bool bHasDefault = !Value.IsEmpty();
	if (bHasDefault == Param.bHasDefault && (!bHasDefault || Value == Param.Default))
	{
		return;
	}
	Param.bHasDefault = bHasDefault;
	Param.Default = Value;
	if (!ApplyInputs(Params, LOCTEXT("SetInputDefaultTransaction", "Change Bango Parameter Default")))
	{
		// Refused (like a required parameter after an optional one): the box goes back to what's in the node.
		InputsText.Reset();
		UpdateInputs();
	}
}

FString SBangoScriptEditor::CheckInputName(int32 Index, const FString& NewName) const
{
	if (!InputItems.IsValidIndex(Index) || NewName.Equals(InputItems[Index]->Param.Name, ESearchCase::CaseSensitive))
	{
		return FString();
	}
	if (!BangoScriptEditorPrivate::IsVariableName(NewName))
	{
		return TEXT("A parameter's name has only letters, digits and _, doesn't start with a digit, and isn't a keyword (like who or _who).");
	}
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		if (Item->Param.Name.Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("There's already a parameter called %s."), *NewName);
		}
	}
	for (const TSharedPtr<FBangoGraphVariable>& Variable : VariableItems)
	{
		if ((Variable->Kind == FBangoGraphVariable::EKind::Local || Variable->Kind == FBangoGraphVariable::EKind::Loop) && Variable->Name.Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("This graph already has a local called %s."), *NewName);
		}
	}
	return FString();
}

void SBangoScriptEditor::RenameInput(int32 Index, const FString& NewName)
{
	if (!InputItems.IsValidIndex(Index) || NewName.Equals(InputItems[Index]->Param.Name, ESearchCase::CaseSensitive) || !CheckInputName(Index, NewName).IsEmpty())
	{
		return;
	}
	TArray<FBangoEntryParam> Params;
	for (const TSharedPtr<FInputEntry>& Item : InputItems)
	{
		Params.Add(Item->Param);
	}
	const FString OldName = Params[Index].Name;
	Params[Index].Name = NewName;
	ApplyInputs(Params, LOCTEXT("RenameInputTransaction", "Rename Bango Parameter"), OldName, NewName);
}

bool SBangoScriptEditor::ApplyInputs(const TArray<FBangoEntryParam>& Params, const FText& Description, const FString& RenamedFrom, const FString& RenamedTo)
{
	UBangoEdGraphNode* Entry = GetShownEntryNode();
	if (!Entry)
	{
		return false;
	}
	FString Text, Error;
	if (!Bango::WriteEntryParams(Entry->Text, ShownGraph > 0, Params, Text, Error))
	{
		Notify(FText::Format(LOCTEXT("InputsFailed", "Couldn't change the parameters: {0}"), FText::FromString(Error)), false);
		return false;
	}

	const FScopedTransaction Transaction(Description);
	UEdGraph* Graph = EdGraphs[ShownGraph].Get();
	Entry->Modify();
	Entry->Text = Text;
	Entry->UpdateLayout();
	Graph->NotifyNodeChanged(Entry);
	if (!RenamedFrom.IsEmpty())
	{
		// Parameters are locals of their graph, so only this graph's nodes use them.
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
			FString NodeText = BangoNode && BangoNode != Entry ? BangoNode->Text : FString();
			if (BangoNode && BangoNode != Entry && Bango::RenameVariableInText(NodeText, RenamedFrom, RenamedTo))
			{
				FString Problem;
				if (Bango::ValidateNodeText(BangoNode->GetKind(), ShownGraph > 0, NodeText, Problem))
				{
					BangoNode->Modify();
					BangoNode->Text = NodeText;
					BangoNode->UpdateLayout();
					Graph->NotifyNodeChanged(BangoNode);
				}
			}
		}
	}
	UpdateInputs();
	UpdateVariablesNow();
	return true;
}

// ---- GLOBALS and LOCALS ----

void SBangoScriptEditor::UpdateVariablesNow()
{
	FBangoGraphDocument Current;
	FString Error;
	if (LayoutContext.IsValid() && BuildCurrentDocument(Current, nullptr, Error))
	{
		UpdateVariables(Current, Bango::MakeNodeLayoutContext(Current, &FBangoModule::Get().GetCommands()));
	}
}

void SBangoScriptEditor::UpdateVariables(const FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context)
{
	TArray<FBangoGraphVariable> Variables;
	if (EdGraphs.IsValidIndex(ShownGraph))
	{
		Bango::GetGraphVariables(Document, ShownGraph == 0 ? INDEX_NONE : ShownGraph - 1, Context, Variables);
	}
	VariableItems.Reset();
	for (const FBangoGraphVariable& Variable : Variables)
	{
		VariableItems.Add(MakeShared<FBangoGraphVariable>(Variable));
	}

	// The rows: the file's globals, then (greyed) the ones the graph uses that the file doesn't declare; the graph's
	// locals, then (greyed) its loop variables.
	TArray<TSharedPtr<FDeclEntry>> Globals;
	TArray<TSharedPtr<FDeclEntry>> Locals;
	const FBangoGraph* Shown = !EdGraphs.IsValidIndex(ShownGraph) ? nullptr : ShownGraph == 0 ? &Document.Main
		: Document.Functions.IsValidIndex(ShownGraph - 1) ? &Document.Functions[ShownGraph - 1] : nullptr;
	if (Shown)
	{
		for (int32 i = 0; i < Document.Globals.Num(); ++i)
		{
			TSharedPtr<FDeclEntry> Item = MakeShared<FDeclEntry>();
			Item->bGlobal = true;
			Item->Index = i;
			Item->Decl = Document.Globals[i];
			Globals.Add(Item);
		}
		for (int32 i = 0; i < Shown->Locals.Num(); ++i)
		{
			TSharedPtr<FDeclEntry> Item = MakeShared<FDeclEntry>();
			Item->Index = i;
			Item->Decl = Shown->Locals[i];
			Locals.Add(Item);
		}
		for (const FBangoGraphVariable& Variable : Variables)
		{
			const bool bUndeclared = Variable.Kind == FBangoGraphVariable::EKind::Global && Variable.Index == INDEX_NONE;
			if (!bUndeclared && Variable.Kind != FBangoGraphVariable::EKind::Loop)
			{
				continue;
			}
			TSharedPtr<FDeclEntry> Item = MakeShared<FDeclEntry>();
			Item->bGlobal = bUndeclared;
			Item->bReadOnly = true;
			Item->Decl.Name = Variable.Name;
			Item->Decl.Type = Variable.Type;
			Item->LoopNode = bUndeclared ? INDEX_NONE : Variable.Index;
			(bUndeclared ? Globals : Locals).Add(Item);
		}
	}

	// Only make the rows again when something changed, so the lists don't flicker or lose their scroll position.
	FString Shape = FString::Printf(TEXT("%d|"), ShownGraph);
	for (const TArray<TSharedPtr<FDeclEntry>>* List : { &Globals, &Locals })
	{
		for (const TSharedPtr<FDeclEntry>& Item : *List)
		{
			Shape += FString::Printf(TEXT("%d %d %d %s %s %d %s %s|"), Item->bGlobal ? 1 : 0, Item->Index, Item->bReadOnly ? 1 : 0, *Item->Decl.Type, *Item->Decl.Name,
				Item->Decl.bHasValue ? 1 : 0, *Item->Decl.Value, *Item->Decl.Comment);
		}
		Shape += TEXT("#");
	}
	if (Shape == DeclsShown)
	{
		return;
	}
	DeclsShown = Shape;
	GlobalItems = Globals;
	LocalItems = Locals;
	GlobalList->RequestListRefresh();
	LocalList->RequestListRefresh();

	// A new declaration is renamed straight away, once its row exists (next frame).
	const TArray<TSharedPtr<FDeclEntry>>& PendingIn = bPendingDeclGlobal ? GlobalItems : LocalItems;
	if (PendingIn.IsValidIndex(PendingDeclRename) && !PendingIn[PendingDeclRename]->bReadOnly)
	{
		TWeakPtr<FDeclEntry> WeakItem = PendingIn[PendingDeclRename];
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakItem](double, float)
		{
			TSharedPtr<FDeclEntry> Pinned = WeakItem.Pin();
			if (TSharedPtr<SInlineEditableTextBlock> NameWidget = Pinned.IsValid() ? Pinned->NameWidget.Pin() : nullptr)
			{
				NameWidget->EnterEditingMode();
			}
			return EActiveTimerReturnType::Stop;
		}));
	}
	PendingDeclRename = INDEX_NONE;
}

TArray<FBangoGraphDecl> SBangoScriptEditor::GetDecls(bool bGlobal) const
{
	TArray<FBangoGraphDecl> Decls;
	const UBangoEdGraphNode* Entry = bGlobal ? (EdGraphs.Num() > 0 ? BangoScriptEditor::FindEntryNode(EdGraphs[0].Get()) : nullptr) : GetShownEntryNode();
	if (Entry)
	{
		for (const FBangoEdDecl& Decl : bGlobal ? Entry->Globals : Entry->Locals)
		{
			Decls.Add(Decl.ToGraphDecl());
		}
	}
	return Decls;
}

bool SBangoScriptEditor::ApplyDecls(bool bGlobal, const TArray<FBangoGraphDecl>& Decls, const FText& Description, const FString& RenamedFrom, const FString& RenamedTo)
{
	const int32 TargetGraph = bGlobal ? 0 : ShownGraph;
	UBangoEdGraphNode* Entry = EdGraphs.IsValidIndex(TargetGraph) ? BangoScriptEditor::FindEntryNode(EdGraphs[TargetGraph].Get()) : nullptr;
	if (!Entry)
	{
		return false;
	}
	const FScopedTransaction Transaction(Description);
	Entry->Modify();
	TArray<FBangoEdDecl>& Target = bGlobal ? Entry->Globals : Entry->Locals;
	Target.Reset();
	for (const FBangoGraphDecl& Decl : Decls)
	{
		Target.Add(FBangoEdDecl::From(Decl));
	}

	if (!RenamedFrom.IsEmpty())
	{
		// A global is visible in every graph (except where a local or parameter has its name); a local only in its own.
		for (int32 g = 0; g < EdGraphs.Num(); ++g)
		{
			if (!bGlobal && g != ShownGraph)
			{
				continue;
			}
			UEdGraph* Graph = EdGraphs[g].Get();
			if (bGlobal)
			{
				const UBangoEdGraphNode* GraphEntry = BangoScriptEditor::FindEntryNode(Graph);
				TArray<FBangoEntryParam> GraphParams;
				if (GraphEntry)
				{
					Bango::ReadEntryParams(GraphEntry->Text, g > 0, GraphParams);
				}
				const bool bHidden = GraphEntry
					&& (GraphEntry->Locals.ContainsByPredicate([&RenamedFrom](const FBangoEdDecl& Decl) { return Decl.Name.Equals(RenamedFrom, ESearchCase::CaseSensitive); })
						|| GraphParams.ContainsByPredicate([&RenamedFrom](const FBangoEntryParam& Param) { return Param.Name.Equals(RenamedFrom, ESearchCase::CaseSensitive); }));
				if (bHidden)
				{
					continue; // its uses there are of the local or parameter, not the global
				}
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
				if (!BangoNode || BangoNode->IsEntry() || BangoNode->GetKind() == EBangoGraphNodeKind::Label || BangoNode->GetKind() == EBangoGraphNodeKind::Goto)
				{
					continue; // a label's name isn't a variable
				}
				FString NodeText = BangoNode->Text;
				FString Problem;
				if (Bango::RenameVariableInText(NodeText, RenamedFrom, RenamedTo) && Bango::ValidateNodeText(BangoNode->GetKind(), g > 0, NodeText, Problem))
				{
					BangoNode->Modify();
					BangoNode->Text = NodeText;
					BangoNode->UpdateLayout();
					Graph->NotifyNodeChanged(BangoNode);
				}
			}
		}
	}
	UpdateVariablesNow();
	return true;
}

FReply SBangoScriptEditor::OnAddDeclClicked(bool bGlobal)
{
	if (!GetShownEntryNode())
	{
		return FReply::Handled();
	}
	TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	TSet<FString> Used = GetShownGraphNames();
	for (const FBangoGraphDecl& Global : GetDecls(true))
	{
		Used.Add(Global.Name);
	}
	for (const TSharedPtr<FGraphEntry>& Function : FunctionItems)
	{
		Used.Add(Function->Label);
	}
	FBangoGraphDecl& Added = Decls.AddDefaulted_GetRef();
	Added.Type = TEXT("bool");
	Added.Name = Bango::MakeUniqueName(bGlobal ? TEXT("newGlobal") : TEXT("_newLocal"), Used);
	bPendingDeclGlobal = bGlobal;
	PendingDeclRename = Decls.Num() - 1;
	if (!ApplyDecls(bGlobal, Decls, bGlobal ? LOCTEXT("AddGlobalTransaction", "Add Bango Global") : LOCTEXT("AddLocalTransaction", "Add Bango Local")))
	{
		PendingDeclRename = INDEX_NONE;
	}
	return FReply::Handled();
}

void SBangoScriptEditor::DeclareGlobal(const FString& Name, const FString& Type)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(true);
	FBangoGraphDecl& Added = Decls.AddDefaulted_GetRef();
	Added.Type = Type;
	Added.Name = Name;
	ApplyDecls(true, Decls, LOCTEXT("DeclareGlobalTransaction", "Declare Bango Global"));
}

bool SBangoScriptEditor::IsGlobalUsed(const FString& Name) const
{
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
			FString Text = BangoNode && !BangoNode->IsEntry() ? BangoNode->Text : FString();
			if (BangoNode && BangoNode->GetKind() != EBangoGraphNodeKind::Label && BangoNode->GetKind() != EBangoGraphNodeKind::Goto
				&& Bango::RenameVariableInText(Text, Name, Name + TEXT("_")))
			{
				return true;
			}
		}
	}
	return false;
}

void SBangoScriptEditor::DeleteDecl(bool bGlobal, int32 Index)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	if (!Decls.IsValidIndex(Index))
	{
		return;
	}
	if (bGlobal && IsGlobalUsed(Decls[Index].Name))
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("DeleteGlobalConfirm", "This script uses the global {0}. Remove its declaration anyway?\n\nIts uses are left alone, and won't compile until it's declared again (or they're changed)."),
			FText::FromString(Decls[Index].Name)));
		if (Answer != EAppReturnType::Yes)
		{
			return;
		}
	}
	Decls.RemoveAt(Index);
	ApplyDecls(bGlobal, Decls, bGlobal ? LOCTEXT("DeleteGlobalTransaction", "Remove Bango Global") : LOCTEXT("DeleteLocalTransaction", "Remove Bango Local"));
}

void SBangoScriptEditor::SetDeclType(bool bGlobal, int32 Index, const FString& Type)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	if (!Decls.IsValidIndex(Index) || Decls[Index].Type == Type)
	{
		return;
	}
	Decls[Index].Type = Type;
	// A starting value made for the old type rarely fits the new one (nil fits any type).
	if (Decls[Index].bHasValue && Decls[Index].Value != TEXT("nil"))
	{
		Decls[Index].bHasValue = false;
		Decls[Index].Value.Reset();
	}
	ApplyDecls(bGlobal, Decls, bGlobal ? LOCTEXT("SetGlobalTypeTransaction", "Change Bango Global Type") : LOCTEXT("SetLocalTypeTransaction", "Change Bango Local Type"));
}

void SBangoScriptEditor::SetDeclValue(int32 Index, const FString& Typed)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(false);
	if (!Decls.IsValidIndex(Index))
	{
		return;
	}
	FBangoGraphDecl& Decl = Decls[Index];
	// Typed like a node's field: a string or a text as plain text (or =expression), anything else as code.
	FBangoNodeField Field;
	Field.bHasValue = true;
	Field.bOptional = true;
	Field.bPresent = Decl.bHasValue;
	Field.TypeHint = Decl.Type;
	Field.Text = Decl.Value;
	const FString Value = Bango::GetFieldValueFromTyped(Field, Typed).TrimStartAndEnd();
	const bool bHasValue = !Value.IsEmpty();
	if (bHasValue == Decl.bHasValue && (!bHasValue || Value == Decl.Value))
	{
		return;
	}
	FString Problem;
	if (bHasValue && !Bango::CheckDeclValue(Decl.Type, Value, Problem))
	{
		Notify(FText::Format(LOCTEXT("LocalValueFailed", "Couldn't set {0}'s starting value: {1}"), FText::FromString(Decl.Name), FText::FromString(Problem)), false);
		DeclsShown.Reset(); // the box goes back to what's declared
		UpdateVariablesNow();
		return;
	}
	Decl.bHasValue = bHasValue;
	Decl.Value = bHasValue ? Value : FString();
	ApplyDecls(false, Decls, LOCTEXT("SetLocalValueTransaction", "Change Bango Local Value"));
}

void SBangoScriptEditor::MoveDecl(bool bGlobal, int32 From, int32 To)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	if (!Decls.IsValidIndex(From) || To < 0 || To > Decls.Num())
	{
		return;
	}
	const FBangoGraphDecl Moved = Decls[From];
	Decls.RemoveAt(From);
	const int32 Insert = From < To ? To - 1 : To;
	if (Insert == From)
	{
		return;
	}
	Decls.Insert(Moved, Insert);
	ApplyDecls(bGlobal, Decls, bGlobal ? LOCTEXT("MoveGlobalTransaction", "Reorder Bango Globals") : LOCTEXT("MoveLocalTransaction", "Reorder Bango Locals"));
}

FString SBangoScriptEditor::CheckDeclName(bool bGlobal, int32 Index, const FString& NewName) const
{
	const TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	if (!Decls.IsValidIndex(Index) || NewName.Equals(Decls[Index].Name, ESearchCase::CaseSensitive))
	{
		return FString();
	}
	if (!BangoScriptEditorPrivate::IsVariableName(NewName))
	{
		return FString::Printf(TEXT("A %s's name has only letters, digits and _, doesn't start with a digit, and isn't a keyword (like %s)."),
			bGlobal ? TEXT("global") : TEXT("local"), bGlobal ? TEXT("gold or questLog") : TEXT("count or _count"));
	}
	for (int32 i = 0; i < Decls.Num(); ++i)
	{
		if (i != Index && Decls[i].Name.Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("There's already a %s called %s."), bGlobal ? TEXT("global") : TEXT("local"), *NewName);
		}
	}
	if (!bGlobal)
	{
		for (const TSharedPtr<FBangoGraphVariable>& Variable : VariableItems)
		{
			if ((Variable->Kind == FBangoGraphVariable::EKind::Param || Variable->Kind == FBangoGraphVariable::EKind::Loop) && Variable->Name.Equals(NewName, ESearchCase::CaseSensitive))
			{
				return FString::Printf(TEXT("%s is already a %s of this graph."), *NewName,
					Variable->Kind == FBangoGraphVariable::EKind::Param ? TEXT("parameter") : TEXT("loop's variable"));
			}
		}
	}
	return FString();
}

void SBangoScriptEditor::RenameDecl(bool bGlobal, int32 Index, const FString& NewName)
{
	TArray<FBangoGraphDecl> Decls = GetDecls(bGlobal);
	if (!Decls.IsValidIndex(Index) || NewName.Equals(Decls[Index].Name, ESearchCase::CaseSensitive) || !CheckDeclName(bGlobal, Index, NewName).IsEmpty())
	{
		return;
	}
	const FString OldName = Decls[Index].Name;
	Decls[Index].Name = NewName;
	ApplyDecls(bGlobal, Decls, bGlobal ? LOCTEXT("RenameGlobalTransaction", "Rename Bango Global") : LOCTEXT("RenameLocalTransaction", "Rename Bango Local"), OldName, NewName);
}

void SBangoScriptEditor::OnDeclClicked(TSharedPtr<FDeclEntry> Item)
{
	if (!Item.IsValid() || Item->LoopNode == INDEX_NONE || FSlateApplication::Get().IsDragDropping())
	{
		return;
	}
	FBangoGraphDocument Current;
	TArray<TArray<UBangoEdGraphNode*>> Nodes;
	FString Error;
	if (BuildCurrentDocument(Current, &Nodes, Error) && Nodes.IsValidIndex(ShownGraph) && Nodes[ShownGraph].IsValidIndex(Item->LoopNode) && GraphEditor.IsValid())
	{
		GraphEditor->JumpToNode(Nodes[ShownGraph][Item->LoopNode], /*bRequestRename*/ false, /*bSelectNode*/ true);
	}
}

TSharedRef<ITableRow> SBangoScriptEditor::OnGenerateDeclRow(TSharedPtr<FDeclEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	using namespace BangoScriptEditorPrivate;
	const FDeclEntry Entry = Item.IsValid() ? *Item : FDeclEntry();
	const bool bGlobal = Entry.bGlobal;
	const int32 Index = Entry.Index;
	const int32 ListId = bGlobal ? 1 : 2;
	FLinearColor Tint = FLinearColor::White;
	const FSlateBrush* Icon = GetTypeIcon(Entry.Decl.Type, Tint);

	typedef STableRow<TSharedPtr<FDeclEntry>> FDeclRow;
	TSharedRef<TWeakPtr<FDeclRow>> RowHolder = MakeShared<TWeakPtr<FDeclRow>>();
	auto GetDropZone = [RowHolder](const FDragDropEvent& Event, EItemDropZone Zone)
	{
		if (TSharedPtr<FDeclRow> Pinned = RowHolder->Pin())
		{
			const FGeometry& Geometry = Pinned->GetTickSpaceGeometry();
			return Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y < Geometry.GetLocalSize().Y * 0.5f ? EItemDropZone::AboveItem : EItemDropZone::BelowItem;
		}
		return Zone == EItemDropZone::AboveItem ? EItemDropZone::AboveItem : EItemDropZone::BelowItem;
	};
	TSharedRef<FDeclRow> Row = SNew(FDeclRow, Owner)
		.Padding(FMargin(2.0f, 2.0f, 6.0f, 2.0f))
		.ToolTipText(Entry.bReadOnly
			? (bGlobal
				? LOCTEXT("UndeclaredGlobalTooltip", "Used here, but this script doesn't declare it: another script or a Blueprint may (or it's a typo). Click + to declare it here. Drag it onto the graph to use it.")
				: LOCTEXT("LoopVariableTooltip", "A loop's variable, declared by its For or For Each node. Click to show the loop. Drag it onto the graph to use it."))
			: FText::FromString(Entry.Decl.Comment))
		// Dragged by anything but its handle, a variable goes onto the graph.
		.OnDragDetected_Lambda([this, Entry](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Unhandled();
			}
			return FReply::Handled().BeginDragDrop(FBangoVariableDragDrop::New(AsShared(), Entry.Decl.Name, Entry.Decl.Type, GetShownGraphNames()));
		})
		.OnCanAcceptDrop_Lambda([this, Index, ListId, GetDropZone](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FDeclEntry>) -> TOptional<EItemDropZone>
		{
			TSharedPtr<FBangoInputDragDrop> Op = Event.GetOperationAs<FBangoInputDragDrop>();
			if (!Op.IsValid() || Op->Editor.Pin().Get() != this || Op->List != ListId || Index == INDEX_NONE)
			{
				return TOptional<EItemDropZone>();
			}
			const EItemDropZone Half = GetDropZone(Event, Zone);
			const int32 To = Half == EItemDropZone::AboveItem ? Index : Index + 1;
			if (To == Op->Index || To == Op->Index + 1)
			{
				return TOptional<EItemDropZone>();
			}
			return Half;
		})
		.OnAcceptDrop_Lambda([this, bGlobal, Index, ListId, GetDropZone](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FDeclEntry>)
		{
			TSharedPtr<FBangoInputDragDrop> Op = Event.GetOperationAs<FBangoInputDragDrop>();
			if (!Op.IsValid() || Op->Editor.Pin().Get() != this || Op->List != ListId || Index == INDEX_NONE)
			{
				return FReply::Unhandled();
			}
			MoveDecl(bGlobal, Op->Index, GetDropZone(Event, Zone) == EItemDropZone::AboveItem ? Index : Index + 1);
			return FReply::Handled();
		});
	*RowHolder = Row;

	if (Entry.bReadOnly)
	{
		// A loop variable, or a global declared elsewhere: shown greyed, with a + to declare the global here.
		TSharedRef<SWidget> Action = SNullWidget::NullWidget;
		if (bGlobal)
		{
			const FString Name = Entry.Decl.Name;
			Action = SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.HasDownArrow(false)
				.ContentPadding(FMargin(3.0f, 0.0f))
				.ToolTipText(LOCTEXT("DeclareGlobalTooltip", "Declare it in this script, as a global of the type you pick."))
				.OnGetMenuContent_Lambda([this, Name]() { return MakeTypeMenu(FString(), [this, Name](const FString& Type) { DeclareGlobal(Name, Type); }); })
				.ButtonContent()
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("+")))
				];
		}
		Row->SetContent(
			SNew(SBox)
			.HeightOverride(VariableRowContentHeight) // a fixed height, so the list can be as tall as its rows
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(18.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(Tint * FLinearColor(1.0f, 1.0f, 1.0f, 0.6f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Decl.Name))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(bGlobal ? 64.0f : 160.0f)
					[
						SNew(STextBlock)
						.Text(bGlobal ? LOCTEXT("DeclaredElsewhere", "elsewhere")
							: FText::Format(LOCTEXT("LoopVariableType", "{0} (loop)"), FText::FromString(Entry.Decl.Type.IsEmpty() ? FString(TEXT("?")) : Entry.Decl.Type)))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					Action
				]
			]);
		return Row;
	}

	// A local's starting value, typed like a node's field (a string without quotes). Globals have none.
	TSharedRef<SWidget> ValueBox = SNullWidget::NullWidget;
	if (!bGlobal)
	{
		FBangoNodeField Field;
		Field.bHasValue = true;
		Field.bOptional = true;
		Field.bPresent = Entry.Decl.bHasValue;
		Field.TypeHint = Entry.Decl.Type;
		Field.Text = Entry.Decl.Value;
		ValueBox = SNew(SEditableTextBox)
			.Text(FText::FromString(Bango::GetFieldEditText(Field)))
			.HintText(LOCTEXT("LocalNilHint", "nil"))
			.ToolTipText(LOCTEXT("LocalValueTooltip", "The value it starts with, each time the graph starts. A fixed value only (a number, a string, true, false, nil, a Bango ID, a text, or an array of those); empty starts it as nil. Set anything else in the code."))
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true)
			.OnTextCommitted_Lambda([this, Index](const FText& NewText, ETextCommit::Type CommitType)
			{
				SetDeclValue(Index, NewText.ToString());
			});
	}

	TSharedPtr<SInlineEditableTextBlock> Name;
	const FString DeclName = Entry.Decl.Name;
	const FString DeclType = Entry.Decl.Type;
	Row->SetContent(
		SNew(SBox)
		.HeightOverride(VariableRowContentHeight) // a fixed height, so the list can be as tall as its rows
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// The grab handle: drag it up or down to reorder.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SBangoInputHandle)
				.ToolTipText(LOCTEXT("DeclHandleTooltip", "Drag up or down to reorder."))
				.OnDragDetected_Lambda([this, ListId, Index, DeclName](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						return FReply::Unhandled();
					}
					return FReply::Handled().BeginDragDrop(FBangoInputDragDrop::New(AsShared(), ListId, Index, DeclName));
				})
			]
			// The type's icon is a button: its menu changes the type.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.HasDownArrow(false)
				.ContentPadding(FMargin(2.0f, 1.0f))
				.ToolTipText(FText::Format(LOCTEXT("DeclTypeTooltip", "{0}. Click to change the type."), FText::FromString(DeclType)))
				.OnGetMenuContent_Lambda([this, bGlobal, Index, DeclType]() { return MakeTypeMenu(DeclType, [this, bGlobal, Index](const FString& Type) { SetDeclType(bGlobal, Index, Type); }); })
				.ButtonContent()
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(Tint)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Name, SInlineEditableTextBlock)
				.Text(FText::FromString(DeclName))
				.ToolTipText(bGlobal
					? LOCTEXT("GlobalNameTooltip", "Double-click (or click it again) to rename it; its uses everywhere in the script are renamed too.")
					: LOCTEXT("LocalNameTooltip", "Double-click (or click it again) to rename it; its uses in this graph are renamed too."))
				.IsSelected(Row, &FDeclRow::IsSelectedExclusively)
				.OnVerifyTextChanged_Lambda([this, bGlobal, Index](const FText& NewText, FText& OutError)
				{
					const FString Problem = CheckDeclName(bGlobal, Index, NewText.ToString().TrimStartAndEnd());
					OutError = FText::FromString(Problem);
					return Problem.IsEmpty();
				})
				.OnTextCommitted_Lambda([this, bGlobal, Index](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnCleared)
					{
						RenameDecl(bGlobal, Index, NewText.ToString().TrimStartAndEnd());
					}
				})
			]
			// The type and value columns have fixed widths, so they line up from row to row.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DeclType))
					.ToolTipText(FText::FromString(DeclType))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(bGlobal ? 0.0f : 96.0f)
				[
					ValueBox
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.0f, 0.0f))
				.ToolTipText(bGlobal
					? LOCTEXT("DeleteGlobalTooltip", "Remove this declaration (asks first if the script uses it). Its uses are left as they are.")
					: LOCTEXT("DeleteLocalTooltip", "Remove this local. Its uses in the graph are left as they are."))
				.OnClicked_Lambda([this, bGlobal, Index]()
				{
					DeleteDecl(bGlobal, Index);
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("×")))
				]
			]
		]);
	if (Item.IsValid())
	{
		Item->NameWidget = Name;
	}
	return Row;
}

void SBangoScriptEditor::CloseScript()
{
	GraphHost->SetContent(SNullWidget::NullWidget);
	GraphEditor.Reset();
	EdGraphs.Reset();
	GraphItems.Reset();
	RebuildFunctionItems();
	VariableItems.Reset();
	GlobalItems.Reset();
	LocalItems.Reset();
	DeclsShown.Reset();
	GlobalList->RequestListRefresh();
	LocalList->RequestListRefresh();
	ShownGraph = INDEX_NONE;
	UpdateInputs();
	SavedDocument = FBangoGraphDocument();
	bDirty = false;
	bChangedOnDisk = false;
	KeptFileTime = FDateTime::MinValue();
	SetScriptText(FString());
	TextError.Reset();
	UpdateDescriptionBox(); // empty, with no graphs
}

void SBangoScriptEditor::ReloadScript()
{
	if (!OpenScript.IsEmpty())
	{
		LoadScript(FString(OpenScript)); // a copy: LoadScript replaces OpenScript
	}
}

void SBangoScriptEditor::LoadScript(const FString& Script)
{
	CloseScript();
	OpenScript = Script;
	OpenPath = FPaths::Combine(GetScriptFolder(), Script + TEXT(".bango"));

	FString Source;
	if (!FFileHelper::LoadFileToString(Source, *OpenPath))
	{
		ShowMessage(FText::Format(LOCTEXT("CantRead", "Couldn't read {0}."), FText::FromString(OpenPath)));
		return;
	}
	OpenFileTime = IFileManager::Get().GetTimeStamp(*OpenPath);

	FBangoGraphDocument Document;
	FBangoCompileError Error;
	if (!Bango::BuildGraph(Source, Script + TEXT(".bango"), Document, Error))
	{
		// The text pane shows the file, so it can be fixed there (and applied), or elsewhere and reloaded.
		ShowMessage(FText::Format(LOCTEXT("CantOpen", "This script can't be shown as a graph until its text is fixed. Fix it in the script text pane (the TEXT tab on the left) and Apply, or elsewhere and Reload:\n\n{0}"), FText::FromString(Error.ToString())));
		SetScriptText(Source);
		bTextPaneOpen = true;
		TextError = Error.ToString();
		if (Error.Line > 0)
		{
			TextBox->GoTo(FTextLocation(Error.Line - 1, FMath::Max(Error.Column - 1, 0))); // the pane shows the file as it is
		}
		return;
	}

	ShowDocument(Document);

	// What's on disk, as the editor graphs read it back (so an untouched script isn't "changed").
	FString BuildError;
	BuildCurrentDocument(SavedDocument, nullptr, BuildError);
	bDirty = false;
	UpdateScriptText(&SavedDocument);
}

void SBangoScriptEditor::ShowDocument(const FBangoGraphDocument& Document)
{
	LayoutContext = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(Document, &FBangoModule::Get().GetCommands()));
	EdGraphs.Add(TStrongObjectPtr<UEdGraph>(BangoScriptEditor::CreateEdGraph(Document.Main, LayoutContext)));
	BangoScriptEditor::SetLocNamespace(EdGraphs[0].Get(), Document.bHasLocNamespace, Document.LocNamespace);
	BangoScriptEditor::SetGlobals(EdGraphs[0].Get(), Document.Globals);
	BangoScriptEditor::SetDescription(EdGraphs[0].Get(), Document.Description);
	TSharedPtr<FGraphEntry> Main = MakeShared<FGraphEntry>();
	Main->Label = TEXT("Script");
	GraphItems.Add(Main);

	for (int32 i = 0; i < Document.Functions.Num(); ++i)
	{
		EdGraphs.Add(TStrongObjectPtr<UEdGraph>(BangoScriptEditor::CreateEdGraph(Document.Functions[i], LayoutContext)));
		TSharedPtr<FGraphEntry> Entry = MakeShared<FGraphEntry>();
		Entry->Label = Document.Functions[i].Name;
		Entry->FunctionIndex = i;
		GraphItems.Add(Entry);
	}

	RebuildFunctionItems();
	ShowGraph(0);
	UpdateDescriptionBox();
}

void SBangoScriptEditor::ShowGraph(int32 GraphIndex, bool bZoomToFit)
{
	if (!EdGraphs.IsValidIndex(GraphIndex) || !EdGraphs[GraphIndex].IsValid())
	{
		return;
	}
	ShownGraph = GraphIndex;
	SelectShownFunction();

	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("CornerText", "BANGO");

	GraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(GraphCommands)
		.IsEditable(true)
		.Appearance(Appearance)
		.GraphToEdit(EdGraphs[GraphIndex].Get())
		.ShowGraphStateOverlay(false);
	// A function's graph has a button in its top-left corner that goes back to the main graph.
	TSharedRef<SOverlay> View = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			GraphEditor.ToSharedRef()
		];
	if (GraphIndex > 0)
	{
		View->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(8.0f)
			[
				SNew(SButton)
				.ToolTipText(FText::Format(LOCTEXT("CloseFunctionTooltip", "Close {0} and go back to the Script graph."),
					FText::FromString(GraphItems.IsValidIndex(GraphIndex) ? GraphItems[GraphIndex]->Label : FString())))
				.ContentPadding(FMargin(8.0f, 3.0f))
				.OnClicked_Lambda([this]()
				{
					ShowGraph(0);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BackToScriptGraph", "Back to Script Graph"))
				]
			];
	}
	GraphHost->SetContent(View);
	UpdateInputs();
	UpdateVariablesNow();
	if (bZoomToFit)
	{
		GraphEditor->ZoomToFit(/*bOnlySelection*/ false);
	}
}

void SBangoScriptEditor::ShowMessage(const FText& Message)
{
	GraphEditor.Reset();
	GraphHost->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.0f)
		[
			SNew(STextBlock)
			.Text(Message)
			.AutoWrapText(true)
		]);
}

// ---- SCRIPT DESCRIPTION ----

FString SBangoScriptEditor::GetScriptDescription() const
{
	const UBangoEdGraphNode* Entry = EdGraphs.Num() > 0 ? BangoScriptEditor::FindEntryNode(EdGraphs[0].Get()) : nullptr;
	return Entry ? Entry->Description : FString();
}

void SBangoScriptEditor::SetScriptDescription(const FString& Typed)
{
	UBangoEdGraphNode* Entry = EdGraphs.Num() > 0 ? BangoScriptEditor::FindEntryNode(EdGraphs[0].Get()) : nullptr;
	if (!Entry)
	{
		return;
	}
	// One comment line per line: no trailing spaces, and no empty lines at the end (they'd be lost when read back).
	TArray<FString> Lines;
	Typed.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ false);
	for (FString& Line : Lines)
	{
		Line = Line.Replace(TEXT("\t"), TEXT("    ")).TrimEnd();
	}
	while (Lines.Num() > 0 && Lines.Last().IsEmpty())
	{
		Lines.Pop();
	}
	const FString Description = FString::Join(Lines, TEXT("\n"));
	if (Description.Equals(Entry->Description, ESearchCase::CaseSensitive))
	{
		UpdateDescriptionBox();
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetDescriptionTransaction", "Change Bango Script Description"));
	Entry->Modify();
	Entry->Description = Description;
	UpdateDescriptionBox();
}

void SBangoScriptEditor::UpdateDescriptionBox()
{
	if (!DescriptionBox.IsValid() || DescriptionBox->HasAnyUserFocusOrFocusedDescendants())
	{
		return;
	}
	const FString Description = GetScriptDescription();
	if (!DescriptionBox->GetText().ToString().Equals(Description, ESearchCase::CaseSensitive))
	{
		DescriptionBox->SetText(FText::FromString(Description));
	}
}

// ---- Script text pane ----

TSharedRef<SWidget> SBangoScriptEditor::MakeTextPane()
{
	// Made first, so the text editor can be given it.
	TextVScrollBar = SNew(SScrollBar).Orientation(Orient_Vertical);

	// The script's text, with Bango syntax highlighting (Bango Text Editor Spec). Made before the layout, so the line
	// numbers can be in its font.
	TextBox = SNew(SBangoTextEditor)
		.Padding(FMargin(4.0f, BangoScriptEditorPrivate::ScriptTextTopPadding))
		.VScrollBar(TextVScrollBar) // its scroll position is read back for the line numbers
		.Text(FText::FromString(TextShown))
		.IsReadOnly_Lambda([this]() { return OpenScript.IsEmpty(); })
		.OnTextChanged_Lambda([this](const FText& NewText)
		{
			bTextEdited = !NewText.ToString().Equals(TextShown, ESearchCase::CaseSensitive);
			UpdateLineNumbers(NewText.ToString());
		})
		.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent)
		{
			if ((KeyEvent.GetKey() == EKeys::Enter || KeyEvent.GetKey() == EKeys::Virtual_Accept) && KeyEvent.IsControlDown())
			{
				if (!OpenScript.IsEmpty())
				{
					ApplyScriptText();
				}
				return FReply::Handled();
			}
			return FReply::Unhandled();
		});

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 0.0f, 2.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TextPaneHeading", "SCRIPT TEXT"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyText", "Apply"))
					.ToolTipText(LOCTEXT("ApplyTextTooltip", "Make the graphs again from this text (Ctrl+Enter), keeping the view. It can't be undone with Undo, like Reload. If the text doesn't read, the graphs are left as they are."))
					.IsEnabled_Lambda([this]() { return !OpenScript.IsEmpty() && (bTextEdited || EdGraphs.Num() == 0); })
					.OnClicked_Lambda([this]()
					{
						ApplyScriptText();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RevertText", "Revert"))
					.ToolTipText(LOCTEXT("RevertTextTooltip", "Throw away what was typed here, and show the graphs' text again."))
					.IsEnabled_Lambda([this]() { return bTextEdited; })
					.OnClicked_Lambda([this]()
					{
						RevertScriptText();
						return FReply::Handled();
					})
				]
			]
			// Why the text doesn't read, or that it's been typed in and isn't applied yet.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 0.0f, 2.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return !TextError.IsEmpty() || bTextEdited ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					return !TextError.IsEmpty() ? FText::FromString(TextError)
						: LOCTEXT("TextEditedNote", "Edited. Apply (Ctrl+Enter) to update the graph; until then, changes made in the graph don't show here.");
				})
				.ColorAndOpacity_Lambda([this]()
				{
					return !TextError.IsEmpty() ? FSlateColor(FLinearColor(1.0f, 0.4f, 0.35f)) : FSlateColor::UseSubduedForeground();
				})
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SHorizontalBox)

				// Line numbers, scrolled along with the text (SyncLineNumbers).
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(LineNumberScroll, SScrollBox)
					.ScrollBarVisibility(EVisibility::Collapsed)
					.ConsumeMouseWheel(EConsumeMouseWheel::Never)
					.AllowOverscroll(EAllowOverscroll::No)
					.AnimateWheelScrolling(false)
					+ SScrollBox::Slot()
					// The same top padding as the text; room below for the text's horizontal scrollbar, so the last lines line up.
					.Padding(FMargin(2.0f, BangoScriptEditorPrivate::ScriptTextTopPadding, 6.0f, 24.0f))
					[
						SAssignNew(LineNumbers, STextBlock)
						.Font(TextBox->GetFont()) // the text's own font, so each number is level with its line
						.Justification(ETextJustify::Right)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::FromString(TEXT("1")))
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					TextBox.ToSharedRef()
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					TextVScrollBar.ToSharedRef()
				]
			]
		];
}

void SBangoScriptEditor::SetScriptText(const FString& Text)
{
	// The text as it is, tabs included: the text editor types and shows tabs (one per block level).
	TextShown = Text;
	bTextEdited = false;
	if (TextBox.IsValid())
	{
		TextBox->SetText(FText::FromString(TextShown));
	}	UpdateLineNumbers(TextShown);
}

void SBangoScriptEditor::UpdateLineNumbers(const FString& Text)
{
	int32 NumLines = 1;
	for (const TCHAR Char : Text)
	{
		NumLines += Char == TEXT('\n') ? 1 : 0;
	}
	if (NumLines == LineNumberCount || !LineNumbers.IsValid())
	{
		return;
	}
	LineNumberCount = NumLines;
	FString Numbers;
	Numbers.Reserve(NumLines * 5);
	for (int32 Line = 1; Line <= NumLines; ++Line)
	{
		Numbers += FString::FromInt(Line);
		if (Line < NumLines)
		{
			Numbers += TEXT("\n");
		}
	}
	LineNumbers->SetText(FText::FromString(Numbers));
}

EActiveTimerReturnType SBangoScriptEditor::SyncLineNumbers(double, float)
{
	if (bTextPaneOpen && TextVScrollBar.IsValid() && LineNumberScroll.IsValid() && LineNumbers.IsValid())
	{
		// The text box puts its scroll position on the scrollbar as a fraction of its text's height. The numbers have one
		// line per line of text in the same font, so their height is the text's: the same fraction of it, in pixels.
		const float Offset = TextVScrollBar->DistanceFromTop() * LineNumbers->GetDesiredSize().Y;
		if (!FMath::IsNearlyEqual(LineNumberScroll->GetScrollOffset(), Offset))
		{
			LineNumberScroll->SetScrollOffset(Offset);
		}
	}
	return EActiveTimerReturnType::Continue;
}

void SBangoScriptEditor::UpdateScriptText(const FBangoGraphDocument* Document)
{
	if (!bTextPaneOpen || bTextEdited || EdGraphs.Num() == 0)
	{
		return;
	}
	FBangoGraphDocument Built;
	FString Error;
	if (!Document)
	{
		if (!BuildCurrentDocument(Built, nullptr, Error))
		{
			return;
		}
		Document = &Built;
	}
	FString Text;
	if (!Bango::WriteGraphText(*Document, Text, Error))
	{
		return;
	}
	if (!Text.Equals(TextShown, ESearchCase::CaseSensitive))
	{
		SetScriptText(Text);
	}
}

bool SBangoScriptEditor::ApplyScriptText()
{
	if (!TextBox.IsValid() || OpenScript.IsEmpty())
	{
		return false;
	}
	const FString Text = TextBox->GetText().ToString();
	FBangoGraphDocument Document;
	FBangoCompileError Error;
	if (!Bango::BuildGraph(Text, OpenScript + TEXT(".bango"), Document, Error))
	{
		TextError = Error.ToString();
		if (Error.Line > 0)
		{
			TextBox->GoTo(FTextLocation(Error.Line - 1, FMath::Max(Error.Column - 1, 0)));
		}
		return false;
	}

	// The same file, changed: what it was last saved as, and the changed-on-disk state, stay as they are.
	const FBangoGraphDocument Saved = SavedDocument;
	const bool bWasChangedOnDisk = bChangedOnDisk;
	const FDateTime Kept = KeptFileTime;
	KeepingView([this, &Document]()
	{
		CloseScript();
		ShowDocument(Document);
	});
	SavedDocument = Saved;
	bChangedOnDisk = bWasChangedOnDisk;
	KeptFileTime = Kept;
	TextError.Reset();
	RefreshDirty();
	UpdateScriptText(nullptr); // as it'll be saved
	return true;
}

void SBangoScriptEditor::RevertScriptText()
{
	if (EdGraphs.Num() == 0)
	{
		SetScriptText(TextShown); // the file's text, which didn't read
		return;
	}
	bTextEdited = false;
	TextError.Reset();
	TextShown.Reset(); // so it's put in the box again
	UpdateScriptText(nullptr);
}

FText SBangoScriptEditor::GetTitle() const
{
	if (OpenScript.IsEmpty())
	{
		return FText::GetEmpty();
	}
	FString Title = OpenScript + TEXT(".bango");
	if (bDirty || bTextEdited)
	{
		Title += TEXT(" *");
	}
	if (ShownGraph > 0 && GraphItems.IsValidIndex(ShownGraph))
	{
		Title += TEXT("   >   ") + GraphItems[ShownGraph]->Label;
	}
	return FText::FromString(Title);
}

bool SBangoScriptEditor::BuildCurrentDocument(FBangoGraphDocument& OutDocument, TArray<TArray<UBangoEdGraphNode*>>* OutNodes, FString& OutError) const
{
	TArray<UEdGraph*> Graphs;
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		Graphs.Add(Graph.Get());
	}
	if (!BangoScriptEditor::BuildDocument(Graphs, OutDocument, OutNodes, OutError))
	{
		return false;
	}
	// A placeholder left without a value (Hint's %2 shown as "missing") is saved as nil.
	if (LayoutContext.IsValid())
	{
		Bango::FillMissingPlaceholders(OutDocument, *LayoutContext);
	}
	return true;
}

void SBangoScriptEditor::CheckFileOnDisk()
{
	if (OpenScript.IsEmpty() || bChangedOnDisk)
	{
		return;
	}
	const FDateTime FileTime = IFileManager::Get().GetTimeStamp(*OpenPath);
	// A missing file (deleted or being replaced) is left alone: there's nothing to read, and Save writes it again.
	if (FileTime == FDateTime::MinValue() || FileTime == OpenFileTime || FileTime == KeptFileTime)
	{
		return;
	}
	RefreshDirty();
	if (bDirty || bTextEdited)
	{
		bChangedOnDisk = true; // the bar asks
	}
	else
	{
		ReloadKeepingView(); // nothing to lose
	}
}

void SBangoScriptEditor::ReloadKeepingView()
{
	KeepingView([this]() { LoadScript(FString(OpenScript)); }); // a copy: LoadScript replaces OpenScript
}

void SBangoScriptEditor::KeepingView(TFunctionRef<void()> Rebuild)
{
	const FString ShownLabel = GraphItems.IsValidIndex(ShownGraph) ? GraphItems[ShownGraph]->Label : FString();
	FVector2f ViewLocation(0.0f, 0.0f);
	float ViewZoom = 1.0f;
	const bool bHadView = GraphEditor.IsValid();
	if (bHadView)
	{
		GraphEditor->GetViewLocation(ViewLocation, ViewZoom);
	}

	Rebuild();

	// Back to the same graph (found by name: functions may have been added or removed), seen from the same place.
	// If it's gone, or the text no longer reads, LoadScript's view stays.
	const int32 Index = GraphItems.IndexOfByPredicate([&ShownLabel](const TSharedPtr<FGraphEntry>& Entry) { return Entry.IsValid() && Entry->Label == ShownLabel; });
	if (bHadView && Index != INDEX_NONE)
	{
		ShowGraph(Index, /*bZoomToFit*/ false);
		GraphEditor->SetViewLocation(ViewLocation, ViewZoom);
	}
}

void SBangoScriptEditor::RefreshDirty()
{
	FBangoGraphDocument Current;
	FString Error;
	if (EdGraphs.Num() == 0)
	{
		bDirty = false;
	}
	else if (BuildCurrentDocument(Current, nullptr, Error))
	{
		bDirty = Current != SavedDocument;
	}
}

EActiveTimerReturnType SBangoScriptEditor::OnPoll(double CurrentTime, float DeltaTime)
{
	// First, so a script whose text didn't read is reloaded once it's fixed elsewhere.
	CheckFileOnDisk();

	if (EdGraphs.Num() == 0)
	{
		bDirty = false;
		return EActiveTimerReturnType::Continue;
	}

	FBangoGraphDocument Current;
	FString Error;
	if (!BuildCurrentDocument(Current, nullptr, Error))
	{
		return EActiveTimerReturnType::Continue;
	}
	bDirty = Current != SavedDocument;

	// A function's signature changed (calls to it need new fields, and the graph list a new name), or what some
	// node's variables are (a Set node shows its variable's type, so renaming its Var or changing a declaration matters).
	const FBangoNodeLayoutContext Fresh = Bango::MakeNodeLayoutContext(Current, &FBangoModule::Get().GetCommands());
	if (LayoutContext.IsValid() && !Bango::LayoutContextsMatch(Fresh, *LayoutContext))
	{
		*LayoutContext = Fresh;
		for (int32 i = 1; i < GraphItems.Num() && i - 1 < Current.Functions.Num(); ++i)
		{
			GraphItems[i]->Label = Current.Functions[i - 1].Name;
		}
		RefreshAllNodes();
	}
	UpdateInputs();
	UpdateVariables(Current, Fresh);
	UpdateScriptText(&Current);
	UpdateDescriptionBox();
	return EActiveTimerReturnType::Continue;
}

namespace BangoScriptEditorPrivate
{
	/** What a node looks like on the graph: its layout, and its compile error or warnings. */
	struct FNodeLook
	{
		FBangoNodeLayout Layout;
		bool bHasMessage = false;
		int32 Severity = 0;
		FString Message;
		int32 ErrorField = INDEX_NONE;
		TArray<Bango::FBangoFieldWarning> Warnings;

		explicit FNodeLook(const UBangoEdGraphNode& Node)
			: Layout(Node.Layout)
			, bHasMessage(Node.bHasCompilerMessage)
			, Severity(Node.ErrorType)
			, Message(Node.ErrorMsg)
			, ErrorField(Node.ErrorField)
			, Warnings(Node.FieldWarnings)
		{
		}

		bool Matches(const UBangoEdGraphNode& Node) const
		{
			if (Layout != Node.Layout || bHasMessage != Node.bHasCompilerMessage || ErrorField != Node.ErrorField
				|| Warnings.Num() != Node.FieldWarnings.Num())
			{
				return false;
			}
			if (bHasMessage && (Severity != Node.ErrorType || !Message.Equals(Node.ErrorMsg, ESearchCase::CaseSensitive)))
			{
				return false;
			}
			for (int32 i = 0; i < Warnings.Num(); ++i)
			{
				if (Warnings[i].Field != Node.FieldWarnings[i].Field || !Warnings[i].Message.Equals(Node.FieldWarnings[i].Message, ESearchCase::CaseSensitive))
				{
					return false;
				}
			}
			return true;
		}
	};

	/** How every node of the open script looks now. */
	struct FNodeLooks
	{
		TArray<TPair<TWeakObjectPtr<UBangoEdGraphNode>, FNodeLook>> Looks;

		explicit FNodeLooks(const TArray<TStrongObjectPtr<UEdGraph>>& Graphs)
		{
			for (const TStrongObjectPtr<UEdGraph>& Graph : Graphs)
			{
				if (!Graph.IsValid())
				{
					continue;
				}
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
					{
						Looks.Emplace(BangoNode, FNodeLook(*BangoNode));
					}
				}
			}
		}

		/**
		 * Rebuilds the widget of each node that looks different from when this was taken, and no others. Like
		 * Blueprint, a node's widget is rebuilt in place (NotifyNodeChanged); NotifyGraphChanged() with no action
		 * would throw away and rebuild every node widget in the panel, which makes the whole graph flash.
		 */
		void RefreshChanged() const
		{
			for (const TPair<TWeakObjectPtr<UBangoEdGraphNode>, FNodeLook>& Look : Looks)
			{
				UBangoEdGraphNode* Node = Look.Key.Get();
				UEdGraph* Graph = Node ? Node->GetGraph() : nullptr;
				if (Graph && !Look.Value.Matches(*Node))
				{
					Graph->NotifyNodeChanged(Node);
				}
			}
		}
	};
}

static void RelayoutNodes(const TArray<TStrongObjectPtr<UEdGraph>>& Graphs)
{
	for (const TStrongObjectPtr<UEdGraph>& Graph : Graphs)
	{
		if (!Graph.IsValid())
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
			{
				BangoNode->UpdateLayout();
			}
		}
	}
}

void SBangoScriptEditor::RefreshAllNodes()
{
	const BangoScriptEditorPrivate::FNodeLooks Before(EdGraphs);
	RelayoutNodes(EdGraphs);
	Before.RefreshChanged();
}

void SBangoScriptEditor::RebuildAllNodes()
{
	RelayoutNodes(EdGraphs);
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		if (Graph.IsValid())
		{
			Graph->NotifyGraphChanged();
		}
	}
}

FReply SBangoScriptEditor::OnCompileClicked()
{
	if (bTextEdited && !ApplyScriptText())
	{
		Notify(FText::Format(LOCTEXT("CantCompileText", "The script text doesn't read: {0}"), FText::FromString(TextError)), false);
		return FReply::Handled();
	}
	FBangoGraphDocument Document;
	TArray<TArray<UBangoEdGraphNode*>> Nodes;
	FString Error;
	FString Text;
	if (!BuildCurrentDocument(Document, &Nodes, Error) || !Bango::WriteGraphText(Document, Text, Error))
	{
		Notify(FText::FromString(Error), false);
		return FReply::Handled();
	}

	// Lay out every node again, which clears earlier compile errors and warnings. Whatever this compile finds is
	// shown below; when this returns, only the nodes that look different are rebuilt.
	const BangoScriptEditorPrivate::FNodeLooks Before(EdGraphs);
	ON_SCOPE_EXIT
	{
		Before.RefreshChanged();
	};
	RelayoutNodes(EdGraphs);

	// Warnings (like a placeholder with no value) don't stop the compile; they're shown in yellow.
	int32 NumWarnings = 0;
	UBangoEdGraphNode* FirstWarned = nullptr;
	int32 FirstWarnedGraph = INDEX_NONE;
	for (int32 GraphIndex = 0; GraphIndex < Nodes.Num(); ++GraphIndex)
	{
		for (UBangoEdGraphNode* Node : Nodes[GraphIndex])
		{
			const int32 NodeWarnings = Node->UpdateCompileWarnings();
			if (NodeWarnings > 0 && !FirstWarned)
			{
				FirstWarned = Node;
				FirstWarnedGraph = GraphIndex;
			}
			NumWarnings += NodeWarnings;
		}
	}

	FBangoProgram Program;
	FBangoCompileError CompileError;
	const FString FileName = OpenScript + TEXT(".bango");
	if (Bango::CompileSource(Text, FileName, FBangoModule::Get().GetCommands(), Program, CompileError))
	{
		if (NumWarnings == 0)
		{
			Notify(FText::Format(LOCTEXT("Compiled", "{0} compiled without errors."), FText::FromString(FileName)), true);
			return FReply::Handled();
		}
		// Show the first warning, like an error, but the script is fine to run.
		if (FirstWarnedGraph != ShownGraph)
		{
			ShowGraph(FirstWarnedGraph);
		}
		if (GraphEditor.IsValid())
		{
			GraphEditor->JumpToNode(FirstWarned, /*bRequestRename*/ false, /*bSelectNode*/ true);
		}
		Notify(FText::Format(LOCTEXT("CompiledWithWarnings", "{0} compiled with {1} warning(s). {2}"), FText::FromString(FileName),
			FText::AsNumber(NumWarnings), FText::FromString(FirstWarned->FieldWarnings[0].Message)), true);
		return FReply::Handled();
	}

	Bango::FBangoErrorPlace Place;
	if (Bango::FindErrorPlace(Document, Text, CompileError, Place))
	{
		const int32 GraphIndex = Place.Graph == INDEX_NONE ? 0 : Place.Graph + 1;
		if (Nodes.IsValidIndex(GraphIndex) && Nodes[GraphIndex].IsValidIndex(Place.Node))
		{
			UBangoEdGraphNode* Node = Nodes[GraphIndex][Place.Node];
			const int32 Field = Place.Offset == INDEX_NONE ? INDEX_NONE : Bango::FindFieldAt(Node->Layout, Place.Offset);
			Node->SetCompileError(CompileError.Message, Field);
			if (GraphIndex != ShownGraph)
			{
				ShowGraph(GraphIndex);
			}
			if (GraphEditor.IsValid())
			{
				GraphEditor->JumpToNode(Node, /*bRequestRename*/ false, /*bSelectNode*/ true);
			}
		}
	}
	Notify(FText::FromString(CompileError.Message), false);
	return FReply::Handled();
}

FReply SBangoScriptEditor::OnReloadClicked()
{
	if (ConfirmCloseScript())
	{
		ReloadKeepingView();
	}
	return FReply::Handled();
}

FReply SBangoScriptEditor::OnSaveClicked()
{
	Save();
	return FReply::Handled();
}

bool SBangoScriptEditor::Save()
{
	if (DescriptionBox.IsValid() && DescriptionBox->HasAnyUserFocusOrFocusedDescendants())
	{
		SetScriptDescription(DescriptionBox->GetText().ToString()); // Ctrl+S while typing it: what's typed is saved
	}
	if (bTextEdited && !ApplyScriptText())
	{
		Notify(FText::Format(LOCTEXT("CantSaveText", "Can't save: the script text doesn't read. Fix it, or Revert it. {0}"), FText::FromString(TextError)), false);
		return false;
	}
	FBangoGraphDocument Document;
	FString Error;
	FString Text;
	if (!BuildCurrentDocument(Document, nullptr, Error) || !Bango::WriteGraphText(Document, Text, Error))
	{
		Notify(FText::Format(LOCTEXT("CantSave", "Can't save: {0}"), FText::FromString(Error)), false);
		return false;
	}

	// Don't silently overwrite changes made to the file outside the editor.
	const FDateTime FileTime = IFileManager::Get().GetTimeStamp(*OpenPath);
	// (Not when the user already chose Keep Mine for this change.)
	if (FileTime != FDateTime::MinValue() && FileTime != OpenFileTime && FileTime != KeptFileTime)
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("ChangedOnDisk", "{0} was changed outside the graph editor since it was opened. Overwrite those changes?"),
			FText::FromString(OpenScript + TEXT(".bango"))));
		if (Answer != EAppReturnType::Yes)
		{
			return false;
		}
	}

	if (!FFileHelper::SaveStringToFile(Text, *OpenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Notify(FText::Format(LOCTEXT("WriteFailed", "Couldn't write {0}. Is it read-only?"), FText::FromString(OpenPath)), false);
		return false;
	}

	OpenFileTime = IFileManager::Get().GetTimeStamp(*OpenPath);
	SavedDocument = Document;
	bDirty = false;
	bChangedOnDisk = false;
	KeptFileTime = FDateTime::MinValue();
	Notify(FText::Format(LOCTEXT("Saved", "Saved {0}."), FText::FromString(OpenScript + TEXT(".bango"))), true);
	return true;
}

bool SBangoScriptEditor::ConfirmCloseScript()
{
	// A script in a tab that isn't shown isn't polled, so check now.
	RefreshDirty();
	if (!bTextEdited && (!bDirty || EdGraphs.Num() == 0))
	{
		return true;
	}
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNoCancel, FText::Format(
		LOCTEXT("SaveChanges", "Save the changes to {0}?"), FText::FromString(OpenScript + TEXT(".bango"))));
	if (Answer == EAppReturnType::Yes)
	{
		return Save();
	}
	if (Answer == EAppReturnType::No)
	{
		bDirty = false; // thrown away on purpose: no recovery copy when the editor closes
		bTextEdited = false;
		return true;
	}
	return false;
}

void SBangoScriptEditor::WriteRecoveryCopy() const
{
	FBangoGraphDocument Document;
	FString Error;
	FString Text;
	if (bTextEdited && TextBox.IsValid())
	{
		Text = TextBox->GetText().ToString(); // what was typed, even if it doesn't read
	}
	else if (OpenScript.IsEmpty() || !BuildCurrentDocument(Document, nullptr, Error) || !Bango::WriteGraphText(Document, Text, Error))
	{
		return;
	}
	if (OpenScript.IsEmpty())
	{
		return;
	}
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Bango"), TEXT("Unsaved"), OpenScript + TEXT(".bango"));
	if (FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogBangoScriptEditor, Warning, TEXT("The Bango graph editor closed with unsaved changes to %s. They were saved to %s."), *OpenScript, *FPaths::ConvertRelativePathToFull(Path));
	}
}

void SBangoScriptEditor::DeleteSelectedNodes()
{
	if (!GraphEditor.IsValid() || !EdGraphs.IsValidIndex(ShownGraph))
	{
		return;
	}
	TArray<UBangoEdGraphNode*> ToDelete;
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && !Node->IsEntry())
		{
			ToDelete.Add(Node);
		}
	}
	if (ToDelete.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteNodes", "Delete Bango Nodes"));
	UEdGraph* Graph = EdGraphs[ShownGraph].Get();
	Graph->Modify();
	for (UBangoEdGraphNode* Node : ToDelete)
	{
		// Deleting a reroute keeps the wire it bent: what led into it is wired to where it led.
		UEdGraphPin* From = nullptr;
		UEdGraphPin* To = nullptr;
		if (Node->IsReroute())
		{
			const UEdGraphPin* In = Node->GetInputPin();
			const UEdGraphPin* Out = Node->GetNextPin();
			From = In && In->LinkedTo.Num() > 0 ? In->LinkedTo[0] : nullptr;
			To = Out && Out->LinkedTo.Num() > 0 ? Out->LinkedTo[0] : nullptr;
		}
		Node->Modify();
		Node->DestroyNode();
		if (From && To)
		{
			From->GetOwningNode()->Modify();
			To->GetOwningNode()->Modify();
			From->MakeLinkTo(To);
		}
	}
	// Each removal already tells the panel (DestroyNode removes the node's widget), and wires are drawn from the
	// pins, so there's nothing else to refresh.
	GraphEditor->ClearSelectionSet();
}

void SBangoScriptEditor::CopySelectedNodes()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	// Everything but the entry node (each graph has exactly one). Wires to nodes that aren't copied are dropped
	// when pasting; wires between copied nodes are kept.
	TSet<UObject*> ToCopy;
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && Node->CanDuplicateNode())
		{
			Node->PrepareForCopying();
			ToCopy.Add(Node);
		}
	}
	if (ToCopy.Num() == 0)
	{
		return;
	}
	FString Exported;
	FEdGraphUtilities::ExportNodesToText(ToCopy, Exported);
	FPlatformApplicationMisc::ClipboardCopy(*Exported);
}

bool SBangoScriptEditor::CanCopySelectedNodes() const
{
	if (!GraphEditor.IsValid())
	{
		return false;
	}
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		const UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void SBangoScriptEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	DeleteSelectedNodes();
}

bool SBangoScriptEditor::CanCutSelectedNodes() const
{
	return CanCopySelectedNodes() && CanDeleteSelectedNodes();
}

void SBangoScriptEditor::PasteNodes()
{
	if (GraphEditor.IsValid())
	{
		PasteNodesHere(GraphEditor->GetPasteLocation2f());
	}
}

void SBangoScriptEditor::PasteNodesHere(const FVector2f& Location)
{
	if (!GraphEditor.IsValid() || !EdGraphs.IsValidIndex(ShownGraph) || !EdGraphs[ShownGraph].IsValid())
	{
		return;
	}
	UEdGraph* Graph = EdGraphs[ShownGraph].Get();
	// New nodes share the graph's layout context and kind (main or function graph) with its entry node.
	const UBangoEdGraphNode* Entry = nullptr;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		Entry = Cast<UBangoEdGraphNode>(Node);
		if (Entry && Entry->IsEntry())
		{
			break;
		}
	}
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	if (!Entry || !FEdGraphUtilities::CanImportNodesFromText(Graph, Clipboard))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PasteNodes", "Paste Bango Nodes"));
	Graph->Modify();
	GraphEditor->ClearSelectionSet();

	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, Clipboard, Imported);
	TArray<UBangoEdGraphNode*> Pasted;
	for (UEdGraphNode* Node : Imported)
	{
		if (UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
		{
			Pasted.Add(BangoNode);
		}
	}
	if (Pasted.Num() == 0)
	{
		return;
	}

	// Keep the nodes where they were relative to each other, centred on Location, on the grid (positions are saved
	// in whole grid units).
	FVector2f Average(0.0f, 0.0f);
	for (const UBangoEdGraphNode* Node : Pasted)
	{
		Average += FVector2f((float)Node->NodePosX, (float)Node->NodePosY);
	}
	Average /= (float)Pasted.Num();

	for (UBangoEdGraphNode* Node : Pasted)
	{
		Node->NodePosX = FMath::RoundToInt((Node->NodePosX - Average.X + Location.X) / BangoGraphGridSize) * BangoGraphGridSize;
		Node->NodePosY = FMath::RoundToInt((Node->NodePosY - Average.Y + Location.Y) / BangoGraphGridSize) * BangoGraphGridSize;
		Node->CreateNewGuid();
		// NodeID 0: each gets a free ID the next time the editor reads the graph (BangoScriptEditor::BuildDocument).
		Node->NodeID = 0;
		Node->bInFunctionGraph = Entry->bInFunctionGraph;
		Node->LayoutContext = Entry->LayoutContext;
		Node->ExpandedTextFields.Reset();
		Node->UpdateLayout();
	}

	// The import puts the nodes in the graph's list without telling the panel. Adding each one again through AddNode
	// tells the panel about just that node (like adding one from the menu), rather than rebuilding every node
	// widget; their widgets appear next frame.
	for (UBangoEdGraphNode* Node : Pasted)
	{
		Graph->Nodes.Remove(Node);
		Graph->AddNode(Node, /*bUserAction*/ true, /*bSelectNewNode*/ false);
	}
	for (UBangoEdGraphNode* Node : Pasted)
	{
		GraphEditor->SetNodeSelection(Node, true);
	}
}

bool SBangoScriptEditor::CanPasteNodes() const
{
	if (!GraphEditor.IsValid() || !EdGraphs.IsValidIndex(ShownGraph) || !EdGraphs[ShownGraph].IsValid())
	{
		return false;
	}
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	return FEdGraphUtilities::CanImportNodesFromText(EdGraphs[ShownGraph].Get(), Clipboard);
}

void SBangoScriptEditor::DuplicateSelectedNodes()
{
	// Like Blueprint: copy, then paste where the mouse is.
	CopySelectedNodes();
	PasteNodes();
}

bool SBangoScriptEditor::CanDeleteSelectedNodes() const
{
	if (!GraphEditor.IsValid())
	{
		return false;
	}
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		const UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && !Node->IsEntry())
		{
			return true;
		}
	}
	return false;
}

void SBangoScriptEditor::Undo()
{
	if (GEditor)
	{
		GEditor->UndoTransaction(); // PostUndo refreshes the nodes
	}
}

void SBangoScriptEditor::Redo()
{
	if (GEditor)
	{
		GEditor->RedoTransaction(); // PostRedo refreshes the nodes
	}
}

void SBangoScriptEditor::PostUndo(bool bSuccess)
{
	// Undo can bring back pins as new objects, which the old pin widgets don't know about, so their wires
	// wouldn't be drawn. Rebuilding the node widgets fixes that, and picks up text that changed back.
	RebuildAllNodes();
	RefreshDirty(); // the undo may have changed a script in a tab that isn't shown, so its * can't wait for the poll
}

void SBangoScriptEditor::PostRedo(bool bSuccess)
{
	RebuildAllNodes();
	RefreshDirty();
}

void SBangoScriptEditor::Notify(const FText& Message, bool bSuccess)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = bSuccess ? 3.0f : 8.0f;
	Info.bUseSuccessFailIcons = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
