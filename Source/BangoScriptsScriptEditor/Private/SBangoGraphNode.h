#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphNode.h"
#include "BangoCompletion.h"

class SEditableTextBox;
class SMenuAnchor;
class SVerticalBox;
class UBangoEdGraphNode;
struct FBangoNodeField;
struct FBangoTextLiteral;

/**
 * A Bango node: the title, then a body in three parts: the input pin on the left, the fields in the middle and the
 * output pins on the right. The pins are circles centred on the node's edges, with their names outside the node
 * (SBangoGraphPin). The fields are in columns, like the details panel: type icons, names, then values.
 *
 * Clicking a value edits it in place. Enter or clicking elsewhere applies the change (if it's valid; otherwise
 * a notification says why and the value stays as it was); Escape cancels.
 *
 * A localized text field (type text, written as LOCTEXT(...) or INVTEXT(...)) shows its source text, plus an arrow
 * that opens a Translated checkbox and, for a translated text, Key, Namespace and Notes rows under it, like an FText
 * property's advanced options.
 *
 * Typing a name in a field opens an autocomplete list under it, like the script text pane's: the variables the node
 * can see, this script's functions, the commands and the built-ins. Up/Down pick, Enter or Tab accept, Escape closes it,
 * Ctrl+Space opens it.
 *
 * A For node has a (Simple) / (Complex) toggle right after its title: lit, as (Complex), while the whole header is shown as one row.
 * A simple counting loop can switch between that and Var / First / Last; any other header always shows it.
 */
class SBangoGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SBangoGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UBangoEdGraphNode* InNode);

protected:
	//~ Begin SGraphNode
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	/** Entry nodes (Script Start, a function's start) stay at (0,0): they're never moved, even with other nodes. */
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SGraphNode

	/** Draws the debugger's outline (see EBangoNodeDebugState) behind the node. */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UBangoEdGraphNode* BangoNode = nullptr;

	/** The field being edited, or INDEX_NONE. */
	int32 EditingField = INDEX_NONE;
	/** One edit box per field (null for fields that can't be edited). */
	TArray<TSharedPtr<SEditableTextBox>> EditBoxes;
	/**
	 * Enter or Tab in a field: the field it was pressed in, and which way to move on (1 next, -1 previous; Tab wraps
	 * around, Enter stops after the last). The node rebuilds itself after the edit, so the next field is found and
	 * opened then. INDEX_NONE when there's nowhere to move on to.
	 */
	int32 PendingEditFrom = INDEX_NONE;
	int32 PendingEditStep = 1;
	bool bPendingEditWrap = false;

	// ---- Autocomplete in the field being edited (FBangoCompletion) ----

	/** One per edit box: where the list opens, under the box. */
	TArray<TSharedPtr<SMenuAnchor>> CompletionAnchors;
	/** Everything the field can use, sorted by name; gathered when the list opens. */
	TArray<FBangoCompletionItem> CompletionItems;
	/** The items that start with the word being typed (indices into CompletionItems). */
	TArray<int32> CompletionShown;
	/** Into CompletionShown, and the first one shown (the list shows CompletionVisibleRows at a time). */
	int32 CompletionSelected = 0;
	int32 CompletionTop = 0;
	/** Where the word being typed starts in the box's text, or INDEX_NONE while the list is closed. */
	int32 CompletionWordStart = INDEX_NONE;
	/** The edit box's cursor column, from its cursor-moved event. */
	int32 CompletionCursor = 0;
	/** The rows, rebuilt when the shown items or the selection change. */
	TSharedPtr<SVerticalBox> CompletionRows;
	/** An update is queued for the next frame (the cursor may move after the text changes). */
	bool bCompletionQueued = false;
	bool bCompletionMayOpen = false;
	/** Set while accepting, so the text change it makes doesn't reopen the list. */
	bool bCompletionAccepting = false;
	static constexpr int32 CompletionVisibleRows = 8;

	bool IsCompletionOpen() const { return CompletionWordStart != INDEX_NONE; }
	/** True for a field whose box holds code: not a plain string or text being typed (one that starts with = is code). */
	bool FieldTakesCompletion(int32 FieldIndex, const FString& Typed) const;
	void QueueCompletionUpdate(int32 FieldIndex, bool bMayOpen);
	void UpdateCompletion(int32 FieldIndex, bool bMayOpen);
	void GatherCompletionItems();
	void CloseCompletion();
	void MoveCompletionSelection(int32 Delta);
	void AcceptCompletion(int32 FieldIndex);
	void RebuildCompletionRows();
	TSharedRef<SWidget> MakeCompletionPopup(int32 FieldIndex);
	FReply OnEditBoxKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent, int32 FieldIndex);

	/** Which part of a text literal a Key / Namespace / Notes row edits. */
	enum class ETextPart : uint8 { Key, Namespace, Notes };

	/** The fields, for the middle of the body (an empty widget for a node with no fields). */
	TSharedRef<SWidget> MakeFieldsArea();

	/**
	 * Adds the rows of an expanded text field, from row Row on: Translated (LOCTEXT or INVTEXT), then, for a translated
	 * one, Key, Namespace and Notes. Returns the next free row.
	 */
	int32 AddTextPartRows(const TSharedRef<class SGridPanel>& Grid, int32 Row, int32 LabelColumn, int32 ValueColumn, int32 FieldIndex);
	void OnTextPartCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex, ETextPart Part);
	/** The Translated checkbox: makes the text a LOCTEXT (with a new key) or an INVTEXT. */
	void OnTextTranslatedChanged(ECheckBoxState NewState, int32 FieldIndex);
	/** Sets a text field to Literal, or shows why it can't be. */
	void SetTextLiteral(int32 FieldIndex, const FBangoTextLiteral& Literal, FString Error);
	FReply OnToggleTextExpanded(int32 FieldIndex);
	/** A For node's Simple / Complex toggle, shown after its title. */
	TSharedRef<SWidget> MakeExpressionToggle();
	void OnShowExpressionChanged(ECheckBoxState NewState);

	/** The name, for the column after the icons. */
	TSharedRef<SWidget> MakeFieldLabel(const FBangoNodeField& Field) const;
	/** The row's type icon (with the type as its tooltip), or an empty space of the same size. */
	TSharedRef<SWidget> MakeFieldIcon(const FBangoNodeField& Field) const;
	/** The value box (nothing for rows without a value), for the last column. */
	TSharedRef<SWidget> MakeFieldValue(int32 FieldIndex);

	FReply OnValueClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent, int32 FieldIndex);
	/** Opens FieldIndex's edit box and gives it keyboard focus (on the next frame, once the box is showing). */
	void BeginEditingField(int32 FieldIndex);
	/** The next field (by Step) that has an edit box, after From; wrapping around if bWrap. INDEX_NONE if none. */
	int32 FindEditableField(int32 From, int32 Step, bool bWrap) const;
	/** Enter or Tab: applies what's typed, then (once the node has rebuilt) opens the next field. */
	FReply CommitAndMoveOn(int32 FieldIndex, int32 Step, bool bWrap);
	/** After a rebuild: opens the field Enter or Tab moved on to, if any. */
	void ResumePendingEdit();
	void OnValueCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex);
	/** Rebuilds the node's widgets on the next frame (never from inside one of its own callbacks). */
	void RefreshSoon();
};

/** Creates SBangoGraphNode widgets for Bango nodes. */
struct FBangoGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};
