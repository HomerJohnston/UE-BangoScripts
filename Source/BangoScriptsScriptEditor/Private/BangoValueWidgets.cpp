#include "BangoValueWidgets.h"

#include "BangoCompiler.h"
#include "BangoEditorStyle.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoIDActorComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BangoValueWidgets"

namespace BangoValueWidgetsPrivate
{
	/** The value as a node's field, which is how it's typed and shown. */
	static FBangoNodeField MakeField(const FString& Type, const FString& Value)
	{
		FBangoNodeField Field;
		Field.bHasValue = true;
		Field.bOptional = true;
		Field.bPresent = !Value.IsEmpty();
		Field.TypeHint = Type;
		Field.Text = Value;
		return Field;
	}
}

// ---- SBangoTypePicker ----

const FSlateBrush* SBangoTypePicker::GetTypeIcon(const FString& TypeName, FLinearColor& OutTint)
{
	// The plain variable icon when the type isn't known.
	const FSlateBrush* Icon = TypeName.IsEmpty() ? nullptr : FBangoEditorStyle::GetTypeIcon(TypeName, OutTint);
	return Icon ? Icon : FBangoEditorStyle::GetVariableIcon(TypeName, OutTint);
}

TSharedRef<SWidget> SBangoTypePicker::MakeTypeMenu(const FString& Current, TFunction<void(const FString&)> OnPick)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	auto AddType = [&Menu, &Current, &OnPick](const FString& TypeName)
	{
		FLinearColor Tint = FLinearColor::White;
		const FSlateBrush* TypeIcon = GetTypeIcon(TypeName, Tint);
		Menu.AddMenuEntry(
			FUIAction(
				FExecuteAction::CreateLambda([OnPick, TypeName]() { OnPick(TypeName); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Current, TypeName]() { return Current == TypeName; })),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(TypeIcon)
				.ColorAndOpacity(Tint)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(TypeName))
			],
			NAME_None, TAttribute<FText>(), EUserInterfaceActionType::RadioButton);
	};
	// Bango::GetValueTypeNames: each type, then an array of each.
	const TArray<FString>& TypeNames = Bango::GetValueTypeNames();
	Menu.BeginSection(NAME_None, LOCTEXT("Types", "Type"));
	for (const FString& TypeName : TypeNames)
	{
		if (!TypeName.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
		{
			AddType(TypeName);
		}
	}
	Menu.EndSection();
	Menu.BeginSection(NAME_None, LOCTEXT("ArrayTypes", "Array of"));
	for (const FString& TypeName : TypeNames)
	{
		if (TypeName.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
		{
			AddType(TypeName);
		}
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

void SBangoTypePicker::Construct(const FArguments& InArgs)
{
	Type = InArgs._Type;
	OnTypePicked = InArgs._OnTypePicked;

	const TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image_Lambda([this]()
			{
				FLinearColor Tint = FLinearColor::White;
				return GetTypeIcon(Type.Get(FString()), Tint);
			})
			.ColorAndOpacity_Lambda([this]()
			{
				FLinearColor Tint = FLinearColor::White;
				GetTypeIcon(Type.Get(FString()), Tint);
				return FSlateColor(Tint);
			})
		];
	if (InArgs._ShowTypeName)
	{
		Content->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Type.Get(FString())); })
			];
	}

	ChildSlot
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.HasDownArrow(false)
		.ContentPadding(FMargin(2.0f, 1.0f))
		.ToolTipText_Lambda([this]() { return FText::Format(LOCTEXT("TypeTooltip", "{0}. Click to change."), FText::FromString(Type.Get(FString()))); })
		.OnGetMenuContent_Lambda([this]()
		{
			FOnBangoTypePicked Picked = OnTypePicked;
			return MakeTypeMenu(Type.Get(FString()), [Picked](const FString& TypeName) { Picked.ExecuteIfBound(TypeName); });
		})
		.ButtonContent()
		[
			Content
		]
	];
}

// ---- SBangoActorNamePicker ----

