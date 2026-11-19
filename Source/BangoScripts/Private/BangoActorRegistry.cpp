#include "BangoActorRegistry.h"

#include "BangoIDActorComponent.h"
#include "BangoTypes.h"
#include "BangoLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

UBangoActorRegistry* UBangoActorRegistry::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<UBangoActorRegistry>() : nullptr;
}

bool UBangoActorRegistry::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UBangoActorRegistry::Register(const FString& Name, AActor* Actor, FString& OutError)
{
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Can't register Bango ID '%s': no actor."), *Name);
		return false;
	}

	const Bango::EBangoEntityIDKind Kind = Bango::GetEntityIDKind(Name);
	if (Kind != Bango::EBangoEntityIDKind::Named && Kind != Bango::EBangoEntityIDKind::Automatic)
	{
		OutError = FString::Printf(TEXT("Actor '%s' has an invalid Bango ID name '%s'. Use letters, digits and '_', not starting with a digit (scripts write it as @%s)."),
			*Actor->GetName(), *Name, *Name);
		return false;
	}

	if (const TWeakObjectPtr<AActor>* Existing = Actors.Find(Name))
	{
		const AActor* ExistingActor = Existing->Get();
		if (ExistingActor && ExistingActor != Actor)
		{
			OutError = Kind == Bango::EBangoEntityIDKind::Named
				? FString::Printf(TEXT("Actor '%s' uses Bango ID name '%s', which is already used by actor '%s'."), *Actor->GetName(), *Name, *ExistingActor->GetName())
				: FString::Printf(TEXT("Actor '%s' has the same automatic Bango ID as actor '%s' (was one copied from the other?), so it isn't registered."),
					*Actor->GetName(), *ExistingActor->GetName());
			return false;
		}
	}

	Add(Name, Actor);
	return true;
}

void UBangoActorRegistry::Add(const FString& Name, AActor* Actor)
{
	Actors.Add(Name, Actor);
	Bango::SetEntityDisplayName(Name, Actor->GetActorNameOrLabel());

	FString& EntityID = EntityIDs.FindOrAdd(Actor);
	const bool bHasOne = !EntityID.IsEmpty() && EntityID != Name && Actors.Contains(EntityID);
	if (!bHasOne)
	{
		EntityID = Name;
	}
	else if (Bango::GetEntityIDKind(Name) == Bango::EBangoEntityIDKind::Named && Bango::GetEntityIDKind(EntityID) != Bango::EBangoEntityIDKind::Named)
	{
		// It was an entity before it got a Bango ID name: the old ID still finds it, but the name is used from now on.
		Bango::ReportError(FString::Printf(TEXT("Actor '%s' was given the Bango ID name '%s' after scripts had already used it without one. Both find it, but scripts may hold the old one, which isn't equal to @%s."),
			*Actor->GetActorNameOrLabel(), *Name, *Name));
		EntityID = Name;
	}
}

void UBangoActorRegistry::Unregister(const FString& Name, const AActor* Actor)
{
	if (const TWeakObjectPtr<AActor>* Existing = Actors.Find(Name))
	{
		// Also removes stale entries whose actor is already gone.
		if (!Existing->IsValid() || Existing->Get() == Actor)
		{
			Actors.Remove(Name);
		}
	}
	if (const FString* EntityID = EntityIDs.Find(Actor); EntityID && *EntityID == Name)
	{
		EntityIDs.Remove(Actor);
	}
}

AActor* UBangoActorRegistry::FindActor(const FString& Name) const
{
	if (Bango::IsPlayerEntityName(Name))
	{
		return GetPlayerPawn();
	}
	if (const TWeakObjectPtr<AActor>* Found = Actors.Find(Name))
	{
		if (AActor* Actor = Found->Get())
		{
			return Actor;
		}
	}
	// A Level ID from before (a saved script, or one made in an earlier play): find it by its path.
	const FString Path = Bango::GetLevelEntityPath(Name);
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	AActor* Actor = FindLevelActor(Path);
	if (Actor)
	{
		Actors.Add(Name, Actor);
		Bango::SetEntityDisplayName(Name, Actor->GetActorNameOrLabel());
		FString& EntityID = EntityIDs.FindOrAdd(Actor);
		if (EntityID.IsEmpty())
		{
			EntityID = Name;
		}
	}
	return Actor;
}

