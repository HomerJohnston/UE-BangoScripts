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

	/** True if the line had a `//@ id (x,y)` comment. The params line always counts as node 0 at (0,0), so it's true there too. */
	bool bHasMetadata = false;
	int64 NodeID = -1;
	int32 X = 0;
	int32 Y = 0;

	/**
	 * Where the node's editable text is on its line (1-based columns, end exclusive), for the graph editor:
	 * the condition of if/while, the header of for/forEach, the name of label/goto, the value of return,
	 * the inside of params (...), and the whole statement without ';' otherwise. Empty for break/continue.
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

/** IsNil(x). Operand is always a Name or an Index chain rooted at a Name. */
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
	Break,
	Continue,
	Return,
	Label,
	Goto,
	Params,
	Reroute,
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

/** `int _x = 1;` or `global int x;` */
struct FBangoVarDeclStmt : FBangoStmt
{
	static constexpr EBangoStmtKind StaticKind = EBangoStmtKind::VarDecl;
	FBangoVarDeclStmt() : FBangoStmt(StaticKind) {}

	bool bGlobal = false;
	FBangoType Type;
	FString Name;
	/** Null if declared without a value (nil). */
	FBangoExprPtr Init;
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

/** A call used as a statement: `Hint("x");` Call is always an FBangoCallExpr. */
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

/** One parameter in a script's `params (...)` line. */
struct FBangoScriptParamDecl
{
	FBangoType Type;
	FString Name;
	FBangoSourceLocation Loc;
	/** A literal (number, string, bool, nil, or an array of those). Null if the parameter is required. */
	FBangoExprPtr Default;
};

/** `#define params (int _a, string _b = "x")` Always the first statement of a script, if present. */
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
	/** The script's entry point: every top-level statement, in order. If the script has a params line, it is TopLevel[0]. */
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
	 * `#define LOCTEXT_NAMESPACE "Bango.Intro"`: the namespace for the script's LOCTEXT and NOTELOCTEXT texts,
	 * written like C++ so Unreal's localization gatherer reads it. Comes after the params line (if any) and before
	 * everything else; the script must then end with `#undef LOCTEXT_NAMESPACE` (LocNamespaceEndLoc). Neither is a
	 * statement: they have no graph nodes.
	 */
	bool bHasLocNamespace = false;
	FString LocNamespace;
	FBangoSourceLocation LocNamespaceLoc;
	FBangoSourceLocation LocNamespaceEndLoc;

	/** The params line, or null if the script has none. Points into TopLevel. */
	const FBangoParamsStmt* GetParams() const
	{
		return TopLevel.Num() > 0 ? TopLevel[0]->As<FBangoParamsStmt>() : nullptr;
	}
};

namespace Bango
{
	/**
	 * A compact one-line dump of the tree, for tests and debugging. For example:
	 * `(global int gold) (if (> gold 10) ((call Hint "rich")))`
	 */
	BANGOSCRIPTS_API FString DumpScript(const FBangoScript& Script);
	BANGOSCRIPTS_API FString DumpExpr(const FBangoExpr& Expr);
}
