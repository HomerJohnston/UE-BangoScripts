#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Shows a define (FBangoDefineSetting, Project Settings > Plugins > Bango > Defines) on one row, as a constant is
 * shown: its name, the type picker (SBangoTypePicker) and the value box (SBangoValueBox). A row that can't be a
 * define has a warning beside it saying why (Bango::GetDefineRowError). Its category and description are on rows
 * under it.
 */
class FBangoDefineSettingCustomization : public IPropertyTypeCustomization
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

	FString GetString(const TSharedPtr<IPropertyHandle>& Handle) const;
	void SetType(const FString& NewType);
	/** Why this row isn't a define, or empty. */
	FString GetError() const;
};
