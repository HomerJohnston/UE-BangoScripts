#include "SBangoScriptEditor.h"

#include "BangoScriptAsset.h"
#include "BangoScriptSync.h"

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
#include "BangoGraphCommands.h"
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
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "SBangoTextEditor.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/StyleColors.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
#include "BangoScriptCache.h"
#include "BangoScriptSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Colors/SColorBlock.h"

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
	GraphCommands->MapAction(FBangoGraphCommands::Get().ToggleNodesDisabled,
		FExecuteAction::CreateSP(this, &SBangoScriptEditor::ToggleSelectedNodesDisabled),
		FCanExecuteAction::CreateSP(this, &SBangoScriptEditor::CanToggleSelectedNodesDisabled));
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

	ChildSlot
	[
		SNew(SVerticalBox)

		// Top: the toolbar, like an asset editor's (Blueprint's): Reload, Compile (with its status) and Save, then Debug.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToolbar()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(4)
			[
				// The same brush as the toolbar and the panes, so the gap is the window's background colour (and
				// follows the editor's theme).
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Background"))
				.Padding(0.0f)
			]
		]
		
		// Below: the functions and variables, then the graph (a small gap under the toolbar)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			// Sized to its content, a width it keeps when the window is resized; dragging the splitter changes it (ScriptDataWidth).
			.SizeRule(SSplitter::SizeToContent)
			.Resizable(true) // a sized-to-content slot is only dragged if it says so (and has OnSlotResized)
			.MinSize(MinSidePaneWidth)
			.OnSlotResized_Lambda([this](float NewWidth) { ScriptDataWidth = FMath::Max(NewWidth, MinSidePaneWidth); })
			[
				SNew(SBox)
				.WidthOverride_Lambda([this]() { return ScriptDataWidth; })
				// While the text pane has typed text that isn't applied, the text is what counts: the lists wait for it.
				.IsEnabled_Lambda([this]() { return !bTextEdited; })
				.ToolTipText_Lambda([this]() { return bTextEdited ? LOCTEXT("PaneLockedTooltip", "The script's text has been edited. Apply it (Ctrl+Enter) or Revert it to use these again.") : FText::GetEmpty(); })
				[
					// SCRIPT DESCRIPTION, SCRIPT INPUT, GLOBAL VARS, LOCAL VARS and FUNCTIONS (the order of the script's text): foldouts one after another in a single scrolling pane.
					// Each list is as tall as its rows, so only the pane scrolls. A closed foldout is just its header.
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						// SCRIPT DESCRIPTION: the comment lines at the top of the file. Not shown in the graph.
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							MakeFoldout(LOCTEXT("DescriptionHeading", "SCRIPT DESCRIPTION"), &bDescriptionExpanded,
								SNew(SBox)
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
								])
						]

						// SCRIPT INPUT
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							MakeFoldout(LOCTEXT("InputsHeading", "SCRIPT INPUT"), &bInputsExpanded,
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
								],
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

						// GLOBAL VARS
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							MakeFoldout(LOCTEXT("GlobalsHeading", "GLOBAL VARS"), &bGlobalsExpanded,
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
								],
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

						// LOCAL VARS
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							MakeFoldout(LOCTEXT("LocalsHeading", "LOCAL VARS"), &bLocalsExpanded,
								SNew(SBox)
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
								],
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

						// FUNCTIONS
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							MakeFoldout(LOCTEXT("FunctionsHeading", "FUNCTIONS"), &bFunctionsExpanded,
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
								],
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
					]
				]
			]

			// The rest, which takes up any change in the window's width.
			+ SSplitter::Slot()
			.Value(1.0f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				// The script text pane. While it's hidden, the graph has the whole width. Opening and closing slide it
				// (its width grows from nothing, TextPaneOpenness). It keeps its width when the window is resized (the
				// graph takes the change); the splitter sets its open width.
				+ SSplitter::Slot()
				.SizeRule(SSplitter::SizeToContent)
				.Resizable(true) // a sized-to-content slot is only dragged if it says so (and has OnSlotResized)
				.OnSlotResized_Lambda([this](float NewWidth)
				{
					if (TextPaneOpenness >= 1.0f)
					{
						TextPaneWidth = FMath::Max(NewWidth, MinSidePaneWidth);
					}
				})
				[
					SNew(SBox)
					.WidthOverride_Lambda([this]() { return TextPaneWidth * FMath::InterpEaseInOut(0.0f, 1.0f, TextPaneOpenness, 2.0f); })
					.Clipping(EWidgetClipping::ClipToBounds)
					.Visibility_Lambda([this]() { return TextPaneOpenness > 0.0f ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeTextPane()
					]
				]

				// The graph: the only part whose width follows the window's.
				+ SSplitter::Slot()
				.Value(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
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
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SAssignNew(GraphHost, SBox)
								.IsEnabled_Lambda([this]() { return !bTextEdited; })
							]
							// While the text pane has typed text that isn't applied, the graph is covered (and takes no clicks):
							// the text is what counts until it's applied or reverted.
							+ SOverlay::Slot()
							[
								SNew(SBorder)
								.Visibility_Lambda([this]() { return bTextEdited && EdGraphs.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									.Padding(0.0f, 0.0f, 0.0f, 8.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GraphLockedNote", "The script's text has been edited.\nApply it to see it in the graph, or Revert it to go back."))
										.Justification(ETextJustify::Center)
										.Font(FAppStyle::GetFontStyle("BoldFont"))
										.ColorAndOpacity(FStyleColors::AccentOrange)
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(0.0f, 0.0f, 4.0f, 0.0f)
										[
											SNew(SButton)
											.Text(LOCTEXT("GraphLockedApply", "Apply (Ctrl+Enter)"))
											.OnClicked_Lambda([this]()
											{
												ApplyScriptText();
												return FReply::Handled();
											})
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SButton)
											.Text(LOCTEXT("GraphLockedRevert", "Revert"))
											.OnClicked_Lambda([this]()
											{
												RevertScriptText();
												return FReply::Handled();
											})
										]
									]
								]
							]
						]
					]

					// The Source Editor tab, floating on the graph's left edge: on the pane's right side when it's open, so
					// opening the pane pulls the tab out with it.
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(0.0f, 90.0f, 0.0f, 0.0f)
					[
						MakeTextTab()
					]

					// The Command Palette tab, the same on the graph's right edge, for the reference pane (COMMANDS and NODES).
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					.Padding(0.0f, 90.0f, 0.0f, 0.0f)
					[
						MakeCommandsTab()
					]
				]

				// The reference pane (COMMANDS and NODES), on the right: hidden to start with, and slid out by its tab like the text pane.
				+ SSplitter::Slot()
				.SizeRule(SSplitter::SizeToContent)
				.Resizable(true) // a sized-to-content slot is only dragged if it says so (and has OnSlotResized)
				.OnSlotResized_Lambda([this](float NewWidth)
				{
					if (CommandsPaneOpenness >= 1.0f)
					{
						CommandsPaneWidth = FMath::Max(NewWidth, MinSidePaneWidth);
					}
				})
				[
					SNew(SBox)
					.WidthOverride_Lambda([this]() { return CommandsPaneWidth * FMath::InterpEaseInOut(0.0f, 1.0f, CommandsPaneOpenness, 2.0f); })
					.Clipping(EWidgetClipping::ClipToBounds)
					.Visibility_Lambda([this]() { return CommandsPaneOpenness > 0.0f ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeCommandsPane()
					]
				]
			]
		]
	];

	ShowMessage(EmptyMessage); // until a script asset is opened (OpenScriptAsset)
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::OnPoll));
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::SyncLineNumbers)); // every frame
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::UpdateDebugging));
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

	/**
	 * A line of the reference pane's NODES list: a small card with a grab handle and the node's name, which lights up
	 * under the mouse and can be dragged onto the graph to place the node (onto a pin, it's wired to it).
	 */
	class SBangoNodeReferenceRow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBangoNodeReferenceRow) {}
			SLATE_ARGUMENT(FText, Name)
			SLATE_ARGUMENT(FText, ToolTipText)
			SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			static const FSlateRoundedBoxBrush CardBrush(FLinearColor::White, 4.0f, FSlateColor(FStyleColors::InputOutline), 1.0f);
			DragDetected = InArgs._OnDragDetected;
			SetCursor(EMouseCursor::GrabHand);
			SetToolTipText(InArgs._ToolTipText);
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(&CardBrush)
				.BorderBackgroundColor_Lambda([this]()
				{
					return IsHovered() ? FSlateColor(FStyleColors::Hover) : FSlateColor(FStyleColors::Dropdown);
				})
				.Padding(FMargin(2.0f, 2.0f, 6.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(14.0f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
							.ColorAndOpacity_Lambda([this]()
							{
								return IsHovered() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
							})
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Name)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
					]
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
			Shape += FString::Printf(TEXT("%d %d %d %s %s %d %s %s %d|"), Item->bGlobal ? 1 : 0, Item->Index, Item->bReadOnly ? 1 : 0, *Item->Decl.Type, *Item->Decl.Name,
				Item->Decl.bHasValue ? 1 : 0, *Item->Decl.Value, *Item->Decl.Comment, Item->LoopNode);
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
	CompileStatus = ECompileStatus::Unknown;
	bChangedOnDisk = false;
	KeptFileTime = FDateTime::MinValue();
	SetScriptText(FString());
	TextError.Reset();
	UpdateDescriptionBox(); // empty, with no graphs
}

