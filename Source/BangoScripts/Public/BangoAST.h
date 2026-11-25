#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"

/**
 * The Bango syntax tree, produced by Bango::ParseScript (BangoParser.h).
 *
 * The parser only checks syntax and the rules that can be seen from a single line
 * (naming, one node per line, block and semicolon rules). Types, scopes, labels and
 * command signatures are checked later by the compiler.
 *
 * Nodes are owned through TUniquePtr. Use Kind (or As<T>()) to find the concrete type;
 * Unreal builds have no RTTI.
 */

struct FBangoSourceLocation
{
	int32 Line = 0;    // 1-based
	int32 Column = 0;  // 1-based
};

/** Graph-node information for a statement line (spec section 1). */
struct FBangoNodeInfo
{
	/** Line the statement is on. Every statement node is on exactly one line. */
	int32 Line = 0;

	/** True if the line had a `//@ id (x,y)` comment. The inputs line always counts as node 0 at (0,0), so it's true there too. */
	bool bHasMetadata = false;
	int64 NodeID = -1;
	int32 X = 0;
	int32 Y = 0;

	/**
	 * Where the node's editable text is on its line (1-based columns, end exclusive), for the graph editor:
	 * the condition of if/while, the header of for/forEach, the name of label/goto, the value of return,
	 * the inside of inputs (...), and the whole statement without ';' otherwise. Empty for break/continue.
	 */
	int32 TextBegin = 0;
	int32 TextEnd = 0;
};

/** An ordinary `//` comment (not `//@` metadata). */
struct FBangoSourceComment
{
	int32 Line = 0;
	int32 Column = 0;
	/** The text after the `//`, unchanged. */
	FString Text;
	/** False if code comes before the comment on its line. */
	bool bOwnLine = true;
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class EBangoExprKind : uint8
{
	Literal,
	ArrayLiteral,
	Name,
	Index,
	Unary,
	Binary,
	Call,
	IsNil,
	Member,
	/** A value left empty, like the condition of `if ()`. It parses, so the graph editor can save it, but never compiles. */
	Empty,
};

struct FBangoExpr
{
	const EBangoExprKind Kind;
	FBangoSourceLocation Loc;

	virtual ~FBangoExpr() = default;

	template<typename T> T* As() { return Kind == T::StaticKind ? static_cast<T*>(this) : nullptr; }
	template<typename T> const T* As() const { return Kind == T::StaticKind ? static_cast<const T*>(this) : nullptr; }

protected:
	explicit FBangoExpr(EBangoExprKind InKind) : Kind(InKind) {}
};

using FBangoExprPtr = TUniquePtr<FBangoExpr>;

enum class EBangoLiteralKind : uint8
{
	Bool,
	Int,
	Double,
	String,
	Nil,
	/** `@john`: StringValue holds the Bango ID name. */
	Entity,
	/**
	 * Localized text: `LOCTEXT("Key", "Source")`, `NSLOCTEXT("Namespace", "Key", "Source")`, and NOTELOCTEXT /
	 * NSNOTELOCTEXT with translator notes last; or `INVTEXT("Source")`, a text that is never translated
	 * (bTextInvariant). StringValue holds the source text.
	 */
	Text,
};

/** true, 5, 2.5, "text", nil, @john */
struct FBangoLiteralExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Literal;
	FBangoLiteralExpr() : FBangoExpr(StaticKind) {}

	EBangoLiteralKind LiteralKind = EBangoLiteralKind::Nil;
	bool BoolValue = false;
	/** Always within int32 range (the lexer rejects larger literals). */
	int64 IntValue = 0;
	double DoubleValue = 0.0;
	FString StringValue;

