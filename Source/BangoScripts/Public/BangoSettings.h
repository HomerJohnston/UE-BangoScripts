#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BangoSettings.generated.h"

/**
 * A define (spec section 4, Defines): a named value every script can use and none can change, like `SELECT_RANDOM`
 * for a dialogue plugin's `int runMode = 3`. A use compiles to the value, so it works wherever a fixed value does: in
 * a `case`, a global's default, a script variable's start. Shown on one row in Project Settings, with the script
 * editor's type picker and value box (BangoScriptsScriptEditor).
 */
USTRUCT()
struct BANGOSCRIPTS_API FBangoDefineSetting
{
	GENERATED_BODY()

	/** SHOUTING_SNAKE_CASE: capital letters, digits and '_', starting with a letter (`SELECT_RANDOM`, `MAX_PARTY_2`). */
	UPROPERTY(EditAnywhere, Category = "Define")
	FString Name = TEXT("NEW_DEFINE");

	/** Its type, as scripts write it: "int", "string", "entity", "int[]" (Bango::GetValueTypeNames). */
	UPROPERTY(EditAnywhere, Category = "Define")
	FString Type = TEXT("int");

	/**
	 * Its value as Bango text, as a script variable's starting value is written (`3`, `"north"`, `@narrator`,
	 * `[1, 2]`); empty is nil. It may use the defines above it. A text is written INVTEXT("..."): a define isn't
	 * gathered for translation.
	 */
	UPROPERTY(EditAnywhere, Category = "Define")
	FString Value = TEXT("0");

	/** Optional: groups it in autocomplete and the editors' lists ("Dialogue"). */
	UPROPERTY(EditAnywhere, Category = "Define")
	FString Category;

	/** Optional: shown with it in autocomplete and the editors' lists. */
	UPROPERTY(EditAnywhere, Category = "Define", meta = (MultiLine = true))
	FString Description;
};

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

	/**
	 * How often a watched condition (FBangoInlineCondition, UBangoScriptSubsystem::CreateWatchGroup) is checked, in
	 * seconds, when its answer can change without a global or input changing (it calls a command, or uses `call`), and
	 * its own Poll Interval isn't set. 0: every frame.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Scripts", meta = (ClampMin = "0", Units = "s"))
	float DefaultConditionPollInterval = 0.5f;

	/** Deepest allowed nesting of script function calls. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "8"))
	int32 MaxCallDepth = 200;

	/**
	 * Large Script Threshold: when nodes are moved in the graph editor in a script with at least this many nodes, the
	 * script gets a `#USES_GRAPH_EDITOR` line, and from then on it's saved with section headings (// ==== ...) and
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

	/**
	 * Named values every script can use by name and none can change (spec section 4, Defines): `SELECT_RANDOM = 3`
	 * lets scripts write `SELECT_RANDOM` instead of `3`. Scripts that use one are compiled again when it changes. A row
	 * that can't be a define is left out, and says why.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Defines", meta = (TitleProperty = "Name"))
	TArray<FBangoDefineSetting> Defines;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
};

namespace Bango
{
	/**
	 * Gives the define table (Bango::GetDefines) the project's defines, UBangoSettings::Defines. Done when the module
	 * starts and when they're changed in Project Settings; a row that can't be one is logged and left out.
	 */
	BANGOSCRIPTS_API void ApplyDefineSettings();
}
