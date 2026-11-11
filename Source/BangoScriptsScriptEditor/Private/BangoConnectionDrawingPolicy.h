#pragma once

#include "CoreMinimal.h"
#include "ConnectionDrawingPolicy.h"
#include "EdGraphUtilities.h"

/** Draws Bango wires like Blueprint exec wires: no arrowheads, exec thickness and colour. */
class FBangoConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FBangoConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements);

	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
};

/** Gives graphs that use the Bango schema the Bango wire drawer. */
struct FBangoConnectionDrawingPolicyFactory : public FGraphPanelPinConnectionFactory
{
	virtual FConnectionDrawingPolicy* CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;
};
