// Bango text editor: keeps node metadata (//@12(4, 7)) out of the text being edited.
// See "Bango Text Editor Spec", section 4, Hidden metadata.

#include "BangoTextMetadata.h"

#include "BangoLineTokenizer.h"

namespace BangoTextMetadataPrivate
{
	static bool IsSpace(TCHAR C)
	{
		return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\r');
	}

	static bool IsBlank(const FString& Line)
	{
		for (int32 I = 0; I < Line.Len(); ++I)
		{
			if (!IsSpace(Line[I]))
			{
				return false;
			}
		}
		return true;
	}

	static bool SameText(const FString& A, const FString& B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}

	/** The kind of the line's last token, or Default if it has none. */
	static EBangoSyntaxKind LastTokenKind(const FString& Line, int32* OutStart = nullptr)
	{
		TArray<FBangoSyntaxToken> Tokens;
		FBangoLineTokenizer::TokenizeLine(FStringView(Line), Tokens);
		if (Tokens.Num() == 0)
		{
			return EBangoSyntaxKind::Default;
		}
		const FBangoSyntaxToken& Last = Tokens[Tokens.Num() - 1];
		if (OutStart)
		{
			*OutStart = Last.Start;
		}
		return Last.Kind;
	}
}

void FBangoMetadataTracker::SplitLines(const FString& Text, TArray<FString>& OutLines)
{
	OutLines.Reset();
	int32 Start = 0;
	for (int32 I = 0; I <= Text.Len(); ++I)
	{
		if (I == Text.Len() || Text[I] == TEXT('\n'))
		{
			int32 End = I;
			if (End > Start && Text[End - 1] == TEXT('\r'))
			{
				--End;
			}
			OutLines.Add(Text.Mid(Start, End - Start));
			Start = I + 1;
		}
	}
}

FString FBangoMetadataTracker::JoinLines(const TArray<FString>& InLines)
{
	FString Result;
	for (int32 I = 0; I < InLines.Num(); ++I)
	{
		if (I > 0)
		{
			Result += TEXT("\n");
		}
		Result += InLines[I];
	}
	return Result;
}

FString FBangoMetadataTracker::SplitLine(const FString& Line, FBangoLineMetadata& OutMeta)
{
	using namespace BangoTextMetadataPrivate;

	OutMeta = FBangoLineMetadata();

	// The tokenizer finds metadata as the lexer does: //@ outside a string, running to the end of the line.
	int32 MetaStart = INDEX_NONE;
	if (LastTokenKind(Line, &MetaStart) != EBangoSyntaxKind::Metadata)
	{
		return Line;
	}

	int32 CodeEnd = MetaStart;
	while (CodeEnd > 0 && IsSpace(Line[CodeEnd - 1]))
	{
		--CodeEnd;
	}
	OutMeta.Spacing = Line.Mid(CodeEnd, MetaStart - CodeEnd);
	OutMeta.Metadata = Line.Mid(MetaStart, Line.Len() - MetaStart);
	return Line.Mid(0, CodeEnd);
}

FString FBangoMetadataTracker::JoinLine(const FString& Visible, const FBangoLineMetadata& Meta)
{
	using namespace BangoTextMetadataPrivate;

	if (!Meta.HasMetadata())
	{
		return Visible;
	}
	FString Result = Visible;
	if (!Meta.Spacing.IsEmpty())
	{
		Result += Meta.Spacing;
	}
	else if (Visible.Len() > 0 && !IsSpace(Visible[Visible.Len() - 1]))
	{
		// Something between the code and the //@, so a line ending in '/' can't run into it.
		Result += TEXT("    ");
	}
	Result += Meta.Metadata;
	return Result;
}