void SBangoScriptEditor::OpenScriptAsset(UBangoScriptAsset* Asset)
{
	OpenAsset = Asset;
	if (Asset)
	{
		LoadScript(Asset->GetScriptName());
	}
	else
	{
		CloseScript();
	}
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

	// A linked script's text is its .bango file (the file wins); an embedded one's is in the asset.
	FString Source;
	UBangoScriptAsset* Asset = OpenAsset.Get();
	if (!Asset)
	{
		OpenScript.Reset();
		ShowMessage(FText::Format(LOCTEXT("ScriptGone", "The Bango Script asset {0} isn't there any more (was it deleted?). Open a script from the Content Browser or the script list."), FText::FromString(Script)));
		return;
	}
	OpenScript = Asset->GetScriptName(); // its name now, if it was renamed or moved while open
	{
		OpenPath = Asset->bLinked ? FBangoScriptSync::GetLinkedFilePath(OpenScript) : FString();
		if (OpenPath.IsEmpty() || !FFileHelper::LoadFileToString(Source, *OpenPath))
		{
			if (!OpenPath.IsEmpty())
			{
				UE_LOG(LogBangoScriptEditor, Warning, TEXT("Couldn't read %s, so %s's own copy of its text is shown."), *OpenPath, *OpenScript);
			}
			Source = Asset->Source;
		}
	}
	OpenFileTime = OpenPath.IsEmpty() ? FDateTime::MinValue() : IFileManager::Get().GetTimeStamp(*OpenPath);

	FBangoGraphDocument Document;
	FBangoCompileError Error;
	if (!Bango::BuildGraph(Source, OpenScript + TEXT(".bango"), Document, Error))
	{
		// The text pane shows the file, so it can be fixed there (and applied), or elsewhere and reloaded.
		ShowMessage(FText::Format(LOCTEXT("CantOpen", "This script can't be shown as a graph until its text is fixed. Fix it in the script text pane (the Source Editor tab on the left) and Apply, or elsewhere and Reload:\n\n{0}"), FText::FromString(Error.ToString())));
		SetScriptText(Source);
		SetTextPaneOpen(true, false); // at once: there's nothing else to show
		SetTextError(Error.ToString());
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
	CompileQuietly(); // so the Compile Test icon starts with the script's real status
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
	RefreshNodeList();
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

	// Sync Graph: selecting nodes moves the script text's cursor to them.
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SBangoScriptEditor::OnGraphSelectionChanged);

	GraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(GraphCommands)
		.IsEditable(true)
		.Appearance(Appearance)
		.GraphToEdit(EdGraphs[GraphIndex].Get())
		.GraphEvents(GraphEvents)
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

// ---- The left pane's foldouts, and the reference pane's COMMANDS and NODES lists ----

TSharedRef<SWidget> SBangoScriptEditor::MakeFoldout(const FText& Title, bool* bExpanded, TSharedRef<SWidget> Body, TSharedPtr<SWidget> Extra)
{
	// The header: the arrow and the title, which open and close it, and an optional button (like + Add) on the right.
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.0f, 4.0f))
			.ToolTipText(LOCTEXT("FoldoutTooltip", "Click to show or hide this section."))
			.OnClicked_Lambda([bExpanded]()
			{
				*bExpanded = !*bExpanded;
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.Image_Lambda([bExpanded]()
					{
						return FAppStyle::GetBrush(*bExpanded ? "TreeArrow_Expanded" : "TreeArrow_Collapsed");
					})
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Title)
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
			]
		];
	if (Extra.IsValid())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			Extra.ToSharedRef()
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.0f)
			[
				Header
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([bExpanded]() { return *bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Body
			]
		];
}

void SBangoScriptEditor::RefreshCommandList()
{
	if (!CommandReference.ListBox.IsValid())
	{
		return;
	}
	// Every command a script can call: the registered ones (the plugin's and the game's) and the array commands built
	// into the compiler.
	const FBangoCommandTable& Table = FBangoModule::Get().GetCommands();
	TArray<FString> Names;
	Table.GetCommandNames(Names);
	TArray<FReferenceEntry> Entries;
	for (const FString& Name : Names)
	{
		const FBangoCommandSignature* Signature = Table.FindCommand(Name);
		FReferenceEntry Entry;
		Entry.Name = Name;
		Entry.Category = Signature && !Signature->CategoryName.IsEmpty() ? Signature->CategoryName : FString(TEXT("Other"));
		Entry.bBuiltIn = !Signature || Signature->bBuiltIn;
		if (Signature)
		{
			// Hovering a name shows how it's called and what it does, and whether it's the plugin's or the game's.
			Entry.Tooltip = Signature->ToString();
			if (!Signature->CommandDescription.IsEmpty())
			{
				Entry.Tooltip += TEXT("\n\n") + Signature->CommandDescription;
			}
			Entry.Tooltip += Signature->bBuiltIn ? TEXT("\n\n(Built into Bango)") : TEXT("\n\n(This project's own command)");
		}
		Entries.Add(MoveTemp(Entry));
	}
	for (const TCHAR* Builtin : { TEXT("Count"), TEXT("InsertAt"), TEXT("Pop"), TEXT("Push"), TEXT("RemoveAt") })
	{
		if (!Table.FindCommand(Builtin))
		{
			FReferenceEntry Entry;
			Entry.Name = Builtin;
			Entry.Category = TEXT("Arrays");
			Entry.Tooltip = LOCTEXT("BuiltinCommandTooltip", "An array command, built into Bango: called on an array, as in _list Push(_x).").ToString();
			Entries.Add(MoveTemp(Entry));
		}
	}
	FillReferenceList(CommandReference, Entries, TArray<FString>{ FString(TEXT("Other")) },
		LOCTEXT("NoCommandsMatch", "No command names contain \"{0}\"."));
}

void SBangoScriptEditor::RefreshNodeList()
{
	if (!NodeReference.ListBox.IsValid() || !LayoutContext.IsValid())
	{
		return;
	}
	// The same nodes the graph's right-click menu offers: flow control, variables, arrays, every command that can be a
	// line of its own (not the ones whose value must be used), and this script's functions.
	TArray<FBangoNodeMenuEntry> MenuEntries;
	Bango::GetNodeMenuEntries(FBangoGraphDocument(), *LayoutContext, MenuEntries);
	TArray<FReferenceEntry> Entries;
	for (const FBangoNodeMenuEntry& MenuEntry : MenuEntries)
	{
		FReferenceEntry Entry;
		Entry.Name = MenuEntry.Title;
		Entry.Category = MenuEntry.Category;
		Entry.bBuiltIn = MenuEntry.bBuiltIn;
		Entry.Tooltip = MenuEntry.Description;
		const FString Origin = MenuEntry.Category == TEXT("This Script") ? FString(TEXT("(This script's own function)"))
			: MenuEntry.bBuiltIn ? FString(TEXT("(Built into Bango)")) : FString(TEXT("(This project's own command)"));
		Entry.Tooltip += (Entry.Tooltip.IsEmpty() ? TEXT("") : TEXT("\n\n")) + Origin;
		Entry.Tooltip += TEXT("\n\nDrag it onto the graph to place it, or onto a pin to wire it there.");
		Entry.Node = MakeShared<FBangoNodeMenuEntry>(MenuEntry);
		Entries.Add(MoveTemp(Entry));
	}
	FillReferenceList(NodeReference, Entries, TArray<FString>{ FString(TEXT("Commands")), FString(TEXT("This Script")) },
		LOCTEXT("NoNodesMatch", "No node names contain \"{0}\"."));
}

