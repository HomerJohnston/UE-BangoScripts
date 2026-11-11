#include "SBangoGraphPin.h"

#include "BangoEditorStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SToolTip.h"

namespace BangoGraphPinStyle
{
	/** Space between the circle and its name. */
	static constexpr float LabelGap = 3.0f;
	/** How far the name sits above the wire, as a fraction of its height (a little over half: just clear of the wire). */
	static constexpr float LabelRaise = 0.6f;
	static const FLinearColor CircleColor(0.85f, 0.85f, 0.85f);
	static const FLinearColor HoveredCircleColor(1.0f, 1.0f, 1.0f);
}

void SBangoGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	// Not SGraphPin::Construct: that builds a row with the pin's name and value beside the circle, inside a hover
	// border with padding, which would put the circle off the node's edge. This pin is the circle and nothing else.
	GraphPinObj = InPin;
	check(GraphPinObj);
	bShowLabel = false;
	SetCursor(EMouseCursor::Default);
	SetVisibility(MakeAttributeSP(this, &SBangoGraphPin::GetPinVisiblity));

	PinImage = SNew(SBox)
		.WidthOverride(PinSize)
		.HeightOverride(PinSize)
		[
			SNew(SImage)
			.Image(this, &SBangoGraphPin::GetCircleBrush)
			.ColorAndOpacity(this, &SBangoGraphPin::GetCircleColor)
		];
	PinImage->SetCursor(TAttribute<TOptional<EMouseCursor::Type>>::CreateSP(this, &SBangoGraphPin::GetPinCursor));

	SBorder::Construct(SBorder::FArguments()
		.BorderImage(FAppStyle::GetNoBrush())
		.Padding(0.0f)
		.OnMouseButtonDown(this, &SBangoGraphPin::OnPinNameMouseDown)
		[
			PinImage.ToSharedRef()
		]);

	SetToolTip(SNew(SToolTip).Text(this, &SBangoGraphPin::GetTooltipText));
}

const FSlateBrush* SBangoGraphPin::GetCircleBrush() const
{
	return FBangoEditorStyle::GetPinBrush(IsConnected());
}

FSlateColor SBangoGraphPin::GetCircleColor() const
{
	if (IsHovered())
	{
		return FSlateColor(BangoGraphPinStyle::HoveredCircleColor);
	}
	return FSlateColor(IsConnected() ? BangoGraphPinStyle::CircleColor : FBangoEditorStyle::GetDisconnectedPinColor());
}

int32 SBangoGraphPin::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace BangoGraphPinStyle;
	const int32 MaxLayer = SGraphPin::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const UEdGraphPin* Pin = GetPinObj();
	if (!Pin || !Pin->GetOwningNodeUnchecked() || UseLowDetailPinNames() || !FSlateApplication::IsInitialized())
	{
		return MaxLayer;
	}
	const FText Label = Pin->GetOwningNodeUnchecked()->GetPinDisplayName(Pin);
	if (Label.IsEmpty())
	{
		return MaxLayer;
	}

	// Outside the circle, in the circle's own (unscaled) units: Slate scales it with the graph's zoom.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const auto Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label, Font);
	const FVector2f TextSize((float)Measured.X, (float)Measured.Y);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float X = GetDirection() == EGPD_Input ? -LabelGap - TextSize.X : Size.X + LabelGap;
	const float Y = Size.Y * 0.5f - TextSize.Y * (0.5f + LabelRaise);

	FSlateDrawElement::MakeText(OutDrawElements, MaxLayer + 1,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(FVector2f(X, Y))),
		Label, Font, ESlateDrawEffect::None,
		InWidgetStyle.GetColorAndOpacityTint() * FLinearColor::White); // white, so it stands out from the grey wires it can overlap
	return MaxLayer + 1;
}
