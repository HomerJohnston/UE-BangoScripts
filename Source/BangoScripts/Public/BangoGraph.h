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
	/** Nodes[0] of every graph. Text: the inside of inputs (...) for the main graph, the signature for a function. */
	Entry,

	Assign,
	IncDec,
	/**
	 * `int _x = 1;`: declares a local, which exists from here to the end of its block, and sets it each time this
	 * runs. Text: the declaration, like `int _x = 1` or `float[] _list` ("Local Int", "Local Float Array", ...).
	 */
	Local,
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
	/**
	 * `spawn { }` or `_id = spawn { }`: its block (Branches[0], Run) starts as a separate running copy of the script;
	 * Next (Then) follows straight away. Text: where the copy's id goes, or empty; or a local it declares for it, with
	 * its type (`int _t`).
	 */
	Spawn,
	Break,
	Continue,
	Return,
	Label,
	Goto,
	/** A bend in a wire: no text, one input, one output. Written as a line with only its //@ comment. */
	Reroute,
	/**
	 * `waitUntil (condition);`: pauses until the condition (Text) is true; then Next. With a heldFor or a timeout,
	 * Text is all between the statement's outer brackets, like `condition) heldFor (seconds) timeout (seconds, true`
	 * (Bango::SplitWaitUntilText splits it), and Branches[0] ("Timeout") is the block run when the time is up,
	 * before Next. It's written with a timeout if Text has one or Timeout is wired.
	 */
	WaitUntil,
	/**
	 * `global bool opened = true;` in the code: sets a global each time this runs (and declares it for the whole file,
	 * like one at the top). Text: without `global`, like `bool opened = true` ("Set Global Bool", ...).
	 */
	Global,
	/**
	 * `@john Speech(0, 1) { ... }`: a command that takes a block (FBangoCommandSignature::Block). Text is the call,
	 * like a Call node's (`@john Speech(0, 1)`); Branches[0] ("Body") is its block, and Next (Then) what follows.
	 * (Last in the list: it was added last, and the editor keeps kinds as numbers.)
	 */
	BlockCall,
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
	 * - Local: the declaration, like `int _x = 2` or `string _s`
	 * - Global: the declaration without `global`, like `bool opened = true`
	 * - If, While, Switch: the condition or value. For: `int _i = 0; _i < 3; _i++`. ForEach: `_x in _array`
	 * - Return: the value, or empty. Spawn: where the copy's id goes, or empty. Label, Goto: the label name. Break,
	 *   Continue, Sequence, Reroute: empty.
	 */
	FString Text;

	/** The comment bubble, from `//` lines above the statement. Lines are separated by '\n'. Empty for none. */
	FString Comment;

	/** Index into the graph's Nodes of the node the main exec output leads to, or INDEX_NONE. */
	int32 Next = INDEX_NONE;

	/**
	 * If: [Do, Else]. While, For, ForEach: [Do]. Spawn: [Run]. BlockCall: [Body]. Sequence: one per block ([Scope 0, Scope 1, ...], at least one).
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
 * A declaration: one of the file's globals at the top (`global int gold ?= 0;`), or a script variable at the top of a
 * graph (`scriptVar int _count = 0;`). These aren't nodes; the editor lists them beside the graph. (A local, declared
 * without `scriptVar`, is a node: EBangoGraphNodeKind::Local; so is a global set in the code: EBangoGraphNodeKind::Global.)
 */
namespace Bango
{
	/**
	 * A Global node's statement, without its ';': its text is the declaration without 'global' (`bool opened = true`,
	 * or `persistent bool gold = 5` for a persistent global), and this puts the keyword back
	 * (`global bool opened = true`, `persistent global bool gold = 5`).
	 */
	BANGOSCRIPTS_API FString MakeGlobalNodeStatement(const FString& Text);
	/** The other way: the node text for a `global ...` or `persistent global ...` statement's text. */
	BANGOSCRIPTS_API FString MakeGlobalNodeText(const FString& Statement);
	/**
	 * A Set Global node's text (MakeGlobalNodeText's, `bool open = true` or `persistent bool open = true`) for the
	 * global Name made a persistent global or a world global: true, with Text changed, if it declares Name and
	 * wasn't already that kind. For the editor's Globals list, which keeps a global's nodes the same kind as it.
	 */
	BANGOSCRIPTS_API bool SetGlobalNodePersistent(FString& Text, const FString& Name, bool bPersistent);
}