void SBangoScriptEditor::FillReferenceList(FReferenceList& List, TArray<FReferenceEntry>& Entries, const TArray<FString>& Trailing, const FText& NoMatch)
{
	// Only the names that contain what's typed in the search box.
	if (!List.Filter.IsEmpty())
	{
		const FString Filter = List.Filter;
		Entries.RemoveAll([&Filter](const FReferenceEntry& Entry) { return !Entry.Name.Contains(Filter, ESearchCase::IgnoreCase); });
	}
	const auto ByName = [](const FString& A, const FString& B)
	{
		const int32 IgnoringCase = A.Compare(B, ESearchCase::IgnoreCase);
		return IgnoringCase != 0 ? IgnoringCase < 0 : A.Compare(B, ESearchCase::CaseSensitive) < 0;
	};
	// Each entry's group: by category, or (Alphabetical) Native for what's built into Bango and Game for the rest.
	const bool bByCategory = List.bByCategory;
	const auto GroupOf = [bByCategory](const FReferenceEntry& Entry) -> FString
	{
		return bByCategory ? Entry.Category : FString(Entry.bBuiltIn ? TEXT("Native") : TEXT("Game"));
	};
	const auto GroupOrder = [bByCategory, &ByName, &Trailing](const FString& A, const FString& B)
	{
		if (!bByCategory)
		{
			return A == TEXT("Native") && B != TEXT("Native"); // Native first, then Game
		}
		const int32 TrailA = Trailing.IndexOfByKey(A);
		const int32 TrailB = Trailing.IndexOfByKey(B);
		if (TrailA != TrailB)
		{
			// Categories by name, then the trailing ones in their order.
			return TrailA == INDEX_NONE || (TrailB != INDEX_NONE && TrailA < TrailB);
		}
		return ByName(A, B);
	};
	Entries.Sort([&ByName, &GroupOf, &GroupOrder](const FReferenceEntry& A, const FReferenceEntry& B)
	{
		const FString GroupA = GroupOf(A);
		const FString GroupB = GroupOf(B);
		if (GroupA != GroupB)
		{
			return GroupOrder(GroupA, GroupB);
		}
		return ByName(A.Name, B.Name);
	});

	List.ListBox->ClearChildren();
	if (Entries.Num() == 0)
	{
		List.ListBox->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(List.Filter.IsEmpty() ? LOCTEXT("ReferenceEmpty", "Nothing to list.") : FText::Format(NoMatch, FText::FromString(List.Filter)))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
		return;
	}

	// One foldout per group, like the left pane's sections. Whether each is open is kept per group (and per order).
	int32 Index = 0;
	while (Index < Entries.Num())
	{
		const FString Group = GroupOf(Entries[Index]);
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		int32 Count = 0;
		for (; Index < Entries.Num() && GroupOf(Entries[Index]) == Group; ++Index, ++Count)
		{
			const FReferenceEntry& Entry = Entries[Index];
			if (Entry.Node.IsValid())
			{
				// A node: a card that can be dragged onto the graph.
				Rows->AddSlot()
				.AutoHeight()
				.Padding(6.0f, 1.0f, 6.0f, 1.0f)
				[
					MakeNodeReferenceRow(Entry)
				];
				continue;
			}
			Rows->AddSlot()
			.AutoHeight()
			.Padding(8.0f, 1.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.Name))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.ToolTipText(FText::FromString(Entry.Tooltip))
			];
		}
		const FString Key = FString(bByCategory ? TEXT("Category:") : TEXT("Alphabetical:")) + Group;
		TSharedPtr<bool>& bExpanded = List.GroupsExpanded.FindOrAdd(Key);
		if (!bExpanded.IsValid())
		{
			bExpanded = MakeShared<bool>(true);
		}
		List.ListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			MakeFoldout(FText::Format(LOCTEXT("ReferenceGroupTitle", "{0}  ({1})"), FText::FromString(Group.ToUpper()), FText::AsNumber(Count)),
				bExpanded.Get(),
				SNew(SBox)
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 4.0f))
				[
					Rows
				])
		];
	}
}

TSharedRef<SWidget> SBangoScriptEditor::MakeNodeReferenceRow(const FReferenceEntry& Entry)
{
	using namespace BangoScriptEditorPrivate;
	const TSharedPtr<FBangoNodeMenuEntry> Node = Entry.Node;
	return SNew(SBangoNodeReferenceRow)
		.Name(FText::FromString(Entry.Name))
		.ToolTipText(FText::FromString(Entry.Tooltip))
		.OnDragDetected_Lambda([this, Node](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (!Node.IsValid() || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Unhandled();
			}
			// The list was made without the script's names, so take the entry again now, from the right-click menu's
			// entries for the script as it is: its made-up names (like the For's _i) then don't clash with any.
			FBangoNodeMenuEntry Placed = *Node;
			FBangoGraphDocument Current;
			FString Error;
			if (LayoutContext.IsValid() && BuildCurrentDocument(Current, nullptr, Error))
			{
				TArray<FBangoNodeMenuEntry> Fresh;
				Bango::GetNodeMenuEntries(Current, *LayoutContext, Fresh);
				if (const FBangoNodeMenuEntry* Match = Fresh.FindByPredicate([&Placed](const FBangoNodeMenuEntry& Candidate)
					{
						return Candidate.Kind == Placed.Kind && Candidate.Category == Placed.Category && Candidate.Title == Placed.Title;
					}))
				{
					Placed = *Match;
				}
			}
			// The same action as the right-click menu's entry, so dropping it works the same way.
			TSharedPtr<FEdGraphSchemaAction> Action = MakeShared<FBangoSchemaAction_NewNode>(
				FText::FromString(Placed.Category), FText::FromString(Placed.Title), FText::FromString(Placed.Description),
				FText::FromString(Placed.Keywords), (uint8)Placed.Kind, Placed.Text);
			return FReply::Handled().BeginDragDrop(FGraphSchemaActionDragDropAction::New(Action));
		});
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

/**
 * The debugger's marks in the script text pane's gutter, one row per line of text, behind and beside the line numbers:
 * a dot and a tint where the followed copy is (pulsing yellow), along its calls (dim yellow) and where it failed (red),
 * and a fading blue tint on the lines it just ran (the trail).
 */
class SBangoLineMarkers : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoLineMarkers) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}

	/** Width of the dot column, left of the numbers. */
	static constexpr float DotColumnWidth = 10.0f;

	/** How many lines the text has; the widget's height is divided between them. */
	int32 NumLines = 1;
	/** Per line, from 0: the debug state, and when the trail last ran it (FPlatformTime::Seconds, 0 for never). */
	TArray<EBangoNodeDebugState> LineStates;
	TArray<double> LinePings;

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(DotColumnWidth, 0.0f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		static const FSlateRoundedBoxBrush DotBrush(FLinearColor::White, 3.5f);
		const FSlateBrush* RowBrush = FAppStyle::GetBrush("WhiteBrush");
		const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
		const float RowHeight = (float)Size.Y / FMath::Max(1, NumLines);
		const double Now = FPlatformTime::Seconds();
		const float Pulse = 0.5f + 0.5f * FMath::Sin((float)Now * 5.0f);
		const int32 Rows = FMath::Max(LineStates.Num(), LinePings.Num());
		for (int32 Line = 0; Line < Rows; ++Line)
		{
			const FSlateLayoutTransform RowAt(FVector2D(0.0f, Line * RowHeight));
			// The trail: a fading blue row.
			const double SincePing = LinePings.IsValidIndex(Line) && LinePings[Line] > 0.0 ? Now - LinePings[Line] : 1.0;
			if (SincePing < 1.0)
			{
				const float Fade = 1.0f - (float)SincePing;
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, RowHeight), RowAt),
					RowBrush, ESlateDrawEffect::None, FLinearColor(0.55f, 0.85f, 1.0f, 0.35f * Fade * Fade));
			}
			const EBangoNodeDebugState State = LineStates.IsValidIndex(Line) ? LineStates[Line] : EBangoNodeDebugState::None;
			if (State == EBangoNodeDebugState::None)
			{
				continue;
			}
			FLinearColor Color;
			float RowAlpha = 0.0f;
			switch (State)
			{
			case EBangoNodeDebugState::Paused:
				Color = FLinearColor(1.0f, 0.85f, 0.1f, 0.6f + 0.4f * Pulse);
				RowAlpha = 0.15f + 0.15f * Pulse;
				break;
			case EBangoNodeDebugState::CallStack:
				Color = FLinearColor(1.0f, 0.85f, 0.1f, 0.45f);
				RowAlpha = 0.08f;
				break;
			default:
				Color = FLinearColor(1.0f, 0.25f, 0.2f, 1.0f);
				RowAlpha = 0.25f;
				break;
			}
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, RowHeight), RowAt),
				RowBrush, ESlateDrawEffect::None, Color.CopyWithNewOpacity(RowAlpha));
			const float Dot = FMath::Min(7.0f, RowHeight - 2.0f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(Dot, Dot), FSlateLayoutTransform(FVector2D((DotColumnWidth - Dot) * 0.5f, Line * RowHeight + (RowHeight - Dot) * 0.5f))),
				&DotBrush, ESlateDrawEffect::None, Color);
		}
		return LayerId + 1;
	}
};

namespace BangoScriptEditorPrivate
{
	/** Shows its content turned a quarter turn anticlockwise, so it reads from bottom to top, and takes up the turned size in
	 *  layout (a render transform on its own would leave the space of the unturned content). */
	class SBangoSidewaysBox : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBangoSidewaysBox)
			: _bClockwise(false)
		{}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			/** Turned clockwise instead, reading from top to bottom (for a tab on a right-hand edge). */
			SLATE_ARGUMENT(bool, bClockwise)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bClockwise = InArgs._bClockwise;
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const FVector2D ChildSize = FVector2D(ChildSlot.GetWidget()->GetDesiredSize());
			return FVector2D(ChildSize.Y, ChildSize.X);
		}

		virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
		{
			const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
			if (!ArrangedChildren.Accepts(Child->GetVisibility()))
			{
				return;
			}
			// The content gets its own (unturned) size, centred on this box, and is turned about its centre.
			const FVector2D ChildSize = FVector2D(Child->GetDesiredSize());
			const FVector2D Offset = (FVector2D(AllottedGeometry.GetLocalSize()) - ChildSize) * 0.5;
			const FGeometry ChildGeometry = AllottedGeometry.MakeChild(
				ChildSize,
				FSlateLayoutTransform(Offset),
				FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(bClockwise ? 90.0f : -90.0f))),
				FVector2D(0.5, 0.5));
			ArrangedChildren.AddWidget(FArrangedWidget(Child, ChildGeometry));
		}

	private:
		bool bClockwise = false;
	};
}

