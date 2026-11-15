#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * How an FBangoInlineScript property shows in the details panel (spec section 15): a box to type the script in, with
 * the script text pane's colours and autocomplete, up to about ten lines tall (it scrolls past that), and an Edit
 * button that opens the full graph editor in a window of its own. While that window is open, the box can't be typed
 * in, and Edit becomes Show Window.
 */
class FBangoInlineScriptCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils) override;
	/** Nothing: the header row has the box, so the struct's Source isn't shown again. */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils) override {}
	//~ End IPropertyTypeCustomization
};
