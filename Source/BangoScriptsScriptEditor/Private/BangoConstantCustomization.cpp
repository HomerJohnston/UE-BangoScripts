#include "BangoConstantCustomization.h"

#include "BangoConstant.h"
#include "BangoValueWidgets.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "BangoConstantCustomization"

TSharedRef<IPropertyTypeCustomization> FBangoConstantCustomization::MakeInstance()
{
	return MakeShared<FBangoConstantCustomization>();
}

void FBangoConstantCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;
	NameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoConstant, Name));
	TypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoConstant, Type));
	ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoConstant, Value));
	ActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoConstant, Actor));
	ActorsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoConstant, Actors));
	if (!NameHandle || !TypeHandle || !ValueHandle || !ActorHandle || !ActorsHandle)
	{
		return;
	}

	// The name on the left, where an array element's index would be; type and value on the right. The details panel
	// adds the array's own buttons (insert, delete, duplicate) after them.
	HeaderRow
	.NameContent()
	[
		SNew(SEditableTextBox)
		.Font(CustomizationUtils.GetRegularFont())
		.MinDesiredWidth(100.0f)
		.Text_Lambda([this]()
		{
			FString Name;
			return NameHandle->GetValue(Name) == FPropertyAccess::MultipleValues
				? LOCTEXT("MultipleNames", "Multiple Values")
				: FText::FromString(Name);
		})
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
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return GetValueWidgetIndex(); })
			+ SWidgetSwitcher::Slot()
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
			+ SWidgetSwitcher::Slot()
			[
				// The engine's actor picker (and eyedropper), for actors in the same level.
				ActorHandle->CreatePropertyValueWidget()
			]
			+ SWidgetSwitcher::Slot()
			[
				// The engine's array summary: how many, and its add and empty buttons. The actors are on the rows below.
				ActorsHandle->CreatePropertyValueWidget()
			]
		]
	];
}

void FBangoConstantCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (!ActorsHandle)
	{
		return;
	}
	// An entity[]'s actors, one picker a row, under the constant's row (which has the array's own summary and
	// buttons, so no header of their own). Another type has none: picking it empties the list (SetType).
	TSharedRef<FDetailArrayBuilder> ActorRows = MakeShareable(new FDetailArrayBuilder(ActorsHandle.ToSharedRef(), /*InGenerateHeader*/ false, /*InDisplayResetToDefault*/ false, /*InDisplayElementNum*/ false));
	ActorRows->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateLambda(
		[](TSharedRef<IPropertyHandle> ElementHandle, int32 Index, IDetailChildrenBuilder& Children)
		{
			Children.AddProperty(ElementHandle);
		}));
	ChildBuilder.AddCustomBuilder(ActorRows);
}

FString FBangoConstantCustomization::GetString(const TSharedPtr<IPropertyHandle>& Handle) const
{
	FString Value;
	// With several objects selected whose values differ: empty.
	if (Handle && Handle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return FString();
}

int32 FBangoConstantCustomization::GetValueWidgetIndex() const
{
	FBangoConstant Probe;
	Probe.Type = GetString(TypeHandle);
	return Probe.IsActorType() ? 1 : Probe.IsActorArrayType() ? 2 : 0;
}

void FBangoConstantCustomization::SetType(const FString& NewType)
{
	if (NewType == GetString(TypeHandle))
	{
		return;
	}
	// One undoable change: the type, and the value cleared as SCRIPT VARS clears it (nil stays). The name isn't touched.
	const FScopedTransaction Transaction(LOCTEXT("ChangeConstantType", "Change Constant Type"));
	const FString OldValue = GetString(ValueHandle);
	TypeHandle->SetValue(NewType);
	ValueHandle->SetValue(SBangoValueBox::GetValueAfterTypeChange(OldValue));
	FBangoConstant Probe;
	Probe.Type = NewType;
	if (!Probe.IsActorType())
	{
		const UObject* NoActor = nullptr;
		ActorHandle->SetValue(NoActor);
	}
	if (!Probe.IsActorArrayType())
	{
		if (const TSharedPtr<IPropertyHandleArray> ActorArray = ActorsHandle->AsArray())
		{
			uint32 NumActors = 0;
			if (ActorArray->GetNumElements(NumActors) == FPropertyAccess::Success && NumActors > 0)
			{
				ActorArray->EmptyArray();
			}
		}
	}
	else if (StructHandle)
	{
		// Its actors are on rows under this one: show them.
		StructHandle->SetExpanded(true);
	}
}

#undef LOCTEXT_NAMESPACE
