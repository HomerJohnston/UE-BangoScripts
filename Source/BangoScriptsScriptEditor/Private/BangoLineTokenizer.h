// Bango text editor: the text layer of syntax highlighting.
// See "Bango Text Editor Spec", section 2.
//
// Works from one line alone: Bango statements fit on one line, strings end on
// the line they start and there are no block comments, so no state is carried
// from one line to the next.
//
// Depends only on FStringView and TArray, so it can be unit-tested without Slate.

#pragma once

#include "CoreMinimal.h"

enum class EBangoSyntaxKind : uint8
{
	Default,

	// Text layer
	Keyword,            // if else while for forEach in
	JumpKeyword,        // return break continue
	LabelKeyword,       // label goto
	LabelName,          // the name after label / goto
	DeclKeyword,        // global function
	Type,               // int float ... and a [] right after one
	Constant,           // true false
	Nil,                // nil
	Preprocessor,       // #define #undef params LOCTEXT_NAMESPACE
	Local,              // _name
	Global,             // any other name that isn't a call
	Entity,             // @john
	Number,
	String,
	StringEscape,       // \n \" \\ and `x in a localized source
	Placeholder,        // %0 %% and {0}
	LocMacro,           // LOCTEXT NSLOCTEXT NOTELOCTEXT NSNOTELOCTEXT
	LocKey,             // key / namespace string of a localized-text macro
	LocNotes,           // notes string of NOTELOCTEXT / NSNOTELOCTEXT
	VectorPart,         // .x .y .z (the part after the dot)
	Call,               // a name followed by (
	Builtin,            // IsDefined Count Push InsertAt Pop RemoveAt
	Comment,
	Metadata,           // //@12(4, 7) to the end of the line
	DisconnectedMarker, // the //~ at the start of a disconnected line
	Structure,          // { }
	Operator,
	Punctuation,        // ( ) [ ] , ; .
	Error,

	// Meaning layer (planned; set by the semantic pass, never by the tokenizer)
	Command,
	Function,
	Parameter,
	UndeclaredGlobal,

	Count
};

struct FBangoSyntaxToken
{
	EBangoSyntaxKind Kind = EBangoSyntaxKind::Default;

	/** Start of the token, in characters from the start of the line. */
	int32 Start = 0;

	/** Length in characters. */
	int32 Len = 0;

	/** True for every token on a //~ line after the //~ itself. */
	bool bDisconnected = false;
};

class FBangoLineTokenizer
{
public:
	/**
	 * Tokenizes one line (without its line break). Appends to OutTokens, in order,
	 * without overlaps. Whitespace gets no token; callers treat gaps as Default.
	 */
	static void TokenizeLine(FStringView Line, TArray<FBangoSyntaxToken>& OutTokens);
};
