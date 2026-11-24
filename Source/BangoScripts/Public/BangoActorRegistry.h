#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoContainers.h"
#include "BangoActorRegistry.generated.h"

class AActor;
class APawn;
class UActorComponent;
class FBangoValue;

/**
 * Maps entity names to actors and actor components for one game world (spec section 4, Bango IDs and Entity IDs).
 *
 * Scripts name an actor with an entity literal: `@john Speak("Hi");`. Bango IDs are their own namespace:
 * they don't create globals, so they can't clash with them. Any other actor, and any actor component, gets an entity
 * name of its own the first time it becomes an entity (GetOrMakeEntityID): a Level ID for an actor placed in a level,
 * an Automatic ID for one spawned while playing or a component added while playing, and a Component ID (its actor's
 * ID and its name) for a component made by its actor's class, Blueprint or level (Bango::EBangoEntityIDKind).
 *
 * Entity values only store the name. They are resolved through FindEntity() each time they're used,
 * so saved scripts never hold pointers.
 */
UCLASS()
class BANGOSCRIPTS_API UBangoActorRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The registry for WorldContext's world, or nullptr. */
	static UBangoActorRegistry* Get(const UObject* WorldContext);

	/**
	 * Registers Name for Actor: a Bango ID component's name, or the Automatic ID it holds. Fails if the name isn't one
	 * of those, or is already used by another live actor. An actor can have several (an old ID keeps working); the one
	 * GetOrMakeEntityID gives is its Bango ID name if it has one. An actor that already had a Level or Automatic ID
	 * and is then given a Bango ID name is an error, logged, but both work, and the name is used from then on.
	 */
	bool Register(const FString& Name, AActor* Actor, FString& OutError);

	/** Removes Name, if it is registered to Object. Entity values naming it stay; using them afterwards fails to find it. */
	void Unregister(const FString& Name, const UObject* Object);

	/**
	 * The actor or actor component an entity name stands for, or nullptr. Names are case-sensitive. The `player`
	 * entity's name (Bango::GetPlayerEntityName) finds the first local player's pawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	UObject* FindEntity(const FString& Name) const;

	/** FindEntity, if it's an actor (nullptr for a component). */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	AActor* FindActor(const FString& Name) const;

	/**
	 * The entity name scripts use for Object, an actor or an actor component, made now if it has none.
	 * - An actor: its Bango ID name; else, for one placed in a level, its Level ID; else, for one spawned while
	 *   playing, a new Automatic ID, kept on a Bango ID component added to it (so a save system that saves its
	 *   components keeps it). The `player` entity for the player's pawn without a Bango ID name.
	 * - A component: the ID it keeps, if it's an IBangoEntity that has one; else, for one whose name is the same each
	 *   time (made by its actor's class, Blueprint or level), a Component ID: its actor's ID (made now if need be) and
	 *   its name; else (added while playing), a new Automatic ID, kept on it if it's an IBangoEntity.
	 * Empty (OutError) for null, anything else (only actors and actor components can be entities), one that isn't in
	 * this world, or an actor whose Bango ID component's name couldn't be registered (invalid, or another actor's).
	 */
	FString GetOrMakeEntityID(const UObject* Object, FString& OutError);

	/** The first local player's pawn (what a script's `player` is), or nullptr if there's none right now. */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	APawn* GetPlayerPawn() const;

	/**
	 * A number that goes up whenever a name is added or removed. While it's the same, an actor's entity name is the
	 * same too (but for the player's pawn, which can become another actor without anything changing here).
	 */
	uint64 GetVersion() const { return Version; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FString GetOrMakeActorID(const AActor* Actor, FString& OutError);
	FString GetOrMakeComponentID(const UActorComponent* Component, FString& OutError);
	/** A Level ID's actor, found by its path in this world (with this world's Play in Editor prefix), or null. */
	AActor* FindLevelActor(const FString& Path) const;
	/**
	 * Adds Name for Object, and makes it Object's entity name if it has none, or it's a Bango ID name over another
	 * kind. Const for FindEntity, which remembers what it finds.
	 */
	void Add(const FString& Name, UObject* Object) const;
	/** Whether Name is registered to a live object other than Object. */
	bool IsTakenByAnother(const FString& Name, const UObject* Object) const;

	// Mutable: FindEntity remembers the Level and Component IDs it finds.
	mutable TBangoNameMap<TWeakObjectPtr<UObject>> Objects;
	/** Each actor's and component's entity name (GetOrMakeEntityID). */
	mutable TMap<TWeakObjectPtr<const UObject>, FString> EntityIDs;
	/** GetVersion's. Mutable as the maps are. */
	mutable uint64 Version = 1;
};

namespace Bango
{
	/**
	 * The entity value for Actor, for passing to scripts (UBangoActorRegistry::GetOrMakeEntityID), or nil if Actor is
	 * null. Any actor in a game world can be one. Fails only for an actor whose Bango ID component's name couldn't be
	 * registered (invalid or duplicate), or one outside a game world.
	 */
	BANGOSCRIPTS_API bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError);

	/**
	 * The entity value for Object, an actor or an actor component (UBangoActorRegistry::GetOrMakeEntityID), or nil if
	 * Object is null. Fails for anything else, or one outside a game world.
	 */
	BANGOSCRIPTS_API bool ObjectToEntity(const UObject* Object, FBangoValue& OutValue, FString& OutError);

	/**
	 * Registers an IBangoEntity component's ID now (making one if it has none), so it's found by it before anything
	 * asks for it: call it in its InitializeComponent (with bWantsInitializeComponent set), which runs before any
	 * actor's BeginPlay; its own BeginPlay can be too late for a script another actor's BeginPlay starts. Does
	 * nothing outside a game world. Logs an error if it can't be.
	 */
	BANGOSCRIPTS_API void RegisterEntity(const UObject* Object);
}
