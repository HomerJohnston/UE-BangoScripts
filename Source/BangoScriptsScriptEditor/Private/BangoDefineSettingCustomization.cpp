#include "BangoDefineSettingCustomization.h"

#include "BangoDefines.h"
#include "BangoSettings.h"
#include "BangoValueWidgets.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "BangoDefineSettingCustomization"

TSharedRef<IPropertyTypeCustomization> FBangoDefineSettingCustomization::MakeInstance()
{
	return MakeShared<FBangoDefineSettingCustomization>();
}

void FBangoDefineSettingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;
	NameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoDefineSetting, Name));
	TypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoDefineSetting, Type));
	ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoDefineSetting, Value));
	if (!NameHandle || !TypeHandle || !ValueHandle)
	{
		return;
	}

	HeaderRow
	.NameContent()
	[
		SNew(SEditableTextBox)
		.Font(CustomizationUtils.GetRegularFont())
		.MinDesiredWidth(140.0f)
		.Text_Lambda([this]() { return FText::FromString(GetString(NameHandle)); })
		.ToolTipText(LOCTEXT("NameTooltip", "The name scripts use, in SHOUTING_SNAKE_CASE: capital letters, digits and '_', starting with a letter (SELECT_RANDOM, MAX_PARTY_2)."))
		.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
		{
			const FString NewName = NewText.ToString().TrimStartAndEnd();
			if (NewName != GetString(NameHandle))
			{
				NameHandle->SetValue(NewName);
			}
		})
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(600.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SBangoTypePicker)
			.ShowTypeName(true)
			.Type_Lambda([this]() { return GetString(TypeHandle); })
			.OnTypePicked_Lambda([this](const FString& NewType) { SetType(NewType); })
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBangoValueBox)
			.Type_Lambda([this]() { return GetString(TypeHandle); })
			.Value_Lambda([this]() { return GetString(ValueHandle); })
			.HintText(LOCTEXT("NoValue", "nil"))
			.OnValueCommitted_Lambda([this](const FString& BangoText)
			{
				if (BangoText != GetString(ValueHandle))
				{
					ValueHandle->SetValue(BangoText);
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			// Why this row isn't a define (it's left out until it's fixed).
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
			.Visibility_Lambda([this]() { return GetError().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.ToolTipText_Lambda([this]() { return FText::FromString(GetError() + TEXT(" Until it's fixed, scripts can't use it.")); })
		]
	];
}

void FBangoDefineSettingCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Its category and description, under its row.
	for (const FName Child : { GET_MEMBER_NAME_CHECKED(FBangoDefineSetting, Category), GET_MEMBER_NAME_CHECKED(FBangoDefineSetting, Description) })
	{
		if (const TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Child))
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

FString FBangoDefineSettingCustomization::GetString(const TSharedPtr<IPropertyHandle>& Handle) const
{
	FString Value;
	if (Handle && Handle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return FString();
}

FString FBangoDefineSettingCustomization::GetError() const
{
	// The rows are given to the table in order (Bango::ApplyDefineSettings), so a row's index is its row there.
	const int32 Row = StructHandle ? StructHandle->GetIndexInArray() : INDEX_NONE;
	return Row == INDEX_NONE ? FString() : Bango::GetDefineRowError(Row);
}

void FBangoDefineSettingCustomization::SetType(const FString& NewType)
{
	if (NewType == GetString(TypeHandle))
	{
		return;
	}
	// One undoable change: the type, and the value cleared as SCRIPT VARS clears it (nil stays).
	const FScopedTransaction Transaction(LOCTEXT("ChangeDefineType", "Change Define Type"));
	const FString OldValue = GetString(ValueHandle);
	TypeHandle->SetValue(NewType);
	ValueHandle->SetValue(SBangoValueBox::GetValueAfterTypeChange(OldValue));
}

#undef LOCTEXT_NAMESPACE
