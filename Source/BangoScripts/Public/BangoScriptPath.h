#pragma once

#include "CoreMinimal.h"

/**
 * Script names (Bango spec section 15). A script is a Bango Script asset, named by its asset path: "/Game/Quests/Intro".
 * A script path can be full ("/Game/Quests/Intro") or relative to a folder ("Intro", "../Shared/Greet"): from a script
 * (Exec) it's relative to that script's folder; from Blueprint and C++ it's relative to /Game.
 */
namespace Bango
{
	/**
	 * Turns ScriptPath into a full asset path, like "/Game/Quests/Intro". A path that starts with "/" is full; any
	 * other is relative to RelativeToFolder (like "/Game/Quests", or "/Game" from Blueprint and C++).
	 *
	 * Backslashes work as slashes and doubled slashes are ignored; a trailing ".bango" is dropped, and so is a repeated
	 * asset name (an object path, "/Game/Quests/Intro.Intro"). "." folders are skipped; ".." goes up one, but not above
	 * the content root (the first folder, like /Game). Returns false, with OutError, for an empty path, one with a ':'
	 * (a drive letter), one that goes above its content root, or one without a folder and a name.
	 *
	 * It doesn't check that the asset exists, or that the names are allowed in asset names.
	 */
	BANGOSCRIPTS_API bool ResolveScriptPath(const FString& ScriptPath, const FString& RelativeToFolder, FString& OutPackagePath, FString& OutError);

	/** The folder a script is in, for paths relative to it: "/Game/Quests" for "/Game/Quests/Intro" (or ".../Intro.bango"). */
	BANGOSCRIPTS_API FString GetScriptPathFolder(const FString& ScriptName);

	/** The folder Blueprint and C++ script paths are relative to. */
	inline const TCHAR* DefaultScriptFolder() { return TEXT("/Game"); }
}
