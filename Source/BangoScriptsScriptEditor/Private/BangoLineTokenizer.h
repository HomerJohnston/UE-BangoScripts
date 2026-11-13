// Bango text editor: the text layer of syntax highlighting.
// See "Bango Text Editor Spec", section 2.
//
// Works from one line alone: Bango statements fit on one line and strings end on
// the line they start. The one thing carried from line to line is how many
// disabled regions (slash-star ... star-slash, spec section 1) are open: what's
// inside one is still code, tokenized as usual, and marked bDisabled.
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
	Local,              // a name the graph declares (set by FBangoScriptNames)
	Global,             // any other name that isn't a call (the tokenizer gives every name this)
	Entity,             // @john
	Number,
	String,
	StringEscape,       // \n \" \\ and `x in a localized source
	Placeholder,        // %0 %% and {0}
	LocMacro,           // LOCTEXT NSLOCTEXT NOTELOCTEXT NSNOTELOCTEXT INVTEXT
	LocKey,             // key / namespace string of a localized-text macro
	LocNotes,           // notes string of NOTELOCTEXT / NSNOTELOCTEXT
	VectorPart,         // .x .y .z (the part after the dot)
	Call,               // a name followed by (
	Builtin,            // IsNil Count Push InsertAt Pop RemoveAt
	Comment,
	Metadata,           // //@12(4, 7) to the end of the line
	DisconnectedMarker, // the //~ at the start of a disconnected line
	DisabledMarker,     // the slash-star and star-slash around disabled nodes
	Structure,          // { }
	Operator,
	Punctuation,        // ( ) [ ] , ; .
	Error,

	// Meaning layer: set from the whole file, never by the tokenizer
	Command,            // (planned)
	Function,           // (planned)
	Parameter,          // a script or function parameter (FBangoScriptNames)
	UndeclaredGlobal,   // a global the file uses but doesn't declare (FBangoScriptNames)

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

	/** True for every token inside a disabled region (but not for the markers themselves). */
	bool bDisabled = false;
};

class FBangoLineTokenizer
{
public:
	/**
	 * Tokenizes one line (without its line break). Appends to OutTokens, in order,
	 * without overlaps. Whitespace gets no token; callers treat gaps as Default.
	 */
	static void TokenizeLine(FStringView Line, TArray<FBangoSyntaxToken>& OutTokens);

	/**
	 * The same, for a line in a file: InOutDisabledDepth is how many disabled regions are open at the start of
	 * the line (0 for the first line), and is updated for the next one.
	 */
	static void TokenizeLine(FStringView Line, TArray<FBangoSyntaxToken>& OutTokens, int32& InOutDisabledDepth);
};
