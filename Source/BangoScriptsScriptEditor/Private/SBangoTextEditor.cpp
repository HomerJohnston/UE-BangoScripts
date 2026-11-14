// Bango text editor widget: a multi-line text box with Bango syntax highlighting.
// See "Bango Text Editor Spec", section 4.

#include "SBangoTextEditor.h"

#include "InputCoreTypes.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "BangoSyntaxMarshaller.h"
#include "BangoEditingAids.h"
#include "BangoSyntaxStyle.h"
#include "SBangoScrollFollower.h"
#include "Bango.h"
#include "BangoCommand.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

void SBangoTextEditor::Construct(const FArguments& InArgs)
{
	OnKeyDownHandler = InArgs._OnKeyDownHandler;
	OnTextChangedHandler = InArgs._OnTextChanged;
	OnTextCommittedHandler = InArgs._OnTextCommitted;
	OnCursorNodesChangedHandler = InArgs._OnCursorNodesChanged;

	SyntaxStyle = MakeShared<FBangoSyntaxStyle>(InArgs._FontSize);
	Marshaller = FBangoSyntaxMarshaller::Create(SyntaxStyle.ToSharedRef());
	Marshaller->SetMetadataIsError(true); // the box's text has none; any //@ in it was typed by hand

	// The frame's look (brushes, padding, scrollbars) is the editor's normal text box's.
	BoxStyle = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");

	// The scrollbars: the text only uses them (SMultiLineEditableText never places them), so each is drawn once,
	// where it's put here or by the caller.
	const bool bOwnScrollBar = !InArgs._VScrollBar.IsValid();
	VScrollBar = InArgs._VScrollBar;
	if (bOwnScrollBar)
	{
		VScrollBar = SNew(SScrollBar)
			.Style(&BoxStyle.ScrollBarStyle)
			.Orientation(Orient_Vertical)
			.Thickness(FVector2D(9.0f, 9.0f));
	}
	const TSharedRef<SWidget> OwnScrollBar = bOwnScrollBar ? StaticCastSharedRef<SWidget>(VScrollBar.ToSharedRef()) : SNullWidget::NullWidget;

	HScrollBar = SNew(SScrollBar)
		.Style(&BoxStyle.ScrollBarStyle)
		.Orientation(Orient_Horizontal)
		.Thickness(FVector2D(9.0f, 9.0f));

	const TAttribute<FMargin> TextPadding = InArgs._Padding.IsSet() ? InArgs._Padding : TAttribute<FMargin>(BoxStyle.Padding);

	const FString FullText = InArgs._Text.IsSet() ? InArgs._Text.Get().ToString() : FString();
	const FString VisibleText = Metadata.Reset(FullText);

	ChildSlot
	[
		SNew(SOverlay)

		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				// The frame, like a text box's, around the text, the divider and the gutter.
				SNew(SBorder)
				.BorderImage(this, &SBangoTextEditor::GetFrameBrush)
				.BorderBackgroundColor(SyntaxStyle->GetBackgroundColor())
				.Padding(0.0f)
				[
					SNew(SHorizontalBox)

					// The text, with its horizontal scrollbar under it.
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SBox)
						.Padding(TextPadding)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SAssignNew(TextBox, SMultiLineEditableText)
								.Text(FText::FromString(VisibleText))
								.Marshaller(Marshaller)
								.TextStyle(&SyntaxStyle->GetDefaultStyle()) // the text the marshaller doesn't colour, like an empty line, and the cursor's height
								.AutoWrapText(false)
								.IsReadOnly(InArgs._IsReadOnly)
								.HScrollBar(HScrollBar)
								.VScrollBar(VScrollBar)
								.OnTextChanged(this, &SBangoTextEditor::HandleTextChanged)
								.OnTextCommitted(this, &SBangoTextEditor::HandleTextCommitted)
								.OnKeyDownHandler(this, &SBangoTextEditor::HandleKeyDown)
								.OnIsTypedCharValid(this, &SBangoTextEditor::HandleIsTypedCharValid)
								.OnCursorMoved(this, &SBangoTextEditor::HandleCursorMoved)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								HScrollBar.ToSharedRef()
							]
						]
					]

					// The divider: a thin button the full height of the pane, drawn as a 1 px line, that shows and hides the
					// gutter. With the gutter hidden it sits at the right edge, beside the scrollbar.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(DividerButton, SButton)
						.ButtonStyle(FAppStyle::Get(), "InvisibleButton")
						.ContentPadding(0.0f)
						.Visibility_Lambda([this]() { return bGutterHasMetadata ? EVisibility::Visible : EVisibility::Collapsed; })
						.ToolTipText_Lambda([this]()
						{
							return bMetadataShown
								? NSLOCTEXT("BangoTextEditor", "HideMetadata", "Hide the node metadata")
								: NSLOCTEXT("BangoTextEditor", "ShowMetadata", "Show the node metadata");
						})
						.OnClicked_Lambda([this]()
						{
							SetMetadataShown(!bMetadataShown);
							return FReply::Handled();
						})
						[
							// Wider than the line, so it's easy to hit.
							SNew(SBox)
							.WidthOverride(DividerHitWidth)
							.HAlign(HAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(1.0f)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("WhiteBrush"))
									.ColorAndOpacity_Lambda([this]()
									{
										const bool bLit = DividerButton.IsValid() && (DividerButton->IsHovered() || DividerButton->IsPressed());
										return bLit ? FStyleColors::Highlight : FSlateColor(DividerColor);
									})
								]
							]
						]
					]

					// Node metadata, one line per line of text, scrolled along with it as it's painted, after the text
					// (SBangoScrollFollower). Not editable: the graph editor writes it.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.Visibility_Lambda([this]() { return bGutterHasMetadata && bMetadataShown ? EVisibility::Visible : EVisibility::Collapsed; })
						// As far down as the text starts, so each entry is level with its line.
						.Padding_Lambda([TextPadding]() { const FMargin Pad = TextPadding.Get(); return FMargin(6.0f, Pad.Top, 6.0f, Pad.Bottom); })
						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								SAssignNew(GutterScroll, SBangoScrollFollower)
								.ScrollBar(VScrollBar)
								[
									SNew(SOverlay)

									+ SOverlay::Slot()
									.HAlign(HAlign_Left)
									.VAlign(VAlign_Top)
									[
										SAssignNew(GutterText, STextBlock)
										.Font(SyntaxStyle->GetDefaultStyle().Font) // the text's own font, so each entry is level with its line
										.ColorAndOpacity(FSlateColor::UseSubduedForeground())
										.ToolTipText(NSLOCTEXT("BangoTextEditor", "GutterTooltip", "Node metadata: each line's node ID and position in the graph. The graph editor keeps these up to date, so they can't be edited here."))
									]

									// The cursor's line, in white, exactly over its grey copy: the same font, and blank lines above it.
									+ SOverlay::Slot()
									.HAlign(HAlign_Left)
									.VAlign(VAlign_Top)
									[
										SAssignNew(GutterHighlightText, STextBlock)
										.Font(SyntaxStyle->GetDefaultStyle().Font)
										.ColorAndOpacity(FLinearColor::White)
										.Visibility(EVisibility::HitTestInvisible)
									]
								]
							]

							// Sync Graph: whether the cursor's line selects its node in the graph, and the other way round.
							+ SOverlay::Slot()
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Top)
							.Padding(0, 7, 3, 0)
							[
								SNew(SCheckBox)
								.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
								.IsChecked_Lambda([this]() { return bGraphSync ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
								.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetGraphSyncEnabled(NewState == ECheckBoxState::Checked); })
								.ToolTipText(NSLOCTEXT("BangoTextEditor", "SyncGraphTooltip", "While on, the line the cursor is on selects its node in the graph, and selecting a node in the graph moves the cursor to its line."))
								[
									SNew(SBox)
									.Padding(FMargin(6.0f, 1.0f))
									[
										SNew(STextBlock)
										.Text(NSLOCTEXT("BangoTextEditor", "SyncGraph", "Sync Graph"))
										// .Font(FAppStyle::GetFontStyle("SmallFont"))
									]
								]
							]
						]
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				OwnScrollBar
			]
		]

		// Autocomplete: the list of names, beside the word being typed. Drawn here, over the text, rather than in a pop-up
		// window, so the text keeps keyboard focus (FBangoCompletion).
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateSP(this, &SBangoTextEditor::GetCompletionPadding))
		[
			MakeCompletionPopup()
		]
	];

	UpdateGutter();
}

