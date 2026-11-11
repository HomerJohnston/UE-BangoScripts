#include "BangoConnectionDrawingPolicy.h"

#include "BangoEdGraphSchema.h"
#include "BangoEditorStyle.h"
#include "GraphEditorSettings.h"

/** Wires are this fraction of the editor's exec wire thickness. */
static constexpr float WireThicknessScale = 0.5f;

FBangoConnectionDrawingPolicy::FBangoConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
{
	// No arrowhead at the input end (the default policy draws one, which ends up inside the node).
	ArrowImage = nullptr;
	ArrowRadius = FVector2D::ZeroVector;
}

void FBangoConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
{
	FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
	// A little thinner than Blueprint's exec wires, and grey rather than white, like the pins' names.
	Params.WireThickness = Settings->DefaultExecutionWireThickness * WireThicknessScale;
	Params.WireColor = FBangoEditorStyle::GetWireColor();
}

FConnectionDrawingPolicy* FBangoConnectionDrawingPolicyFactory::CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	if (Schema && Schema->IsA(UBangoEdGraphSchema::StaticClass()))
	{
		return new FBangoConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements);
	}
	return nullptr;
}
