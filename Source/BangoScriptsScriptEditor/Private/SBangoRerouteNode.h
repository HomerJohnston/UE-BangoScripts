#pragma once

#include "CoreMinimal.h"
#include "SGraphNodeKnot.h"

class UBangoEdGraphNode;

/**
 * A Bango reroute node, drawn like Blueprint's: one dot that is both the input and the output, with a
 * larger area around it to grab and move it. Drag from the dot to the left to wire its input, or to the
 * right to wire its output; Ctrl-drag moves it. The dot is grey, like the wires.
 *
 * Unlike Blueprint's, the grab area is two grid squares each way with the dot in its middle, so the dot sits
 * on a grid line, level with the pins of other nodes (SBangoGraphNode keeps those on grid lines too).
 */
class SBangoRerouteNode : public SGraphNodeKnot
{
public:
	SLATE_BEGIN_ARGS(SBangoRerouteNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UBangoEdGraphNode* InNode);

	//~ Begin SGraphNode
	virtual void UpdateGraphNode() override;
	virtual TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* Pin) const override;
	//~ End SGraphNode

	/** Width and height of the grab area: two grid squares, so its middle is on a grid line. */
	static float GetSize();
};

/** A reroute's dot. The input sits under the output and is drawn invisible, as in Blueprint. */
class SBangoReroutePin : public SGraphPinKnot
{
public:
	SLATE_BEGIN_ARGS(SBangoReroutePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	//~ Begin SGraphPin
	virtual FSlateColor GetPinColor() const override;
	/** The same circle as other Bango pins, at the same size (SBangoGraphPin::PinSize). */
	virtual const FSlateBrush* GetPinIcon() const override;
	//~ End SGraphPin
};