void SBangoActorNamePicker::GetNamedActors(TArray<FNamedActor>& OutActors)
{
	OutActors.Reset();
	const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}
	for (TObjectIterator<UBangoIDActorComponent> It; It; ++It)
	{
		const UBangoIDActorComponent* Component = *It;
		if (!IsValid(Component) || Component->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Component->GetWorld() != World)
		{
			continue;
		}
		const AActor* Actor = Component->GetOwner();
		const FString Name = Component->BangoName.ToString();
		if (!IsValid(Actor) || Name.IsEmpty())
		{
			continue;
		}
		FNamedActor& Added = OutActors.AddDefaulted_GetRef();
		Added.Name = Name;
		Added.Label = Actor->GetActorLabel();
	}
	OutActors.Sort([](const FNamedActor& A, const FNamedActor& B)
	{
		const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
		return ByName != 0 ? ByName < 0 : A.Label.Compare(B.Label, ESearchCase::IgnoreCase) < 0;
	});
}

FString SBangoActorNamePicker::AddNameToValue(const FString& TypeName, const FString& BangoText, const FString& Name)
{
	const FString Entity = FString(TEXT("@")) + Name;
	if (!TypeName.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
	{
		return Entity;
	}
	const FString Old = BangoText.TrimStartAndEnd();
	if (Old.Len() < 2 || !Old.StartsWith(TEXT("["), ESearchCase::CaseSensitive) || !Old.EndsWith(TEXT("]"), ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s]"), *Entity);
	}
	const FString Inside = Old.Mid(1, Old.Len() - 2).TrimStartAndEnd();
	return Inside.IsEmpty() ? FString::Printf(TEXT("[%s]"), *Entity) : FString::Printf(TEXT("[%s, %s]"), *Inside, *Entity);
}

TSharedRef<SWidget> SBangoActorNamePicker::MakeMenu() const
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	TArray<FNamedActor> Actors;
	GetNamedActors(Actors);
	Menu.BeginSection(NAME_None, LOCTEXT("NamedActors", "Actors with a Bango ID name"));
	if (Actors.Num() == 0)
	{
		Menu.AddMenuEntry(
			LOCTEXT("NoNamedActors", "None in this level"),
			LOCTEXT("NoNamedActorsTip", "No actor in the open level has a Bango ID component with a name. Add one to an actor and name it, and it's listed here."),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	const FOnBangoActorNamePicked Picked = OnActorNamePicked;
	for (const FNamedActor& Actor : Actors)
	{
		const FString Name = Actor.Name;
		Menu.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("@%s"), *Actor.Name)),
			FText::Format(LOCTEXT("NamedActorTip", "The actor {0} in the open level."), FText::FromString(Actor.Label)),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Picked, Name]() { Picked.ExecuteIfBound(Name); })));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

void SBangoActorNamePicker::Construct(const FArguments& InArgs)
{
	OnActorNamePicked = InArgs._OnActorNamePicked;

	FLinearColor Tint = FLinearColor::White;
	const FSlateBrush* Icon = SBangoTypePicker::GetTypeIcon(TEXT("entity"), Tint);
	ChildSlot
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.HasDownArrow(true)
		.ContentPadding(FMargin(1.0f, 1.0f))
		.ToolTipText(LOCTEXT("ActorNamePickerTip", "Pick an actor in the open level, by its Bango ID name. Only actors that have a Bango ID component with a name are listed, since only those can be written as @name."))
		.OnGetMenuContent_Lambda([this]() { return MakeMenu(); })
		.ButtonContent()
		[
			SNew(SImage)
			.Image(Icon)
			.ColorAndOpacity(FSlateColor(Tint))
		]
	];
}

// ---- SBangoValueBox ----

void SBangoValueBox::Construct(const FArguments& InArgs)
{
	Type = InArgs._Type;
	Value = InArgs._Value;
	TextNamespace = InArgs._TextNamespace;
	bUseScriptNamespace = InArgs._UseScriptNamespace;
	OnCheckValue = InArgs._OnCheckValue;
	OnValueCommitted = InArgs._OnValueCommitted;
	OnValueRejected = InArgs._OnValueRejected;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(TextBox, SEditableTextBox)
			.Text(this, &SBangoValueBox::GetShownText)
			.HintText(InArgs._HintText)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true)
			.OnTextChanged(this, &SBangoValueBox::HandleTextChanged)
			.OnTextCommitted(this, &SBangoValueBox::HandleTextCommitted)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			// An entity, or a list of them: pick an actor in the level instead of typing its @name.
			SNew(SBangoActorNamePicker)
			.Visibility_Lambda([this]()
			{
				const FString TypeName = Type.Get(FString());
				return TypeName == TEXT("entity") || TypeName == TEXT("entity[]") ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnActorNamePicked(this, &SBangoValueBox::HandleActorNamePicked)
		]
	];
}