TSharedRef<SWidget> SBangoScriptEditor::MakeTextTab()
{
	// A folder tab standing out from the graph's left edge: square on the left (against the edge or the pane), rounded on the right.
	static const FSlateRoundedBoxBrush TabBrush(FLinearColor::White, FVector4(0.0f, 6.0f, 6.0f, 0.0f), FSlateColor(FStyleColors::InputOutline), 1.0f);

	return SAssignNew(TextTabButton, SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(0.0f))
		.ToolTipText_Lambda([this]()
		{
			return bTextPaneOpen
				? LOCTEXT("HideTextTooltip", "Put the script's text away.")
				: LOCTEXT("ShowTextTooltip", "Pull out the script's text beside the graph, where it can be edited too.");
		})
		.OnClicked_Lambda([this]()
		{
			SetTextPaneOpen(!bTextPaneOpen, true);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage(&TabBrush)
			.Padding(FMargin(4.0f, 12.0f, 5.0f, 12.0f))
			.BorderBackgroundColor_Lambda([this]() -> FSlateColor
			{
				if (bTextPaneOpen)
				{
					return FSlateColor(FStyleColors::Primary); // pulled out
				}
				return TextTabButton.IsValid() && TextTabButton->IsHovered() ? FSlateColor(FStyleColors::Hover) : FSlateColor(FStyleColors::Dropdown);
			})
			[
				// Written along the tab, turned to read from bottom to top.
				SNew(BangoScriptEditorPrivate::SBangoSidewaysBox)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TextTab", "Source Editor"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
					.ColorAndOpacity_Lambda([this]() -> FSlateColor
					{
						return bTextPaneOpen || (TextTabButton.IsValid() && TextTabButton->IsHovered()) ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
					})
				]
			]
		];
}

void SBangoScriptEditor::SetTextPaneOpen(bool bOpen, bool bAnimate)
{
	bTextPaneOpen = bOpen;
	if (bOpen)
	{
		UpdateScriptText(nullptr); // up to date before it slides into view
	}
	if (!bAnimate)
	{
		TextPaneOpenness = bOpen ? 1.0f : 0.0f;
		return;
	}
	if (!bTextPaneSliding)
	{
		bTextPaneSliding = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::AnimateTextPane));
	}
}

EActiveTimerReturnType SBangoScriptEditor::AnimateTextPane(double, float InDeltaTime)
{
	static constexpr float SlideSeconds = 0.18f;
	const float Target = bTextPaneOpen ? 1.0f : 0.0f;
	TextPaneOpenness = FMath::FInterpConstantTo(TextPaneOpenness, Target, InDeltaTime, 1.0f / SlideSeconds);
	if (FMath::IsNearlyEqual(TextPaneOpenness, Target))
	{
		TextPaneOpenness = Target;
		bTextPaneSliding = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SBangoScriptEditor::MakeCommandsTab()
{
	// The Source Editor tab's twin on the graph's right edge: square on the right, rounded on the left, reading top to bottom.
	static const FSlateRoundedBoxBrush TabBrush(FLinearColor::White, FVector4(6.0f, 0.0f, 0.0f, 6.0f), FSlateColor(FStyleColors::InputOutline), 1.0f);

	return SAssignNew(CommandsTabButton, SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(0.0f))
		.ToolTipText_Lambda([this]()
		{
			return bCommandsPaneOpen
				? LOCTEXT("HideCommandsTooltip", "Put the Command Palette (commands and nodes) away.")
				: LOCTEXT("ShowCommandsTooltip", "Pull out the Command Palette beside the graph: every command a script can call, and every node you can drag onto the graph.");
		})
		.OnClicked_Lambda([this]()
		{
			SetCommandsPaneOpen(!bCommandsPaneOpen, true);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage(&TabBrush)
			.Padding(FMargin(5.0f, 12.0f, 4.0f, 12.0f))
			.BorderBackgroundColor_Lambda([this]() -> FSlateColor
			{
				if (bCommandsPaneOpen)
				{
					return FSlateColor(FStyleColors::Primary);
				}
				return CommandsTabButton.IsValid() && CommandsTabButton->IsHovered() ? FSlateColor(FStyleColors::Hover) : FSlateColor(FStyleColors::Dropdown);
			})
			[
				SNew(BangoScriptEditorPrivate::SBangoSidewaysBox)
				.bClockwise(true)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CommandsTab", "Command Palette"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
					.ColorAndOpacity_Lambda([this]() -> FSlateColor
					{
						return bCommandsPaneOpen || (CommandsTabButton.IsValid() && CommandsTabButton->IsHovered()) ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
					})
				]
			]
		];
}

TSharedRef<SWidget> SBangoScriptEditor::MakeReferenceTab(FReferenceList& List, const TCHAR* ConfigKey, const FText& SearchHint, const FText& CategoryTooltip, TFunction<void()> Refresh)
{
	// How the list is ordered is remembered between sessions, like the script browser's settings.
	FString Order;
	if (GConfig->GetString(TEXT("BangoScriptEditor"), ConfigKey, Order, GEditorPerProjectIni))
	{
		List.bByCategory = Order == TEXT("Category");
	}
	FReferenceList* ListPtr = &List;
	const FString Key = ConfigKey;

	return SNew(SVerticalBox)
		// A search box (by name), and how the list is ordered.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
				.HintText(SearchHint)
				.ToolTipText(LOCTEXT("ReferenceSearchTooltip", "Shows only the names that contain this."))
				.OnTextChanged_Lambda([ListPtr, Refresh](const FText& NewText)
				{
					ListPtr->Filter = NewText.ToString().TrimStartAndEnd();
					Refresh();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("ReferenceOrderTooltip", "How the list is grouped: Native and Game, by name, or by category."))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([ListPtr]()
					{
						return ListPtr->bByCategory ? LOCTEXT("ReferenceOrderCategory", "Category") : LOCTEXT("ReferenceOrderAlphabetical", "Alphabetical");
					})
				]
				.OnGetMenuContent_Lambda([ListPtr, Key, Refresh, CategoryTooltip]()
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
					const auto AddOrder = [ListPtr, &Key, &Refresh, &Menu](bool bByCategory, const FText& Label, const FText& Tooltip)
					{
						Menu.AddMenuEntry(Label, Tooltip, FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([ListPtr, Key, Refresh, bByCategory]()
								{
									ListPtr->bByCategory = bByCategory;
									GConfig->SetString(TEXT("BangoScriptEditor"), *Key, bByCategory ? TEXT("Category") : TEXT("Alphabetical"), GEditorPerProjectIni);
									Refresh();
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([ListPtr, bByCategory]() { return ListPtr->bByCategory == bByCategory; })),
							NAME_None, EUserInterfaceActionType::RadioButton);
					};
					AddOrder(false, LOCTEXT("ReferenceOrderAlphabetical", "Alphabetical"),
						LOCTEXT("ReferenceOrderAlphabeticalTooltip", "Two groups, each by name: Native (built into Bango) and Game (the project's own)."));
					AddOrder(true, LOCTEXT("ReferenceOrderCategory", "Category"), CategoryTooltip);
					return Menu.MakeWidget();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SAssignNew(List.ListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SBangoScriptEditor::MakeCommandsPane()
{
	int32 Tab = 0;
	if (GConfig->GetInt(TEXT("BangoScriptEditor"), TEXT("ReferenceTab"), Tab, GEditorPerProjectIni))
	{
		ReferenceTab = Tab == 1 ? 1 : 0;
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			// The tabs: COMMANDS and NODES.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 6.0f, 8.0f, 2.0f)
			[
				SNew(SSegmentedControl<int32>)
				.Value_Lambda([this]() { return ReferenceTab; })
				.OnValueChanged_Lambda([this](int32 NewTab)
				{
					ReferenceTab = NewTab;
					GConfig->SetInt(TEXT("BangoScriptEditor"), TEXT("ReferenceTab"), NewTab, GEditorPerProjectIni);
					// Read again, so what other modules (or this script's functions) have added since shows up.
					if (NewTab == 1)
					{
						RefreshNodeList();
					}
					else
					{
						RefreshCommandList();
					}
				})
				+ SSegmentedControl<int32>::Slot(0)
				.Text(LOCTEXT("CommandsTabLabel", "COMMANDS"))
				.ToolTip(LOCTEXT("CommandsTabTooltip", "Every command a script can call. Hover one to see how it's called and what it does."))
				+ SSegmentedControl<int32>::Slot(1)
				.Text(LOCTEXT("NodesTabLabel", "NODES"))
				.ToolTip(LOCTEXT("NodesTabTooltip", "Every node you can place in the graph (commands whose value must be used aren't nodes). Hover one to see what it does."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return ReferenceTab; })
				+ SWidgetSwitcher::Slot()
				[
					MakeReferenceTab(CommandReference, TEXT("CommandReferenceOrder"), LOCTEXT("CommandSearchHint", "Search commands"),
						LOCTEXT("CommandOrderCategoryTooltip", "In groups (Actors, Flow Control, Math, Text, ...), each by name."),
						[this]() { RefreshCommandList(); })
				]
				+ SWidgetSwitcher::Slot()
				[
					MakeReferenceTab(NodeReference, TEXT("NodeReferenceOrder"), LOCTEXT("NodeSearchHint", "Search nodes"),
						LOCTEXT("NodeOrderCategoryTooltip", "In groups like the graph's right-click menu (Flow Control, Variables, Math, ...), each by name."),
						[this]() { RefreshNodeList(); })
				]
			]
		];
}

void SBangoScriptEditor::SetCommandsPaneOpen(bool bOpen, bool bAnimate)
{
	bCommandsPaneOpen = bOpen;
	if (bOpen)
	{
		// Read again, so commands other modules have added (and this script's new functions) show up.
		RefreshCommandList();
		RefreshNodeList();
	}
	if (!bAnimate)
	{
		CommandsPaneOpenness = bOpen ? 1.0f : 0.0f;
		return;
	}
	if (!bCommandsPaneSliding)
	{
		bCommandsPaneSliding = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SBangoScriptEditor::AnimateCommandsPane));
	}
}

EActiveTimerReturnType SBangoScriptEditor::AnimateCommandsPane(double, float InDeltaTime)
{
	static constexpr float SlideSeconds = 0.18f;
	const float Target = bCommandsPaneOpen ? 1.0f : 0.0f;
	CommandsPaneOpenness = FMath::FInterpConstantTo(CommandsPaneOpenness, Target, InDeltaTime, 1.0f / SlideSeconds);
	if (FMath::IsNearlyEqual(CommandsPaneOpenness, Target))
	{
		CommandsPaneOpenness = Target;
		bCommandsPaneSliding = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

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
		.OnCursorNodesChanged(this, &SBangoScriptEditor::OnTextCursorNodes) // Sync Graph
		.OnTextChanged_Lambda([this](const FText& NewText)
		{
			bTextEdited = !NewText.ToString().Equals(TextShown, ESearchCase::CaseSensitive);
			// Typed back to the text the graphs show (Apply and Revert grey out): an error from applying something
			// else no longer applies. Not when there are no graphs: then the text shown is a file that didn't read,
			// and its error still stands.
			if (!bTextEdited && EdGraphs.Num() > 0)
			{
				TextError.Reset();
			}
			UpdateLineNumbers(NewText.ToString());
		})
		.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent)
		{
			if (KeyEvent.GetKey() == EKeys::Enter && KeyEvent.IsControlDown())
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
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TextPaneHeading", "SCRIPT"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				// That the text has been typed in and isn't applied yet, beside the heading.
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]() { return bTextEdited ? EVisibility::Visible : EVisibility::Hidden; })
					.Text(LOCTEXT("TextEditedNote", "Press (Ctrl+Enter) to apply changes."))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
					.ColorAndOpacity(FStyleColors::AccentOrange)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
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
						// The debugger's marks behind the numbers, with a column of their own on the left.
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SAssignNew(LineMarkers, SBangoLineMarkers)
						]
						+ SOverlay::Slot()
						.Padding(FMargin(SBangoLineMarkers::DotColumnWidth, 0.0f, 0.0f, 0.0f))
						[
							SAssignNew(LineNumbers, STextBlock)
							.Font(TextBox->GetFont()) // the text's own font, so each number is level with its line
							.Justification(ETextJustify::Right)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(FText::FromString(TEXT("1")))
						]
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
			// Why the text doesn't read, under the text (so the text doesn't move down when it shows), below a line. It
			// flashes once each time it's set. That the text has been edited is shown beside the heading.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return !TextError.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
					.Thickness(1.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this]()
					{
						// The panel's recessed colour, lit up red when the error's just been set, fading back.
						const FLinearColor Base = FStyleColors::Recessed.GetColor(FWidgetStyle());
						const FLinearColor Lit(0.55f, 0.08f, 0.06f);
						const float Flash = TextErrorFlash.IsPlaying() ? 1.0f - TextErrorFlash.GetLerp() : 0.0f;
						return FSlateColor(FMath::Lerp(Base, Lit, Flash));
					})
					.Padding(FMargin(6.0f, 4.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TextErrorHeading", "ERROR:"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f)))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text_Lambda([this]() { return FText::FromString(TextError); })
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.35f)))
						]
					]
				]
			]
		];
}

