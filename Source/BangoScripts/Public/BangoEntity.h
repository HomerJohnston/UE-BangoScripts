#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "BangoEntity.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UBangoEntity : public UInterface
{
	GENERATED_BODY()
};

/**
 * A component that keeps its own entity ID (spec section 4, Entity IDs), for one added to its actor while playing.
 *
 * A component made by its actor's class, Blueprint or level is an entity by its actor's ID and its name, which are the
 * same each time. One added while playing has no such name: it gets an Automatic ID (a random GUID) the first time
 * it becomes an entity. Implementing this keeps that ID on the component, in a property of its own: make it a
 * `UPROPERTY(SaveGame)` FString, and a save system that saves the component (and adds it again on loading) keeps it,
 * so saved scripts that held it find it again. Call Bango::RegisterEntity(this) in InitializeComponent (with
 * bWantsInitializeComponent set, and only in a game world), so it's found by that ID before anything asks for it:
 * a script started from another actor's BeginPlay, say, which can run before this component's own BeginPlay.
 *
 * The ID it keeps, once it has one, is always the one used. One made by its actor's class or level, with none kept,
 * uses its name (and is never given one).
 */
class BANGOSCRIPTS_API IBangoEntity
{
	GENERATED_BODY()

public:
	/** The ID kept, or empty if it has none yet. */
	virtual FString GetBangoEntityID() const = 0;

	/** Keeps ID (an Automatic ID Bango made for it). */
	virtual void SetBangoEntityID(const FString& ID) = 0;
};