struct BANGOSCRIPTS_API FBangoGraphDecl
{
	/** As written: `int`, `string[]`. */
	FString Type;
	FString Name;
	/**
	 * The value as written (a fixed value, like `0`, `"hi"` or `[1, 2]`): a script variable's starting value, or a
	 * global's default (`?=`, given only if the global doesn't exist yet or is nil).
	 */
	bool bHasValue = false;
	FString Value;
	/** From `//` lines above the declaration. Lines are separated by '\n'. */
	FString Comment;
	/** A global's: `persistent global` (it outlives the world). */
	bool bPersistent = false;

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

	/** The script variables (scriptVar) declared at the top of the graph, in order. */
	TArray<FBangoGraphDecl> ScriptVars;

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

	/** The script's `#TEXT_NAMESPACE "..."` line (see FBangoScript::LocNamespace). Saved after the description, above the inputs line. Edited in the editor's SCRIPT CONFIG panel. */
	bool bHasLocNamespace = false;
	FString LocNamespace;

	/**
	 * The script's `#USES_GRAPH_EDITOR` line (see FBangoScript): it's saved with its node metadata, when written
	 * following it (FBangoGraphWriteOptions::bFollowUsesGraphEditor). Saved after the namespace line.
	 */
	bool bUsesGraphEditor = false;

	/** The file's global declarations, in order (written after the settings lines). */
	TArray<FBangoGraphDecl> Globals;

	FBangoGraph Main;
	/** In file order. */
	TArray<FBangoGraph> Functions;

	/**
	 * An inline script's inputs given by its inputs signature (FBangoInlineScriptBase::UseInputsSignature), each one's
	 * Type and Name: the main graph's parameters, as for its node layouts and variable lists. Set by the editor; not
	 * read from the text, written, or compared.
	 */
	TArray<FBangoGraphDecl> GivenInputs;

	bool operator==(const FBangoGraphDocument& Other) const;
	bool operator!=(const FBangoGraphDocument& Other) const { return !(*this == Other); }
};

struct FBangoCompileError;

/** How Bango::WriteGraphText writes a document. */
struct FBangoGraphWriteOptions
{
	/**
	 * Written as the script says: in full (section headings and node metadata) if it has `#USES_GRAPH_EDITOR`,
	 * compact if not. The script editor's saves. Off: in full, whatever it says (unless bCompact).
	 */
	bool bFollowUsesGraphEditor = false;

