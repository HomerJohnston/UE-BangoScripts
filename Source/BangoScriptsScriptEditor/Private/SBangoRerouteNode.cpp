#include "SBangoRerouteNode.h"

#include "BangoEdGraphNode.h"
#include "BangoEditorStyle.h"
#include "BangoGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GraphEditorSettings.h"
#include "SCommentBubble.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

void SBangoRerouteNode::Construct(const FArguments& InArgs, UBangoEdGraphNode* InNode)
{
	SGraphNodeKnot::Construct(SGraphNodeKnot::FArguments(), InNode);
}

float SBangoRerouteNode::GetSize()
{
	return 2.0f * BangoGraphGridSize;
}

void SBangoRerouteNode::UpdateGraphNode()
{
	// SGraphNodeKnot::UpdateGraphNode, with a square grab area and the dot in its middle (not at its top).
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();
	SetupErrorReporting();

	GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				// The area to grab to move the node
				SNew(SBox)
				.WidthOverride(GetSize())
				.HeightOverride(GetSize())
				.Visibility(EVisibility::Visible)
				.Cursor(EMouseCursor::CardinalCross)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				// The input and output pins, one on top of the other
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(LeftNodeBox, SVerticalBox)
				]
				+ SOverlay::Slot()
				[
					SAssignNew(RightNodeBox, SVerticalBox)
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			[
				ErrorReporting->AsWidget()
			]
		];

	SAssignNew(CommentBubble, SCommentBubble)
		.GraphNode(GraphNode)
		.Text(this, &SBangoRerouteNode::GetNodeComment)
		.OnTextCommitted(this, &SBangoRerouteNode::OnCommentTextCommitted)
		.EnableTitleBarBubble(true)
		.EnableBubbleCtrls(true)
		.AllowPinning(true)
		.ColorAndOpacity(GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor)
		.GraphLOD(this, &SBangoRerouteNode::GetCurrentLOD)
		.IsGraphNodeHovered(this, &SBangoRerouteNode::IsHovered)
		.OnToggled(this, &SBangoRerouteNode::OnCommentBubbleToggled);

	GetOrAddSlot(ENodeZone::TopCenter)
		.SlotOffset2f(TAttribute<FVector2f>(this, &SBangoRerouteNode::GetCommentOffset))
		.SlotSize2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetSize2f))
		.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
		.VAlign(VAlign_Top)
		[
			CommentBubble.ToSharedRef()
		];

	CreatePinWidgets();
}

TSharedPtr<SGraphPin> SBangoRerouteNode::CreatePinWidget(UEdGraphPin* Pin) const
{
	return SNew(SBangoReroutePin, Pin);
}

void SBangoReroutePin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPinKnot::Construct(SGraphPinKnot::FArguments(), InPin);
}

const FSlateBrush* SBangoReroutePin::GetPinIcon() const
{
	return FBangoEditorStyle::GetPinBrush(IsConnected());
}

FSlateColor SBangoReroutePin::GetPinColor() const
{
	const UEdGraphPin* Pin = GetPinObj();
	if (!Pin || Pin->Direction == EGPD_Input)
	{
		return FLinearColor::Transparent; // under the output dot; drawing both would double the colour
	}
	if (IsHovered())
	{
		return FLinearColor::White;
	}
	// The dot is both the input and the output: dark only when neither side has a wire.
	const UEdGraphNode* Node = Pin->GetOwningNodeUnchecked();
	const bool bAnyWire = Node && Node->Pins.ContainsByPredicate([](const UEdGraphPin* Each) { return Each && Each->LinkedTo.Num() > 0; });
	return bAnyWire ? FBangoEditorStyle::GetWireColor() : FBangoEditorStyle::GetDisconnectedPinColor();
}