	// Text literals only:
	/** INVTEXT: a text that is never translated, so it has no namespace, key or notes, and TextArgLocs holds only the source. */
	bool bTextInvariant = false;
	/** NSLOCTEXT / NSNOTELOCTEXT: the namespace given. LOCTEXT / NOTELOCTEXT use the script's LOCTEXT_NAMESPACE. */
	bool bTextHasNamespace = false;
	FString TextNamespace;
	FString TextKey;
	/** NOTELOCTEXT / NSNOTELOCTEXT: notes for translators. */
	FString TextNotes;
	/** Where each of the literal's strings is: namespace (if given), key, source, notes (if given). */
	TArray<FBangoSourceLocation> TextArgLocs;

	/** Where the source text is in TextArgLocs. */
	int32 GetTextSourceArgIndex() const { return bTextInvariant ? 0 : (bTextHasNamespace ? 2 : 1); }
};

/** [a, b, c] */
struct FBangoArrayLiteralExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::ArrayLiteral;
	FBangoArrayLiteralExpr() : FBangoExpr(StaticKind) {}

	TArray<FBangoExprPtr> Elements;
};

/** A variable or global: _x, john */
struct FBangoNameExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Name;
	FBangoNameExpr() : FBangoExpr(StaticKind) {}

	FString Name;
};

/** Array[Index]. Nested indexing is a chain: _g[1][2] is Index(Index(_g, 1), 2). */
struct FBangoIndexExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Index;
	FBangoIndexExpr() : FBangoExpr(StaticKind) {}

	FBangoExprPtr Array;
	FBangoExprPtr Index;
};

enum class EBangoUnaryOp : uint8
{
	Not,     // !
	Negate,  // -
};

struct FBangoUnaryExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Unary;
	FBangoUnaryExpr() : FBangoExpr(StaticKind) {}

	EBangoUnaryOp Op = EBangoUnaryOp::Not;
	FBangoExprPtr Operand;
};

enum class EBangoBinaryOp : uint8
{
	Add, Subtract, Multiply, Divide, Modulo,
	Less, LessEqual, Greater, GreaterEqual,
	Equal, NotEqual,
	And, Or,
};

struct FBangoBinaryExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Binary;
	FBangoBinaryExpr() : FBangoExpr(StaticKind) {}

	EBangoBinaryOp Op = EBangoBinaryOp::Add;
	FBangoExprPtr Left;
	FBangoExprPtr Right;
};

/**
 * A command or function call: `Name(Args)` or `Target Name(Args)`.
 * Whether Name is a script function or a C++ command is decided by the compiler.
 */
struct FBangoCallExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Call;
	FBangoCallExpr() : FBangoExpr(StaticKind) {}

	/** Null if there is no target. */
	FBangoExprPtr Target;
	FString Name;
	FBangoSourceLocation NameLoc;
	TArray<FBangoExprPtr> Args;
	/**
	 * `target call Name(args)`: Name is a Blueprint function of the target's actor, found and called by reflection
	 * when it runs (spec section 4, call), not a command. Always has a target.
	 */
	bool bReflect = false;
	/** For a reflection call: where `call` is. */
	FBangoSourceLocation CallLoc;
};

/** A vector's part: `_pos.x`, `_path[2].y`, `_who GetPos().z`. */
struct FBangoMemberExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Member;
	FBangoMemberExpr() : FBangoExpr(StaticKind) {}

	FBangoExprPtr Object;
	/** "x", "y" or "z" (the parser allows no other names). */
	FString Member;
	/** 0 for x, 1 for y, 2 for z. */
	int32 Part = 0;
	FBangoSourceLocation MemberLoc;
};

/**
 * A value that hasn't been filled in: an empty slot right before ')' or ';', like `if ()`, `_x = ;` or
 * `forEach (_e in )`, or the variable of `= 5;` and `++;`. New graph nodes start with their fields empty, and
 * this keeps their text readable until they're filled in. Compiling one is an error.
 */
struct FBangoEmptyExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::Empty;
	FBangoEmptyExpr() : FBangoExpr(StaticKind) {}
};

