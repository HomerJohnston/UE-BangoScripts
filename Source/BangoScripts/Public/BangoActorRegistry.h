#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoContainers.h"
#include "BangoActorRegistry.generated.h"

class AActor;
class APawn;
class FBangoValue;

/**
 * Maps entity names to actors for one game world (spec section 4, Bango IDs).
 *
 * Scripts name an actor with an entity literal: `@john Speak("Hi");`. Bango IDs are their own namespace:
 * they don't create globals, so they can't clash with them. Any other actor gets an entity name of its own the first
 * time it becomes an entity (GetOrMakeEntityID): a Level ID for one placed in a level, an Automatic ID for one
 * spawned while playing (Bango::EBangoEntityIDKind).
 *
 * Entity values only store the name. They are resolved through FindActor() each time they're used,
 * so saved scripts never hold actor pointers.
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

	/** Removes Name, if it is registered to Actor. Entity values naming it stay; using them afterwards fails to find the actor. */
	void Unregister(const FString& Name, const AActor* Actor);

	/**
	 * The actor registered under Name, or nullptr. Names are case-sensitive. The `player` entity's name
	 * (Bango::GetPlayerEntityName) finds the first local player's pawn instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	AActor* FindActor(const FString& Name) const;

	/**
	 * The entity name scripts use for Actor, made now if it has none: its Bango ID name; else, for an actor placed in a
	 * level, its Level ID; else, for one spawned while playing, a new Automatic ID, kept on a Bango ID component added
	 * to it (so a save system that saves its components keeps it). The `player` entity for the player's pawn without a
	 * Bango ID name. Empty (OutError) for a null actor, one that isn't in this world, or one whose Bango ID component's
	 * name couldn't be registered (invalid, or another actor's).
	 */
	FString GetOrMakeEntityID(const AActor* Actor, FString& OutError);

	/** The first local player's pawn (what a script's `player` is), or nullptr if there's none right now. */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	APawn* GetPlayerPawn() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** A Level ID's actor, found by its path in this world (with this world's Play in Editor prefix), or null. */
	AActor* FindLevelActor(const FString& Path) const;
	/** Adds Name for Actor, and makes it Actor's entity name if it has none, or it's a Bango ID name over another kind. */
	void Add(const FString& Name, AActor* Actor);

	// Mutable: FindActor remembers the Level IDs it finds.
	mutable TBangoNameMap<TWeakObjectPtr<AActor>> Actors;
	/** Each actor's entity name (GetOrMakeEntityID). */
	mutable TMap<TWeakObjectPtr<const AActor>, FString> EntityIDs;
};

namespace Bango
{
	/**
	 * The entity value for Actor, for passing to scripts (UBangoActorRegistry::GetOrMakeEntityID), or nil if Actor is
	 * null. Any actor in a game world can be one. Fails only for an actor whose Bango ID component's name couldn't be
	 * registered (invalid or duplicate), or one outside a game world.
	 */
	BANGOSCRIPTS_API bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError);
}
