#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * How an FBangoIDName property shows in the details panel: one row, its name on the left, and on the right an '@'
 * followed by the name's usual text box, as scripts write it (`@john`).
 */
class FBangoIDNameCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils) override;
	/** Nothing: the header row has the name's box, so the struct isn't expanded. */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils) override {}
	//~ End IPropertyTypeCustomization
};