/** isNil(x). Operand is always a Name or an Index chain rooted at a Name. */
struct FBangoIsNilExpr : FBangoExpr
{
	static constexpr EBangoExprKind StaticKind = EBangoExprKind::IsNil;
	FBangoIsNilExpr() : FBangoExpr(StaticKind) {}

	FBangoExprPtr Operand;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class EBangoStmtKind : uint8
{
	VarDecl,
	Assign,
	IncDec,
	Call,
	If,
	While,
	For,
	ForEach,
	Sequence,
	Spawn,
	BlockCall,
	Switch,
	Break,
	Continue,
	Return,
	Label,
	Goto,
	Params,
	Reroute,
	WaitUntil,
};

struct FBangoStmt
{
	const EBangoStmtKind Kind;
	FBangoSourceLocation Loc;

	/**
	 * The graph node for this statement. Statements inside a `for (...)` header are part
	 * of the for node and have Node.Line == 0.
	 */
	FBangoNodeInfo Node;

	/**
	 * True if the statement is between slash-star and star-slash: a disabled node (spec section 1). It's
	 * parsed but never compiled, so it does nothing and execution goes on to the next statement; a
	 * disabled if, while, for or forEach skips its whole block. Only the statements written directly
	 * in the region are marked: those in their blocks keep their own setting.
	 */
	bool bDisabled = false;

	virtual ~FBangoStmt() = default;

	template<typename T> T* As() { return Kind == T::StaticKind ? static_cast<T*>(this) : nullptr; }
	template<typename T> const T* As() const { return Kind == T::StaticKind ? static_cast<const T*>(this) : nullptr; }

protected:
	explicit FBangoStmt(EBangoStmtKind InKind) : Kind(InKind) {}
};

using FBangoStmtPtr = TUniquePtr<FBangoStmt>;
using FBangoBlock = TArray<FBangoStmtPtr>;

/**
 * `int _x = 1;` (a local), `scriptVar int _x = 1;` (a script variable) or `global int x;` (a global).
 *  - A local can be declared anywhere: it exists from its declaration to the end of its block (as in C), and gets
 *    its value each time the declaration runs. It's a graph node ("Local Int" and so on).
 *  - A script variable is declared at the top of its graph, before its code, with a fixed value (or none); it
 *    exists in the whole graph and gets its value when the graph starts. It isn't a node: the editor lists it.
 *  - A global is declared at the top of the file, with a default or none: `global int gold;`, `global int gold ?= 0;`
 *    (bDefault). Or in the code, with a value it's set to each time the line runs: `global bool opened = true;`. It
 *    exists in the whole file either way. In the code, it's a graph node ("Set Global Bool" and so on).
 */
struct FBangoVarDeclStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::VarDecl;
	FBangoVarDeclStmt() : FBangoStmt(StaticKind) {}

	bool bGlobal = false;
	bool bScriptVar = false;
	/** `persistent global int gold;`: a global that outlives the world (FBangoGlobalStore). Only with bGlobal, for now. */
	bool bPersistent = false;
	/** A local: not global, not a script variable. */
	bool IsLocal() const { return !bGlobal && !bScriptVar; }
	FBangoType Type;
	FString Name;
	/** Null if declared without a value (nil). */
	FBangoExprPtr Init;
	/** A global's `?= value`: Init is its default. Otherwise a global's Init (`= value`) is set each time the line runs. */
	bool bDefault = false;
	/** A global declared in the code: `global bool opened = true;`, a statement (not at the top of the file). */
	bool IsGlobalAssignment() const { return bGlobal && !bDefault && Init.IsValid(); }
};

enum class EBangoAssignOp : uint8
{
	Assign,    // =
	Add,       // +=
	Subtract,  // -=
	Multiply,  // *=
	Divide,    // /=
};

