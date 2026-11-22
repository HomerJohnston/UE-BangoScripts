#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BangoIDName.h"
#include "BangoIDActorComponent.generated.h"

/**
 * Gives its actor a friendly name that Bango scripts can use, written with an '@': `@john Speak("Hello.");`
 *
 * Or, added by Bango to an actor spawned while playing the first time it becomes an entity, it holds that actor's
 * Automatic ID instead (AutomaticID; spec section 4, Bango IDs), so a save system that saves and respawns actors with
 * their components keeps the actor's ID.
 *
 * The name is registered with UBangoActorRegistry when the component is initialized for play (InitializeComponent),
 * which Unreal does for every actor of a level before it calls any of their BeginPlays, and for a spawned actor
 * before its own. So a script started from any BeginPlay, or the level's start script, finds every Bango ID of the
 * actors that are there, whichever actor begins play first. Any problem (an empty, invalid or duplicate name) is
 * shown on screen as an error, once, and the name is not registered.
 */
UCLASS(ClassGroup = (Bango), meta = (BlueprintSpawnableComponent, DisplayName = "Bango ID"))
class BANGOSCRIPTS_API UBangoIDActorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBangoIDActorComponent();

	/**
	 * The name scripts use for this actor, as @Name. Case-sensitive. Letters, digits and '_', not starting with a digit.
	 * An FBangoIDName: an FString that the details panel shows after an '@'.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bango")
	FBangoIDName BangoName;

	/**
	 * An Automatic ID ('$' and 32 hex digits), given by Bango to an actor spawned while playing, with no BangoName,
	 * the first time it became an entity. Not shown to people (they see the actor's name). Saved with the component
	 * (and by save systems that save SaveGame properties).
	 */
	UPROPERTY(VisibleInstanceOnly, AdvancedDisplay, SaveGame, Category = "Bango")
	FString AutomaticID;

	/** True if its BangoName or its AutomaticID was registered successfully. */
	UFUNCTION(BlueprintPure, Category = "Bango")
	bool IsRegistered() const { return bRegistered; }

	/**
	 * Registers BangoName and AutomaticID, if that hasn't been tried since it was last unregistered.
	 * InitializeComponent calls it (and BeginPlay, for a component that wasn't initialized first).
	 */
	void RegisterName();

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Takes back what RegisterName registered, so it can be tried again. */
	void UnregisterName();

	bool bRegistered = false;
	/** What was registered, to unregister when it ends play or is uninitialized. */
	TArray<FString> RegisteredNames;
	/** RegisterName has run (and reported any problem), so it isn't tried again until UnregisterName. */
	bool bTriedRegistering = false;
};
