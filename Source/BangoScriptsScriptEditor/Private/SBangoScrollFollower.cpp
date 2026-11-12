// A column that scrolls along with a text: line numbers, node metadata.
// See "Bango Text Editor Spec", section 4.

#include "SBangoScrollFollower.h"

#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SScrollBar.h"

void SBangoScrollFollower::Construct(const FArguments& InArgs)
{
	ScrollBar = InArgs._ScrollBar;
	SetClipping(EWidgetClipping::ClipToBounds);
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SBangoScrollFollower::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Content = ChildSlot.GetWidget();
	if (!ArrangedChildren.Accepts(Content->GetVisibility()))
	{
		return;
	}

	const FVector2D ContentSize = Content->GetDesiredSize();

	// The text puts its scroll position on the scrollbar as a fraction of its whole height. The content is as tall
	// as the text, so the same fraction of it, in pixels. Read now, while painting: the text has already ticked.
	const float Offset = ScrollBar.IsValid() ? ScrollBar->DistanceFromTop() * ContentSize.Y : 0.0f;

	ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(
		Content,
		FVector2D(0.0f, -Offset),
		FVector2D(FMath::Max(AllottedGeometry.GetLocalSize().X, ContentSize.X), ContentSize.Y)));
}

FVector2D SBangoScrollFollower::ComputeDesiredSize(float) const
{
	// As wide as the content, but no height of its own: it's as tall as whatever it's put beside.
	return FVector2D(ChildSlot.GetWidget()->GetDesiredSize().X, 0.0f);
}
