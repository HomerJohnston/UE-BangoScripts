#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

/**
 * A Bango exec pin: a circle (filled when connected, a ring when not) that sits centred on the edge of its node,
 * with its name painted outside the node, a little above the wire: to the left of an input (right-aligned) and to
 * the right of an output (left-aligned). The name takes no space in the node and doesn't catch the mouse.
 */
class SBangoGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SBangoGraphPin) {}
	SLATE_END_ARGS()

	/** The circle's width and height. SBangoGraphNode uses it to put the circle's centre on the node's edge. */
	static constexpr float PinSize = 10.0f;

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

	//~ Begin SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	//~ End SWidget

private:
	const FSlateBrush* GetCircleBrush() const;
	FSlateColor GetCircleColor() const;
};
