// A column that scrolls along with a text: line numbers, node metadata.
// See "Bango Text Editor Spec", section 4.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBar;

/**
 * Shows its content moved up by as much as a scrollbar says its text is scrolled, clipped to its own height.
 * The content should be one line per line of the text, in the same font, so it's as tall as the text.
 *
 * The position is read when the content is laid out for painting, not on a timer or in Tick. A text widget
 * updates its scrollbar in its own Tick, which Slate calls while painting it, so put this after the text (to its
 * right, or later in the same panel): it's then painted after the text has scrolled, in the same frame, and never
 * lags a frame behind.
 */
class SBangoScrollFollower : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoScrollFollower)
	{}
		/** The text's vertical scrollbar. */
		SLATE_ARGUMENT(TSharedPtr<SScrollBar>, ScrollBar)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TSharedPtr<SScrollBar> ScrollBar;
};