void SBangoScriptEditor::SetTextError(const FString& Error)
{
	TextError = Error;
	if (!TextError.IsEmpty())
	{
		// One flash, fading out; playing it on this widget keeps it drawing until the flash is over.
		TextErrorFlash = FCurveSequence(0.0f, 0.6f, ECurveEaseFunction::QuadOut);
		TextErrorFlash.Play(AsShared());
	}
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
	// Locals can be declared anywhere while typing: they're moved up to their graph's other locals first.
	const FString Typed = TextBox->GetText().ToString();
	FString Text;
	FBangoCompileError Error;
	if (!Bango::HoistLateLocals(Typed, Text, Error))
	{
		Error.FileName = OpenScript + TEXT(".bango");
		SetTextError(Error.ToString());
		if (Error.Line > 0)
		{
			TextBox->GoTo(FTextLocation(Error.Line - 1, FMath::Max(Error.Column - 1, 0)));
		}
		return false;
	}
	FBangoGraphDocument Document;
	if (!Bango::BuildGraph(Text, OpenScript + TEXT(".bango"), Document, Error))
	{
		SetTextError(Error.ToString());
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
	// A placeholder left without a value (Hint's {2} shown as "missing") is saved as nil.
	if (LayoutContext.IsValid())
	{
		Bango::FillMissingPlaceholders(OutDocument, *LayoutContext);
	}
	return true;
}

void SBangoScriptEditor::CheckFileOnDisk()
{
	if (OpenScript.IsEmpty() || OpenPath.IsEmpty() || bChangedOnDisk)
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

// ---- Debugging ----

void SBangoScriptEditor::GatherDebugEntries(TArray<FDebugEntry>& OutEntries) const
{
	if (!GEngine || OpenScript.IsEmpty())
	{
		return;
	}
	const FString Wanted = OpenScript + TEXT(".bango"); // the runtime's name for it ("/Game/Quests/Intro.bango")
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.WorldType == EWorldType::PIE ? Context.World() : nullptr;
		const UBangoScriptSubsystem* Subsystem = World ? World->GetSubsystem<UBangoScriptSubsystem>() : nullptr;
		if (!Subsystem)
		{
			continue;
		}
		TArray<FBangoScriptDebugInfo> Scripts;
		Subsystem->GetDebugInfo(Scripts);
		for (FBangoScriptDebugInfo& Info : Scripts)
		{
			if (Info.Path.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				FDebugEntry& Entry = OutEntries.AddDefaulted_GetRef();
				Entry.PIEInstance = Context.PIEInstance;
				Entry.Info = MoveTemp(Info);
			}
		}
	}
}

const SBangoScriptEditor::FDebugEntry* SBangoScriptEditor::FindDebugEntry() const
{
	if (DebugId == 0)
	{
		return nullptr;
	}
	for (const FDebugEntry& Entry : DebugEntries)
	{
		if (Entry.PIEInstance == DebugPIEInstance && Entry.Info.Id == DebugId)
		{
			return &Entry;
		}
	}
	return nullptr;
}

EActiveTimerReturnType SBangoScriptEditor::UpdateDebugging(double CurrentTime, float DeltaTime)
{
	DebugEntries.Reset();
	GatherDebugEntries(DebugEntries);
	if (!GEditor || !GEditor->PlayWorld)
	{
		bDebugOff = false; // next time it plays, the only copy is followed again
	}
	if (!FindDebugEntry())
	{
		// The copy being followed has finished (or play stopped), or none was: follow the only running one, if there's
		// just one; or, with none running, the one that failed last (it may have failed as it was spawned, before it
		// could be followed).
		DebugId = 0;
		const FDebugEntry* OnlyRunning = nullptr;
		const FDebugEntry* LastFailed = nullptr;
		int32 NumRunning = 0;
		for (const FDebugEntry& Entry : DebugEntries)
		{
			if (Entry.Info.Status == EBangoInstanceStatus::Failed)
			{
				LastFailed = &Entry; // GetDebugInfo lists them oldest first
			}
			else
			{
				OnlyRunning = &Entry;
				++NumRunning;
			}
		}
		const FDebugEntry* Follow = NumRunning == 1 ? OnlyRunning : (NumRunning == 0 ? LastFailed : nullptr);
		if (!bDebugOff && Follow)
		{
			DebugPIEInstance = Follow->PIEInstance;
			DebugId = Follow->Info.Id;
		}
	}
	ApplyDebugStates();
	UpdateDebugTrail();
	// Pings that have faded are dropped.
	const double Now = FPlatformTime::Seconds();
	for (auto It = DebugNodePings.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > 1.5)
		{
			It.RemoveCurrent();
		}
	}
	if (bTextPaneOpen)
	{
		UpdateLineMarkers();
	}
	return EActiveTimerReturnType::Continue;
}

UBangoScriptSubsystem* SBangoScriptEditor::FindDebugSubsystem(int32 PIEInstance)
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.WorldType == EWorldType::PIE && Context.PIEInstance == PIEInstance ? Context.World() : nullptr;
		if (World)
		{
			return World->GetSubsystem<UBangoScriptSubsystem>();
		}
	}
	return nullptr;
}

