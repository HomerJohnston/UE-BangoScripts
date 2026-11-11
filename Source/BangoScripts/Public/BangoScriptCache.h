#pragma once

#include "CoreMinimal.h"
#include "BangoCommand.h"
#include "BangoProgram.h"

/**
 * Loads .bango files from Content/<ScriptDirectory> and compiles them on first use.
 * A compiled script is reused until its file changes on disk, then it is recompiled.
 */
class BANGOSCRIPTS_API FBangoScriptCache
{
public:
	/**
	 * Gets the compiled program for ScriptPath ("Quest/Intro" or "Quest/Intro.bango").
	 * OutDisplayPath is the normalized path used in errors and saves ("Quest/Intro.bango").
	 * Returns false with OutError set if the path is invalid, the file is missing, or it doesn't compile.
	 */
	bool GetProgram(const FString& ScriptPath, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError);

	/** Forgets every compiled script, so each is recompiled on next use. */
	void Clear();

private:
	struct FEntry
	{
		TSharedPtr<const FBangoProgram> Program;
		FDateTime TimeStamp;
	};
	TMap<FString, FEntry> Entries;
};

namespace Bango
{
	/**
	 * Turns a script path into its canonical form: forward slashes, no leading slash, ".bango" added if missing.
	 * Rejects empty paths, absolute paths, drive letters and ".." segments.
	 */
	BANGOSCRIPTS_API bool NormalizeScriptPath(const FString& ScriptPath, FString& OutPath, FString& OutError);

	/** The full file path for a normalized script path. */
	BANGOSCRIPTS_API FString GetScriptFilePath(const FString& NormalizedPath);
}
