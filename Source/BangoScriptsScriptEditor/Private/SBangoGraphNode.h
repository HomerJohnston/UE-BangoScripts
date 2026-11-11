#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphNode.h"

class SEditableTextBox;
class UBangoEdGraphNode;
struct FBangoNodeField;

/**
 * A Bango node: the title, then a body in three parts: the input pin on the left, the fields in the middle and the
 * output pins on the right. The pins are circles centred on the node's edges, with their names outside the node
 * (SBangoGraphPin). The fields are in columns, like the details panel: type icons, names, then values.
 *
 * Clicking a value edits it in place. Enter or clicking elsewhere applies the change (if it's valid; otherwise
 * a notification says why and the value stays as it was); Escape cancels.
 *
 * A localized text field (type text, written as LOCTEXT(...)) shows its source text, plus an arrow that opens
 * Key, Namespace and Notes rows under it, like an FText property's advanced options.
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

private:
	UBangoEdGraphNode* BangoNode = nullptr;

	/** The field being edited, or INDEX_NONE. */
	int32 EditingField = INDEX_NONE;
	/** One edit box per field (null for fields that can't be edited). */
	TArray<TSharedPtr<SEditableTextBox>> EditBoxes;

	/** Which part of a text literal a Key / Namespace / Notes row edits. */
	enum class ETextPart : uint8 { Key, Namespace, Notes };

	/** The fields, for the middle of the body (an empty widget for a node with no fields). */
	TSharedRef<SWidget> MakeFieldsArea();

	/** Adds the Key, Namespace and Notes rows of an expanded text field, from row Row on; returns the next free row. */
	int32 AddTextPartRows(const TSharedRef<class SGridPanel>& Grid, int32 Row, int32 LabelColumn, int32 ValueColumn, int32 FieldIndex);
	void OnTextPartCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex, ETextPart Part);
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
	void OnValueCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex);
	/** Rebuilds the node's widgets on the next frame (never from inside one of its own callbacks). */
	void RefreshSoon();
};

/** Creates SBangoGraphNode widgets for Bango nodes. */
struct FBangoGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};
