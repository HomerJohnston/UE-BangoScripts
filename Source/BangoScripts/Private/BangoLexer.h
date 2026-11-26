#pragma once

#include "CoreMinimal.h"
#include "BangoAST.h"

struct FBangoCompileError;

enum class EBangoTokenKind : uint8
{
	EndOfFile,

	Identifier,
	IntLiteral,
	DoubleLiteral,
	StringLiteral,
	/** `@john`: a Bango ID name. Text is the name without the `@`. */
	EntityLiteral,

	// Keywords
	KwIf, KwElse, KwWhile, KwFor, KwForEach, KwIn, KwSequence, KwSpawn, KwWaitUntil, KwSwitch, KwCase, KwDefault,
	KwBreak, KwContinue, KwReturn, KwLabel, KwGoto,
	KwFunction, KwGlobal, KwScriptVar, KwPersistent, KwTrue, KwFalse, KwNil,
	KwBool, KwInt, KwDouble, KwString, KwEntity, KwText, KwVector2, KwVector3, KwVoid,
	KwIsNil,
	/** `target call Name(args)`: calls a Blueprint function on the target's actor (FBangoCallExpr::bReflect). */
	KwCall,

	/**
	 * A settings line's start: `#inputs`, `#TEXT_NAMESPACE` or `#USES_GRAPH_EDITOR`, first on its line (see
	 * FBangoScript). Its Text is the whole word, with the '#'.
	 */
	Directive,

	// Punctuation
	LParen, RParen, LBrace, RBrace, LBracket, RBracket, Semicolon, Comma, Colon,
	/** `.` before a vector part: `_pos.x` */
	Dot,

	// Operators
	Plus, Minus, Star, Slash, Percent, Bang,
	Assign, EqualEqual, BangEqual,
	Less, LessEqual, Greater, GreaterEqual,
	AndAnd, OrOr,
	PlusAssign, MinusAssign, StarAssign, SlashAssign,
	/** `?=`: a global's default, at the top of the file (`global int gold ?= 0;`). */
	QuestionAssign,
	PlusPlus, MinusMinus,

	// Slash-star and star-slash around disabled nodes (spec section 1). Not comments: what's between them must be statements.
	DisabledBegin, DisabledEnd,
};

struct FBangoToken
{
	EBangoTokenKind Kind = EBangoTokenKind::EndOfFile;
	/** Identifier name, the decoded string literal, the Bango ID name of an entity literal (without `@`), or the source text of other tokens. */
	FString Text;
	int64 IntValue = 0;
	double DoubleValue = 0.0;
	FBangoSourceLocation Loc;
	/** Column just past the token's last character. Tokens never span lines. */
	int32 EndColumn = 0;
};

struct FBangoLexResult
{
	TArray<FBangoToken> Tokens;  // always ends with EndOfFile
	/** `//@id(x, y)` comments, by line (the first entry, if a line has several). */
	TMap<int32, FBangoNodeInfo> Metadata;
	/** The second and later entries of a line with several, `//@7(4, 9) @8(8, 13)`: a run of reroute nodes. */
	TMap<int32, TArray<FBangoNodeInfo>> MoreMetadata;
	/** Lines that have metadata, in file order. */
	TArray<int32> MetadataLines;
	/** Ordinary `//` comments (not metadata), in file order. */
	TArray<FBangoSourceComment> Comments;
};

namespace Bango
{
	/** Splits Source into tokens and reads node metadata. Stops at the first error. */
	bool Lex(const FString& Source, FBangoLexResult& OutResult, FBangoCompileError& OutError);

	/** Text for error messages: "';'", "identifier 'john'", "end of file" */
	FString DescribeToken(const FBangoToken& Token);
	FString DescribeTokenKind(EBangoTokenKind Kind);

	// Added to errors inside a disabled region (slash-star ... star-slash): what's there is disabled code, not a comment.
	const TCHAR* GetDisabledCodeNote();
}
