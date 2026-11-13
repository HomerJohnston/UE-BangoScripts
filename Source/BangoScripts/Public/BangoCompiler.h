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

	/**
	 * Compiles a condition: a script whose main graph returns true or false (`return _count > 3;`), and must return one
	 * of them. Or Source is a single expression, like `foundThings >= 10 && !IsNull(@door)`, on one line (with any
	 * #define lines and comments around it), which is compiled as `return <expression>;` (errors still point into the
	 * expression as written). Everything else is as for any script: #define params, globals, locals, functions.
	 * See UBangoScriptSubsystem::EvaluateBoolCondition.
	 */
	BANGOSCRIPTS_API bool CompileCondition(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError);
}
