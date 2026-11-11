#pragma once

#include "CoreMinimal.h"
#include "BangoCommand.h"
#include "BangoGraph.h"

/**
 * How the graph editor shows a node: a title and a list of typed fields, each one a piece of the node's text.
 *
 * The node's Text stays the source of truth. A layout only says which part of the text each field is,
 * so the editor can show "Sleep  seconds: 1.0" for `Sleep(1.0)` and later edit just that part.
 *
 * A statement that is exactly one call (`[target] Name(args)`) gets a dedicated node for that command or
 * script function: Target, one field per parameter, and extra values for variadic commands. Anything else gets
 * a generic node for its kind (Set, Declare, If, ...), including a line that stores a call's result: in
 * `double _t = Random(1.0, 2.0)` the declaration is what the line does, so it's a Declare Double node.
 */

enum class EBangoNodeStyle : uint8
{
	/** The main graph's entry: one field per script parameter. */
	ScriptEntry,
	/** A function graph's entry: the return type, then one row per parameter. */
	FunctionEntry,
	/** A registered C++ command, like Hint or SetPos. */
	Command,
	/** A call to a function defined in this script. */
	ScriptFunction,
	/** Count, Push, InsertAt, Pop or RemoveAt. */
	ArrayCommand,
	/** Everything else: Declare, Set, If, For, Return, Label, ... and calls that can't be shown as a command. */
	Generic,
};

struct BANGOSCRIPTS_API FBangoNodeField
{
	/** "Time", "Condition", "_name", "Target" */
	FString Label;
	/** The parameter's description from the command's signature (shown when hovering the value), or empty. */
	FString Description;
	/** "float", "bool", "any", ... Empty when there's no single type (like a for loop's Init). */
	FString TypeHint;
	/** This field's text, as written in the node's text. Empty if the field isn't there (see bPresent). */
	FString Text;
	/**
	 * Where Text is in the node's text: characters [Begin, End). For a field that isn't present,
	 * Begin == End is where it would go.
	 */
	int32 Begin = 0;
	int32 End = 0;
	/** False if the node's text doesn't have this part (an optional argument that wasn't passed, no Result, ...). */
	bool bPresent = true;
	bool bOptional = false;
	/** False for rows that only show information and have no value (a function parameter's name and type). */
	bool bHasValue = true;
	/**
	 * True if the field can't be filled in yet: an optional argument after another one that isn't there
	 * (`F(a)` with optional b and c: c is locked until b is filled in).
	 */
	bool bLocked = false;
	/**
	 * True for a value that's shown here but edited elsewhere: the Script Start node's parameter defaults, which
	 * the editor's INPUTS panel edits. Bango::SetNodeField refuses it.
	 */
	bool bReadOnly = false;
	/** True for an extra value that a placeholder in a literal text uses (Hint's %0, %1, ...). */
	bool bPlaceholder = false;

	/**
	 * True for the Script Start node's Loc Namespace row: the script's #define LOCTEXT_NAMESPACE line, not part of the node's
	 * text (see Bango::AddLocNamespaceField). Bango::SetNodeField refuses it; the editor stores it itself.
	 */
	bool bLocNamespace = false;

	/**
	 * True for a field that names a variable (or array element) the node stores into or declares: Set's Var, a
	 * Declare node's Name, For Each's Variable, an array command's Target. The editor shows these with the variable
	 * icon, tinted for VariableType.
	 */
	bool bVariable = false;
	/** For a variable field, its type if known ("int", "string[]"), else empty. Only for showing; see TypeHint. */
	FString VariableType;

	/**
	 * Other places in the node's text that hold the same thing as this field, and change with it: a simple For
	 * loop's variable is in its Init, Condition and Step. Each is the start of a copy of Text.
	 */
	TArray<int32> MirrorBegins;

	/** The editor shows the value without its label, across the label's column too (a For's Expression row). */
	bool bHideLabel = false;

	/** Don't show the type icon (If and While: the condition is always a bool, so the icon adds nothing). TypeHint is still set. */
	bool bHideIcon = false;

	/** A heading the editor shows on its own row above this field ("Input Params", "Localization"), or empty. */
	FString SectionHeader;

	/** What clearing the field removes: [OuterBegin, OuterEnd), its text plus separators (like `, ` or ` = `). */
	int32 OuterBegin = 0;
	int32 OuterEnd = 0;
	/** Written around the value when a field that isn't present is filled in, at Begin (like `, ` or ` = `). */
	FString InsertPrefix;
	FString InsertSuffix;
	bool operator==(const FBangoNodeField& Other) const;
	bool operator!=(const FBangoNodeField& Other) const { return !(*this == Other); }
};

struct BANGOSCRIPTS_API FBangoNodeLayout
{
	EBangoNodeStyle Style = EBangoNodeStyle::Generic;
	FString Title;
	/** For Command/ScriptFunction/ArrayCommand: the name called. */
	FString CallName;
	/** The command's category and description, if it has them. */
	FString CategoryName;
	FString Description;
	TArray<FBangoNodeField> Fields;
	/** A problem worth showing on the node right away, like an unknown command. Empty if none. */
	FString Error;

	/**
	 * For nodes: true if the header is a simple counting loop (`int _i = A; _i <= B; _i++`, or `>=` with `--`),
	 * which can show as Var, First and Last rows. The editor's Expression toggle switches between that and the
	 * whole header.
	 */
	bool bHasSimpleForm = false;
	/** For nodes: true if the header is shown as one Expression row (it isn't simple, or that was asked for). */
	bool bShowsExpression = false;

