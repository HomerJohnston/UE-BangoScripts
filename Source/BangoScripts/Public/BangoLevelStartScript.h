#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "BangoLevelStartScript.generated.h"

class UBangoScriptAsset;
class UWorld;

/**
 * The level's start script (spec section 15, Level start scripts): kept on the main level's World Settings as Asset
 * User Data, and run by UBangoScriptSubsystem when the world begins play, before any actor's BeginPlay, so before
 * anything else can start a script. It must finish straight away (it can't wait; it can spawn blocks that do), and
 * has no `self`. The editor's Bango toolbar menu opens, chooses and removes it.
 */
UCLASS(meta = (DisplayName = "Bango Level Start Script"))
class BANGOSCRIPTS_API UBangoLevelStartScript : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** The script to run when the level starts. */
	UPROPERTY(EditAnywhere, Category = "Bango")
	TObjectPtr<UBangoScriptAsset> Script;

	/** World's start script data (on its main level's World Settings), or nullptr. */
	static UBangoLevelStartScript* Find(const UWorld* World);
};
