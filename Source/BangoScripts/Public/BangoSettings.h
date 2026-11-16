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

	/**
	 * Large Script Threshold: when nodes are moved in the graph editor in a script with at least this many nodes, the
	 * script gets a `#set USES_GRAPH_EDITOR` line, and from then on it's saved with section headings (// ==== ...) and
	 * node metadata (//@), so its layout is kept. Until then it's saved compact, so it reads like plain code, and its
	 * nodes are laid out afresh each time it's opened. 0: moving any node adds the line.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor", meta = (ClampMin = "0"))
	int32 LargeScriptThreshold = 3;

	/**
	 * Bango ID labels in the level editor's viewport (Show > Bango IDs): beyond this distance from the camera, a label
	 * starts to fade out...
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Editor", meta = (ClampMin = "0", Units = "cm"))
	float IDLabelFadeDistance = 7500.0f;

	/** ...and from this distance on, it isn't drawn. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor", meta = (ClampMin = "0", Units = "cm"))
	float IDLabelHideDistance = 10000.0f;

	/** How long a `WriteScreen` message stays on screen when the script doesn't say. */
	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (ClampMin = "0.5"))
	float WriteScreenSeconds = 5.0f;
};
