#include "BangoActorRegistry.h"

#include "BangoIDActorComponent.h"
#include "BangoEntity.h"
#include "Components/ActorComponent.h"
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

bool UBangoActorRegistry::IsTakenByAnother(const FString& Name, const UObject* Object) const
{
	const TWeakObjectPtr<UObject>* Existing = Objects.Find(Name);
	const UObject* ExistingObject = Existing ? Existing->Get() : nullptr;
	return ExistingObject && ExistingObject != Object;
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

	if (IsTakenByAnother(Name, Actor))
	{
		const UObject* Existing = Objects.Find(Name)->Get();
		OutError = Kind == Bango::EBangoEntityIDKind::Named
			? FString::Printf(TEXT("Actor '%s' uses Bango ID name '%s', which is already used by '%s'."), *Actor->GetName(), *Name, *Existing->GetName())
			: FString::Printf(TEXT("Actor '%s' has the same automatic Bango ID as '%s' (was one copied from the other?), so it isn't registered."),
				*Actor->GetName(), *Existing->GetName());
		return false;
	}

	Add(Name, Actor);
	return true;
}

namespace BangoActorRegistryPrivate
{
	/** How people are shown Object: an actor's name as the Outliner shows it; a component's actor's, and its name. */
	static FString GetObjectDisplayName(const UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor->GetActorNameOrLabel();
		}
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		return Owner ? FString::Printf(TEXT("%s (%s)"), *Owner->GetActorNameOrLabel(), *Component->GetName()) : GetNameSafe(Object);
	}
}

void UBangoActorRegistry::Add(const FString& Name, UObject* Object) const
{
	Objects.Add(Name, Object);
	Bango::SetEntityDisplayName(Name, BangoActorRegistryPrivate::GetObjectDisplayName(Object));

	FString& EntityID = EntityIDs.FindOrAdd(Object);
	const bool bHasOne = !EntityID.IsEmpty() && EntityID != Name && Objects.Contains(EntityID);
	if (!bHasOne)
	{
		EntityID = Name;
	}
	else if (Bango::GetEntityIDKind(Name) == Bango::EBangoEntityIDKind::Named && Bango::GetEntityIDKind(EntityID) != Bango::EBangoEntityIDKind::Named)
	{
		// It was an entity before it got a Bango ID name: the old ID still finds it, but the name is used from now on.
		Bango::ReportError(FString::Printf(TEXT("Actor '%s' was given the Bango ID name '%s' after scripts had already used it without one. Both find it, but scripts may hold the old one, which isn't equal to @%s."),
			*BangoActorRegistryPrivate::GetObjectDisplayName(Object), *Name, *Name));
		EntityID = Name;
	}
}

void UBangoActorRegistry::Unregister(const FString& Name, const UObject* Object)
{
	if (const TWeakObjectPtr<UObject>* Existing = Objects.Find(Name))
	{
		// Also removes stale entries whose object is already gone.
		if (!Existing->IsValid() || Existing->Get() == Object)
		{
			Objects.Remove(Name);
		}
	}
	if (const FString* EntityID = EntityIDs.Find(Object); EntityID && *EntityID == Name)
	{
		EntityIDs.Remove(Object);
	}
}

UObject* UBangoActorRegistry::FindEntity(const FString& Name) const
{
	if (Bango::IsPlayerEntityName(Name))
	{
		return GetPlayerPawn();
	}
	if (const TWeakObjectPtr<UObject>* Found = Objects.Find(Name))
	{
		if (UObject* Object = Found->Get())
		{
			return Object;
		}
	}

	// A Level ID from before (a saved script, or one made in an earlier play): find it by its path.
	const FString Path = Bango::GetLevelEntityPath(Name);
	if (!Path.IsEmpty())
	{
		AActor* Actor = FindLevelActor(Path);
		if (Actor)
		{
			Add(Name, Actor);
		}
		return Actor;
	}

	// A Component ID: its actor, then the component of that name on it.
	FString OwnerID;
	FString ComponentName;
	if (!Bango::SplitComponentEntityID(Name, OwnerID, ComponentName))
	{
		return nullptr;
	}
	AActor* Owner = Cast<AActor>(FindEntity(OwnerID));
	if (!Owner)
	{
		return nullptr;
	}
	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (IsValid(Component) && Component->GetName().Equals(ComponentName, ESearchCase::CaseSensitive))
		{
			// Remembered, except on the player's pawn: `player` can be another pawn later.
			if (Bango::GetEntityIDKind(OwnerID) != Bango::EBangoEntityIDKind::Keyword)
			{
				Add(Name, Component);
			}
			return Component;
		}
	}
	return nullptr;
}

