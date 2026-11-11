#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "BangoEdGraphSchema.generated.h"

/**
 * An add-node menu entry (right-click on the graph, or drag a wire off a pin and let go): makes one node.
 * Dragged off a pin, the new node is wired to it; if that pin already had a wire, the node goes in the middle.
 */
USTRUCT()
struct FBangoSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	static FName StaticGetTypeId() { static FName Type(TEXT("FBangoSchemaAction_NewNode")); return Type; }
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }

	FBangoSchemaAction_NewNode() = default;
	FBangoSchemaAction_NewNode(FText InCategory, FText InMenuDesc, FText InToolTip, FText InKeywords, uint8 InKind, const FString& InText)
		: FEdGraphSchemaAction(MoveTemp(InCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), 0, MoveTemp(InKeywords))
		, Kind(InKind)
		, Text(InText)
	{
	}

	/** EBangoGraphNodeKind of the new node, and its text (see FBangoNodeMenuEntry). */
	uint8 Kind = 0;
	FString Text;

	//~ Begin FEdGraphSchemaAction
	using FEdGraphSchemaAction::PerformAction; // the many-pins form, which calls this one with the first pin
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction
};

/** Graph rules for Bango script graphs: exec wires only, one wire per pin. */
UCLASS()
class UBangoEdGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	/** The only pin category: Bango graphs have no data pins (expressions are text inside nodes). */
	static const FName PC_BangoExec;

	//~ Begin UEdGraphSchema
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual bool ShouldHidePinDefaultValue(UEdGraphPin* Pin) const override { return true; }

	/**
	 * The graph editor calls these without opening an undo transaction (Alt-click on a pin, "Break This Link",
	 * "Break Link To...", Alt-click on a wire): each schema opens its own, as Blueprint's does.
	 */
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	/** The add-node menu: every node Bango::GetNodeMenuEntries offers, by category. */
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	/** Double-clicking a wire puts a reroute node on it there, like in Blueprint. */
	virtual void OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const override;
	//~ End UEdGraphSchema
};