	/** True if the two would look the same on the graph (the editor only rebuilds the widgets of nodes whose layout changed). */
	bool operator==(const FBangoNodeLayout& Other) const;
	bool operator!=(const FBangoNodeLayout& Other) const { return !(*this == Other); }
};

/** What a layout needs to know besides the node: the registered commands and this script's functions. */
struct BANGOSCRIPTS_API FBangoNodeLayoutContext
{
	/** Registered commands. May be null (then every call that isn't a script function is unknown). */
	const IBangoCommandLookup* Commands = nullptr;
	/** The script's functions, as signatures (no target, not variadic). */
	TArray<FBangoCommandSignature> Functions;

	/**
	 * The variables each node can see, by NodeID: name -> type ("int", "string[]"), or an empty type when it can't
	 * be told (two declarations could apply, or a For Each over something whose type isn't known). Covers the
	 * script's or function's parameters, every global declared in the file, and the locals declared before the node
	 * in its own chain and the chains around it. Used to show the types of Set nodes and the like.
	 */
	TMap<int64, TMap<FString, FString>> VisibleVariables;

	/**
	 * What each Return node in a function must return, by NodeID: the function's return type ("int", "string[]"),
	 * or empty for a void function. Missing for the main graph's Return nodes (they never return a value) and for
	 * functions whose signature doesn't read.
	 */
	TMap<int64, FString> ReturnTypes;

	const FBangoCommandSignature* FindFunction(const FString& Name) const;
};

/** A variable the editor's VARIABLES list shows for a graph (see Bango::GetGraphVariables). */
struct BANGOSCRIPTS_API FBangoGraphVariable
{
	enum class EKind : uint8
	{
		/** A parameter of the script (main graph) or of the function. */
		Param,
		/** A local declared in the graph: a Declare node, a For's counter, or a For Each's variable. */
		Local,
		/** A global the graph declares or uses. */
		Global,
	};

	FString Name;
	/** "int", "string[]", ...; empty if it can't be told (like a global declared only by another script). */
	FString Type;
	EKind Kind = EKind::Local;
	/** Where it's declared: a graph (INDEX_NONE for the main graph, else a function's index) and a node in it. */
	int32 DeclGraph = INDEX_NONE;
	/** INDEX_NONE if it isn't declared anywhere in the file (a global from another script, or a typo). */
	int32 DeclNode = INDEX_NONE;
};

/** A text literal as written: `LOCTEXT("Key", "Source")` and its NS / NOTE forms (spec section 3). */
struct BANGOSCRIPTS_API FBangoTextLiteral
{
	/** NSLOCTEXT / NSNOTELOCTEXT: has its own namespace. Otherwise the script's LOCTEXT_NAMESPACE is used. */
	bool bHasNamespace = false;
	FString Namespace;
	FString Key;
	FString Source;
	/** NOTELOCTEXT / NSNOTELOCTEXT: has notes for translators. */
	bool bHasNotes = false;
	FString Notes;
};

namespace Bango
{
	/** Builds a layout context for a document: its functions' signatures, plus the given commands. */
	/**
	 * True if two contexts would lay out every node the same way: the same function signatures and the same
	 * visible variables. The editor re-lays out all nodes when this turns false after an edit.
	 */
	BANGOSCRIPTS_API bool LayoutContextsMatch(const FBangoNodeLayoutContext& A, const FBangoNodeLayoutContext& B);

	/**
	 * The variables of one graph (INDEX_NONE for the main graph, else a function's index): its parameters, the locals
	 * it declares, and the globals it declares or uses, each once (the first declaration wins), in that order and
	 * sorted by name within each kind. Context gives the types (from MakeNodeLayoutContext for the same document).
	 */
	BANGOSCRIPTS_API void GetGraphVariables(const FBangoGraphDocument& Document, int32 Graph, const FBangoNodeLayoutContext& Context, TArray<FBangoGraphVariable>& OutVariables);

	BANGOSCRIPTS_API FBangoNodeLayoutContext MakeNodeLayoutContext(const FBangoGraphDocument& Document, const IBangoCommandLookup* Commands);

	/** The layout of one node. bInFunctionGraph says whether an entry node is a function's entry. */
	/**
	 * Adds the Loc Namespace row to the main Script Start node's layout: the script's `#define LOCTEXT_NAMESPACE "..."`, a
	 * string field that is optional (clearing it removes the line).
	 */
	BANGOSCRIPTS_API void AddLocNamespaceField(FBangoNodeLayout& Layout, bool bHasLocNamespace, const FString& LocNamespace);

	/** Reads Text as exactly one text literal (`LOCTEXT("Key", "Hi")`, ...). False if it's anything else. */
	BANGOSCRIPTS_API bool ParseTextLiteral(const FString& Text, FBangoTextLiteral& OutLiteral);

	/**
	 * Writes a text literal: LOCTEXT, NSLOCTEXT, NOTELOCTEXT or NSNOTELOCTEXT, whichever fits what it has (notes
	 * are written only if there are some, so clearing them gives LOCTEXT).
	 */
	BANGOSCRIPTS_API FString MakeTextLiteral(const FBangoTextLiteral& Literal);

	/**
	 * The fields the graph editor shows for Node. bShowExpression makes a For node show its whole header as one
	 * Expression row even when it's a simple counting loop (see FBangoNodeLayout::bHasSimpleForm).
	 */
	BANGOSCRIPTS_API FBangoNodeLayout GetNodeLayout(const FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context, bool bShowExpression = false);

}