void SBangoScriptEditor::UpdateDebugTrail()
{
	const FDebugEntry* Entry = FindDebugEntry();
	if (!Entry)
	{
		TrailId = 0;
		return;
	}
	const bool bNewCopy = TrailPIEInstance != Entry->PIEInstance || TrailId != Entry->Info.Id;
	TArray<int64> Ran;
	uint64 Count = 0;
	if (Entry->Info.Status == EBangoInstanceStatus::Failed)
	{
		// It failed: its trail was kept, up to the error. Ping what wasn't seen while it was running (all of it, for a
		// copy that wasn't followed then, like one that failed as soon as it was spawned).
		const TArray<int64>& Kept = Entry->Info.Trail;
		Count = Entry->Info.TrailCount;
		const uint64 NumNew = bNewCopy ? (uint64)Kept.Num() : (Count > TrailSeen ? Count - TrailSeen : 0);
		const int32 First = FMath::Max(0, Kept.Num() - (int32)FMath::Min(NumNew, (uint64)Kept.Num()));
		Ran.Append(Kept.GetData() + First, Kept.Num() - First);
	}
	else
	{
		UBangoScriptSubsystem* Subsystem = FindDebugSubsystem(Entry->PIEInstance);
		if (!Subsystem)
		{
			TrailId = 0;
			return;
		}
		// A running copy just picked: start from what it has run so far, without pinging all of it.
		Count = Subsystem->GetScriptTrail(Entry->Info.Id, bNewCopy ? MAX_uint64 : TrailSeen, Ran);
	}
	TrailPIEInstance = Entry->PIEInstance;
	TrailId = Entry->Info.Id;
	TrailSeen = Count;
	if (Ran.Num() == 0)
	{
		return;
	}
	const TSet<int64> RanNodes(Ran);
	const double Now = FPlatformTime::Seconds();
	for (const int64 NodeID : Ran)
	{
		DebugNodePings.Add(NodeID, Now); // for the text pane's gutter
	}
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(GraphNode);
			if (Node && RanNodes.Contains(Node->NodeID))
			{
				Node->DebugPingTime = Now;
			}
		}
	}
}

int64 SBangoScriptEditor::GetDebugNodeID(const FDebugEntry& Entry)
{
	if (Entry.Info.Status == EBangoInstanceStatus::Failed)
	{
		return Entry.Info.Error.NodeID;
	}
	return Entry.Info.CallStack.Num() > 0 ? Entry.Info.CallStack.Last().Loc.NodeID : INDEX_NONE;
}

void SBangoScriptEditor::ApplyDebugStates()
{
	TMap<int64, EBangoNodeDebugState> States;
	if (const FDebugEntry* Entry = FindDebugEntry())
	{
		if (Entry->Info.Status == EBangoInstanceStatus::Failed)
		{
			States.Add(Entry->Info.Error.NodeID, EBangoNodeDebugState::Failed);
		}
		else
		{
			const TArray<FBangoCallFrameInfo>& CallStack = Entry->Info.CallStack;
			for (int32 k = 0; k < CallStack.Num(); ++k)
			{
				States.Add(CallStack[k].Loc.NodeID, k == CallStack.Num() - 1 ? EBangoNodeDebugState::Paused : EBangoNodeDebugState::CallStack);
			}
		}
	}
	for (const TStrongObjectPtr<UEdGraph>& Graph : EdGraphs)
	{
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(GraphNode))
			{
				const EBangoNodeDebugState* State = States.Find(Node->NodeID);
				Node->DebugState = State ? *State : EBangoNodeDebugState::None;
			}
		}
	}
	DebugNodeStates = MoveTemp(States);
}

int32 SBangoScriptEditor::FindTextLineOfNode(int64 NodeID)
{
	UpdateLineMarkers(); // makes MarkerLineNodes from the current text
	for (int32 Line = 0; Line < MarkerLineNodes.Num(); ++Line)
	{
		if (MarkerLineNodes[Line].Contains(NodeID))
		{
			return Line;
		}
	}
	return INDEX_NONE;
}

void SBangoScriptEditor::UpdateLineMarkers()
{
	if (!LineMarkers.IsValid() || !TextBox.IsValid())
	{
		return;
	}
	// Which nodes are on which line: the //@ metadata, `//@12(4, 7)`, or a run of reroutes, `//@7(4, 9) @8(8, 13)`.
	const FString Text = TextBox->GetText().ToString();
	if (!Text.Equals(MarkerText, ESearchCase::CaseSensitive) || MarkerLineNodes.Num() == 0)
	{
		MarkerText = Text;
		MarkerLineNodes.Reset();
		MarkerLineNodes.AddDefaulted();
		const int32 Length = Text.Len();
		for (int32 i = 0; i < Length; ++i)
		{
			if (Text[i] == TEXT('\n'))
			{
				MarkerLineNodes.AddDefaulted();
				continue;
			}
			if (Text[i] != TEXT('/') || i + 2 >= Length || Text[i + 1] != TEXT('/') || Text[i + 2] != TEXT('@'))
			{
				continue;
			}
			// Each entry: @, the NodeID, then (X, Y). Spaces are allowed anywhere.
			int32 j = i + 2;
			while (j < Length && Text[j] != TEXT('\n'))
			{
				while (j < Length && (Text[j] == TEXT(' ') || Text[j] == TEXT('\t')))
				{
					++j;
				}
				if (j >= Length || Text[j] != TEXT('@'))
				{
					break;
				}
				++j;
				while (j < Length && Text[j] == TEXT(' '))
				{
					++j;
				}
				int64 NodeID = 0;
				int32 Digits = 0;
				while (j < Length && FChar::IsDigit(Text[j]))
				{
					NodeID = NodeID * 10 + (Text[j] - TEXT('0'));
					++j;
					++Digits;
				}
				if (Digits == 0)
				{
					break;
				}
				MarkerLineNodes.Last().Add(NodeID);
				while (j < Length && Text[j] != TEXT(')') && Text[j] != TEXT('\n'))
				{
					++j;
				}
				if (j < Length && Text[j] == TEXT(')'))
				{
					++j;
				}
			}
			i = j - 1; // the loop's ++i comes to this line's end (or the next character)
		}
	}

	// Each line takes its nodes' strongest state (failed, then paused, then a call) and latest ping.
	auto Strength = [](EBangoNodeDebugState State)
	{
		switch (State)
		{
		case EBangoNodeDebugState::Failed:    return 3;
		case EBangoNodeDebugState::Paused:    return 2;
		case EBangoNodeDebugState::CallStack: return 1;
		default:                              return 0;
		}
	};
	const int32 NumLines = MarkerLineNodes.Num();
	LineMarkers->NumLines = NumLines;
	LineMarkers->LineStates.Init(EBangoNodeDebugState::None, NumLines);
	LineMarkers->LinePings.Init(0.0, NumLines);
	if (DebugNodeStates.Num() == 0 && DebugNodePings.Num() == 0)
	{
		return;
	}
	for (int32 Line = 0; Line < NumLines; ++Line)
	{
		for (const int64 NodeID : MarkerLineNodes[Line])
		{
			if (const EBangoNodeDebugState* State = DebugNodeStates.Find(NodeID))
			{
				if (Strength(*State) > Strength(LineMarkers->LineStates[Line]))
				{
					LineMarkers->LineStates[Line] = *State;
				}
			}
			if (const double* Ping = DebugNodePings.Find(NodeID))
			{
				LineMarkers->LinePings[Line] = FMath::Max(LineMarkers->LinePings[Line], *Ping);
			}
		}
	}
}

void SBangoScriptEditor::SelectDebugEntry(int32 PIEInstance, int64 Id, bool bShowNode)
{
	DebugPIEInstance = PIEInstance;
	DebugId = Id;
	bDebugOff = Id == 0;
	ApplyDebugStates();
	const FDebugEntry* Entry = FindDebugEntry();
	if (bShowNode && Entry)
	{
		ShowNodeByID(GetDebugNodeID(*Entry));
	}
}

void SBangoScriptEditor::ShowNodeByID(int64 NodeID)
{
	if (NodeID < 0)
	{
		return;
	}
	for (int32 GraphIndex = 0; GraphIndex < EdGraphs.Num(); ++GraphIndex)
	{
		for (UEdGraphNode* GraphNode : EdGraphs[GraphIndex]->Nodes)
		{
			UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(GraphNode);
			if (Node && Node->NodeID == NodeID)
			{
				if (GraphIndex != ShownGraph)
				{
					ShowGraph(GraphIndex, /*bZoomToFit*/ false);
				}
				if (GraphEditor.IsValid())
				{
					GraphEditor->JumpToNode(Node, /*bRequestRename*/ false, /*bSelectNode*/ false);
				}
				// And its line in the script text, if the pane is open.
				const int32 Line = bTextPaneOpen && TextBox.IsValid() ? FindTextLineOfNode(NodeID) : INDEX_NONE;
				if (Line != INDEX_NONE)
				{
					TextBox->GoTo(FTextLocation(Line, 0));
				}
				return;
			}
		}
	}
}