AActor* UBangoActorRegistry::FindLevelActor(const FString& Path) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// The path is kept without Play in Editor's prefix (UEDPIE_0_ on its package's name): put this world's back.
	FString WorldPath = Path;
	if (World->IsPlayInEditor())
	{
		int32 Dot = INDEX_NONE;
		if (Path.FindChar(TEXT('.'), Dot))
		{
			WorldPath = UWorld::ConvertToPIEPackageName(Path.Left(Dot), World->GetOutermost()->GetPIEInstanceID()) + Path.Mid(Dot);
		}
	}
	AActor* Actor = FindObject<AActor>(nullptr, *WorldPath);
	return IsValid(Actor) && Actor->GetWorld() == World ? Actor : nullptr;
}

FString UBangoActorRegistry::GetOrMakeEntityID(const AActor* Actor, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("There's no actor.");
		return FString();
	}
	if (Actor->GetWorld() != GetWorld())
	{
		OutError = FString::Printf(TEXT("Actor '%s' isn't in this game world."), *Actor->GetActorNameOrLabel());
		return FString();
	}
	AActor* MutableActor = const_cast<AActor*>(Actor);

	// A Bango ID component that hasn't registered yet (its actor hasn't begun play): now.
	UBangoIDActorComponent* Component = MutableActor->FindComponentByClass<UBangoIDActorComponent>();
	if (Component && !Component->IsRegistered())
	{
		Component->RegisterName();
	}

	const FString* Known = EntityIDs.Find(Actor);
	const bool bKnown = Known && !Known->IsEmpty() && Actors.Contains(*Known);
	if (bKnown && Bango::GetEntityIDKind(*Known) == Bango::EBangoEntityIDKind::Named)
	{
		return *Known;
	}
	if (Component && !Component->BangoName.IsEmpty() && !Component->IsRegistered())
	{
		OutError = FString::Printf(TEXT("Actor '%s' has a Bango ID component, but its name '%s' isn't registered (it may be invalid or used by another actor)."),
			*Actor->GetActorNameOrLabel(), *Component->BangoName);
		return FString();
	}
	if (GetPlayerPawn() == Actor)
	{
		return Bango::GetPlayerEntityName(); // the player's pawn needs no Bango ID: it's the `player` entity
	}
	if (bKnown)
	{
		return *Known;
	}

	// Placed in a level: its path there, which is the same each time the level is played.
	if (Actor->bNetStartup || Actor->HasAnyFlags(RF_WasLoaded)) // bNetStartup: there when its level started (a copy, in Play in Editor)
	{
		const FString ID = Bango::MakeLevelEntityID(UWorld::RemovePIEPrefix(Actor->GetPathName()));
		Add(ID, MutableActor);
		return ID;
	}

	// Spawned while playing: a new Automatic ID, kept on a Bango ID component (the one it has, or a new one).
	if (!Component)
	{
		Component = NewObject<UBangoIDActorComponent>(MutableActor);
		MutableActor->AddInstanceComponent(Component);
		Component->AutomaticID = Bango::MakeAutomaticEntityID(FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Component->RegisterComponent(); // begins play, and registers, if its actor already has
	}
	else if (Component->AutomaticID.IsEmpty())
	{
		Component->AutomaticID = Bango::MakeAutomaticEntityID(FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}
	Component->RegisterName();
	if (const FString* Made = EntityIDs.Find(Actor); Made && Actors.Contains(*Made))
	{
		return *Made;
	}
	OutError = FString::Printf(TEXT("Actor '%s' couldn't be given an automatic Bango ID."), *Actor->GetActorNameOrLabel());
	return FString();
}

APawn* UBangoActorRegistry::GetPlayerPawn() const
{
	UWorld* World = GetWorld();
	const APlayerController* Controller = World && GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	return Controller ? Controller->GetPawn() : nullptr;
}

namespace Bango
{
	bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError)
	{
		if (!Actor)
		{
			OutValue = FBangoValue::MakeNil(FBangoType::Entity());
			return true;
		}
		UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Actor);
		if (!Registry)
		{
			OutError = FString::Printf(TEXT("Actor '%s' isn't in a game world, so it can't be passed to a script as an entity."), *Actor->GetName());
			return false;
		}
		const FString ID = Registry->GetOrMakeEntityID(Actor, OutError);
		if (ID.IsEmpty())
		{
			return false;
		}
		OutValue = FBangoValue::MakeEntity(ID);
		return true;
	}
}
