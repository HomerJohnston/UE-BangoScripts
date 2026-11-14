#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BangoSettings.generated.h"

/** Project Settings > Plugins > Bango. Saved in DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Bango"))
class BANGOSCRIPTS_API UBangoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBangoSettings();

	/**
	 * In the editor (Play in Editor), when a running script's asset changes (saved in the graph editor, or its linked
	 * .bango file changed), its running copies switch to the new
	 * version where they are, without starting over (hot reload). A copy that can't switch carries on with the version
	 * it has, and the output log says why. Without this, only scripts spawned after the save use the new version.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Scripts")
	bool bReloadRunningScripts = true;

	/** A script that runs more instructions than this in one frame without pausing is stopped as an infinite loop. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1000"))
	int32 MaxInstructionsPerFrame = 1000000;

	/** Deepest allowed nesting of script function calls. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "8"))
	int32 MaxCallDepth = 200;

	/** How long `Hint` messages stay on screen when nothing is bound to the Bango script subsystem's OnHint event. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (ClampMin = "0.5"))
	float HintDisplaySeconds = 5.0f;
};