FText SBangoScriptEditor::DescribeDebugEntry(const FDebugEntry& Entry) const
{
	bool bSeveralWorlds = false;
	for (const FDebugEntry& Other : DebugEntries)
	{
		bSeveralWorlds |= Other.PIEInstance != Entry.PIEInstance;
	}
	const FString World = bSeveralWorlds ? FString::Printf(TEXT(" [client %d]"), Entry.PIEInstance + 1) : FString();
	const FBangoScriptDebugInfo& Info = Entry.Info;
	if (Info.Status == EBangoInstanceStatus::Failed)
	{
		return FText::Format(LOCTEXT("DebugFailed", "#{0}{1}: failed at node {2}: {3}"),
			FText::AsNumber(Info.Id), FText::FromString(World), FText::AsNumber(Info.Error.NodeID), FText::FromString(Info.Error.Message));
	}
	const FBangoCallFrameInfo* Where = Info.CallStack.Num() > 0 ? &Info.CallStack.Last() : nullptr;
	const FString InFunction = Where && !Where->FunctionName.IsEmpty() ? FString::Printf(TEXT(" in %s()"), *Where->FunctionName) : FString();
	const FText Node = FText::AsNumber(Where ? Where->Loc.NodeID : -1);
	if (!Info.WaitingOn.IsEmpty())
	{
		return FText::Format(LOCTEXT("DebugWaiting", "#{0}{1}: {2} at node {3}{4}"),
			FText::AsNumber(Info.Id), FText::FromString(World), FText::FromString(Info.WaitingOn), Node, FText::FromString(InFunction));
	}
	return FText::Format(LOCTEXT("DebugRunning", "#{0}{1}: at node {2}{3}"), FText::AsNumber(Info.Id), FText::FromString(World), Node, FText::FromString(InFunction));
}

FText SBangoScriptEditor::GetDebugLabel() const
{
	if (const FDebugEntry* Entry = FindDebugEntry())
	{
		return FText::Format(LOCTEXT("DebugLabel", "Debug: {0}"), DescribeDebugEntry(*Entry));
	}
	if (!GEditor || !GEditor->PlayWorld)
	{
		return LOCTEXT("DebugNotPlaying", "Debug: not playing");
	}
	int32 NumFailed = 0;
	for (const FDebugEntry& Entry : DebugEntries)
	{
		NumFailed += Entry.Info.Status == EBangoInstanceStatus::Failed ? 1 : 0;
	}
	const int32 NumRunning = DebugEntries.Num() - NumFailed;
	if (DebugEntries.Num() == 0)
	{
		return LOCTEXT("DebugNoneRunning", "Debug: none running");
	}
	if (NumFailed == 0)
	{
		return FText::Format(LOCTEXT("DebugNoneChosen", "Debug: none ({0} running)"), FText::AsNumber(NumRunning));
	}
	return FText::Format(LOCTEXT("DebugNoneChosenFailed", "Debug: none ({0} running, {1} failed)"), FText::AsNumber(NumRunning), FText::AsNumber(NumFailed));
}

TSharedRef<SWidget> SBangoScriptEditor::MakeDebugMenu()
{
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	Menu.BeginSection(NAME_None, LOCTEXT("DebugMenuHeading", "Running copies of this script"));
	Menu.AddMenuEntry(
		LOCTEXT("DebugOff", "No debugging"),
		LOCTEXT("DebugOffTooltip", "Don't follow a running copy (until playing stops)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SBangoScriptEditor::SelectDebugEntry, (int32)INDEX_NONE, (int64)0, false),
			FCanExecuteAction(), FIsActionChecked::CreateLambda([this]() { return DebugId == 0; })),
		NAME_None, EUserInterfaceActionType::RadioButton);
	bool bAnyFailed = false;
	for (const FDebugEntry& Entry : DebugEntries)
	{
		bAnyFailed |= Entry.Info.Status == EBangoInstanceStatus::Failed;
		const int32 PIEInstance = Entry.PIEInstance;
		const int64 Id = Entry.Info.Id;
		Menu.AddMenuEntry(
			DescribeDebugEntry(Entry),
			LOCTEXT("DebugEntryTooltip", "Follow this copy, and show the node it's at."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SBangoScriptEditor::SelectDebugEntry, PIEInstance, Id, true),
				FCanExecuteAction(), FIsActionChecked::CreateLambda([this, PIEInstance, Id]() { return DebugPIEInstance == PIEInstance && DebugId == Id; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}
	if (DebugEntries.Num() == 0)
	{
		Menu.AddMenuEntry(
			GEditor && GEditor->PlayWorld ? LOCTEXT("DebugNoCopies", "(none running)") : LOCTEXT("DebugNotPlayingEntry", "(play in the editor to see running copies)"),
			FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	Menu.EndSection();

	Menu.BeginSection(NAME_None);
	Menu.AddMenuEntry(
		LOCTEXT("DebugShowNode", "Show Where It Is"),
		LOCTEXT("DebugShowNodeTooltip", "Show the node the copy being followed is at (or failed at)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (const FDebugEntry* Entry = FindDebugEntry())
			{
				ShowNodeByID(GetDebugNodeID(*Entry));
			}
		}), FCanExecuteAction::CreateLambda([this]() { return FindDebugEntry() != nullptr; })));
	if (bAnyFailed)
	{
		Menu.AddMenuEntry(
			LOCTEXT("DebugForgetFailed", "Forget Failed Copies"),
			LOCTEXT("DebugForgetFailedTooltip", "Take the copies that stopped with an error off this list (in every script)."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.WorldType == EWorldType::PIE ? Context.World() : nullptr;
					if (UBangoScriptSubsystem* Subsystem = World ? World->GetSubsystem<UBangoScriptSubsystem>() : nullptr)
					{
						Subsystem->ForgetEndedScripts();
					}
				}
			})));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
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
	if (CompileStatus != ECompileStatus::Unknown && Current != CompiledDocument)
	{
		CompileStatus = ECompileStatus::Unknown; // changed since it was compiled
	}

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
		RefreshNodeList(); // a function was added, removed or renamed
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

TSharedRef<SWidget> SBangoScriptEditor::MakeToolbar()
{
	// The same builder and style as an asset editor's toolbar (Blueprint's), so the buttons have icons and labels.
	FSlimHorizontalToolBarBuilder Toolbar(nullptr, FMultiBoxCustomization::None);
	Toolbar.SetStyle(&FAppStyle::Get(), "AssetEditorToolbar");
	Toolbar.SetLabelVisibility(EVisibility::Visible); // each button's label beside its icon (the slim style hides them)
	const FName StyleSet = FAppStyle::GetAppStyleSetName();

	// Save and Reload | Compile | Debug: each section has a separator before it.
	Toolbar.BeginSection("File");
	Toolbar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { OnSaveClicked(); }),
			FCanExecuteAction::CreateLambda([this]() { return EdGraphs.Num() > 0 || bTextEdited; })),
		NAME_None,
		LOCTEXT("Save", "Save"),
		LOCTEXT("SaveTooltip", "Write the script to its .bango file (Ctrl+S). Text typed in the script text pane is applied first."),
		FSlateIcon(StyleSet, "AssetEditor.SaveAsset"));
	Toolbar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { OnReloadClicked(); }),
			// Also when the text didn't read, so it can be fixed and reloaded.
			FCanExecuteAction::CreateLambda([this]() { return !OpenScript.IsEmpty(); })),
		NAME_None,
		LOCTEXT("Reload", "Reload"),
		LOCTEXT("ReloadTooltip", "Read the script again from its .bango file, to pick up changes made to the text outside the graph editor. Asks first if there are unsaved changes."),
		FSlateIcon(StyleSet, "Icons.Refresh"));
	Toolbar.EndSection();

	Toolbar.BeginSection("Compile");
	// Compile Test, with Blueprint's status icon: a ? while it's changed since it was last compiled, a tick, a warning or
	// a cross after.
	Toolbar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { OnCompileClicked(); }),
			FCanExecuteAction::CreateLambda([this]() { return EdGraphs.Num() > 0 || bTextEdited; })),
		NAME_None,
		LOCTEXT("CompileTest", "Compile Test"),
		TAttribute<FText>::CreateLambda([this]()
		{
			const FText Status =
				bTextEdited || CompileStatus == ECompileStatus::Unknown ? LOCTEXT("CompileStatusUnknown", "Changed since it was last compiled.")
				: CompileStatus == ECompileStatus::Good ? LOCTEXT("CompileStatusGood", "Compiled without errors.")
				: CompileStatus == ECompileStatus::Warning ? LOCTEXT("CompileStatusWarning", "Compiled with warnings.")
				: LOCTEXT("CompileStatusError", "Didn't compile: there's an error.");
			return FText::Format(LOCTEXT("CompileTestTooltip", "Check the script for errors. The first error is shown on its node. Text typed in the script text pane is applied first.\n\nThis is only a test: scripts are always compiled when they're loaded at runtime, so there's nothing to compile before playing or saving. The script is also test-compiled when it's opened, for this button's icon.\n\n{0}"), Status);
		}),
		TAttribute<FSlateIcon>::CreateLambda([this, StyleSet]()
		{
			const FName Overlay =
				bTextEdited || CompileStatus == ECompileStatus::Unknown ? FName("Blueprint.CompileStatus.Overlay.Unknown")
				: CompileStatus == ECompileStatus::Good ? FName("Blueprint.CompileStatus.Overlay.Good")
				: CompileStatus == ECompileStatus::Warning ? FName("Blueprint.CompileStatus.Overlay.Warning")
				: FName("Blueprint.CompileStatus.Overlay.Error");
			return FSlateIcon(StyleSet, "Blueprint.CompileStatus.Background", NAME_None, Overlay);
		}));
	Toolbar.EndSection();

	// Debug: which running copy of this script to follow, like Blueprint's debug object.
	Toolbar.BeginSection("Debug"); // a section has a separator before it
	Toolbar.AddComboButton(
		FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([this]() { return !OpenScript.IsEmpty(); })),
		FOnGetContent::CreateSP(this, &SBangoScriptEditor::MakeDebugMenu),
		TAttribute<FText>::CreateSP(this, &SBangoScriptEditor::GetDebugLabel),
		LOCTEXT("DebugTooltip", "While playing in the editor: the running copy of this script to follow. Its graph shows where it is with a pulsing outline, and the calls on the way there with a dim one; a copy that stopped with an error shows its node in red. The only running copy is followed by itself."),
		FSlateIcon(StyleSet, "Debug"));
	Toolbar.EndSection();

	return Toolbar.MakeWidget();
}

