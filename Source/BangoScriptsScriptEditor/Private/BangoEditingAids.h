// Bango text editor: editing comforts (indenting on Enter, outdenting a typed '}', matching brackets).
// See "Bango Text Editor Spec", section 4, Editing.
//
// Depends only on FString, TArray and the line tokenizer, so it can be unit-tested without Slate.

#pragma once

#include "CoreMinimal.h"
#include "BangoLineTokenizer.h"

/** A place in the text: a line and a column, both from 0. */
struct FBangoTextPosition
{
	int32 Line = 0;
	int32 Column = 0;
};

/** What pressing Enter types. */
struct FBangoNewLineEdit
{
	/** Typed at the cursor in place of the line break: "\n" and the new line's indentation (and, between braces, a second line). */
	FString Insert;

	/** Where the cursor goes afterwards: lines below the cursor's line, and the column on that line. */
	int32 CursorLinesDown = 1;
	int32 CursorColumn = 0;
};

/** The bracket next to the cursor, and its match. */
struct FBangoBracketMatch
{
	/** Whether the cursor is next to a bracket at all. */
	bool bFound = false;
	FBangoTextPosition Bracket;

	/** Whether it has a match; a bracket without one is shown as an error. */
	bool bMatched = false;
	FBangoTextPosition Match;
};

class FBangoEditingAids
{
public:
	/**
	 * Enter with the cursor at CursorColumn of Line. The new line gets the same indentation (the tabs and spaces the
	 * line starts with, up to the cursor), one tab more after a '{'. Between '{' and '}' the '}' goes on a line of its
	 * own at the outer indentation, and the cursor on an indented line between them.
	 */
	static FBangoNewLineEdit NewLine(const FString& Line, int32 CursorColumn);

	/**
	 * Typing '}' at CursorColumn of Line: if everything before the cursor is indentation ending in a tab, the column of
	 * that tab, which should be removed first so the '}' lines up with its block's start. Otherwise INDEX_NONE.
	 */
	static int32 OutdentBeforeClosingBrace(const FString& Line, int32 CursorColumn);

	/**
	 * The bracket ({ } ( ) [ ]) just before the cursor, or else just after it, and its match. Brackets in strings and
	 * comments aren't brackets (the tokenizer tells them apart). Braces match across lines. Code on disconnected (//~)
	 * lines only matches code on disconnected lines, and connected code only connected code.
	 */
	static FBangoBracketMatch FindMatchingBracket(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens,
		const FBangoTextPosition& Cursor);
};
