#pragma once

#include "CoreMinimal.h"

/**
 * The node-graph form of a Bango script, used by the graph editor.
 *
 * The .bango text is the source of truth. Bango::BuildGraph turns text into a graph document, and
 * Bango::WriteGraphText turns a graph document back into text. Writing a document that was built from
 * a file, without changes, gives the file back in a fixed layout (tab indents, aligned //@ comments).
 *
 * Graph shape:
 * - Each graph (the script's main graph, and one per function) is a set of nodes. Nodes[0] is the entry node.
 * - Every node except the entry node has one exec input. Each input has at most one wire into it.
 * - Every node has one main exec output, Next: "Then" on if nodes and loops, and the plain exec
 *   output on everything else. Return, break, continue and goto have one too; whatever follows them only
 *   runs if it is reached through a label.
 * - If nodes also have Do and Else outputs, loops have a Do output, and sequences have an output per block, named
 *   Scope 0, Scope 1, Scope 2, ... (Branches). A sequence's Next is its Then output, for what follows its blocks.
 * - A chain of nodes that nothing wires into, other than the entry node's chain, is "disconnected". It is
 *   saved as `//~` comment lines, so the compiler ignores it but the graph keeps it.
 *
 * Positions are in grid units of BangoGraphGridSize pixels.
 */

/** Size of one graph coordinate unit, in graph-editor pixels (the Blueprint editor's default snap grid). */
static constexpr int32 BangoGraphGridSize = 16;

enum class EBangoGraphNodeKind : uint8
{
	/** Nodes[0] of every graph. Text: the inside of params (...) for the main graph, the signature for a function. */
	Entry,

	Assign,
	IncDec,
	Call,
	If,
	While,
	For,
	ForEach,
	/** `sequence { } { }`: an output per block (Branches), each run to its end before the next; then Next. No text. */
	Sequence,
	/**
	 * `switch (value) { case 1: { } case 2, 3: { } default: { } }`: Text is the value; a row per case (CaseValues),
	 * each with an output (Branches), then the Default output (the last of Branches); then Next.
	 */
	Switch,
	Break,
	Continue,
	Return,
	Label,
	Goto,
	/** A bend in a wire: no text, one input, one output. Written as a line with only its //@ comment. */
	Reroute,
};

struct BANGOSCRIPTS_API FBangoGraphNode
{
	EBangoGraphNodeKind Kind = EBangoGraphNodeKind::Call;

	/** The NodeID written in the //@ comment. Always 0 for entry nodes; unique across the file otherwise. */
	int64 NodeID = 0;
	int32 X = 0;
	int32 Y = 0;

	/**
	 * The text the user edits, without keywords, brackets or ';':
	 * - Entry: `int _a, string _b = "x"` (main graph) or `int AddScore(int _amount)` (function)
	 * - Assign, IncDec, Call: the statement, like `_x += 2`, `_x++`, `john Speak("Hi")`
	 * - If, While, Switch: the condition or value. For: `int _i = 0; _i < 3; _i++`. ForEach: `_x in _array`
	 * - Return: the value, or empty. Label, Goto: the label name. Break, Continue, Sequence, Reroute: empty.
	 */
	FString Text;

	/** The comment bubble, from `//` lines above the statement. Lines are separated by '\n'. Empty for none. */
	FString Comment;

	/** Index into the graph's Nodes of the node the main exec output leads to, or INDEX_NONE. */
	int32 Next = INDEX_NONE;

	/**
	 * If: [Do, Else]. While, For, ForEach: [Do]. Sequence: one per block ([Scope 0, Scope 1, ...], at least one).
	 * Switch: one per case, then Default (CaseValues.Num() + 1). Unused entries are INDEX_NONE. Every kind but Sequence
	 * and Switch has two entries; see Bango::GetGraphNodeBranchCount for how many are used.
	 */
	TArray<int32> Branches = { INDEX_NONE, INDEX_NONE };

	/** Switch only: each case's values as written, like `1` or `"north", "south"`. Branches[i] is case i's output. */
	TArray<FString> CaseValues;

	/**
	 * A disabled node (written between slash-star and star-slash): it does nothing, and execution goes
	 * straight on to Next. A disabled If or loop skips its blocks. The nodes in those blocks keep their own setting.
	 */
	bool bDisabled = false;

	bool operator==(const FBangoGraphNode& Other) const;
	bool operator!=(const FBangoGraphNode& Other) const { return !(*this == Other); }
};

/**
 * A declaration: one of the file's globals (`global int gold;`), or a local at the top of a graph
 * (`int _count = 0;`). Declarations aren't nodes; the editor lists them beside the graph.
 */
struct BANGOSCRIPTS_API FBangoGraphDecl
{
	/** As written: `int`, `string[]`. */
	FString Type;
	FString Name;
	/** Locals only: the starting value as written (a fixed value, like `0`, `"hi"` or `[1, 2]`). Globals never have one. */
	bool bHasValue = false;
	FString Value;
	/** From `//` lines above the declaration. Lines are separated by '\n'. */
	FString Comment;

	bool operator==(const FBangoGraphDecl& Other) const;
	bool operator!=(const FBangoGraphDecl& Other) const { return !(*this == Other); }
};

struct BANGOSCRIPTS_API FBangoGraph
{
	/** True for a function's graph. */
	bool bIsFunction = false;

	/** The function's name (from the entry node's signature). Empty for the main graph. */
	FString Name;

