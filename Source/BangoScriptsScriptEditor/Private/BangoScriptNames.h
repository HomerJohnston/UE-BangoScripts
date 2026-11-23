// Bango text editor: which names are locals, parameters and globals, from the whole file.
// See "Bango Text Editor Spec", section 2, Meaning layer.
//
// A name is the graph's local, parameter or loop variable of that name if there is one, and otherwise a global
// (Bango Language Spec, section 4). One line can't tell, so the tokenizer calls every name a Global; this reads the
// declarations of the whole file, which are all in fixed places:
//  - globals: `global <type> name;`, in one block at the top of the file;
//  - the main graph's parameters: `#inputs (<type> name, ...)`;
//  - a function's parameters: its header, `function <type> Name(<type> name, ...)`, which may span several lines;
//  - locals and script variables: `<type> name ...;` and `scriptVar <type> name ...;`, visible from their line to the
//    `}` that closes their block (C-style block scope; a graph's top level runs to the end of the graph);
//  - loop variables: `for (<type> name = ...` and `forEach (name in ...)`, visible in their loop's header and block;
//  - a spawn's declared id: `int _t = spawn`, visible from the `}` that closes the spawn's block.
// A function's graph is the lines from its `function` line to the `};` that closes its body. A function also sees the
// main graph's parameters and script variables (not its locals), and can't declare anything with one's name.
// Brace matching is by line tokens only, so text being typed with unbalanced braces just gives looser scopes.
//
// Depends only on FString, TArray and the line tokenizer, so it can be unit-tested without Slate.

#pragma once

#include "CoreMinimal.h"
#include "BangoLineTokenizer.h"

/** A name a line can use, for autocomplete: a parameter, local or loop variable of its graph, a global, or a function. */
struct FBangoVisibleName
{
	FString Name;

	/** The declared type (`int`, `string[]`), or empty if unknown (a forEach loop's variable). For a function, its signature. */
	FString Type;

	/** Parameter, Local, Global or Function. */
	EBangoSyntaxKind Kind = EBangoSyntaxKind::Local;

	/** For an input given by an inputs signature: what it is (may be empty). */
	FString Tooltip;
};

struct FBangoInputsSignature;

namespace BangoTextEditor
{
	/** An inputs signature's inputs, for FBangoScriptNames::SetGivenInputs. */
	TArray<FBangoVisibleName> GivenInputs(const FBangoInputsSignature& Signature);
}

class FBangoScriptNames
{
public:
	/** Reads the declarations of a whole file: its lines, and each line's tokens from FBangoLineTokenizer. */
	void Build(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens);

	/**
	 * An inline script's inputs given by its inputs signature (FBangoInlineScriptBase::UseInputsSignature): the main
	 * graph's parameters, as if an #inputs line listed them. Each one's Name, Type and Tooltip. Used by the next Build.
	 */
	void SetGivenInputs(const TArray<FBangoVisibleName>& Inputs) { GivenInputs = Inputs; }

	/**
	 * What a name on a line is: Parameter (in a function, the script's too), Local (a local, script variable or loop
	 * variable visible on that line; in a function, the script's script variables too),
	 * Global (declared in this file), or UndeclaredGlobal (used here, declared elsewhere, a misspelling, or a local
	 * used outside its block).
	 */
	EBangoSyntaxKind Classify(int32 LineIndex, const FString& Name) const;

	/**
	 * Which variable a name on a line is, as a key: two uses of a name are the same variable if their keys are equal.
	 * A parameter of the line's graph, the local visible there (or declared there: a spawn's id is declared on the
	 * spawn's line, but visible only after its block), or else the global of that name. For highlighting a variable's
	 * other uses.
	 */
	FString GetVariableKey(int32 LineIndex, const FString& Name) const;

	/** The graph a line belongs to: 0 for the main graph, then one per function in file order. */
	int32 GetLineGraph(int32 LineIndex) const;

	/** A graph's function name, or empty for the main graph (or a function whose name hasn't been typed yet). */
	FString GetGraphFunctionName(int32 Graph) const;

	/** Whether the line is its graph's entry: the `#inputs` line, or a function's `function` line. */
	bool IsEntryLine(int32 LineIndex) const;

	/** The entry line of the main graph (FunctionName empty) or of the function with that name, or INDEX_NONE. */
	int32 FindEntryLine(const FString& FunctionName) const;

	/**
	 * The names a line can use: its graph's parameters and the locals visible on it (script variables and loop
	 * variables included; of two visible with one name, the inner one), in a function the script's parameters and script
	 * variables, the file's globals that aren't hidden by them, and the file's functions. In that order.
	 */
	void GetVisibleNames(int32 LineIndex, TArray<FBangoVisibleName>& OutNames) const;

	/** Gives each Global token on a line its kind from Classify. */
	void RefineTokens(int32 LineIndex, const FString& Line, TArray<FBangoSyntaxToken>& InOutTokens) const;

private:
	struct FGraphNames
	{
		TArray<FString> Params;
		TArray<FString> ParamTypes;
		/** By index, for the given inputs (SetGivenInputs), which come first. */
		TArray<FString> ParamTooltips;
		FString FunctionName;
		FString ReturnType;
		int32 EntryLine = INDEX_NONE;
	};

	static bool Contains(const TArray<FString>& Names, const FString& Name);
	static void AddUnique(TArray<FString>& Names, const FString& Name);
	static void AddUnique(TArray<FString>& Names, TArray<FString>& Types, const FString& Name, const FString& Type);

	/** Graph 0 is the main graph; then one per function, in file order. */
	TArray<FGraphNames> Graphs;

	/** A local, script variable or loop variable, and the lines it's visible on. */
	struct FLocalName
	{
		FString Name;
		FString Type;
		int32 Graph = 0;
		/** The line it's declared on. */
		int32 DeclLine = 0;
		/** The first line it's visible on: its declaration's, or for a spawn's id, the one its block closes on. */
		int32 StartLine = 0;
		/** The line of the `}` that closes its block, or MAX_int32 (a graph's top level, or a block not closed yet). */
		int32 EndLine = MAX_int32;
		/** Declared with `scriptVar`: the main graph's are seen by the functions too. */
		bool bScriptVar = false;

		bool IsVisible(int32 InGraph, int32 LineIndex) const { return Graph == InGraph && StartLine <= LineIndex && LineIndex <= EndLine; }
	};

	/** The visible local of that name on the line (the innermost: the latest declared), or nullptr. */
	const FLocalName* FindVisibleLocal(int32 LineIndex, const FString& Name) const;

	/** The main graph's script variable of that name (its index in Locals), which every function sees, or INDEX_NONE. */
	int32 FindScriptVar(const FString& Name) const;

	TArray<FLocalName> Locals;

	TArray<FBangoVisibleName> GivenInputs;

	/** The graph each line belongs to. */
	TArray<int32> LineGraph;

	TArray<FString> Globals;
	TArray<FString> GlobalTypes;
};
