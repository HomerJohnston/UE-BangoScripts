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
	 * Parses and compiles in one step. With a Signature (an inline script's, FBangoInlineScriptBase::UseInputsSignature),
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
	 * Reads a fixed value: Text checked as a fixed value of the type TypeName ("vector3", "entity[]", "text", ...),
	 * exactly as a script variable's starting value or a global's default is (spec section 4), and its value. A fixed
	 * value is a number (or a negative one), a string, true, false, nil, a Bango ID (@john), a text, or an array of
	 * those ([1, 2], and [0, 0, 100] for a vector), written as in a script. Empty Text (or only spaces) is nil.
	 *
	 * False, with a message for the user, if TypeName isn't a type or Text isn't a fixed value of it. A Bango ID
	 * isn't looked up: whether an actor has it is only known in a level, when it's used. A text is written
	 * TEXT_INV("...") or TEXT_NS("Namespace", "Key", "..."): a plain TEXT has no script here to take its namespace
	 * from. Works in packaged games (it needs no editor).
	 */
	BANGOSCRIPTS_API bool ParseFixedValue(const FString& TypeName, const FString& Text, FBangoValue& OutValue, FString& OutError);

	/**
	 * The types a value can have, for a type menu, in the order the script editor shows them: bool, int, float,
	 * string, entity, text, vector2, vector3, then an array of each (bool[], ...). Deeper arrays (int[][]) are types
	 * too (FBangoType::FromString reads them), but aren't listed.
	 */
	BANGOSCRIPTS_API const TArray<FString>& GetValueTypeNames();

	/**
	 * Reads an inputs signature's inputs, each written like one in an #inputs line but with no default (`entity _who`).
	 * False, with OutError, if one doesn't read, has a default, or two have the same name. A name that doesn't start
	 * with '_' still works, with a warning in OutWarnings. OutSignature's Label is left for the caller to set.
	 */
	BANGOSCRIPTS_API bool ParseInputsSignature(const TArray<FBangoInputDecl>& Inputs, FBangoInputsSignature& OutSignature, FString& OutError, TArray<FString>& OutWarnings);

	/**
	 * The inputs an inline script is given: a registered signature's (Signature; null for none), then an object's own
	 * instance inputs (FBangoInlineScriptBase::SetInstanceInputs), written like a signature's. OutInputs gets them all,
	 * in that order, with its labels and NumInstanceInputs. False, with OutError, if an instance input doesn't read,
	 * has a default, or has the name of another one or of one of the signature's. A name that doesn't start with '_'
	 * still works, with a warning in OutWarnings.
	 */
	BANGOSCRIPTS_API bool AddInstanceInputs(const FBangoInputsSignature* Signature, const TArray<FBangoInputDecl>& InstanceInputs, FBangoInputsSignature& OutInputs, FString& OutError,
		TArray<FString>& OutWarnings);

	/**
	 * If Source is a condition written as a single expression (as CompileCondition reads it), OutScript is it written as
	 * a script: the expression as `return <expression>;`, with everything around it (settings lines, comments) kept. That's
	 * what CompileCondition compiles, and what the graph editor can show. False, and OutScript untouched, otherwise.
	 */
	BANGOSCRIPTS_API bool ConditionExpressionToScript(const FString& Source, FString& OutScript);
}
