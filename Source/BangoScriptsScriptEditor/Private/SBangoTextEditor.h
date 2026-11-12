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

	/** Moves the bracket highlight along with the cursor. */
	void HandleCursorMoved(const FTextLocation& Location);
	void HandleTextChanged(const FText& NewVisibleText);
	void HandleTextCommitted(const FText& NewVisibleText, ETextCommit::Type CommitType);

	/** Shows the tracker's metadata in the gutter. */
	void UpdateGutter();

	FOnKeyDown OnKeyDownHandler;
	FOnTextChanged OnTextChangedHandler;
	FOnTextCommitted OnTextCommittedHandler;

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

	/** Whether the gutter is shown (the divider toggles it). */
	bool bMetadataShown = true;

	/** The divider between the text and the gutter. */
	TSharedPtr<SButton> DividerButton;

	/** The divider's line: a quiet grey, lit with the highlight colour while hovered. */
	static constexpr float DividerHitWidth = 7.0f;
	inline static const FLinearColor DividerColor = FLinearColor(0.07f, 0.07f, 0.07f);
};
