// Bango text editor: keeps node metadata (//@12(4, 7)) out of the text being edited.
// See "Bango Text Editor Spec", section 4, Hidden metadata.
//
// The editable box shows the script without its metadata; the metadata is kept here, one entry per line,
// and shown in a gutter beside the text. Metadata is always the last thing on its line, so taking it off
// never changes the number of lines or the columns before it.
//
// Depends only on FString and TArray (and the line tokenizer), so it can be unit-tested without Slate.

#pragma once

#include "CoreMinimal.h"

/** One line's metadata. Lines without any have an empty Metadata. */
struct FBangoLineMetadata
{
	/** The spaces and tabs between the line's code and its metadata, as they were, so an unchanged line comes back the same. */
	FString Spacing;

	/** From `//@` to the end of the line, like `//@12(4, 7)` or `//@7(4, 9) @8(8, 13)`. Empty if the line has none. */
	FString Metadata;

	bool HasMetadata() const { return !Metadata.IsEmpty(); }
};

class FBangoMetadataTracker
{
public:
	/** Splits text into lines on \n. A \r before a \n is dropped. There is always at least one line. */
	static void SplitLines(const FString& Text, TArray<FString>& OutLines);
	static FString JoinLines(const TArray<FString>& Lines);

	/** Takes the metadata off one line. Returns the line without it. */
	static FString SplitLine(const FString& Line, FBangoLineMetadata& OutMeta);

	/** Puts metadata back on a line without it. */
	static FString JoinLine(const FString& Visible, const FBangoLineMetadata& Meta);

	/**
	 * Works out each new line's metadata after an edit, from what the lines were before.
	 *  - Lines that didn't change, above and below the edit, keep theirs.
	 *  - A line edited in place keeps its own.
	 *  - A line split in two (Enter in the middle): the left half keeps it. Enter at the left of a line's code
	 *    moves it down with the code; Enter at the right end leaves it where it is.
	 *  - Lines joined into one: the upper one's, else the lower one's.
	 *  - Lines replaced by others (paste): a line whose text comes back exactly keeps its metadata; the rest lose theirs.
	 *  - A line that now ends in a // comment loses it, since metadata after a comment would be read as part of the comment.
	 * CursorLine, the cursor's line after the edit, tells apart edits that give the same text (INDEX_NONE if unknown).
	 */
	static void Remap(const TArray<FString>& OldLines, const TArray<FBangoLineMetadata>& OldMeta,
		const TArray<FString>& NewLines, int32 CursorLine, TArray<FBangoLineMetadata>& OutMeta);

	/** Starts again from a whole script's text. Returns the text without metadata, for the box. */
	FString Reset(const FString& FullText);

	/**
	 * After the box's text changed. If the text is one it had before (Undo, Redo), that text's metadata comes back,
	 * so undoing an edit that dropped metadata brings it back too.
	 */
	void OnVisibleTextChanged(const FString& NewVisibleText, int32 CursorLine);

	/** The whole script: the box's text with the metadata put back. */
	FString GetFullText() const;

	/** One line per line of text: its metadata, or empty. */
	FString GetGutterText() const;

	const TArray<FString>& GetLines() const { return Lines; }
	const TArray<FBangoLineMetadata>& GetMetadata() const { return Meta; }

	/** How many past texts are kept for Undo. */
	static constexpr int32 MaxHistory = 200;

private:
	struct FState
	{
		FString VisibleText;
		TArray<FBangoLineMetadata> Meta;
	};

	void PushHistory();

	FString VisibleText;
	TArray<FString> Lines;
	TArray<FBangoLineMetadata> Meta;
	TArray<FState> History;
};
