#include "BangoIDNameCustomization.h"

#include "BangoIDName.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BangoIDNameCustomization"

TSharedRef<IPropertyTypeCustomization> FBangoIDNameCustomization::MakeInstance()
{
	return MakeShared<FBangoIDNameCustomization>();
}

void FBangoIDNameCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils)
{
	const TSharedPtr<IPropertyHandle> NameHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoIDName, Name));
	if (!NameHandle.IsValid())
	{
		HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()];
		return;
	}

	HeaderRow
	.NameContent()
	[
		StructHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(150.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("At", "@"))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.ToolTipText(LOCTEXT("AtTooltip", "Scripts use this actor as @ and the name, like @john."))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			NameHandle->CreatePropertyValueWidget()
		]
	];
}

#undef LOCTEXT_NAMESPACE