/** `x = e;` `_a[i] += e;` `_v.x = e;` Target is a Name or an Index chain rooted at a Name, optionally ending in one vector part (FBangoMemberExpr). */
struct FBangoAssignStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Assign;
	FBangoAssignStmt() : FBangoStmt(StaticKind) {}

	EBangoAssignOp Op = EBangoAssignOp::Assign;
	FBangoExprPtr Target;
	FBangoExprPtr Value;
};

/** `x++;` `--x;` Target is a place, as for FBangoAssignStmt. */
struct FBangoIncDecStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::IncDec;
	FBangoIncDecStmt() : FBangoStmt(StaticKind) {}

	bool bIncrement = true;
	FBangoExprPtr Target;
};

/** A call used as a statement: `WriteScreen("x");` Call is always an FBangoCallExpr. */
struct FBangoCallStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Call;
	FBangoCallStmt() : FBangoStmt(StaticKind) {}

	FBangoExprPtr Call;
};

/** `if (c) {...} else {...};`  An `else if` is an Else block holding a single If. */
struct FBangoIfStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::If;
	FBangoIfStmt() : FBangoStmt(StaticKind) {}

	FBangoExprPtr Condition;
	FBangoBlock Then;
	bool bHasElse = false;
	FBangoBlock Else;
};

struct FBangoWhileStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::While;
	FBangoWhileStmt() : FBangoStmt(StaticKind) {}

	FBangoExprPtr Condition;
	FBangoBlock Body;
};

/** `for (Init; Condition; Step) {...};` Each part may be null. */
struct FBangoForStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::For;
	FBangoForStmt() : FBangoStmt(StaticKind) {}

	/** A local VarDecl, an Assign or an IncDec. */
	FBangoStmtPtr Init;
	FBangoExprPtr Condition;
	/** An Assign or an IncDec. */
	FBangoStmtPtr Step;
	FBangoBlock Body;
};

/** `forEach (_x in Array) {...};` */
struct FBangoForEachStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::ForEach;
	FBangoForEachStmt() : FBangoStmt(StaticKind) {}

	FString VarName;
	FBangoExprPtr Array;
	FBangoBlock Body;
};

/**
 * `sequence { ... } { ... }`: runs its blocks in order, each to the end (sleeps and all) before the next starts;
 * then what follows. Only groups statements: it makes no scope, and break, continue, return and goto work in its
 * blocks as they do outside them.
 */
struct FBangoSequenceStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Sequence;
	FBangoSequenceStmt() : FBangoStmt(StaticKind) {}

	/** At least one. */
	TArray<FBangoBlock> Blocks;
};

/**
 * `spawn { ... }` or `_id = spawn { ... }`: starts its block as a separate running copy of the script, with copies of
 * the variables as they are now, and carries on straight away (after storing the copy's id in IdTarget, if given). In
 * the block, return ends the copy; break, continue and goto can't leave it.
 */
struct FBangoSpawnStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Spawn;
	FBangoSpawnStmt() : FBangoStmt(StaticKind) {}

	/** Where the copy's id goes (an int variable, or an element of an int array), or null. */
	FBangoExprPtr IdTarget;
	/**
	 * `int _t = spawn { ... }`: the line declares the variable too, as a local (IdTarget is then its name). It's
	 * visible after the spawn (not in its block), like a local declared there.
	 */
	bool bDeclaresId = false;
	FBangoType IdType;
	FString IdName;
	FBangoSourceLocation IdNameLoc;
	FBangoBlock Body;
};

/**
 * `@john Speech(0, 1) { ... }`: a command that takes a block (FBangoCommandSignature::Block). The command runs at the
 * opening brace and its Close handler at the closing brace; the commands that belong to it (Inside) are called in
 * the block. The parser makes one of any call followed by a `{`; the compiler says whether the command takes one.
 */
struct FBangoBlockCallStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::BlockCall;
	FBangoBlockCallStmt() : FBangoStmt(StaticKind) {}

	/** An FBangoCallExpr. */
	FBangoExprPtr Call;
	FBangoBlock Body;
};

