#include "BangoInlineScriptTarget.h"

#include "BangoInlineScript.h"
#include "GameFramework/Actor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "PropertyHandle.h"
#include "SBangoScriptEditor.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "BangoInlineScript"

namespace BangoInlineScriptTargetPrivate
{
	/** "Steps[2]" → "Steps" and 2; "Script" → "Script" and INDEX_NONE. */
	static void SplitSegment(const FString& Segment, FString& OutName, int32& OutIndex)
	{
		OutName = Segment;
		OutIndex = INDEX_NONE;
		int32 Open = INDEX_NONE;
		if (Segment.EndsWith(TEXT("]")) && Segment.FindChar(TEXT('['), Open))
		{
			OutName = Segment.Left(Open);
			const FString Number = Segment.Mid(Open + 1, Segment.Len() - Open - 2);
			if (Number.IsNumeric())
			{
				OutIndex = FCString::Atoi(*Number);
			}
		}
	}

	static FString LabelOf(const UObject* Object)
	{
		if (!Object)
		{
			return FString();
		}
#if WITH_EDITOR
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor->GetActorLabel();
		}
#endif
		if (Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			// A Blueprint's defaults: its name, without the generated class's _C.
			FString Name = Object->GetClass()->GetName();
			Name.RemoveFromEnd(TEXT("_C"));
			return Name;
		}
		return Object->GetName();
	}
}

FBangoInlineScriptTarget::FBangoInlineScriptTarget(UObject* InOwner, const FString& InPropertyPath, const TSharedPtr<IPropertyHandle>& InStructHandle, const FText& InPropertyLabel)
	: Owner(InOwner)
	, PropertyPath(InPropertyPath)
	, StructHandle(InStructHandle)
	, OwnerLabel(BangoInlineScriptTargetPrivate::LabelOf(InOwner))
	, PropertyLabel(InPropertyLabel)
	, bCondition(InStructHandle.IsValid() && InStructHandle->HasMetaData(TEXT("BangoCondition")))
{
}

FString FBangoInlineScriptTarget::GetKey() const
{
	const UObject* Object = Owner.Get();
	return (Object ? Object->GetPathName() : FString()) + TEXT(":") + PropertyPath;
}

FString FBangoInlineScriptTarget::GetScriptName() const
{
	const UObject* Object = Owner.Get();
	return (Object ? Object->GetName() : OwnerLabel) + TEXT(".") + PropertyPath;
}

FText FBangoInlineScriptTarget::GetDisplayName() const
{
	return FText::Format(LOCTEXT("DisplayName", "{0}: {1}"), FText::FromString(OwnerLabel), PropertyLabel);
}

bool FBangoInlineScriptTarget::Read(FString& OutSource) const
{
	const FBangoInlineScript* Script = Resolve(Owner.Get(), PropertyPath);
	if (!Script)
	{
		return false;
	}
	OutSource = Script->Source;
	return true;
}

bool FBangoInlineScriptTarget::Write(const FString& Source, FString& OutError)
{
	UObject* Object = Owner.Get();
	FProperty* TopProperty = nullptr;
	FBangoInlineScript* Script = Resolve(Object, PropertyPath, &TopProperty);
	if (!Script)
	{
		OutError = FText::Format(LOCTEXT("WriteGone", "Couldn't save {0}: it isn't there any more (was its object deleted?)."), GetDisplayName()).ToString();
		return false;
	}

	// Through the details panel, while it shows the property: it makes the undoable change and passes a Blueprint
	// default on to its instances, as typing in the panel does.
	if (const TSharedPtr<IPropertyHandle> Handle = StructHandle.Pin())
	{
		const TSharedPtr<IPropertyHandle> SourceHandle = Handle->IsValidHandle() ? Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoInlineScript, Source)) : nullptr;
		TArray<UObject*> Outers;
		if (SourceHandle.IsValid() && SourceHandle->IsValidHandle())
		{
			Handle->GetOuterObjects(Outers);
		}
		if (Outers.Num() == 1 && Outers[0] == Object && SourceHandle->SetValue(Source) == FPropertyAccess::Success)
		{
			return true;
		}
	}

	// Otherwise straight into the object.
	const FScopedTransaction Transaction(FText::Format(LOCTEXT("EditInlineScript", "Edit {0}"), GetDisplayName()));
	Object->Modify();
	Object->PreEditChange(TopProperty);
	Script->Source = Source;
	FPropertyChangedEvent Event(TopProperty, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(Event);
	Object->MarkPackageDirty();
	return true;
}