void SBangoScriptEditor::CompileQuietly()
{
	// Like Compile Test, but only for its icon: no notification, nothing marked on the nodes, the view left alone.
	FBangoGraphDocument Document;
	TArray<TArray<UBangoEdGraphNode*>> Nodes;
	FString Error;
	FString Text;
	if (EdGraphs.Num() == 0 || !BuildCurrentDocument(Document, &Nodes, Error) || !Bango::WriteGraphText(Document, Text, Error))
	{
		return; // left Unknown
	}
	CompiledDocument = Document;
	FBangoProgram Program;
	FBangoCompileError CompileError;
	if (!Bango::CompileSource(Text, OpenScript + TEXT(".bango"), FBangoModule::Get().GetCommands(), Program, CompileError))
	{
		CompileStatus = ECompileStatus::Error;
		return;
	}
	int32 NumWarnings = 0;
	for (const TArray<UBangoEdGraphNode*>& GraphNodes : Nodes)
	{
		for (const UBangoEdGraphNode* Node : GraphNodes)
		{
			TArray<Bango::FBangoFieldWarning> Warnings;
			Bango::GetNodeWarnings(Node->Layout, Warnings);
			NumWarnings += Warnings.Num();
		}
	}
	CompileStatus = NumWarnings == 0 ? ECompileStatus::Good : ECompileStatus::Warning;
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
	// What the toolbar's Compile icon shows is for this version of the script, until it's changed (OnPoll).
	CompiledDocument = Document;
	CompileStatus = ECompileStatus::Error;

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
		CompileStatus = NumWarnings == 0 ? ECompileStatus::Good : ECompileStatus::Warning;
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
	const FDateTime FileTime = OpenPath.IsEmpty() ? FDateTime::MinValue() : IFileManager::Get().GetTimeStamp(*OpenPath);
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

	if (UBangoScriptAsset* Asset = OpenAsset.Get())
	{
		// A linked script: its file first (checked out with source control), then the asset's copy of it. An embedded
		// one: just the asset. Either way the asset is saved, and running copies of the script pick up the new text.
		if (!OpenPath.IsEmpty())
		{
			FString WriteError;
			if (!FBangoScriptSync::WriteScriptFile(OpenPath, Text, WriteError))
			{
				Notify(FText::FromString(WriteError), false);
				return false;
			}
		}
		Asset->Modify();
		Asset->Source = Text;
		if (!OpenPath.IsEmpty())
		{
			Asset->SourceFileHash = FBangoScriptSync::HashFile(OpenPath);
		}
		Asset->MarkPackageDirty();
		FBangoScriptSync::SaveAssets({ Asset });
	}
	else if (!FFileHelper::SaveStringToFile(Text, *OpenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Notify(FText::Format(LOCTEXT("WriteFailed", "Couldn't write {0}. Is it read-only?"), FText::FromString(OpenPath)), false);
		return false;
	}

	OpenFileTime = OpenPath.IsEmpty() ? FDateTime::MinValue() : IFileManager::Get().GetTimeStamp(*OpenPath);
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
	FString Relative = OpenScript;
	Relative.RemoveFromStart(TEXT("/")); // an asset's path ("/Game/Quests/Intro")
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Bango"), TEXT("Unsaved"), Relative + TEXT(".bango"));
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

void SBangoScriptEditor::ToggleSelectedNodesDisabled()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	TArray<UBangoEdGraphNode*> Nodes;
	bool bAnyEnabled = false;
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && Node->CanBeDisabled())
		{
			Nodes.Add(Node);
			bAnyEnabled |= !Node->IsDisabledNode();
		}
	}
	if (Nodes.Num() == 0)
	{
		return;
	}
	// Off unless they're all off already, like Blueprint's: then a mixed selection goes all off with one press.
	const FScopedTransaction Transaction(bAnyEnabled ? LOCTEXT("DisableNodesTransaction", "Disable Bango Nodes") : LOCTEXT("EnableNodesTransaction", "Enable Bango Nodes"));
	for (UBangoEdGraphNode* Node : Nodes)
	{
		Node->Modify();
		Node->SetEnabledState(bAnyEnabled ? ENodeEnabledState::Disabled : ENodeEnabledState::Enabled);
		if (UEdGraph* Graph = Node->GetGraph())
		{
			Graph->NotifyNodeChanged(Node); // rebuilds its widget, with or without the Disabled banner
		}
	}
	// The poll sees the change: the text pane and the * follow.
}

bool SBangoScriptEditor::CanToggleSelectedNodesDisabled() const
{
	if (!GraphEditor.IsValid())
	{
		return false;
	}
	for (UObject* Selected : GraphEditor->GetSelectedNodes())
	{
		const UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
		if (Node && Node->CanBeDisabled())
		{
			return true;
		}
	}
	return false;
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

void SBangoScriptEditor::OnTextCursorNodes(const FString& FunctionName, const TArray<int64>& NodeIDs)
{
	// The main graph, or the function with that name (GraphItems' labels are the functions' names).
	int32 GraphIndex = FunctionName.IsEmpty() ? 0 : INDEX_NONE;
	for (int32 i = 1; GraphIndex == INDEX_NONE && i < GraphItems.Num(); ++i)
	{
		if (GraphItems[i].IsValid() && GraphItems[i]->Label.Equals(FunctionName, ESearchCase::CaseSensitive))
		{
			GraphIndex = i;
		}
	}
	if (!EdGraphs.IsValidIndex(GraphIndex) || !EdGraphs[GraphIndex].IsValid())
	{
		return;
	}

	// The nodes, by NodeID (0 is the graph's entry node).
	TArray<UBangoEdGraphNode*> Found;
	for (UEdGraphNode* Node : EdGraphs[GraphIndex]->Nodes)
	{
		UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
		if (BangoNode && NodeIDs.Contains(BangoNode->NodeID))
		{
			Found.Add(BangoNode);
		}
	}
	if (Found.Num() == 0)
	{
		return; // a line typed in and not applied yet, say
	}

	TGuardValue<bool> Guard(bSyncingFromText, true);
	const bool bSwitched = GraphIndex != ShownGraph;
	if (bSwitched)
	{
		ShowGraph(GraphIndex, /*bZoomToFit*/ false);
	}
	if (!GraphEditor.IsValid())
	{
		return;
	}
	GraphEditor->ClearSelectionSet();
	for (UBangoEdGraphNode* Node : Found)
	{
		GraphEditor->SetNodeSelection(Node, true);
	}
	// Pan only when the node isn't in view, so moving through the text line by line doesn't keep moving the graph.
	// (Not IsNodeTitleVisible: it reads the node's title widget, which a reroute node doesn't have, and crashes.)
	if (bSwitched || !IsNodeInView(Found[0]))
	{
		GraphEditor->JumpToNode(Found[0], /*bRequestRename*/ false, /*bSelectNode*/ false);
	}
}

bool SBangoScriptEditor::IsNodeInView(const UEdGraphNode* Node) const
{
	if (!GraphEditor.IsValid() || !Node)
	{
		return false;
	}
	// The node's box in graph space, and the part of the graph that's shown: from the view's top-left corner, as wide
	// and tall as the graph editor divided by the zoom.
	FSlateRect NodeRect;
	if (!GraphEditor->GetBoundsForNode(Node, NodeRect, 0.0f))
	{
		return false;
	}
	FVector2f ViewLocation(0.0f, 0.0f);
	float ViewZoom = 1.0f;
	GraphEditor->GetViewLocation(ViewLocation, ViewZoom);
	const FVector2f ViewSize = FVector2f(GraphEditor->GetTickSpaceGeometry().GetLocalSize()) / FMath::Max(ViewZoom, 0.01f);
	if (ViewSize.X <= 0.0f || ViewSize.Y <= 0.0f)
	{
		return false; // not laid out yet
	}
	// In view if its top-left corner is (a reroute is small; a big node's top is where its title is).
	const FVector2f Corner(NodeRect.Left, NodeRect.Top);
	return Corner.X >= ViewLocation.X && Corner.Y >= ViewLocation.Y
		&& Corner.X <= ViewLocation.X + ViewSize.X && Corner.Y <= ViewLocation.Y + ViewSize.Y;
}

void SBangoScriptEditor::OnGraphSelectionChanged(const TSet<UObject*>& Selection)
{
	if (bSyncingFromText || !TextBox.IsValid())
	{
		return;
	}
	TArray<int64> NodeIDs;
	for (UObject* Selected : Selection)
	{
		if (const UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Selected))
		{
			NodeIDs.Add(BangoNode->NodeID);
		}
	}
	if (NodeIDs.Num() == 0)
	{
		return;
	}
	const FString FunctionName = ShownGraph > 0 && GraphItems.IsValidIndex(ShownGraph) && GraphItems[ShownGraph].IsValid()
		? GraphItems[ShownGraph]->Label : FString();
	TextBox->ShowNodes(FunctionName, NodeIDs);
}

#undef LOCTEXT_NAMESPACE