/** One `case v1, v2: { ... }` of a switch. The case line is not a node of its own: it is a row on the switch node. */
struct FBangoSwitchCase
{
	/** At least one; each a fixed value (a literal) of the switch value's type. */
	TArray<FBangoExprPtr> Values;
	FBangoSourceLocation Loc;
	/** The values' text as columns [ValuesBegin, ValuesEnd) on Loc.Line: the row's editable text. */
	int32 ValuesBegin = 0;
	int32 ValuesEnd = 0;
	FBangoBlock Body;
};

/**
 * `switch (value) { case 1: { ... } case 2, 3: { ... } default: { ... } }`: evaluates the value once and runs the
 * first case that has an equal value, else the default (if any); then what follows. There is no fallthrough. The
 * value is an int, a string or an entity. Like sequence, it isn't a loop: break and continue inside it act on the
 * loop around it.
 */
struct FBangoSwitchStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Switch;
	FBangoSwitchStmt() : FBangoStmt(StaticKind) {}

	FBangoExprPtr Value;
	TArray<FBangoSwitchCase> Cases;
	bool bHasDefault = false;
	FBangoSourceLocation DefaultLoc;
	FBangoBlock Default;
};

struct FBangoBreakStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Break;
	FBangoBreakStmt() : FBangoStmt(StaticKind) {}
};

struct FBangoContinueStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Continue;
	FBangoContinueStmt() : FBangoStmt(StaticKind) {}
};

struct FBangoReturnStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Return;
	FBangoReturnStmt() : FBangoStmt(StaticKind) {}

	/** Null for `return;` */
	FBangoExprPtr Value;
};

/**
 * `waitUntil (Condition);`: carries on at once if Condition is true, else pauses the script until it is. Rechecked when
 * a global it reads changes, or every frame if it calls a command (the compiler's FBangoWaitSet says which).
 *
 * `waitUntil (Condition) timeout (Seconds) { ... }`: the same, but it waits no longer than Seconds (worked out once,
 * before the first check): once they've passed with the condition still false, TimeoutBlock runs, then what follows.
 *
 * `waitUntil (Condition) heldFor (Seconds);`: the condition must stay true for Seconds (worked out once, before the
 * first check) without a break. If it turns false first, the count starts over the next time it's true.
 *
 * Both together, `heldFor (...)` first. The timeout's time runs from the start, hold or not. If it's up while the
 * condition is true and being held, the hold is let finish (and the timeout block runs if the condition turns false
 * first), unless the timeout's second value, IgnoreHold, is true: `timeout (Seconds, true)` runs its block then.
 *
 * `timeout` and `heldFor` aren't reserved: they're only read as words of the language here, after a `)`.
 */
struct FBangoWaitUntilStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::WaitUntil;
	FBangoWaitUntilStmt() : FBangoStmt(StaticKind) {}

	FBangoExprPtr Condition;
	bool bHasHold = false;
	/** The seconds the condition must stay true for (a number). Only with bHasHold. */
	FBangoExprPtr Hold;
	bool bHasTimeout = false;
	/** The seconds to wait at most (a number). Only with bHasTimeout. */
	FBangoExprPtr Timeout;
	/** Whether the timeout cuts a hold short (a bool), or null (it doesn't). Only with bHasTimeout and bHasHold. */
	FBangoExprPtr IgnoreHold;
	/** What runs if the time is up first. Only with bHasTimeout. */
	FBangoBlock TimeoutBlock;
};

struct FBangoLabelStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Label;
	FBangoLabelStmt() : FBangoStmt(StaticKind) {}

	/** Empty for `label ;` (a new node not filled in yet), which doesn't compile. */
	FString Name;
	FBangoSourceLocation NameLoc;
};

struct FBangoGotoStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Goto;
	FBangoGotoStmt() : FBangoStmt(StaticKind) {}

	/** Empty for `goto ;` (a new node not filled in yet), which doesn't compile. */
	FString Name;
	FBangoSourceLocation NameLoc;
};