	/**
	 * Always compact: no section headings (not even FUNCTIONS), no node metadata (and so no reroutes), and no #USES_GRAPH_EDITOR line.
	 * For inline scripts made editable in their box.
	 */
	bool bCompact = false;
};

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
	 * Where the parts of a Wait Until node's text are. The text is its condition, then optionally
	 * `) heldFor (seconds`, then optionally `) timeout (seconds` or `) timeout (seconds, ignoreHold`:
	 * what's between the outer brackets of `waitUntil (condition) heldFor (2) timeout (5, true)`.
	 * A part that isn't there is empty, at the end of the text.
	 */
	struct FBangoWaitUntilText
	{
		/** The condition is [0, ConditionEnd): up to the `)` that closes it, or the whole text. */
		int32 ConditionEnd = 0;
		/** `heldFor (`: its seconds are [HoldBegin, HoldEnd), HoldEnd being its `)` (or the end of the text). */
		bool bHold = false;
		int32 HoldBegin = 0;
		int32 HoldEnd = 0;
		/** `timeout (`: the `)` before the word is at TimeoutOuterBegin, and its seconds are [SecondsBegin, SecondsEnd). */
		bool bTimeout = false;
		int32 TimeoutOuterBegin = 0;
		int32 SecondsBegin = 0;
		int32 SecondsEnd = 0;
		/** The timeout's second value: its `,` is at SecondsEnd, and the value is from IgnoreHoldBegin to the end of the text. */
		bool bIgnoreHold = false;
		int32 IgnoreHoldBegin = 0;
	};
	BANGOSCRIPTS_API FBangoWaitUntilText SplitWaitUntilText(const FString& Text);
	/** True if a Wait Until node is written with a timeout and its block: its text has one, or its Timeout output is wired. */
	BANGOSCRIPTS_API bool HasWaitUntilTimeout(const FBangoGraphNode& Node);
	/** A Wait Until node's line of code: `waitUntil (...);`, or `waitUntil (...) timeout (seconds)` before its block. */
	BANGOSCRIPTS_API FString MakeWaitUntilHeader(const FString& Text, bool bWithTimeout);

	/**
	 * The order functions are written in: depth-first by first use. Walking the main graph in the order
	 * its lines are written (connected nodes, then disconnected ones), the first call to a function places
	 * it, and that function's own calls are placed right after it, before the main graph goes on.
	 * Functions nothing calls follow, in their current order (with their own callees after each).
	 * Returns indices into Document.Functions. BuildGraph already stores functions in this order.
	 */
	BANGOSCRIPTS_API TArray<int32> GetFunctionWriteOrder(const FBangoGraphDocument& Document);

	/**
	 * The section headings the writer puts in a file (the text after the //): SCRIPT INPUTS above the #inputs line,
	 * GLOBAL VARS and LOCAL VARS above the declarations, SCRIPT BODY above the main graph's code, and FUNCTIONS
	 * above the first function. Every heading starts with "====". A compact script (one without #USES_GRAPH_EDITOR)
	 * has only the FUNCTIONS heading; text written with FBangoGraphWriteOptions::bCompact has none.
	 */
	BANGOSCRIPTS_API const TCHAR* GetScriptInputHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetGlobalVarsHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetScriptVarsHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetScriptBodyHeadingComment();
	BANGOSCRIPTS_API const TCHAR* GetFunctionsHeadingComment();
	/**
	 * True if a comment's text (after the //) is a section heading: it starts with "====" (after any spaces).
	 * Reading a file skips these on lines of their own, anywhere, so they never become comment bubbles.
	 */
	BANGOSCRIPTS_API bool IsSectionHeadingComment(const FString& CommentText);
	/**
	 * True if a comment's text is a line of dashes (ten or more, ignoring spaces around it). Reading skips one outside
	 * function bodies: files used to have one between functions. The writer no longer writes any.
	 */
	BANGOSCRIPTS_API bool IsFunctionSeparatorComment(const FString& CommentText);

	/** "WriteScreen", "If", "Set", ... a short title for the node. */
	BANGOSCRIPTS_API FString GetGraphNodeKindName(EBangoGraphNodeKind Kind);

	/**
	 * Parses Source and builds its graph. Statements without //@ metadata get new NodeIDs and positions
	 * near the node before them. The global and scriptVar declarations at the top of the file and of each
	 * function become Globals and ScriptVars (any //@ metadata on them is dropped); locals are Local nodes.
	 * Fails with a syntax error, if the `//~` lines of disconnected nodes can't be read, if a global or
	 * scriptVar declaration isn't at the top of its graph.
	 */
	BANGOSCRIPTS_API bool BuildGraph(const FString& Source, const FString& FileName, FBangoGraphDocument& OutDocument, FBangoCompileError& OutError);

	/**
	 * Writes Document as Bango text. Fails (with a message naming the node) if a node has two wires into it,
	 * if wires form a loop, if a link points at a missing node, or if a node's text contains a line break.
	 * Node text is written as-is; it is not checked for syntax. Without options, it's always written in full.
	 */
	BANGOSCRIPTS_API bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError);
	BANGOSCRIPTS_API bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError, const FBangoGraphWriteOptions& Options);

	/** The script's nodes, in all its graphs, not counting the entry nodes (what the project's Large Script Threshold counts). */
	BANGOSCRIPTS_API int32 CountGraphNodes(const FBangoGraphDocument& Document);

	/**
	 * Whether WriteGraphText writes Document compact: no section headings and no node metadata (so no reroutes).
	 * That's when Options.bCompact says so, or when following the script's flag (bFollowUsesGraphEditor) and it
	 * has no #USES_GRAPH_EDITOR line.
	 */
	BANGOSCRIPTS_API bool IsWrittenCompact(const FBangoGraphDocument& Document, const FBangoGraphWriteOptions& Options);
}
