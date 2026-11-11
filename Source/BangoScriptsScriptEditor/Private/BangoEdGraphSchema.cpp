#include "BangoEdGraphSchema.h"

#include "BangoEdGraphNode.h"
#include "BangoGraph.h"
#include "BangoGraphMenu.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BangoEdGraphSchema)

#define LOCTEXT_NAMESPACE "BangoEdGraphSchema"

const FName UBangoEdGraphSchema::PC_BangoExec(TEXT("BangoExec"));

/** True if following output wires from From reaches Target. */
static bool LeadsTo(const UEdGraphNode* From, const UEdGraphNode* Target)
{
	TArray<const UEdGraphNode*> Stack;
	TSet<const UEdGraphNode*> Seen;
	Stack.Add(From);
	while (Stack.Num() > 0)
	{
		const UEdGraphNode* Node = Stack.Pop();
		if (!Node || Seen.Contains(Node))
		{
			continue;
		}
		if (Node == Target)
		{
			return true;
		}
		Seen.Add(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked)
					{
						Stack.Add(Linked->GetOwningNode());
					}
				}
			}
		}
	}
	return false;
}

const FPinConnectionResponse UBangoEdGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	if (!PinA || !PinB || PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SameNode", "Both pins are on the same node."));
	}

	const UEdGraphPin* InputPin = nullptr;
	const UEdGraphPin* OutputPin = nullptr;
	if (!CategorizePinsByDirection(PinA, PinB, InputPin, OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Directions", "Connect an output to an input."));
	}

	// Wires can't go round in a circle: that's what loops and goto are for.
	if (LeadsTo(InputPin->GetOwningNode(), OutputPin->GetOwningNode()))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Loop", "Wires can't form a loop. Use a While, For or For Each node, or a Go To, to repeat."));
	}

	// Each pin has at most one wire: a new wire replaces the old ones on both ends.
	const bool bBreakInput = InputPin->LinkedTo.Num() > 0;
	const bool bBreakOutput = OutputPin->LinkedTo.Num() > 0;
	if (bBreakInput && bBreakOutput)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_AB, LOCTEXT("ReplaceBoth", "Replace the existing wires"));
	}
	if (bBreakInput || bBreakOutput)
	{
		const bool bBreakA = (bBreakInput && InputPin == PinA) || (bBreakOutput && OutputPin == PinA);
		return FPinConnectionResponse(bBreakA ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("Replace", "Replace the existing wire"));
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

void UBangoEdGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	// Without a transaction, Modify() on the nodes records nothing, and Ctrl+Z undoes the previous action instead.
	const FScopedTransaction Transaction(LOCTEXT("BreakPinLinks", "Break Pin Links"));
	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
}

void UBangoEdGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction(LOCTEXT("BreakSinglePinLink", "Break Pin Link"));
	Super::BreakSinglePinLink(SourcePin, TargetPin);
}

void UBangoEdGraphSchema::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const
{
	UEdGraphPin* OutputPin = nullptr;
	UEdGraphPin* InputPin = nullptr;
	if (!PinA || !PinB || !CategorizePinsByDirection(PinA, PinB, InputPin, OutputPin))
	{
		return;
	}
	const UBangoEdGraphNode* From = Cast<UBangoEdGraphNode>(OutputPin->GetOwningNode());
	UEdGraph* Graph = From ? From->GetGraph() : nullptr;
	if (!Graph)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddReroute", "Add Reroute Node"));
	Graph->Modify();
	OutputPin->GetOwningNode()->Modify();
	InputPin->GetOwningNode()->Modify();

	// NodeID 0: the editor gives it a free ID the next time it reads the graph (BangoScriptEditor::BuildDocument).
	UBangoEdGraphNode* Reroute = NewObject<UBangoEdGraphNode>(Graph, NAME_None, RF_Transactional);
	Reroute->KindValue = (uint8)EBangoGraphNodeKind::Reroute;
	Reroute->NodeID = 0;
	Reroute->bInFunctionGraph = From->bInFunctionGraph;
	Reroute->LayoutContext = From->LayoutContext;
	Reroute->CreateNewGuid();
	// Centred on the click (a reroute is two grid squares each way), on the grid (positions are saved in whole grid units).
	const FVector2f HalfSize((float)BangoGraphGridSize, (float)BangoGraphGridSize);
	Reroute->NodePosX = FMath::RoundToInt((GraphPosition.X - HalfSize.X) / BangoGraphGridSize) * BangoGraphGridSize;
	Reroute->NodePosY = FMath::RoundToInt((GraphPosition.Y - HalfSize.Y) / BangoGraphGridSize) * BangoGraphGridSize;
	Reroute->UpdateLayout();
	Reroute->AllocateDefaultPins();
	Graph->AddNode(Reroute, /*bUserAction*/ true, /*bSelectNewNode*/ false);

	OutputPin->BreakLinkTo(InputPin);
	OutputPin->MakeLinkTo(Reroute->GetInputPin());
	Reroute->GetNextPin()->MakeLinkTo(InputPin);
	// AddNode already told the panel to make the reroute's widget, and wires are drawn from the pins: no need to
	// rebuild the whole graph (NotifyGraphChanged() with no action would, and the graph would flash).
}

