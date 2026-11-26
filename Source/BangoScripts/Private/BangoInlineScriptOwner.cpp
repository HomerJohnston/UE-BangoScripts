// Which of an object's inline scripts one is, for error messages: found by looking through the object's properties,
// and only when there's an error to report.
#include "BangoInlineScript.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Engine/BlueprintGeneratedClass.h"

namespace BangoInlineScriptOwnerPrivate
{
	using FVisit = TFunctionRef<void(FBangoInlineScriptBase& Script, const FString& Path)>;

	struct FSearch
	{
		/** The script to find, by where it is; or null, to find scripts by their text. */
		const FBangoInlineScriptBase* Script = nullptr;
		const FString* Source = nullptr;
		TArray<FString> Found;
	};

	static constexpr int32 MaxDepth = 3;          // structs inside structs
	static constexpr int32 MaxArrayElements = 1024;

	static void VisitStruct(const UStruct* Type, void* Container, const FString& Prefix, int32 Depth, FVisit Visit);

	static void VisitValue(const FStructProperty* Property, void* Value, const FString& Name, int32 Depth, FVisit Visit)
	{
		if (!Property->Struct || !Value)
		{
			return;
		}
		if (Property->Struct->IsChildOf(FBangoInlineScriptBase::StaticStruct()))
		{
			Visit(*static_cast<FBangoInlineScriptBase*>(Value), Name);
			return;
		}
		if (Depth < MaxDepth)
		{
			VisitStruct(Property->Struct, Value, Name + TEXT("."), Depth + 1, Visit);
		}
	}

	static void VisitStruct(const UStruct* Type, void* Container, const FString& Prefix, int32 Depth, FVisit Visit)
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
					VisitValue(Struct, Struct->ContainerPtrToValuePtr<void>(Container, Index), Name, Depth, Visit);
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
					VisitValue(Inner, Helper.GetRawPtr(Index), FString::Printf(TEXT("%s%s[%d]"), *Prefix, *Array->GetAuthoredName(), Index), Depth, Visit);
				}
			}
		}
	}

	static void SearchObject(const UObject* Object, const FString& Prefix, FSearch& Search)
	{
		if (!IsValid(Object))
		{
			return;
		}
		// Only read here.
		VisitStruct(Object->GetClass(), const_cast<UObject*>(Object), Prefix, 0, [&Search](FBangoInlineScriptBase& Candidate, const FString& Name)
		{
			const bool bMatches = Search.Script
				? &Candidate == Search.Script
				: Search.Source != nullptr && Candidate.Source.Equals(*Search.Source, ESearchCase::CaseSensitive);
			if (bMatches)
			{
				Search.Found.AddUnique(Name);
			}
		});
	}

	/** A script's ID as its archetype (a Blueprint's default, or its component's template) has it at the same place, if it's a Blueprint's. */
	static bool IsInheritedFromBlueprint(const UObject* Object, const FString& Path, const FBangoInlineScriptBase& Script)
	{
		const UObject* Archetype = Object ? Object->GetArchetype() : nullptr;
		if (!Archetype || !Cast<UBlueprintGeneratedClass>(Archetype->GetClass()))
		{
			return false; // no archetype, or a C++ class's default: nothing to fix it in but this object
		}
		const FBangoInlineScriptBase* There = Bango::FindInlineScriptAt(const_cast<UObject*>(Archetype), Path);
		return There && There->ScriptId == Script.ScriptId && There->Source.Equals(Script.Source, ESearchCase::CaseSensitive);
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

namespace Bango
{
	void ForEachInlineScript(UObject* Object, TFunctionRef<void(FBangoInlineScriptBase& Script, const FString& Path)> Visit)
	{
		if (IsValid(Object))
		{
			BangoInlineScriptOwnerPrivate::VisitStruct(Object->GetClass(), Object, FString(), 0, Visit);
		}
	}

	FBangoInlineScriptBase* FindInlineScriptAt(UObject* Object, const FString& Path)
	{
		FBangoInlineScriptBase* Found = nullptr;
		ForEachInlineScript(Object, [&Found, &Path](FBangoInlineScriptBase& Script, const FString& ScriptPath)
		{
			if (!Found && ScriptPath.Equals(Path, ESearchCase::CaseSensitive))
			{
				Found = &Script;
			}
		});
		return Found;
	}

	TArray<FBangoInlineScriptIdProblem> FindInlineScriptIdProblems(TConstArrayView<UObject*> Objects, bool bMissing)
	{
		using namespace BangoInlineScriptOwnerPrivate;
		struct FSeen
		{
			UObject* Object = nullptr;
			FString Path;
		};
		TMap<FGuid, FSeen> Seen;
		TArray<FBangoInlineScriptIdProblem> Problems;
		for (UObject* Object : Objects)
		{
			ForEachInlineScript(Object, [&](FBangoInlineScriptBase& Script, const FString& Path)
			{
				if (!Script.ScriptId.IsValid())
				{
					if (bMissing && !Script.IsEmpty() && !IsInheritedFromBlueprint(Object, Path, Script))
					{
						FBangoInlineScriptIdProblem& Problem = Problems.AddDefaulted_GetRef();
						Problem.bMissing = true;
						Problem.Object = Object;
						Problem.Path = Path;
					}
					return;
				}
				if (const FSeen* First = Seen.Find(Script.ScriptId))
				{
					if (!IsInheritedFromBlueprint(Object, Path, Script))
					{
						FBangoInlineScriptIdProblem& Problem = Problems.AddDefaulted_GetRef();
						Problem.Object = Object;
						Problem.Path = Path;
						Problem.OtherObject = First->Object;
						Problem.OtherPath = First->Path;
					}
					return;
				}
				Seen.Add(Script.ScriptId, FSeen{ Object, Path });
			});
		}
		return Problems;
	}

	TArray<UObject*> GetInlineScriptIdUnit(AActor* Actor)
	{
		TArray<UObject*> Unit;
		if (IsValid(Actor))
		{
			Unit.Add(Actor);
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (IsValid(Component))
				{
					Unit.Add(Component);
				}
			}
		}
		return Unit;
	}
}
