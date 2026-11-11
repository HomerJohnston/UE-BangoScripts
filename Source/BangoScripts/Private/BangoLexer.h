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
	KwIf, KwElse, KwWhile, KwFor, KwForEach, KwIn,
	KwBreak, KwContinue, KwReturn, KwLabel, KwGoto,
	KwFunction, KwGlobal, KwTrue, KwFalse, KwNil,
	KwBool, KwInt, KwDouble, KwString, KwEntity, KwText, KwVector2, KwVector3, KwVoid,
	KwParams,
	KwIsNil,

	/** `#define` / `#undef` at the start of a line: the script's settings lines (see FBangoScript). */
	HashDefine, HashUndef,

	// Punctuation
	LParen, RParen, LBrace, RBrace, LBracket, RBracket, Semicolon, Comma,
	/** `.` before a vector part: `_pos.x` */
	Dot,

	// Operators
	Plus, Minus, Star, Slash, Percent, Bang,
	Assign, EqualEqual, BangEqual,
	Less, LessEqual, Greater, GreaterEqual,
	AndAnd, OrOr,
	PlusAssign, MinusAssign, StarAssign, SlashAssign,
	PlusPlus, MinusMinus,
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
}
