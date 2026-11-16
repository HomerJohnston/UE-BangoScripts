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
#include "BangoCompletion.h"

class FBangoSyntaxMarshaller;
class FBangoSyntaxStyle;
class SScrollBar;
class SBangoScrollFollower;
class SButton;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;
class STextBlock;
struct FTextLocation;
struct FBangoCompileError;

/** The nodes on the cursor's line(s): the graph's function name (empty for the main graph), and their NodeIDs (0 for the graph's entry). */
DECLARE_DELEGATE_TwoParams(FOnBangoCursorNodesChanged, const FString& /*FunctionName*/, const TArray<int64>& /*NodeIDs*/);

class SBangoTextEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoTextEditor)
		: _FontSize(10.0f)
		, _InlineScriptEditor(false)
	{}
		/** The script text, metadata included. Read once, when the widget is made; use SetText after that. */
		SLATE_ATTRIBUTE(FText, Text)

		/** Font size in points. */
		SLATE_ARGUMENT(float, FontSize)

		/** If true, the text can be read and selected but not changed. */
		SLATE_ATTRIBUTE(bool, IsReadOnly)

		/**
		 * For an inline script's box (FBangoInlineScript, in the details panel): no node metadata gutter and no divider
		 * to show it, and the text is read-only while it has any node metadata (HasNodeMetadata), as such a script is
		 * edited in the full editor. Off: the script editor's text pane.
		 */
		SLATE_ARGUMENT(bool, InlineScriptEditor)

		/**
		 * Compiles the script a moment after typing stops (Editor Preferences > Plugins > Bango), and shows its first
		 * error: underlined in the text, and its message in a box hanging under this widget while it has focus.
		 */
		SLATE_ARGUMENT(bool, CompileAsYouType)

		/** Adds entries to the text's right-click menu (after the usual Cut, Copy, Paste, ...). */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtender)

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

	/**
	 * Shows an error found outside this widget (the script editor's Apply: the text didn't read as a graph) as compile
	 * as you type shows its own: underlined, with its message under this widget. It stays until the text changes, or
	 * compiling as you type finds an error of its own. bFocus: puts the keyboard focus in the text (so the message shows).
	 */
	void ShowError(const FBangoCompileError& Error, bool bFocus);

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

	/** True if any line of the text has node metadata (//@). */
	bool HasNodeMetadata() const { return bGutterHasMetadata; }

	/** True while the vertical scrollbar (the editor's own, or the one given) is shown: the text is taller than the box. */
	bool IsVerticalScrollBarShown() const;
	/** True while the horizontal scrollbar is shown: a line is wider than the box. */
	bool IsHorizontalScrollBarShown() const;

	// SWidget
	/** Opens, filters or closes the autocomplete list once the text and cursor have settled. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

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

	// Autocomplete (FBangoCompletion)
	/** Opens the list for the word before the cursor (only if bMayOpen), filters it, or closes it. */
	void UpdateCompletion(bool bMayOpen);
	/** Everything the line can use: its graph's names, the file's globals and functions, the commands and built-ins. */
	void GatherCompletionItems(int32 Line);
	void CloseCompletion();
	void MoveCompletionSelection(int32 Delta);
	/** Replaces the word with the selected entry's name (and '(' for a call). */
	void AcceptCompletion();
	TSharedPtr<FBangoCompletionItem> GetSelectedCompletion() const;
	/** Where the list goes: under the word being typed (FBangoSyntaxMarshaller::SetProbe), or above it near the bottom. */
	/** Where the word being completed is, in this widget's space (false if the list isn't open or it isn't painted yet). */
	bool GetCompletionWordRect(FVector2D& OutTopLeft, FVector2D& OutBottomRight) const;
	FVector2D GetCompletionWordPosition() const;
	FVector2D GetCompletionWordSize() const;
	TSharedRef<SWidget> MakeCompletionPopup();
	TSharedRef<ITableRow> MakeCompletionRow(TSharedPtr<FBangoCompletionItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
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

	/** Autocomplete: the list's entries, the ones shown for the word typed so far, and where that word is. */
	TArray<FBangoCompletionItem> CompletionItems;
	TArray<TSharedPtr<FBangoCompletionItem>> CompletionItemPtrs;
	TArray<TSharedPtr<FBangoCompletionItem>> CompletionShown;
	TSharedPtr<SListView<TSharedPtr<FBangoCompletionItem>>> CompletionList;
	TSharedPtr<SWidget> CompletionPopup;
	/** Floats the list over the word being typed (see Construct). */
	TSharedPtr<class SMenuAnchor> CompletionAnchor;
	/** Set while this widget opens or closes CompletionAnchor itself, so its OnMenuOpenChanged only reacts to others. */
	bool bChangingCompletionAnchor = false;
	/** The text changed since it was last given to BangoScriptReferences (if this is the script it draws). */
	bool bReferencesDirty = true;

	/** Compile as you type (CompileAsYouType). */
	void CompileForErrors();
	/** Sets the error shown (CompileErrorText, the underline) from Error. */
	void SetShownError(const FBangoCompileError& Error);
	/** Hides the error shown, if any. */
	void ClearShownError();
	/** The error shown came from ShowError: compiling without errors leaves it, as it isn't a compile error. */
	bool bErrorFromOutside = false;
	TSharedRef<SWidget> MakeErrorPopup();
	bool bCompileAsYouType = false;
	/** The text changed since it was last compiled; it's compiled once typing has stopped for the preference's delay. */
	bool bCompileDirty = false;
	double LastEditSeconds = 0.0;
	/** The first compile error, if the last compile had one (hidden again as soon as the text changes). */
	bool bHasCompileError = false;
	FTextLocation CompileErrorLocation;
	FText CompileErrorText;
	/** Hangs the error's message under this widget (see Construct). */
	TSharedPtr<class SMenuAnchor> ErrorAnchor;
	bool bCompletionOpen = false;
	int32 CompletionLine = INDEX_NONE;
	int32 CompletionStart = INDEX_NONE;
	/** The text or cursor changed: update the list next Tick. */
	bool bCompletionDirty = false;
	/** A name's character was typed since the last update, so the list may open. */
	bool bTypedNameChar = false;
	/** Tab put in an entry: the tab character that follows it isn't typed. */
	bool bSwallowTab = false;
	static constexpr float CompletionWidth = 460.0f;
	static constexpr float CompletionRowHeight = 18.0f;
	static constexpr int32 CompletionVisibleRows = 8;

	/** Whether the gutter is shown (the divider toggles it). */
	bool bMetadataShown = true;

	/** See the InlineScriptEditor argument. */
	bool bInlineScriptEditor = false;

	/** The divider between the text and the gutter. */
	TSharedPtr<SButton> DividerButton;

	/** The divider's line: a quiet grey, lit with the highlight colour while hovered. */
	static constexpr float DividerHitWidth = 7.0f;
	inline static const FLinearColor DividerColor = FLinearColor(0.07f, 0.07f, 0.07f);
};