FBangoInlineScript* FBangoInlineScriptTarget::Resolve(UObject* Owner, const FString& Path, FProperty** OutTopProperty)
{
	using namespace BangoInlineScriptTargetPrivate;
	if (OutTopProperty)
	{
		*OutTopProperty = nullptr;
	}
	if (!IsValid(Owner) || Path.IsEmpty())
	{
		return nullptr;
	}
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."));

	const UStruct* Struct = Owner->GetClass();
	void* Container = Owner;  // what Struct's properties are in
	FProperty* Current = nullptr;
	void* Value = nullptr;    // Current's value (its first element, for a fixed-size array)
	void* CurrentContainer = nullptr;
	for (const FString& Segment : Segments)
	{
		FString Name;
		int32 Index = INDEX_NONE;
		SplitSegment(Segment, Name, Index);

		if (Current && Index != INDEX_NONE && Current->GetName() == Name)
		{
			// An element of the property just found: `Steps` then `Steps[2]`.
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Current))
			{
				FScriptArrayHelper Helper(Array, Value);
				if (!Helper.IsValidIndex(Index))
				{
					return nullptr;
				}
				Value = Helper.GetRawPtr(Index);
				Current = Array->Inner;
				CurrentContainer = nullptr;
				continue;
			}
			if (Current->ArrayDim > 1 && CurrentContainer)
			{
				if (Index < 0 || Index >= Current->ArrayDim)
				{
					return nullptr;
				}
				Value = Current->ContainerPtrToValuePtr<void>(CurrentContainer, Index);
				continue;
			}
			return nullptr;
		}

		// A property of the struct or object the path has reached.
		if (Current)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Current))
			{
				Struct = StructProperty->Struct;
				Container = Value;
			}
			else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Current))
			{
				UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
				if (!IsValid(Object))
				{
					return nullptr;
				}
				Struct = Object->GetClass();
				Container = Object;
			}
			else
			{
				return nullptr;
			}
		}
		FProperty* Property = Struct ? FindFProperty<FProperty>(Struct, *Name) : nullptr;
		if (!Property || !Container)
		{
			return nullptr;
		}
		if (OutTopProperty && !*OutTopProperty)
		{
			*OutTopProperty = Property;
		}
		const int32 Element = Index != INDEX_NONE && Property->ArrayDim > 1 ? Index : 0;
		if (Element < 0 || Element >= Property->ArrayDim)
		{
			return nullptr;
		}
		Current = Property;
		CurrentContainer = Container;
		Value = Property->ContainerPtrToValuePtr<void>(Container, Element);
	}

	const FStructProperty* Found = CastField<FStructProperty>(Current);
	return Found && Found->Struct == FBangoInlineScript::StaticStruct() ? static_cast<FBangoInlineScript*>(Value) : nullptr;
}

// ---------------------------------------------------------------------------

TMap<FString, TWeakPtr<SWindow>>& FBangoInlineScriptWindows::Windows()
{
	static TMap<FString, TWeakPtr<SWindow>> Open;
	return Open;
}

bool FBangoInlineScriptWindows::IsOpen(const FString& Key)
{
	const TWeakPtr<SWindow>* Window = Windows().Find(Key);
	return Window && Window->IsValid();
}

void FBangoInlineScriptWindows::Open(const TSharedRef<FBangoInlineScriptTarget>& Target)
{
	const FString Key = Target->GetKey();
	if (const TWeakPtr<SWindow>* Existing = Windows().Find(Key))
	{
		if (const TSharedPtr<SWindow> Window = Existing->Pin())
		{
			Window->BringToFront(true);
			return;
		}
	}

	const TSharedRef<SBangoScriptEditor> Editor = SNew(SBangoScriptEditor);
	Editor->OpenInlineScript(Target);
	const TWeakPtr<SBangoScriptEditor> WeakEditor = Editor;
	const FText DisplayName = Target->GetDisplayName();

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title_Lambda([WeakEditor, DisplayName]()
		{
			const TSharedPtr<SBangoScriptEditor> Shown = WeakEditor.Pin();
			return Shown.IsValid() && Shown->HasUnsavedChanges()
				? FText::Format(LOCTEXT("WindowTitleDirty", "{0} *"), DisplayName)
				: DisplayName;
		})
		.ClientSize(FVector2D(1400.0f, 900.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			Editor
		];

	// Closing asks about unsaved changes (Cancel keeps the window open), then frees the property's box.
	Window->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride::CreateLambda([WeakEditor, Key](const TSharedRef<SWindow>& Closing)
	{
		const TSharedPtr<SBangoScriptEditor> Shown = WeakEditor.Pin();
		if (Shown.IsValid() && !Shown->ConfirmCloseScript())
		{
			return;
		}
		Closing->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride());
		Windows().Remove(Key);
		FSlateApplication::Get().RequestDestroyWindow(Closing);
	}));

	Windows().Add(Key, Window);
	const TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow();
	if (Root.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, Root.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
}

#undef LOCTEXT_NAMESPACE
