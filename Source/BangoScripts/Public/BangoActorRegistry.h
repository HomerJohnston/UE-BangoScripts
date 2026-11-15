#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoContainers.h"
#include "BangoActorRegistry.generated.h"

class AActor;
class APawn;
class FBangoValue;

/**
 * Maps Bango ID names to actors for one game world.
 *
 * Scripts name an actor with an entity literal: `@john Speak("Hi");`. Bango IDs are their own namespace:
 * they don't create globals, so they can't clash with them.
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

	/** Registers Name for Actor. Fails if the name isn't an identifier, or is already used by another live actor. */
	bool Register(const FString& Name, AActor* Actor, FString& OutError);

	/** Removes Name, if it is registered to Actor. Entity values naming it stay; using them afterwards fails to find the actor. */
	void Unregister(const FString& Name, const AActor* Actor);

	/**
	 * The actor registered under Name, or nullptr. Names are case-sensitive. The `player` entity's name
	 * (Bango::GetPlayerEntityName) finds the first local player's pawn instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	AActor* FindActor(const FString& Name) const;

	/** The first local player's pawn (what a script's `player` is), or nullptr if there's none right now. */
	UFUNCTION(BlueprintCallable, Category = "Bango")
	APawn* GetPlayerPawn() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TBangoNameMap<TWeakObjectPtr<AActor>> Actors;
};

namespace Bango
{
	/**
	 * The entity value for Actor, for passing to scripts: its Bango ID name, or nil if Actor is null. The first local
	 * player's pawn, without a registered Bango ID, is the `player` entity. Fails if any other actor has no Bango ID
	 * component, or its name isn't registered (empty, invalid or duplicate).
	 */
	BANGOSCRIPTS_API bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError);
}
