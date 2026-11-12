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
