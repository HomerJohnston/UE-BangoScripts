#pragma once

#include "CoreMinimal.h"
#include "BangoAST.h"

/** A compile error, pointing at the line (and graph node, if the line has metadata) that caused it. */
struct BANGOSCRIPTS_API FBangoCompileError
{
	FString FileName;
	int32 Line = 0;
	int32 Column = 0;
	/** -1 if the line has no node metadata. */
	int64 NodeID = -1;
	FString Message;

	/** "Quest/Intro.bango(12:5) [node 7]: Expected ';'" */
	FString ToString() const;
};

namespace Bango
{
	/**
	 * Parses Source into OutScript.
	 * Stops at the first error: returns false and fills OutError. OutScript is then incomplete and must not be used.
	 */
	BANGOSCRIPTS_API bool ParseScript(const FString& Source, const FString& FileName, FBangoScript& OutScript, FBangoCompileError& OutError);
}