const FSlateBrush* SBangoTextEditor::GetFrameBrush() const
{
	if (!TextBox.IsValid())
	{
		return &BoxStyle.BackgroundImageNormal;
	}
	if (TextBox->IsTextReadOnly())
	{
		return &BoxStyle.BackgroundImageReadOnly;
	}
	if (TextBox->HasKeyboardFocus())
	{
		return &BoxStyle.BackgroundImageFocused;
	}
	return IsHovered() ? &BoxStyle.BackgroundImageHovered : &BoxStyle.BackgroundImageNormal;
}

void SBangoTextEditor::SetMetadataShown(bool bShown)
{
	bMetadataShown = bShown;
}

FText SBangoTextEditor::GetText() const
{
	return FText::FromString(Metadata.GetFullText());
}

void SBangoTextEditor::SetText(const FText& InText)
{
	const FString VisibleText = Metadata.Reset(InText.ToString());
	bHasReportedNodes = false;
	UpdateGutter();
	if (TextBox.IsValid())
	{
		TextBox->SetText(FText::FromString(VisibleText));
	}
}

void SBangoTextEditor::GoTo(const FTextLocation& Location)
{
	if (!TextBox.IsValid())
	{
		return;
	}
	// The same line and column in the box: taking metadata off the end of a line moves nothing before it.
	// A column inside the metadata goes to the end of the line's code.
	const TArray<FString>& Lines = Metadata.GetLines();
	const int32 Line = FMath::Clamp(Location.GetLineIndex(), 0, FMath::Max(Lines.Num() - 1, 0));
	const int32 LineLen = Lines.Num() > 0 ? Lines[Line].Len() : 0;
	TextBox->GoTo(FTextLocation(Line, FMath::Clamp(Location.GetOffset(), 0, LineLen)));
}

