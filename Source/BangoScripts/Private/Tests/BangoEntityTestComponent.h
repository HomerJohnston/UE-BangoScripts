#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "BangoEntity.h"
#include "BangoEntityTestComponent.generated.h"

/** For the automation tests of component entities (Bango.Game.EntityIDs): a scene component that keeps its own entity ID. */
UCLASS(Transient, HideDropdown, NotBlueprintable)
class UBangoEntityTestComponent : public USceneComponent, public IBangoEntity
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	FString EntityID;

	/** For `call` on a component. */
	UPROPERTY()
	int32 Pokes = 0;

	UFUNCTION(BlueprintCallable, Category = "Bango")
	void Poke(int32 Times) { Pokes += Times; }

	virtual FString GetBangoEntityID() const override { return EntityID; }
	virtual void SetBangoEntityID(const FString& ID) override { EntityID = ID; }
};

/** For the same tests: a component with no place in the world, that keeps no ID of its own. */
UCLASS(Transient, HideDropdown, NotBlueprintable)
class UBangoPlainTestComponent : public UActorComponent
{
	GENERATED_BODY()
};