/**
 * A reroute node: a line with only node metadata (`//@ 7 (10,4)`) where a statement could go. It does nothing;
 * it's a point to bend a wire through in the graph editor.
 */
struct FBangoRerouteStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Reroute;
	FBangoRerouteStmt() : FBangoStmt(StaticKind) {}
};

/** One parameter in a script's `inputs (...)` line. */
struct FBangoScriptParamDecl
{
	FBangoType Type;
	FString Name;
	FBangoSourceLocation Loc;
	/** A literal (number, string, bool, nil, or an array of those). Null if the parameter is required. */
	FBangoExprPtr Default;
};

/** `#inputs (int _a, string _b = "x")` Always the first statement of a script, if present. */
struct FBangoParamsStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::Params;
	FBangoParamsStmt() : FBangoStmt(StaticKind) {}

	TArray<FBangoScriptParamDecl> Params;
};

// ---------------------------------------------------------------------------
// Functions and scripts
// ---------------------------------------------------------------------------

struct FBangoParam
{
	FBangoType Type;
	FString Name;
	FBangoSourceLocation Loc;
};

/** `function int Name(int _a) {...};` The function line itself is not a graph node. */
struct FBangoFunction
{
	bool bReturnsVoid = true;
	/** Only meaningful if !bReturnsVoid. */
	FBangoType ReturnType;
	FString Name;
	FBangoSourceLocation Loc;
	TArray<FBangoParam> Params;
	FBangoBlock Body;
	/** The line of the body's closing '}'. */
	int32 EndLine = 0;
};

struct FBangoScript
{
	FString FileName;
	TArray<TUniquePtr<FBangoFunction>> Functions;
	/** The script's entry point: every top-level statement, in order. If the script has an inputs line, it is TopLevel[0]. */
	FBangoBlock TopLevel;
	/** Ordinary comments, in file order, for the graph editor. The compiler ignores them. */
	TArray<FBangoSourceComment> Comments;
	/**
	 * Every LOCTEXT / NSLOCTEXT / NOTELOCTEXT / NSNOTELOCTEXT in the script, in file order (not INVTEXT, which isn't
	 * translated), for localization gathering (Bango::GetLocalizedTexts). Points into the tree. Texts in disabled code
	 * (disabled nodes) aren't here: disabled code isn't run, so its texts aren't translated.
	 */
	TArray<const FBangoLiteralExpr*> TextLiterals;

	/**
	 * `#LOCTEXT_NAMESPACE "Bango.Intro"`: the namespace for the script's LOCTEXT and NOTELOCTEXT texts. Like the
	 * other settings lines, it comes before or after the inputs line (it's written before it), and before everything
	 * else. Settings lines aren't statements: they have no graph nodes.
	 */
	bool bHasLocNamespace = false;
	FString LocNamespace;
	FBangoSourceLocation LocNamespaceLoc;

	/**
	 * `#USES_GRAPH_EDITOR`: the graph editor saves this script with its section headings and node metadata (node
	 * positions). Without it, the script is saved compact. The editor adds it once its nodes are moved in a script with
	 * at least the project's Large Script Threshold of nodes.
	 */
	bool bUsesGraphEditor = false;
	FBangoSourceLocation UsesGraphEditorLoc;

	/** The inputs line, or null if the script has none. Points into TopLevel. */
	const FBangoParamsStmt* GetParams() const
	{
		return TopLevel.Num() > 0 ? TopLevel[0]->As<FBangoParamsStmt>() : nullptr;
	}
};

namespace Bango
{
	/**
	 * A compact one-line dump of the tree, for tests and debugging. For example:
	 * `(global int gold) (if (> gold 10) ((call WriteScreen "rich")))`
	 */
	BANGOSCRIPTS_API FString DumpScript(const FBangoScript& Script);
	BANGOSCRIPTS_API FString DumpExpr(const FBangoExpr& Expr);
}
