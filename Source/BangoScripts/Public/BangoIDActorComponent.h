#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BangoIDActorComponent.generated.h"

/**
 * Gives its actor a friendly name that Bango scripts can use, written with an '@': `@john Speak("Hello.");`
 *
 * On BeginPlay the name is registered with UBangoActorRegistry. Any problem (an empty, invalid or duplicate
 * name) is shown on screen as an error, and the name is not registered.
 */
UCLASS(ClassGroup = (Bango), meta = (BlueprintSpawnableComponent, DisplayName = "Bango ID"))
class BANGOSCRIPTS_API UBangoIDActorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBangoIDActorComponent();

	/** The name scripts use for this actor, as @Name. Case-sensitive. Letters, digits and '_', not starting with a digit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bango")
	FString BangoName;

	/** True if BangoName was registered successfully. */
	UFUNCTION(BlueprintPure, Category = "Bango")
	bool IsRegistered() const { return bRegistered; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bRegistered = false;
};