void FBangoMetadataTracker::Remap(const TArray<FString>& OldLines, const TArray<FBangoLineMetadata>& OldMeta,
	const TArray<FString>& NewLines, int32 CursorLine, TArray<FBangoLineMetadata>& OutMeta)
{
	using namespace BangoTextMetadataPrivate;

	const int32 OldNum = OldLines.Num();
	const int32 NewNum = NewLines.Num();
	OutMeta.Reset();
	OutMeta.SetNum(NewNum);

	auto OldMetaAt = [&OldMeta](int32 I) -> const FBangoLineMetadata&
	{
		static const FBangoLineMetadata None;
		return I >= 0 && I < OldMeta.Num() ? OldMeta[I] : None;
	};

	// The unchanged lines above and below the edit. The edit ends on the cursor's line, so the unchanged lines
	// above it are above that line, and the ones below start at it or later. (Enter at the end of a line puts the
	// cursor on the new line; Enter at the start of one leaves it on the line's code, which moved down.)
	// This only matters when lines next to the edit read the same, such as blank lines.
	const bool bHasCursor = CursorLine >= 0 && CursorLine < NewNum;
	const int32 MaxCommon = FMath::Min(OldNum, NewNum);

	int32 Prefix = 0;
	while (Prefix < MaxCommon && (!bHasCursor || Prefix < CursorLine) && SameText(OldLines[Prefix], NewLines[Prefix]))
	{
		++Prefix;
	}

	int32 Suffix = 0;
	while (Prefix + Suffix < MaxCommon
		&& (!bHasCursor || NewNum - Suffix - 1 >= CursorLine)
		&& SameText(OldLines[OldNum - 1 - Suffix], NewLines[NewNum - 1 - Suffix]))
	{
		++Suffix;
	}

	for (int32 I = 0; I < Prefix; ++I)
	{
		OutMeta[I] = OldMetaAt(I);
	}
	for (int32 I = 0; I < Suffix; ++I)
	{
		OutMeta[NewNum - 1 - I] = OldMetaAt(OldNum - 1 - I);
	}

	// The changed lines: old [Prefix, OldEnd) became new [Prefix, NewEnd).
	const int32 OldEnd = OldNum - Suffix;
	const int32 NewEnd = NewNum - Suffix;
	const int32 OldCount = OldEnd - Prefix;
	const int32 NewCount = NewEnd - Prefix;

	if (OldCount == NewCount)
	{
		// Lines edited in place: each keeps its own.
		for (int32 I = 0; I < NewCount; ++I)
		{
			OutMeta[Prefix + I] = OldMetaAt(Prefix + I);
		}
	}
	else if (OldCount > 0 && NewCount > 0)
	{
		TArray<bool> OldUsed;
		OldUsed.SetNum(OldCount);
		for (int32 I = 0; I < OldCount; ++I)
		{
			OldUsed[I] = false;
		}

		// Lines whose text comes back exactly, in order, keep their metadata.
		int32 SearchFrom = Prefix;
		for (int32 I = Prefix; I < OldEnd; ++I)
		{
			if (!OldMetaAt(I).HasMetadata())
			{
				continue;
			}
			for (int32 J = SearchFrom; J < NewEnd; ++J)
			{
				if (!OutMeta[J].HasMetadata() && SameText(OldLines[I], NewLines[J]))
				{
					OutMeta[J] = OldMetaAt(I);
					OldUsed[I - Prefix] = true;
					SearchFrom = J + 1;
					break;
				}
			}
		}

		// One line split into several (Enter), or several joined into one (Backspace, Delete): the first line with
		// code keeps the first metadata still unused. That's the left half of a split, the code moved down by Enter
		// after an indent, or the upper line of a join (the lower line's if the upper had none).
		if (OldCount == 1 || NewCount == 1)
		{
			int32 Target = Prefix;
			for (int32 J = Prefix; J < NewEnd; ++J)
			{
				if (!IsBlank(NewLines[J]))
				{
					Target = J;
					break;
				}
			}
			if (!OutMeta[Target].HasMetadata())
			{
				for (int32 I = Prefix; I < OldEnd; ++I)
				{
					if (!OldUsed[I - Prefix] && OldMetaAt(I).HasMetadata())
					{
						OutMeta[Target] = OldMetaAt(I);
						OldUsed[I - Prefix] = true;
						break;
					}
				}
			}
		}
	}
	// else: only lines added (no metadata) or only lines removed (their metadata goes with them).

	// Metadata after a // comment would be read as part of the comment.
	for (int32 J = Prefix; J < NewEnd; ++J)
	{
		if (OutMeta[J].HasMetadata() && LastTokenKind(NewLines[J]) == EBangoSyntaxKind::Comment)
		{
			OutMeta[J] = FBangoLineMetadata();
		}
	}
}

FString FBangoMetadataTracker::Reset(const FString& FullText)
{
	TArray<FString> FullLines;
	SplitLines(FullText, FullLines);

	Lines.Reset();
	Meta.Reset();
	for (const FString& FullLine : FullLines)
	{
		FBangoLineMetadata LineMeta;
		Lines.Add(SplitLine(FullLine, LineMeta));
		Meta.Add(LineMeta);
	}
	VisibleText = JoinLines(Lines);

	History.Reset();
	PushHistory();
	return VisibleText;
}

void FBangoMetadataTracker::OnVisibleTextChanged(const FString& NewVisibleText, int32 CursorLine)
{
	TArray<FString> NewLines;
	SplitLines(NewVisibleText, NewLines);
	const FString NewText = JoinLines(NewLines);
	if (NewText.Equals(VisibleText, ESearchCase::CaseSensitive))
	{
		return;
	}

	// A text it had before (Undo, Redo): that text's metadata.
	for (int32 I = History.Num() - 1; I >= 0; --I)
	{
		if (History[I].VisibleText.Equals(NewText, ESearchCase::CaseSensitive))
		{
			VisibleText = NewText;
			Lines = MoveTemp(NewLines);
			Meta = History[I].Meta;
			PushHistory();
			return;
		}
	}

	TArray<FBangoLineMetadata> NewMeta;
	Remap(Lines, Meta, NewLines, CursorLine, NewMeta);
	VisibleText = NewText;
	Lines = MoveTemp(NewLines);
	Meta = MoveTemp(NewMeta);
	PushHistory();
}

FString FBangoMetadataTracker::GetFullText() const
{
	FString Result;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		if (I > 0)
		{
			Result += TEXT("\n");
		}
		Result += JoinLine(Lines[I], I < Meta.Num() ? Meta[I] : FBangoLineMetadata());
	}
	return Result;
}

FString FBangoMetadataTracker::GetGutterText() const
{
	FString Result;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		if (I > 0)
		{
			Result += TEXT("\n");
		}
		if (I < Meta.Num())
		{
			Result += Meta[I].Metadata;
		}
	}
	return Result;
}

void FBangoMetadataTracker::PushHistory()
{
	if (History.Num() > 0 && History[History.Num() - 1].VisibleText.Equals(VisibleText, ESearchCase::CaseSensitive))
	{
		History[History.Num() - 1].Meta = Meta;
		return;
	}
	if (History.Num() >= MaxHistory)
	{
		History.RemoveAt(0);
	}
	FState State;
	State.VisibleText = VisibleText;
	State.Meta = Meta;
	History.Add(MoveTemp(State));
}