/** The layout context and graph kind new nodes in Graph get: the same as its other nodes'. */
static const UBangoEdGraphNode* FindAnyBangoNode(const UEdGraph* Graph)
{
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
		{
			return BangoNode;
		}
	}
	return nullptr;
}

void UBangoEdGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	const UEdGraph* Graph = ContextMenuBuilder.CurrentGraph;
	const UBangoEdGraphNode* Sibling = Graph ? FindAnyBangoNode(Graph) : nullptr;
	if (!Sibling || !Sibling->LayoutContext.IsValid())
	{
		return;
	}

	// The names already used in this graph, so made-up names (like _newInt) don't clash. Names in the script's
	// other graphs that this one can see (globals, parameters) are in the layout context.
	FBangoGraphDocument Names;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
		{
			FBangoGraphNode& Copy = Names.Main.Nodes.AddDefaulted_GetRef();
			Copy.Kind = BangoNode->GetKind();
			Copy.Text = BangoNode->Text;
		}
	}

	TArray<FBangoNodeMenuEntry> Entries;
	Bango::GetNodeMenuEntries(Names, *Sibling->LayoutContext, Entries);
	for (const FBangoNodeMenuEntry& Entry : Entries)
	{
		ContextMenuBuilder.AddAction(MakeShared<FBangoSchemaAction_NewNode>(
			FText::FromString(Entry.Category), FText::FromString(Entry.Title), FText::FromString(Entry.Description),
			FText::FromString(Entry.Keywords), (uint8)Entry.Kind, Entry.Text));
	}
}

UEdGraphNode* FBangoSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode)
{
	const UBangoEdGraphNode* Sibling = ParentGraph ? FindAnyBangoNode(ParentGraph) : nullptr;
	if (!Sibling)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddNode", "Add Node"));
	ParentGraph->Modify();

	// NodeID 0: the editor gives it a free ID the next time it reads the graph (BangoScriptEditor::BuildDocument).
	// Its fields start empty: it shows errors until they're filled in, rather than running with made-up values.
	UBangoEdGraphNode* Node = NewObject<UBangoEdGraphNode>(ParentGraph, NAME_None, RF_Transactional);
	Node->KindValue = Kind;
	Node->Text = Text;
	Node->NodeID = 0;
	Node->bInFunctionGraph = Sibling->bInFunctionGraph;
	Node->LayoutContext = Sibling->LayoutContext;
	Node->CreateNewGuid();

	// Dragged off an input, the new node goes to its left, so it doesn't sit on the node it's wired to.
	FVector2f Position = Location;
	if (FromPin && FromPin->Direction == EGPD_Input)
	{
		Position.X -= 256.0f;
	}
	// On the grid (positions are saved in whole grid units).
	Node->NodePosX = FMath::RoundToInt(Position.X / BangoGraphGridSize) * BangoGraphGridSize;
	Node->NodePosY = FMath::RoundToInt(Position.Y / BangoGraphGridSize) * BangoGraphGridSize;
	Node->UpdateLayout();
	Node->AllocateDefaultPins();
	ParentGraph->AddNode(Node, /*bUserAction*/ true, bSelectNewNode);

	// Wire it to the pin it was dragged off. A pin has at most one wire, so if it already had one, the new node
	// goes in the middle: the wire's other end connects to the new node's other side.
	if (FromPin && FromPin->GetOwningNode())
	{
		FromPin->GetOwningNode()->Modify();
		UEdGraphPin* Other = FromPin->LinkedTo.Num() > 0 ? FromPin->LinkedTo[0] : nullptr;
		if (Other && Other->GetOwningNode())
		{
			Other->GetOwningNode()->Modify();
			FromPin->BreakLinkTo(Other);
		}
		UEdGraphPin* NewInput = Node->GetInputPin();
		UEdGraphPin* NewNext = Node->GetNextPin();
		if (FromPin->Direction == EGPD_Output)
		{
			FromPin->MakeLinkTo(NewInput);
			if (Other)
			{
				NewNext->MakeLinkTo(Other);
			}
		}
		else
		{
			NewNext->MakeLinkTo(FromPin);
			if (Other)
			{
				Other->MakeLinkTo(NewInput);
			}
		}
	}
	// AddNode already told the panel to make the node's widget (on the next frame, by when it's wired), and wires
	// are drawn from the pins: no need to rebuild the whole graph, which would make it flash.
	return Node;
}

FLinearColor UBangoEdGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor::White;
}

#undef LOCTEXT_NAMESPACE
