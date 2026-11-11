#pragma once

#include "CoreMinimal.h"
#include "BangoAST.h"
#include "BangoCommand.h"
#include "BangoParser.h"
#include "BangoProgram.h"

namespace Bango
{
	/**
	 * Checks a parsed script and compiles it to bytecode.
	 *
	 * Checks types, scopes, globals, functions, labels and goto, break/continue, returns,
	 * and every call against its function or command signature.
	 * Stops at the first error: returns false and fills OutError. OutProgram must not be used then.
	 */
	BANGOSCRIPTS_API bool CompileScript(const FBangoScript& Script, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError);

	/** Parses and compiles in one step. */
	BANGOSCRIPTS_API bool CompileSource(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError);
}
