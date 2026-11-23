#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Shows a constant (FBangoConstant) on one row, as the script editor's SCRIPT VARS list shows a script variable: its
 * name, the type picker (SBangoTypePicker) and the value box (SBangoValueBox), each edited on its own. Picking another
 * type clears the value as SCRIPT VARS does, and leaves the name alone. An `entity`'s value is an actor picker instead
 * of the box, and an `entity[]`'s a list of them (its actors on rows under the constant's), so actors without a Bango
 * ID can be used.
 */
class FBangoConstantCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization

private:
	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> NameHandle;
	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> ValueHandle;
	TSharedPtr<IPropertyHandle> ActorHandle;
	TSharedPtr<IPropertyHandle> ActorsHandle;

	FString GetString(const TSharedPtr<IPropertyHandle>& Handle) const;
	void SetType(const FString& NewType);
	/** Which value widget shows: 0 the value box, 1 the actor picker, 2 the actor list's summary. */
	int32 GetValueWidgetIndex() const;
};
