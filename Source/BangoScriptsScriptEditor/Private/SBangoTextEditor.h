// Bango text editor widget: a multi-line text box with Bango syntax highlighting.
// See "Bango Text Editor Spec", section 4.
//
// It's an SMultiLineEditableText in a frame of its own rather than an SMultiLineEditableTextBox: the box always puts
// the vertical scrollbar into its own layout, even one given from outside, so a scrollbar placed by the caller was
// drawn twice. Here the scrollbar given is only used, never placed.
//
// Node metadata (//@12(4, 7)) is kept out of the editable text: it's shown in a gutter on the right, beside its
// line, and put back when the text is read (GetText, OnTextChanged). See FBangoMetadataTracker.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "BangoTextMetadata.h"

class FBangoSyntaxMarshaller;
class FBangoSyntaxStyle;
class SScrollBar;
class SBangoScrollFollower;
class SButton;
class STextBlock;
struct FTextLocation;

/** The nodes on the cursor's line(s): the graph's function name (empty for the main graph), and their NodeIDs (0 for the graph's entry). */
DECLARE_DELEGATE_TwoParams(FOnBangoCursorNodesChanged, const FString& /*FunctionName*/, const TArray<int64>& /*NodeIDs*/);

class SBangoTextEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoTextEditor)
		: _FontSize(10.0f)
	{}
		/** The script text, metadata included. Read once, when the widget is made; use SetText after that. */
		SLATE_ATTRIBUTE(FText, Text)

		/** Font size in points. */
		SLATE_ARGUMENT(float, FontSize)

		/** If true, the text can be read and selected but not changed. */
		SLATE_ATTRIBUTE(bool, IsReadOnly)

		/** The space between the box's border and its text. Unset: the text box style's. */
		SLATE_ATTRIBUTE(FMargin, Padding)

		/**
		 * A vertical scrollbar placed by the caller instead of the box's own (it says how far the text is scrolled).
		 * Without one, the editor makes its own, right of the metadata gutter.
		 */
		SLATE_ARGUMENT(TSharedPtr<SScrollBar>, VScrollBar)

		/** The whole text, metadata included. */
		SLATE_EVENT(FOnTextChanged, OnTextChanged)
		/** The whole text, metadata included. */
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)

		/**
		 * Sync Graph: the cursor moved to a line (or a selection over lines) with nodes on it, while the text has keyboard
		 * focus. Lines that aren't nodes ({, };, declarations) don't send this.
		 */
		SLATE_EVENT(FOnBangoCursorNodesChanged, OnCursorNodesChanged)

		/** Keys the editor doesn't use itself (it uses Tab) go here first, like SMultiLineEditableTextBox's OnKeyDownHandler. */
		SLATE_EVENT(FOnKeyDown, OnKeyDownHandler)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The whole text, metadata included. */
	FText GetText() const;

	/** Sets the whole text, metadata included. The metadata goes to the gutter. */
	void SetText(const FText& InText);

	/** Puts the cursor at a line and column (both from 0) of the whole text, scrolling it into view. */
	void GoTo(const FTextLocation& Location);

	/** The font the text is in (Regular), for things that need to line up with it, like line numbers. */
	const FSlateFontInfo& GetFont() const;

	/**
	 * Sync Graph: moves the cursor to the top-most line of these nodes (0: the entry of FunctionName's graph, or of the
	 * main graph if it's empty) and scrolls it into view. Does nothing while Sync Graph is off.
	 */
	void ShowNodes(const FString& FunctionName, const TArray<int64>& NodeIDs);

	/** The Sync Graph toggle at the gutter's top-right: whether the text and the graph follow each other's selection. On to start with. */
	void SetGraphSyncEnabled(bool bEnabled);
	bool IsGraphSyncEnabled() const { return bGraphSync; }

	/** Shows or hides the node metadata gutter (the divider beside it does the same). Shown to start with. */
	void SetMetadataShown(bool bShown);
	bool IsMetadataShown() const { return bMetadataShown; }

	// SWidget
	/** The mouse wheel over the gutter or the divider scrolls the text too. */
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FReply HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	/** Enter: a line break and the line's indentation (FBangoEditingAids::NewLine). */
	void InsertNewLine();

	/** Asked before each typed character: a '}' on an indentation-only line is typed here, one tab further left. */
	bool HandleIsTypedCharValid(const TCHAR Character);

	/** Moves the bracket highlight and the gutter's white line along with the cursor, and tells the graph (Sync Graph). */
	void HandleCursorMoved(const FTextLocation& Location);

	/** The nodes on lines FirstLine to LastLine, in the first such line's graph. False if there are none. */
	bool GetNodesOfLines(int32 FirstLine, int32 LastLine, FString& OutFunctionName, TArray<int64>& OutNodeIDs) const;

	/** Sends the cursor's nodes to OnCursorNodesChanged, unless they're the ones sent last (or bForce). */
	void ReportCursorNodes(bool bForce);

	/** Shows a line's metadata in white in the gutter. */
	void UpdateGutterHighlight(int32 Line);
	void HandleTextChanged(const FText& NewVisibleText);
	void HandleTextCommitted(const FText& NewVisibleText, ETextCommit::Type CommitType);

	/** Shows the tracker's metadata in the gutter. */
	void UpdateGutter();

	FOnKeyDown OnKeyDownHandler;
	FOnTextChanged OnTextChangedHandler;
	FOnTextCommitted OnTextCommittedHandler;
	FOnBangoCursorNodesChanged OnCursorNodesChangedHandler;

	/** The frame's look, for the current state: read-only, focused, hovered or normal. */
	const FSlateBrush* GetFrameBrush() const;

	TSharedPtr<SMultiLineEditableText> TextBox;
	TSharedPtr<FBangoSyntaxMarshaller> Marshaller;
	TSharedPtr<FBangoSyntaxStyle> SyntaxStyle;

	/** The editor's normal text box style: the frame's brushes, padding and scrollbar look. */
	FEditableTextBoxStyle BoxStyle;

	/** The box's metadata, one entry per line. */
	FBangoMetadataTracker Metadata;

	/** The text's vertical scrollbar: the caller's, or the editor's own. */
	TSharedPtr<SScrollBar> VScrollBar;
	/** The text's horizontal scrollbar, under the text and the gutter. */
	TSharedPtr<SScrollBar> HScrollBar;
	TSharedPtr<SBangoScrollFollower> GutterScroll;
	TSharedPtr<STextBlock> GutterText;

	/** Whether any line has metadata; the gutter and its divider are hidden when none does. */
	bool bGutterHasMetadata = false;

	/** Sync Graph, and the nodes last sent to the graph (so the same line isn't sent twice). */
	bool bGraphSync = true;
	bool bHasReportedNodes = false;
	FString ReportedFunctionName;
	TArray<int64> ReportedNodeIDs;

	/** The cursor's line's metadata, in white, over the gutter's grey text. */
	TSharedPtr<STextBlock> GutterHighlightText;
	int32 GutterHighlightLine = INDEX_NONE;

	/** Whether the gutter is shown (the divider toggles it). */
	bool bMetadataShown = true;

	/** The divider between the text and the gutter. */
	TSharedPtr<SButton> DividerButton;

	/** The divider's line: a quiet grey, lit with the highlight colour while hovered. */
	static constexpr float DividerHitWidth = 7.0f;
	inline static const FLinearColor DividerColor = FLinearColor(0.07f, 0.07f, 0.07f);
};
