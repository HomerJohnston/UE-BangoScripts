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
	BANGOSCRIPTS_API bool CompileScript(const FBangoScript& Script, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature = nullptr);

	/**
	 * Parses and compiles in one step. With a Signature (an inline script's, FBangoInlineScript::UseInputsSignature),
	 * its inputs are the script's parameters, and an #inputs line is an error.
	 */
	BANGOSCRIPTS_API bool CompileSource(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature = nullptr);

	/**
	 * Compiles a condition: a script whose main graph returns true or false (`return _count > 3;`), and must return one
	 * of them. Or Source is a single expression, like `foundThings >= 10 && !IsNull(@door)`, on one line (with any
	 * settings lines and comments around it), which is compiled as `return <expression>;` (errors still point into the
	 * expression as written). Everything else is as for any script: #inputs (or a Signature, as for CompileSource),
	 * globals, locals, functions. See UBangoScriptSubsystem::EvaluateCondition.
	 */
	BANGOSCRIPTS_API bool CompileCondition(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature = nullptr);

	/**
	 * Reads an inputs signature's inputs, each written like one in an #inputs line but with no default (`entity _who`).
	 * False, with OutError, if one doesn't read, has a default, or two have the same name. A name that doesn't start
	 * with '_' still works, with a warning in OutWarnings. OutSignature's Label is left for the caller to set.
	 */
	BANGOSCRIPTS_API bool ParseInputsSignature(const TArray<FBangoInputDecl>& Inputs, FBangoInputsSignature& OutSignature, FString& OutError, TArray<FString>& OutWarnings);

	/**
	 * If Source is a condition written as a single expression (as CompileCondition reads it), OutScript is it written as
	 * a script: the expression as `return <expression>;`, with everything around it (settings lines, comments) kept. That's
	 * what CompileCondition compiles, and what the graph editor can show. False, and OutScript untouched, otherwise.
	 */
	BANGOSCRIPTS_API bool ConditionExpressionToScript(const FString& Source, FString& OutScript);
}