	/** Nodes[0] is the entry node. */
	TArray<FBangoGraphNode> Nodes;

	/** The locals declared at the top of the graph, in order. */
	TArray<FBangoGraphDecl> Locals;

	bool operator==(const FBangoGraph& Other) const;
	bool operator!=(const FBangoGraph& Other) const { return !(*this == Other); }
};

struct BANGOSCRIPTS_API FBangoGraphDocument
{
	/**
	 * The script's description: the `//` lines the file starts with, from line 1 up to the first line that isn't one
	 * (a blank line, code, or a `// ====` heading). Lines are separated by '\n', without the `//` and one space after
	 * it. Not shown in the graph; empty if the file doesn't start with a comment. Saved first, then a blank line.
	 */
	FString Description;

	/** The script's `#define LOCTEXT_NAMESPACE "..."` line (see FBangoScript::LocNamespace). Saved right after the params line, with `#undef LOCTEXT_NAMESPACE` at the end. */
	bool bHasLocNamespace = false;
	FString LocNamespace;

	/** The file's global declarations, in order (written after the #define lines). */
	TArray<FBangoGraphDecl> Globals;

	FBangoGraph Main;
	/** In file order. */
	TArray<FBangoGraph> Functions;

	bool operator==(const FBangoGraphDocument& Other) const;
	bool operator!=(const FBangoGraphDocument& Other) const { return !(*this == Other); }
};

struct FBangoCompileError;

namespace Bango
{
	/** Number of Branches an exec node of this kind has: 2 for If, 1 for loops, 0 otherwise; for Sequence, a new one's (2). */
	BANGOSCRIPTS_API int32 GetGraphNodeBranchCount(EBangoGraphNodeKind Kind);
	/** Number of Branches this node has: a sequence's or switch's own number of outputs, otherwise its kind's. */
	BANGOSCRIPTS_API int32 GetGraphNodeBranchCount(const FBangoGraphNode& Node);

	/**
	 * Display names: "Do"/"Else"/"Then" for If; "Do"/"Then" for loops; "Scope 0", "Scope 1", ... / "Then" for Sequence;
	 * "Case 0", "Case 1", ... / "Then" for Switch (whose last output is "Default": see the node overload); empty for a
	 * plain exec output.
	 */
	BANGOSCRIPTS_API FString GetGraphNodeNextName(EBangoGraphNodeKind Kind);
	BANGOSCRIPTS_API FString GetGraphNodeBranchName(EBangoGraphNodeKind Kind, int32 Branch);
	/** As above, for this node: a switch's case outputs are named by their values, and its last is "Default". */
	BANGOSCRIPTS_API FString GetGraphNodeBranchName(const FBangoGraphNode& Node, int32 Branch);

	/**
	 * The order functions are written in: depth-first by first use. Walking the main graph in the order
	 * its lines are written (connected nodes, then disconnected ones), the first call to a function places
	 * it, and that function's own calls are placed right after it, before the main graph goes on.
	 * Functions nothing calls follow, in their current order (with their own callees after each).
	 * Returns indices into Document.Functions. BuildGraph already stores functions in this order.
	 */
	BANGOSCRIPTS_API TArray<int32> GetFunctionWriteOrder(const FBangoGraphDocument& Document);

	/**
	 * The section headings the writer puts in a file (the text after the //): SCRIPT INPUT above the #define lines,
	 * GLOBAL VARS and LOCAL VARS above the declarations, SCRIPT BODY above the main graph's code, and FUNCTIONS
	 * above the first function. Every heading starts with "====".
	 */
	BANGOSCRIPTS_API const TCHAR* GetScriptInputHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetGlobalVarsHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetLocalVarsHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetScriptBodyHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetFunctionsHeadingComment();
	/** The line the writer puts between functions. */
	BANGOSCRIPTS_API const TCHAR* GetFunctionSeparatorComment();
	/**
	 * True if a comment's text (after the //) is a section heading: it starts with "====" (after any spaces).
	 * Reading a file skips these on lines of their own, anywhere, so they never become comment bubbles.
	 */
	BANGOSCRIPTS_API bool IsSectionHeadingComment(const FString& CommentText);
	/** True if a comment's text is the line between functions (ignoring spaces around it). Reading skips it outside function bodies. */
	BANGOSCRIPTS_API bool IsFunctionSeparatorComment(const FString& CommentText);

	/** "Hint", "If", "Set", ... a short title for the node. */
	BANGOSCRIPTS_API FString GetGraphNodeKindName(EBangoGraphNodeKind Kind);

	/**
	 * Parses Source and builds its graph. Statements without //@ metadata get new NodeIDs and positions
	 * near the node before them. The declarations at the top of the file and of each function become
	 * Globals and Locals (any //@ metadata on them is dropped). Fails with a syntax error, if the `//~`
	 * lines of disconnected nodes can't be read, if a declaration isn't at the top of its graph (so it
	 * would need a node), or if a global declaration has a value.
	 */
	BANGOSCRIPTS_API bool BuildGraph(const FString& Source, const FString& FileName, FBangoGraphDocument& OutDocument, FBangoCompileError& OutError);

	/**
	 * Writes Document as Bango text. Fails (with a message naming the node) if a node has two wires into it,
	 * if wires form a loop, if a link points at a missing node, or if a node's text contains a line break.
	 * Node text is written as-is; it is not checked for syntax.
	 */
	BANGOSCRIPTS_API bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError);
}
