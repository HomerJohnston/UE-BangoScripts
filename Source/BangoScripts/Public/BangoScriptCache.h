#pragma once

#include "CoreMinimal.h"
#include "BangoCommand.h"
#include "BangoProgram.h"
#include "BangoScriptPath.h"

class UBangoScriptAsset;

/**
 * Compiles Bango Script assets (Bango spec section 15) on first use. A compiled script is reused until its asset's
 * text changes (in the editor: saved in the graph editor, or its linked .bango file changed), then it's recompiled.
 */
class BANGOSCRIPTS_API FBangoScriptCache
{
public:
	/**
	 * Gets the compiled program for the script at ScriptPath: a full asset path ("/Game/Quests/Intro") or one relative
	 * to /Game ("Quests/Intro"); see Bango::ResolveScriptPath. Its asset is loaded if it isn't (a synchronous load).
	 * OutDisplayPath is its name as errors and saves show it ("/Game/Quests/Intro.bango").
	 * Returns false with OutError set if the path is invalid, there's no Bango Script asset there, or it doesn't compile.
	 */
	bool GetProgram(const FString& ScriptPath, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError);

	/** GetProgram, for a script asset already in hand. */
	bool GetProgram(const UBangoScriptAsset& Asset, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError);

	/** Forgets every compiled script, so each is recompiled on next use. */
	void Clear();

private:
	struct FEntry
	{
		TSharedPtr<const FBangoProgram> Program;
		/** The text it was compiled from: the asset's text being different means it changed. */
		FString Source;
	};
	/** By script name ("/Game/Quests/Intro"). */
	TMap<FString, FEntry> Entries;
};