const FSlateFontInfo& SBangoTextEditor::GetFont() const
{
	return SyntaxStyle->GetDefaultStyle().Font;
}

void SBangoTextEditor::InsertNewLine()
{
	// One undoable edit: the replaced selection, the line break and the indentation.
	SMultiLineEditableText::FScopedEditableTextTransaction Transaction(TextBox);

	if (TextBox->AnyTextSelected())
	{
		TextBox->DeleteSelectedText();
	}

	// The cursor's line as it is now (the metadata tracker hears about changes only when the edit ends).
	const FTextLocation Cursor = TextBox->GetCursorLocation();
	TArray<FString> Lines;
	FBangoMetadataTracker::SplitLines(TextBox->GetText().ToString(), Lines);
	const FString Line = Lines.IsValidIndex(Cursor.GetLineIndex()) ? Lines[Cursor.GetLineIndex()] : FString();

	const FBangoNewLineEdit Edit = FBangoEditingAids::NewLine(Line, Cursor.GetOffset());
	TextBox->InsertTextAtCursor(Edit.Insert);
	TextBox->GoTo(FTextLocation(Cursor.GetLineIndex() + Edit.CursorLinesDown, Edit.CursorColumn));
}

bool SBangoTextEditor::HandleIsTypedCharValid(const TCHAR Character)
{
	// The Tab that accepted an autocomplete entry isn't typed as well.
	if (Character == TEXT('\t') && bSwallowTab)
	{
		bSwallowTab = false;
		return false;
	}
	bSwallowTab = false;

	// Typing a name's character may open the autocomplete list (UpdateCompletion, next Tick).
	if ((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('A') && Character <= TEXT('Z'))
		|| (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_'))
	{
		bTypedNameChar = true;
	}

	// A '}' typed on a line that's only indentation takes one tab off first, so it lines up with its block's start.
	if (Character != TEXT('}') || !TextBox.IsValid() || TextBox->IsTextReadOnly() || TextBox->AnyTextSelected())
	{
		return true;
	}
	const FTextLocation Cursor = TextBox->GetCursorLocation();
	const TArray<FString>& Lines = Metadata.GetLines();
	if (!Lines.IsValidIndex(Cursor.GetLineIndex()))
	{
		return true;
	}
	const int32 TabColumn = FBangoEditingAids::OutdentBeforeClosingBrace(Lines[Cursor.GetLineIndex()], Cursor.GetOffset());
	if (TabColumn == INDEX_NONE)
	{
		return true;
	}

	// Type it here instead, with the tab taken off, as one undoable edit; returning false stops it being typed again.
	SMultiLineEditableText::FScopedEditableTextTransaction Transaction(TextBox);
	TextBox->SelectText(FTextLocation(Cursor.GetLineIndex(), TabColumn), FTextLocation(Cursor.GetLineIndex(), TabColumn + 1));
	TextBox->DeleteSelectedText();
	TextBox->InsertTextAtCursor(FString(TEXT("}")));
	return false;
}

void SBangoTextEditor::HandleCursorMoved(const FTextLocation& Location)
{
	FBangoTextPosition Position;
	Position.Line = Location.GetLineIndex();
	Position.Column = Location.GetOffset();
	Marshaller->SetCursor(Position);

	UpdateGutterHighlight(Location.GetLineIndex());
	ReportCursorNodes(false);
	bCompletionDirty = true;
}

void SBangoTextEditor::SetGraphSyncEnabled(bool bEnabled)
{
	bGraphSync = bEnabled;
	bHasReportedNodes = false;
	if (bGraphSync)
	{
		ReportCursorNodes(true);
	}
}

bool SBangoTextEditor::GetNodesOfLines(int32 FirstLine, int32 LastLine, FString& OutFunctionName, TArray<int64>& OutNodeIDs) const
{
	const FBangoScriptNames& Names = Marshaller->GetScriptNames();
	OutFunctionName.Reset();
	OutNodeIDs.Reset();

	int32 Graph = INDEX_NONE;
	TArray<int64> LineNodeIDs;
	for (int32 Line = FirstLine; Line <= LastLine; ++Line)
	{
		LineNodeIDs.Reset();
		Metadata.GetNodeIDs(Line, LineNodeIDs);
		if (Names.IsEntryLine(Line))
		{
			LineNodeIDs.Add(0); // a graph's entry node is node 0 of that graph
		}
		if (LineNodeIDs.Num() == 0)
		{
			continue;
		}

		// Only one graph's nodes can be selected at once: the first line's graph.
		const int32 LineGraph = Names.GetLineGraph(Line);
		if (Graph == INDEX_NONE)
		{
			Graph = LineGraph;
			OutFunctionName = Names.GetGraphFunctionName(Graph);
		}
		else if (LineGraph != Graph)
		{
			continue;
		}
		OutNodeIDs.Append(LineNodeIDs);
	}
	return OutNodeIDs.Num() > 0;
}

void SBangoTextEditor::ReportCursorNodes(bool bForce)
{
	// Only while the cursor is being moved here: when the text is set from the graph, the cursor moves too, and that
	// mustn't change what's selected in the graph.
	if (!bGraphSync || !OnCursorNodesChangedHandler.IsBound() || !TextBox.IsValid() || !TextBox->HasKeyboardFocus())
	{
		return;
	}

	int32 FirstLine = TextBox->GetCursorLocation().GetLineIndex();
	int32 LastLine = FirstLine;
	if (TextBox->AnyTextSelected())
	{
		const FTextSelection Selection = TextBox->GetSelection();
		FirstLine = FMath::Min(Selection.GetBeginning().GetLineIndex(), Selection.GetEnd().GetLineIndex());
		LastLine = FMath::Max(Selection.GetBeginning().GetLineIndex(), Selection.GetEnd().GetLineIndex());
	}

	FString FunctionName;
	TArray<int64> NodeIDs;
	if (!GetNodesOfLines(FirstLine, LastLine, FunctionName, NodeIDs))
	{
		return; // a line that isn't a node ({, };, a declaration): leave the graph's selection alone
	}
	if (!bForce && bHasReportedNodes && FunctionName.Equals(ReportedFunctionName, ESearchCase::CaseSensitive) && NodeIDs == ReportedNodeIDs)
	{
		return;
	}
	bHasReportedNodes = true;
	ReportedFunctionName = FunctionName;
	ReportedNodeIDs = NodeIDs;
	OnCursorNodesChangedHandler.Execute(FunctionName, NodeIDs);
}

void SBangoTextEditor::ShowNodes(const FString& FunctionName, const TArray<int64>& NodeIDs)
{
	if (!bGraphSync || !TextBox.IsValid())
	{
		return;
	}

	// The top-most line of the nodes. NodeIDs are unique in the file; 0 is the entry node of the given graph.
	int32 Line = INDEX_NONE;
	for (const int64 NodeID : NodeIDs)
	{
		const int32 NodeLine = NodeID == 0 ? Marshaller->GetScriptNames().FindEntryLine(FunctionName) : Metadata.FindLineOfNode(NodeID);
		if (NodeLine != INDEX_NONE && (Line == INDEX_NONE || NodeLine < Line))
		{
			Line = NodeLine;
		}
	}
	if (Line == INDEX_NONE)
	{
		return;
	}

	// What that line would report, so moving the cursor there doesn't send it back to the graph.
	bHasReportedNodes = GetNodesOfLines(Line, Line, ReportedFunctionName, ReportedNodeIDs);

	const FTextLocation Cursor = TextBox->GetCursorLocation();
	if (Cursor.GetLineIndex() == Line)
	{
		TextBox->ScrollTo(Cursor);
		return;
	}

	// The start of the line's code: past its indentation, and past the //~ of a disconnected line.
	const TArray<FString>& Lines = Metadata.GetLines();
	const FString& Text = Lines.IsValidIndex(Line) ? Lines[Line] : FString();
	int32 Column = 0;
	auto SkipIndent = [&Text, &Column]()
	{
		while (Column < Text.Len() && (Text[Column] == TEXT(' ') || Text[Column] == TEXT('\t')))
		{
			++Column;
		}
	};
	SkipIndent();
	if (Text.Mid(Column, 3) == TEXT("//~"))
	{
		Column += 3;
		SkipIndent();
	}

	const FTextLocation Target(Line, Column);
	TextBox->GoTo(Target);
	TextBox->ScrollTo(Target);
	UpdateGutterHighlight(Line);
}

void SBangoTextEditor::UpdateGutterHighlight(int32 Line)
{
	GutterHighlightLine = Line;
	if (!GutterHighlightText.IsValid())
	{
		return;
	}
	// Blank lines down to the cursor's line, then its metadata: it lands exactly on its grey copy.
	FString Highlight;
	const TArray<FBangoLineMetadata>& LineMeta = Metadata.GetMetadata();
	if (LineMeta.IsValidIndex(Line))
	{
		for (int32 I = 0; I < Line; ++I)
		{
			Highlight += TEXT("\n");
		}
		Highlight += LineMeta[Line].Metadata;
	}
	GutterHighlightText->SetText(FText::FromString(Highlight));
}

FReply SBangoTextEditor::HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	// While the autocomplete list is open: Up and Down move through it, Tab puts in the entry, Escape closes it.
	// Anything else is typed as usual (and filters the list).
	if (bCompletionOpen && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown() && !KeyEvent.IsCommandDown())
	{
		const FKey Key = KeyEvent.GetKey();
		if (Key == EKeys::Up || Key == EKeys::Down)
		{
			MoveCompletionSelection(Key == EKeys::Up ? -1 : 1);
			return FReply::Handled();
		}
		if (Key == EKeys::PageUp || Key == EKeys::PageDown)
		{
			MoveCompletionSelection(Key == EKeys::PageUp ? -CompletionVisibleRows : CompletionVisibleRows);
			return FReply::Handled();
		}
		if (Key == EKeys::Tab && !KeyEvent.IsShiftDown())
		{
			AcceptCompletion();
			bSwallowTab = true; // the tab character that follows this key
			return FReply::Handled();
		}
		if (Key == EKeys::Escape)
		{
			CloseCompletion();
			return FReply::Handled();
		}
		if (Key == EKeys::Enter)
		{
			CloseCompletion(); // and a new line, as usual
		}
	}

	// Tab indents (Bango uses one tab per block level) instead of moving focus
	// to the next widget. Handling the key down stops Slate's focus navigation;
	// the tab character itself is typed by the key-char event that follows.
	if (KeyEvent.GetKey() == EKeys::Tab && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown() && !KeyEvent.IsShiftDown())
	{
		return FReply::Handled();
	}

	// Enter keeps the line's indentation (FBangoEditingAids::NewLine). Ctrl+Enter is left for the caller (Apply).
	if (KeyEvent.GetKey() == EKeys::Enter && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown() && !KeyEvent.IsCommandDown()
		&& TextBox.IsValid() && !TextBox->IsTextReadOnly())
	{
		InsertNewLine();
		return FReply::Handled();
	}

	return OnKeyDownHandler.IsBound() ? OnKeyDownHandler.Execute(Geometry, KeyEvent) : FReply::Unhandled();
}

FReply SBangoTextEditor::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Over the text, the text scrolls itself and this isn't reached. Anywhere else in the editor (the gutter, the
	// divider), the wheel scrolls the text. ForceScroll is the text's public way in: it passes the wheel delta to
	// its own (protected) OnMouseWheel, with its own geometry.
	if (TextBox.IsValid() && MouseEvent.GetWheelDelta() != 0.0f)
	{
		TextBox->ForceScroll(static_cast<int32>(MouseEvent.GetUserIndex()), MouseEvent.GetWheelDelta());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SBangoTextEditor::HandleTextChanged(const FText& NewVisibleText)
{
	// The cursor's line tells apart edits that give the same text (such as Enter between two blank lines).
	const int32 CursorLine = TextBox.IsValid() ? TextBox->GetCursorLocation().GetLineIndex() : INDEX_NONE;
	Metadata.OnVisibleTextChanged(NewVisibleText.ToString(), CursorLine);
	UpdateGutter();
	bCompletionDirty = true;
	OnTextChangedHandler.ExecuteIfBound(FText::FromString(Metadata.GetFullText()));
}

void SBangoTextEditor::HandleTextCommitted(const FText& NewVisibleText, ETextCommit::Type CommitType)
{
	OnTextCommittedHandler.ExecuteIfBound(FText::FromString(Metadata.GetFullText()), CommitType);
}

void SBangoTextEditor::UpdateGutter()
{
	bGutterHasMetadata = false;
	for (const FBangoLineMetadata& LineMeta : Metadata.GetMetadata())
	{
		if (LineMeta.HasMetadata())
		{
			bGutterHasMetadata = true;
			break;
		}
	}
	if (GutterText.IsValid())
	{
		GutterText->SetText(FText::FromString(Metadata.GetGutterText()));
	}
	UpdateGutterHighlight(GutterHighlightLine);
}

// ---- Autocomplete ----

void SBangoTextEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// The list belongs to the text being typed in: it closes when the text loses keyboard focus.
	if (bCompletionOpen && (!TextBox.IsValid() || !TextBox->HasKeyboardFocus()))
	{
		CloseCompletion();
	}

	// Once per frame, after the text and the cursor have settled: open, filter or close the list.
	if (bCompletionDirty)
	{
		bCompletionDirty = false;
		UpdateCompletion(bTypedNameChar);
		bTypedNameChar = false;
	}
}

void SBangoTextEditor::UpdateCompletion(bool bMayOpen)
{
	if (!TextBox.IsValid() || !TextBox->HasKeyboardFocus() || TextBox->IsTextReadOnly() || TextBox->AnyTextSelected())
	{
		CloseCompletion();
		return;
	}

	const FTextLocation Cursor = TextBox->GetCursorLocation();
	const TArray<FString>& Lines = Metadata.GetLines();
	if (!Lines.IsValidIndex(Cursor.GetLineIndex()))
	{
		CloseCompletion();
		return;
	}
	const FString& Line = Lines[Cursor.GetLineIndex()];

	int32 WordStart = INDEX_NONE;
	if (!FBangoCompletion::FindWordBeforeCursor(Line, Cursor.GetOffset(), WordStart))
	{
		CloseCompletion();
		return;
	}

	// Opens only when a name's character is typed; once open, it follows the word as it's typed or deleted.
	const bool bSameWord = bCompletionOpen && CompletionLine == Cursor.GetLineIndex() && CompletionStart == WordStart;
	if (!bSameWord)
	{
		if (!bMayOpen)
		{
			CloseCompletion();
			return;
		}
		GatherCompletionItems(Cursor.GetLineIndex());
	}

	const FString Prefix = Line.Mid(WordStart, Cursor.GetOffset() - WordStart);
	TArray<int32> Indices;
	FBangoCompletion::Filter(CompletionItems, Prefix, Indices);

	// Nothing left, or only the word itself as typed: nothing to put in.
	if (Indices.Num() == 0 || (Indices.Num() == 1 && CompletionItems[Indices[0]].Name.Equals(Prefix, ESearchCase::CaseSensitive)))
	{
		CloseCompletion();
		return;
	}

	// Keep the selected entry if it's still listed.
	const TSharedPtr<FBangoCompletionItem> WasSelected = GetSelectedCompletion();
	CompletionShown.Reset();
	TSharedPtr<FBangoCompletionItem> Select;
	for (const int32 Index : Indices)
	{
		TSharedPtr<FBangoCompletionItem> Item = CompletionItemPtrs[Index];
		CompletionShown.Add(Item);
		if (Item == WasSelected)
		{
			Select = Item;
		}
	}
	if (!Select.IsValid())
	{
		Select = CompletionShown[0];
	}

	bCompletionOpen = true;
	CompletionLine = Cursor.GetLineIndex();
	CompletionStart = WordStart;
	Marshaller->SetProbe(CompletionLine, CompletionStart, Cursor.GetOffset());

	if (CompletionList.IsValid())
	{
		CompletionList->RequestListRefresh();
		CompletionList->SetSelection(Select, ESelectInfo::Direct);
		CompletionList->RequestScrollIntoView(Select);
	}
}

void SBangoTextEditor::GatherCompletionItems(int32 Line)
{
	// The registered commands, asked for each time the list opens (game modules may add commands after this widget is made).
	TArray<FBangoCompletionItem> Commands;
	const FBangoCommandTable& Table = FBangoModule::Get().GetCommands();
	TArray<FString> Names;
	Table.GetCommandNames(Names);
	for (const FString& Name : Names)
	{
		FBangoCompletionItem Item;
		Item.Name = Name;
		Item.Kind = EBangoCompletionKind::Command;
		Item.bCallable = true;
		if (const FBangoCommandSignature* Signature = Table.FindCommand(Name))
		{
			Item.Detail = Signature->ToString();
			Item.Description = Signature->CommandDescription;
		}
		Commands.Add(Item);
	}

	FBangoCompletion::GatherItems(Marshaller->GetScriptNames(), Line, Commands, CompletionItems);
	CompletionItemPtrs.Reset();
	for (const FBangoCompletionItem& Item : CompletionItems)
	{
		CompletionItemPtrs.Add(MakeShared<FBangoCompletionItem>(Item));
	}
}

void SBangoTextEditor::CloseCompletion()
{
	if (!bCompletionOpen)
	{
		return;
	}
	bCompletionOpen = false;
	CompletionLine = INDEX_NONE;
	CompletionStart = INDEX_NONE;
	CompletionShown.Reset();
	Marshaller->ClearProbe();
	if (CompletionList.IsValid())
	{
		CompletionList->RequestListRefresh();
	}
}

TSharedPtr<FBangoCompletionItem> SBangoTextEditor::GetSelectedCompletion() const
{
	if (!CompletionList.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FBangoCompletionItem>> Selected = CompletionList->GetSelectedItems();
	return Selected.Num() > 0 ? Selected[0] : nullptr;
}

void SBangoTextEditor::MoveCompletionSelection(int32 Delta)
{
	if (!CompletionList.IsValid() || CompletionShown.Num() == 0)
	{
		return;
	}
	const int32 Current = CompletionShown.IndexOfByKey(GetSelectedCompletion());
	const int32 Next = FMath::Clamp((Current == INDEX_NONE ? 0 : Current) + Delta, 0, CompletionShown.Num() - 1);
	CompletionList->SetSelection(CompletionShown[Next], ESelectInfo::OnNavigation);
	CompletionList->RequestScrollIntoView(CompletionShown[Next]);
}

void SBangoTextEditor::AcceptCompletion()
{
	const TSharedPtr<FBangoCompletionItem> Item = GetSelectedCompletion();
	if (!Item.IsValid() || !TextBox.IsValid() || CompletionLine == INDEX_NONE)
	{
		CloseCompletion();
		return;
	}

	// The word typed so far is replaced by the whole name; commands and functions get their '(' too (unless one's there).
	const FTextLocation Cursor = TextBox->GetCursorLocation();
	const TArray<FString>& Lines = Metadata.GetLines();
	const FString Line = Lines.IsValidIndex(CompletionLine) ? Lines[CompletionLine] : FString();
	FString Insert = Item->Name;
	if (Item->bCallable && !(Cursor.GetOffset() < Line.Len() && Line[Cursor.GetOffset()] == TEXT('(')))
	{
		Insert += TEXT("(");
	}

	const int32 WordLine = CompletionLine;
	const int32 WordStart = CompletionStart;
	CloseCompletion();

	SMultiLineEditableText::FScopedEditableTextTransaction Transaction(TextBox);
	TextBox->SelectText(FTextLocation(WordLine, WordStart), FTextLocation(WordLine, Cursor.GetOffset()));
	TextBox->DeleteSelectedText();
	TextBox->InsertTextAtCursor(Insert);
}

FMargin SBangoTextEditor::GetCompletionPadding() const
{
	// Under the word being typed (above it, if there's no room below), kept inside this widget.
	const FBangoTextProbe& Probe = Marshaller->GetProbe();
	if (!bCompletionOpen || !Probe.bValid || !CompletionPopup.IsValid())
	{
		return FMargin(0.0f);
	}
	// The probe was recorded while painting, so its "absolute" position is in paint space (relative to the window):
	// convert it with this widget's paint-space geometry. (The tick-space geometry is relative to the desktop, which put
	// the list off by the window's own position on screen.)
	const FGeometry& Geometry = GetPaintSpaceGeometry();
	const FVector2D WordTopLeft = Geometry.AbsoluteToLocal(Probe.AbsolutePosition);
	const FVector2D WordBottomRight = Geometry.AbsoluteToLocal(Probe.AbsolutePosition + Probe.AbsoluteSize);
	const FVector2D PopupSize = CompletionPopup->GetDesiredSize();
	const FVector2D Size = Geometry.GetLocalSize();

	const float X = FMath::Clamp<float>(WordTopLeft.X, 0.0f, FMath::Max<float>(0.0f, Size.X - PopupSize.X));
	float Y = WordBottomRight.Y;
	if (Y + PopupSize.Y > Size.Y && WordTopLeft.Y - PopupSize.Y >= 0.0f)
	{
		Y = WordTopLeft.Y - PopupSize.Y;
	}
	return FMargin(X, Y, 0.0f, 0.0f);
}

TSharedRef<SWidget> SBangoTextEditor::MakeCompletionPopup()
{
	return SAssignNew(CompletionPopup, SBorder)
		.Visibility_Lambda([this]() { return bCompletionOpen && Marshaller->GetProbe().bValid ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(FMargin(2.0f))
		[
			SNew(SBox)
			.WidthOverride(CompletionWidth)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(CompletionRowHeight * CompletionVisibleRows)
					[
						SAssignNew(CompletionList, SListView<TSharedPtr<FBangoCompletionItem>>)
						.ListItemsSource(&CompletionShown)
						.OnGenerateRow(this, &SBangoTextEditor::MakeCompletionRow)
						.SelectionMode(ESelectionMode::Single)
						.IsFocusable(false) // the text keeps keyboard focus
						.OnMouseButtonClick_Lambda([this](TSharedPtr<FBangoCompletionItem> Item)
						{
							CompletionList->SetSelection(Item, ESelectInfo::OnMouseClick);
							AcceptCompletion();
							FSlateApplication::Get().SetKeyboardFocus(TextBox, EFocusCause::SetDirectly);
						})
					]
				]

				// The selected entry's description, if it has one.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(6.0f, 3.0f))
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]()
					{
						const TSharedPtr<FBangoCompletionItem> Item = GetSelectedCompletion();
						return Item.IsValid() && !Item->Description.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]()
					{
						const TSharedPtr<FBangoCompletionItem> Item = GetSelectedCompletion();
						return Item.IsValid() ? FText::FromString(Item->Description) : FText::GetEmpty();
					})
				]
			]
		];
}

