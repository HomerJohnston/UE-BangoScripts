// Bango text editor: which names are locals, parameters and globals, from the whole file.
// See "Bango Text Editor Spec", section 2, Meaning layer.
//
// A name is the graph's local, parameter or loop variable of that name if there is one, and otherwise a global
// (Bango Language Spec, section 4). One line can't tell, so the tokenizer calls every name a Global; this reads the
// declarations of the whole file, which are all in fixed places:
//  - globals: `global <type> name;`, in one block at the top of the file;
//  - the main graph's parameters: `#set inputs (<type> name, ...)`;
//  - a function's parameters: its header, `function <type> Name(<type> name, ...)`, which may span several lines;
//  - locals and script variables: `<type> name ...;` and `scriptVar <type> name ...;`, visible from their line to the
//    `}` that closes their block (C-style block scope; a graph's top level runs to the end of the graph);
//  - loop variables: `for (<type> name = ...` and `forEach (name in ...)`, visible in their loop's header and block;
//  - a spawn's declared id: `int _t = spawn`, visible from the `}` that closes the spawn's block.
// A function's graph is the lines from its `function` line to the `};` that closes its body.
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
};

class FBangoScriptNames
{
public:
	/** Reads the declarations of a whole file: its lines, and each line's tokens from FBangoLineTokenizer. */
	void Build(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens);

	/**
	 * What a name on a line is: Parameter, Local (a local, script variable or loop variable visible on that line),
	 * Global (declared in this file), or UndeclaredGlobal (used here, declared elsewhere, a misspelling, or a local
	 * used outside its block).
	 */
	EBangoSyntaxKind Classify(int32 LineIndex, const FString& Name) const;

	/** The graph a line belongs to: 0 for the main graph, then one per function in file order. */
	int32 GetLineGraph(int32 LineIndex) const;

	/** A graph's function name, or empty for the main graph (or a function whose name hasn't been typed yet). */
	FString GetGraphFunctionName(int32 Graph) const;

	/** Whether the line is its graph's entry: the `#set inputs` line, or a function's `function` line. */
	bool IsEntryLine(int32 LineIndex) const;

	/** The entry line of the main graph (FunctionName empty) or of the function with that name, or INDEX_NONE. */
	int32 FindEntryLine(const FString& FunctionName) const;

	/**
	 * The names a line can use: its graph's parameters and the locals visible on it (script variables and loop
	 * variables included; of two visible with one name, the inner one), the file's globals that aren't hidden by them,
	 * and the file's functions. In that order.
	 */
	void GetVisibleNames(int32 LineIndex, TArray<FBangoVisibleName>& OutNames) const;

	/** Gives each Global token on a line its kind from Classify. */
	void RefineTokens(int32 LineIndex, const FString& Line, TArray<FBangoSyntaxToken>& InOutTokens) const;

private:
	struct FGraphNames
	{
		TArray<FString> Params;
		TArray<FString> ParamTypes;
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
		int32 StartLine = 0;
		/** The line of the `}` that closes its block, or MAX_int32 (a graph's top level, or a block not closed yet). */
		int32 EndLine = MAX_int32;

		bool IsVisible(int32 InGraph, int32 LineIndex) const { return Graph == InGraph && StartLine <= LineIndex && LineIndex <= EndLine; }
	};

	/** The visible local of that name on the line (the innermost: the latest declared), or nullptr. */
	const FLocalName* FindVisibleLocal(int32 LineIndex, const FString& Name) const;

	TArray<FLocalName> Locals;

	/** The graph each line belongs to. */
	TArray<int32> LineGraph;

	TArray<FString> Globals;
	TArray<FString> GlobalTypes;
};
