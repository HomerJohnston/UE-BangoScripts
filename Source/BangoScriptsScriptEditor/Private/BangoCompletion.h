// Bango text editor: autocomplete. Which word is being typed, what it could be, and the list filtered by it.
// See "Bango Text Editor Spec", section 4, Autocomplete.
//
// Depends only on FString, TArray, the line tokenizer and FBangoScriptNames, so it can be unit-tested without Slate.
// The registered commands come in as items, made by the widget from the command table.

#pragma once

#include "CoreMinimal.h"
#include "BangoScriptNames.h"

enum class EBangoCompletionKind : uint8
{
	Parameter,
	Local,
	Global,
	Function,
	Command,
	Builtin,
};

/** One entry in the autocomplete list. */
struct FBangoCompletionItem
{
	FString Name;
	EBangoCompletionKind Kind = EBangoCompletionKind::Command;

	/** Shown beside the name: a variable's type, or a function's or command's signature. */
	FString Detail;

	/** Shown under the list for the selected entry: a command's one-line description. */
	FString Description;

	/** Commands, functions and built-ins: accepting one types `Name(`. */
	bool bCallable = false;
};

class FBangoCompletion
{
public:
	/**
	 * The name being typed: the letters, digits and `_` just before the cursor, starting with a letter or `_`.
	 * False where nothing should be offered: no such word, a number, after `@` (a Bango ID) or `.` (a vector part),
	 * inside a string or a comment, a label's name, and a name being declared (right after a type).
	 */
	static bool FindWordBeforeCursor(const FString& Line, int32 CursorColumn, int32& OutWordStart);

	/** The compiler's built-in calls, as items. */
	static void GetBuiltinItems(TArray<FBangoCompletionItem>& OutItems);

	/**
	 * Everything a line can use: its graph's parameters and locals, the file's globals and functions, then the given
	 * commands and the built-ins. Sorted by name (ignoring case); a name appears once.
	 */
	static void GatherItems(const FBangoScriptNames& Names, int32 LineIndex, const TArray<FBangoCompletionItem>& CommandItems,
		TArray<FBangoCompletionItem>& OutItems);

	/** The items starting with Prefix, ignoring case, in their order. */
	static void Filter(const TArray<FBangoCompletionItem>& Items, const FString& Prefix, TArray<int32>& OutIndices);
};