TSharedRef<ITableRow> SBangoTextEditor::MakeCompletionRow(TSharedPtr<FBangoCompletionItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// The kind as one letter and the name in the text's colours for it, then the type or signature.
	EBangoSyntaxKind Kind = EBangoSyntaxKind::Command;
	const TCHAR* Letter = TEXT("C");
	switch (Item->Kind)
	{
	case EBangoCompletionKind::Parameter: Kind = EBangoSyntaxKind::Parameter; Letter = TEXT("P"); break;
	case EBangoCompletionKind::Local:     Kind = EBangoSyntaxKind::Local;     Letter = TEXT("L"); break;
	case EBangoCompletionKind::Global:    Kind = EBangoSyntaxKind::Global;    Letter = TEXT("G"); break;
	case EBangoCompletionKind::Function:  Kind = EBangoSyntaxKind::Function;  Letter = TEXT("F"); break;
	case EBangoCompletionKind::Builtin:   Kind = EBangoSyntaxKind::Builtin;   Letter = TEXT("B"); break;
	case EBangoCompletionKind::Command:   Kind = EBangoSyntaxKind::Command;   Letter = TEXT("C"); break;
	case EBangoCompletionKind::Keyword:
	{
		// In the colour the text gives that word (flow, jump, type, value, ...).
		Letter = TEXT("K");
		Kind = EBangoSyntaxKind::Keyword;
		TArray<FBangoSyntaxToken> Tokens;
		FBangoLineTokenizer::TokenizeLine(FStringView(Item->Name), Tokens);
		if (Tokens.Num() == 1)
		{
			Kind = Tokens[0].Kind;
		}
		break;
	}
	}
	const FTextBlockStyle& KindStyle = SyntaxStyle->GetStyle(Kind, false);

	return SNew(STableRow<TSharedPtr<FBangoCompletionItem>>, OwnerTable)
		.Padding(FMargin(4.0f, 1.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(12.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Letter))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(KindStyle.ColorAndOpacity)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Name))
				.Font(SyntaxStyle->GetDefaultStyle().Font)
				.ColorAndOpacity(KindStyle.ColorAndOpacity)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Detail))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		];
}
