// Bango text editor: which names are locals, parameters and globals, from the whole file.
// See "Bango Text Editor Spec", section 2, Meaning layer.
//
// A name is the graph's local, parameter or loop variable of that name if there is one, and otherwise a global
// (Bango Language Spec, section 4). One line can't tell, so the tokenizer calls every name a Global; this reads the
// declarations of the whole file, which are all in fixed places:
//  - globals: `global <type> name;`, in one block at the top of the file;
//  - the main graph's parameters: `#define params (<type> name, ...)`;
//  - a function's parameters: its header, `function <type> Name(<type> name, ...)`, which may span several lines;
//  - locals: `<type> name ...;` at the top of their graph;
//  - loop variables: `for (<type> name = ...` and `forEach (name in ...)`.
// A function's graph is the lines from its `function` line to the `};` that closes its body.
//
// Depends only on FString, TArray and the line tokenizer, so it can be unit-tested without Slate.

#pragma once

#include "CoreMinimal.h"
#include "BangoLineTokenizer.h"

class FBangoScriptNames
{
public:
	/** Reads the declarations of a whole file: its lines, and each line's tokens from FBangoLineTokenizer. */
	void Build(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens);

	/**
	 * What a name on a line is: Parameter, Local (also loop variables), Global (declared in this file), or
	 * UndeclaredGlobal (used here, declared elsewhere, or a misspelling).
	 */
	EBangoSyntaxKind Classify(int32 LineIndex, const FString& Name) const;

	/** The graph a line belongs to: 0 for the main graph, then one per function in file order. */
	int32 GetLineGraph(int32 LineIndex) const;

	/** A graph's function name, or empty for the main graph (or a function whose name hasn't been typed yet). */
	FString GetGraphFunctionName(int32 Graph) const;

	/** Whether the line is its graph's entry: the `#define params` line, or a function's `function` line. */
	bool IsEntryLine(int32 LineIndex) const;

	/** The entry line of the main graph (FunctionName empty) or of the function with that name, or INDEX_NONE. */
	int32 FindEntryLine(const FString& FunctionName) const;

	/** Gives each Global token on a line its kind from Classify. */
	void RefineTokens(int32 LineIndex, const FString& Line, TArray<FBangoSyntaxToken>& InOutTokens) const;

private:
	struct FGraphNames
	{
		TArray<FString> Params;
		TArray<FString> Locals;
		FString FunctionName;
		int32 EntryLine = INDEX_NONE;
	};

	static bool Contains(const TArray<FString>& Names, const FString& Name);
	static void AddUnique(TArray<FString>& Names, const FString& Name);

	/** Graph 0 is the main graph; then one per function, in file order. */
	TArray<FGraphNames> Graphs;

	/** The graph each line belongs to. */
	TArray<int32> LineGraph;

	TArray<FString> Globals;
};
