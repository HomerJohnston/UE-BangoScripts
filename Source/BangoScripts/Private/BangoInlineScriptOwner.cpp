// Which of an object's inline scripts one is, for error messages: found by looking through the object's properties,
// and only when there's an error to report.
#include "BangoInlineScript.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace BangoInlineScriptOwnerPrivate
{
	struct FSearch
	{
		/** The script to find, by where it is; or null, to find scripts by their text. */
		const FBangoInlineScriptBase* Script = nullptr;
		const FString* Source = nullptr;
		TArray<FString> Found;
	};

	static constexpr int32 MaxDepth = 3;          // structs inside structs
	static constexpr int32 MaxArrayElements = 1024;

	static void SearchStruct(const UStruct* Type, const void* Container, const FString& Prefix, int32 Depth, FSearch& Search);

	static void SearchValue(const FStructProperty* Property, const void* Value, const FString& Name, int32 Depth, FSearch& Search)
	{
		if (!Property->Struct || !Value)
		{
			return;
		}
		if (Property->Struct->IsChildOf(FBangoInlineScriptBase::StaticStruct()))
		{
			const FBangoInlineScriptBase* Candidate = static_cast<const FBangoInlineScriptBase*>(Value);
			const bool bMatches = Search.Script
				? Candidate == Search.Script
				: Search.Source != nullptr && Candidate->Source.Equals(*Search.Source, ESearchCase::CaseSensitive);
			if (bMatches)
			{
				Search.Found.AddUnique(Name);
			}
			return;
		}
		if (Depth < MaxDepth)
		{
			SearchStruct(Property->Struct, Value, Name + TEXT("."), Depth + 1, Search);
		}
	}

	static void SearchStruct(const UStruct* Type, const void* Container, const FString& Prefix, int32 Depth, FSearch& Search)
	{
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			const FProperty* Property = *It;
			if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				for (int32 Index = 0; Index < Struct->ArrayDim; ++Index)
				{
					const FString Name = Struct->ArrayDim > 1
						? FString::Printf(TEXT("%s%s[%d]"), *Prefix, *Struct->GetAuthoredName(), Index)
						: Prefix + Struct->GetAuthoredName();
					SearchValue(Struct, Struct->ContainerPtrToValuePtr<void>(Container, Index), Name, Depth, Search);
				}
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner);
				if (!Inner)
				{
					continue;
				}
				FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Container));
				const int32 Count = FMath::Min(Helper.Num(), MaxArrayElements);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					SearchValue(Inner, Helper.GetRawPtr(Index), FString::Printf(TEXT("%s%s[%d]"), *Prefix, *Array->GetAuthoredName(), Index), Depth, Search);
				}
			}
		}
	}

	static void SearchObject(const UObject* Object, const FString& Prefix, FSearch& Search)
	{
		if (IsValid(Object))
		{
			SearchStruct(Object->GetClass(), Object, Prefix, 0, Search);
		}
	}
}

namespace Bango
{
	FString DescribeInlineScriptOwner(const UObject* Owner, const FBangoInlineScriptBase* Script, const FString* Source)
	{
		using namespace BangoInlineScriptOwnerPrivate;
		if (!IsValid(Owner))
		{
			return FString();
		}
		FSearch Search;
		Search.Script = Script;
		Search.Source = Source;

		// The owner's own properties, and for an actor its components' (where a script usually is).
		FString Label;
		SearchObject(Owner, FString(), Search);
		if (const AActor* Actor = Cast<AActor>(Owner))
		{
			Label = Actor->GetActorNameOrLabel();
			for (const UActorComponent* Component : Actor->GetComponents())
			{
				if (Component)
				{
					SearchObject(Component, Component->GetName() + TEXT("."), Search);
				}
			}
		}
		else if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Owner))
		{
			const AActor* ComponentActor = OwnerComponent->GetOwner();
			Label = ComponentActor
				? FString::Printf(TEXT("%s (%s)"), *ComponentActor->GetActorNameOrLabel(), *OwnerComponent->GetName())
				: OwnerComponent->GetName();
		}
		else
		{
			Label = Owner->GetName();
		}
		if (Search.Found.Num() == 0 && Script)
		{
			// Not where any property is: a copy of one (a Blueprint pin's, or one its owner keeps aside). By its text, then.
			Search.Script = nullptr;
			Search.Source = &Script->Source;
			SearchObject(Owner, FString(), Search);
			if (const AActor* OwnerActor = Cast<AActor>(Owner))
			{
				for (const UActorComponent* Part : OwnerActor->GetComponents())
				{
					if (Part)
					{
						SearchObject(Part, Part->GetName() + TEXT("."), Search);
					}
				}
			}
		}

		if (Search.Found.Num() == 0)
		{
			return FString::Printf(TEXT("on '%s'"), *Label);
		}
		// Usually one. Several have the same text (or, for a script found by where it is, can't happen).
		FString Names;
		for (int32 Index = 0; Index < Search.Found.Num(); ++Index)
		{
			Names += FString::Printf(TEXT("%s'%s'"), Index == 0 ? TEXT("") : TEXT(" or "), *Search.Found[Index]);
		}
		return FString::Printf(TEXT("on '%s', its script %s"), *Label, *Names);
	}
}
