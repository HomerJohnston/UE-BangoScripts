#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BangoIDName.h"
#include "BangoIDActorComponent.generated.h"

/**
 * Gives its actor a friendly name that Bango scripts can use, written with an '@': `@john Speak("Hello.");`
 *
 * On BeginPlay the name is registered with UBangoActorRegistry, or earlier, when the world begins play, if the level
 * has a start script (UBangoLevelStartScript), so the start script can use it. Any problem (an empty, invalid or
 * duplicate name) is shown on screen as an error, once, and the name is not registered.
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

	/** True if BangoName was registered successfully. */
	UFUNCTION(BlueprintPure, Category = "Bango")
	bool IsRegistered() const { return bRegistered; }

	/** Registers BangoName, if that hasn't been tried since it last began play. BeginPlay calls it. */
	void RegisterName();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bRegistered = false;
	/** RegisterName has run (and reported any problem), so it isn't tried again until EndPlay. */
	bool bTriedRegistering = false;
};
