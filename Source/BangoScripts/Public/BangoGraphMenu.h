#pragma once

#include "CoreMinimal.h"
#include "BangoGraph.h"
#include "BangoGraphLayout.h"

/** One node the graph editor's add-node menu offers. */
struct BANGOSCRIPTS_API FBangoNodeMenuEntry
{
	/** The menu's group, like "Flow Control", "Variables" or a command's category. */
	FString Category;
	/** What the menu shows, like "If" or "Declare Int". */
	FString Title;
	/** The tooltip. */
	FString Description;
	/** Extra words the menu's search matches. */
	FString Keywords;
	/** Built into Bango (flow control, variables, arrays, the plugin's commands), not the project's own commands or the script's own functions. */
	bool bBuiltIn = true;

	/** The new node: its kind and text. Fields start empty, so most new nodes don't compile until they're filled in. */
	EBangoGraphNodeKind Kind = EBangoGraphNodeKind::Call;
	FString Text;
};

namespace Bango
{
	/**
	 * The nodes the add-node menu offers, in menu order (by category, then title): flow control, variables and
	 * globals, every command that can be a line of its own (commands whose value must be used are left out), the
	 * array commands, and the script's own functions ("This Script").
	 *
	 * A new node's fields are empty, so it doesn't compile until they're filled in; the exceptions are names that
	 * must be there for the node to read, which are made up so they don't clash with any name in Document's
	 * nodes or in Context's visible variables (like `_newInt` for a Declare Int, `_i` for a For).
	 */
	BANGOSCRIPTS_API void GetNodeMenuEntries(const FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context, TArray<FBangoNodeMenuEntry>& OutEntries);

	/**
	 * The nodes that work on one variable (a local, parameter or global) of type Type, with the variable filled
	 * in, for dragging it from the editor's variable lists: Set for every type; Add To for numbers, strings and
	 * vectors; Subtract From, Multiply and Divide for numbers and vectors; Increment and Decrement for ints; and for
	 * arrays Push, Insert At, Pop, Remove At and For Each (whose loop variable gets a name not in Used). An unknown
	 * Type (a global declared elsewhere) gets only Set. Category is empty.
	 */
	BANGOSCRIPTS_API void GetVariableNodeEntries(const FString& Name, const FString& Type, const TSet<FString>& Used, TArray<FBangoNodeMenuEntry>& OutEntries);

	/** Name, or Name2, Name3... : the first that isn't in Used. */
	BANGOSCRIPTS_API FString MakeUniqueName(const FString& Name, const TSet<FString>& Used);
}