AActor* UBangoActorRegistry::FindActor(const FString& Name) const
{
	return Cast<AActor>(FindEntity(Name));
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

FString UBangoActorRegistry::GetOrMakeEntityID(const UObject* Object, FString& OutError)
{
	if (!Object)
	{
		OutError = TEXT("There's nothing there (null).");
		return FString();
	}
	if (const AActor* Actor = Cast<AActor>(Object))
	{
		return GetOrMakeActorID(Actor, OutError);
	}
	if (const UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		return GetOrMakeComponentID(Component, OutError);
	}
	OutError = FString::Printf(TEXT("'%s' is a %s: only actors and actor components can be entities."), *Object->GetName(), *Object->GetClass()->GetName());
	return FString();
}

FString UBangoActorRegistry::GetOrMakeActorID(const AActor* Actor, FString& OutError)
{
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
	const bool bKnown = Known && !Known->IsEmpty() && Objects.Contains(*Known);
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
	if (const FString* Made = EntityIDs.Find(Actor); Made && Objects.Contains(*Made))
	{
		return *Made;
	}
	OutError = FString::Printf(TEXT("Actor '%s' couldn't be given an automatic Bango ID."), *Actor->GetActorNameOrLabel());
	return FString();
}

FString UBangoActorRegistry::GetOrMakeComponentID(const UActorComponent* Component, FString& OutError)
{
	const AActor* Owner = Component->GetOwner();
	if (!Owner)
	{
		OutError = FString::Printf(TEXT("Component '%s' isn't on an actor."), *Component->GetName());
		return FString();
	}
	if (Owner->GetWorld() != GetWorld())
	{
		OutError = FString::Printf(TEXT("Component '%s' isn't in this game world."), *BangoActorRegistryPrivate::GetObjectDisplayName(Component));
		return FString();
	}
	UActorComponent* MutableComponent = const_cast<UActorComponent*>(Component);

	const FString* Known = EntityIDs.Find(Component);
	if (Known && !Known->IsEmpty() && Objects.Contains(*Known))
	{
		return *Known;
	}

	// One that keeps its own ID: that one, once it has one.
	IBangoEntity* Keeper = Cast<IBangoEntity>(MutableComponent);
	if (Keeper)
	{
		const FString Kept = Keeper->GetBangoEntityID();
		if (!Kept.IsEmpty())
		{
			if (Bango::GetEntityIDKind(Kept) != Bango::EBangoEntityIDKind::Automatic)
			{
				OutError = FString::Printf(TEXT("Component '%s' keeps the entity ID '%s', which isn't an automatic one Bango made (it should only keep what SetBangoEntityID gives it)."),
					*BangoActorRegistryPrivate::GetObjectDisplayName(Component), *Kept);
				return FString();
			}
			if (IsTakenByAnother(Kept, Component))
			{
				OutError = FString::Printf(TEXT("Component '%s' keeps the same entity ID as '%s' (was one copied from the other?), so it isn't registered."),
					*BangoActorRegistryPrivate::GetObjectDisplayName(Component), *BangoActorRegistryPrivate::GetObjectDisplayName(Objects.Find(Kept)->Get()));
				return FString();
			}
			Add(Kept, MutableComponent);
			return Kept;
		}
	}

	// Made by its actor's class, Blueprint or level, so its name is the same each time: its actor's ID and its name.
	// (The engine's own test for referring to a component by its name over the network; a startup component is one on
	// an actor that was there when its level started, a copy of it in Play in Editor.)
	if (Component->IsNameStableForNetworking() || Component->IsNetStartupComponent())
	{
		const FString OwnerID = GetOrMakeActorID(Owner, OutError);
		if (OwnerID.IsEmpty())
		{
			return FString();
		}
		const FString ID = Bango::MakeComponentEntityID(OwnerID, Component->GetName());
		if (Bango::GetEntityIDKind(OwnerID) != Bango::EBangoEntityIDKind::Keyword)
		{
			Add(ID, MutableComponent);
		}
		return ID;
	}

	// Added while playing: a new Automatic ID, kept on it if it can keep one.
	const FString ID = Bango::MakeAutomaticEntityID(FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (Keeper)
	{
		Keeper->SetBangoEntityID(ID);
	}
	Add(ID, MutableComponent);
	return ID;
}

APawn* UBangoActorRegistry::GetPlayerPawn() const
{
	UWorld* World = GetWorld();
	const APlayerController* Controller = World && GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	return Controller ? Controller->GetPawn() : nullptr;
}

namespace Bango
{
	bool ObjectToEntity(const UObject* Object, FBangoValue& OutValue, FString& OutError)
	{
		if (!Object)
		{
			OutValue = FBangoValue::MakeNil(FBangoType::Entity());
			return true;
		}
		UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Object);
		if (!Registry)
		{
			OutError = FString::Printf(TEXT("'%s' isn't in a game world, so it can't be passed to a script as an entity."), *Object->GetName());
			return false;
		}
		const FString ID = Registry->GetOrMakeEntityID(Object, OutError);
		if (ID.IsEmpty())
		{
			return false;
		}
		OutValue = FBangoValue::MakeEntity(ID);
		return true;
	}

	bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError)
	{
		return ObjectToEntity(Actor, OutValue, OutError);
	}

	void RegisterEntity(const UObject* Object)
	{
		UBangoActorRegistry* Registry = Object ? UBangoActorRegistry::Get(Object) : nullptr;
		if (!Registry)
		{
			return;
		}
		FString Error;
		if (Registry->GetOrMakeEntityID(Object, Error).IsEmpty())
		{
			Bango::ReportError(Error);
		}
	}
}