void SBangoValueBox::HandleActorNamePicked(const FString& Name)
{
	const FString BangoText = SBangoActorNamePicker::AddNameToValue(Type.Get(FString()), Value.Get(FString()), Name);
	FString Error;
	if (!Check(BangoText, Error))
	{
		OnValueRejected.ExecuteIfBound(Error);
		return;
	}
	if (TextBox.IsValid())
	{
		TextBox->SetError(FText::GetEmpty());
	}
	OnValueCommitted.ExecuteIfBound(BangoText);
}

FString SBangoValueBox::GetValueAfterTypeChange(const FString& BangoText)
{
	// A value made for the old type rarely fits the new one; nil fits any type.
	return BangoText.TrimStartAndEnd().Equals(TEXT("nil"), ESearchCase::CaseSensitive) ? FString(TEXT("nil")) : FString();
}

FText SBangoValueBox::GetShownText() const
{
	return FText::FromString(Bango::GetFieldEditText(BangoValueWidgetsPrivate::MakeField(Type.Get(FString()), Value.Get(FString()))));
}

FString SBangoValueBox::ToBangoText(const FString& Typed) const
{
	// Typed like a node's field: a string or a text as plain words (or =code), anything else as code.
	const FBangoNodeField Field = BangoValueWidgetsPrivate::MakeField(Type.Get(FString()), Value.Get(FString()));
	FString Result = Bango::GetFieldValueFromTyped(Field, Typed).TrimStartAndEnd();
	if (!bUseScriptNamespace && !Result.IsEmpty() && Bango::IsLocalizedTextField(Field))
	{
		// Plain words became a TEXT, which takes its namespace from a script. There's none around this value.
		FBangoTextLiteral Literal;
		if (Bango::ParseTextLiteral(Result, Literal) && !Literal.bInvariant && !Literal.bHasNamespace)
		{
			if (TextNamespace.IsEmpty())
			{
				Literal.bInvariant = true;
			}
			else
			{
				Literal.bHasNamespace = true;
				Literal.Namespace = TextNamespace;
			}
			Result = Bango::MakeTextLiteral(Literal);
		}
	}
	return Result;
}

bool SBangoValueBox::Check(const FString& BangoText, FString& OutError) const
{
	if (BangoText.IsEmpty())
	{
		return true; // none
	}
	if (OnCheckValue.IsBound())
	{
		return OnCheckValue.Execute(Type.Get(FString()), BangoText, OutError);
	}
	FBangoValue Unused;
	return Bango::ParseFixedValue(Type.Get(FString()), BangoText, Unused, OutError);
}

void SBangoValueBox::HandleTextChanged(const FText& NewText)
{
	FString Error;
	const bool bOk = Check(ToBangoText(NewText.ToString()), Error);
	if (TextBox.IsValid())
	{
		TextBox->SetError(bOk ? FText::GetEmpty() : FText::FromString(Error));
	}
}

void SBangoValueBox::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (TextBox.IsValid())
	{
		TextBox->SetError(FText::GetEmpty());
	}
	if (CommitType == ETextCommit::OnCleared)
	{
		return; // Escape: the box went back by itself
	}
	const FString BangoText = ToBangoText(NewText.ToString());
	if (BangoText.Equals(Value.Get(FString()).TrimStartAndEnd(), ESearchCase::CaseSensitive))
	{
		return;
	}
	FString Error;
	if (!Check(BangoText, Error))
	{
		// Dropped: the box shows the value it has again.
		if (TextBox.IsValid())
		{
			TextBox->SetText(TAttribute<FText>::CreateSP(this, &SBangoValueBox::GetShownText));
		}
		OnValueRejected.ExecuteIfBound(Error);
		return;
	}
	OnValueCommitted.ExecuteIfBound(BangoText);
}

#undef LOCTEXT_NAMESPACE
