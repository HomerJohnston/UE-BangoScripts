// Bango text editor: editing comforts (indenting on Enter, outdenting a typed '}', matching brackets).
// See "Bango Text Editor Spec", section 4, Editing.

#include "BangoEditingAids.h"

namespace BangoEditingAidsPrivate
{
	static bool IsIndent(TCHAR C)
	{
		return C == TEXT(' ') || C == TEXT('\t');
	}

	static bool IsCode(EBangoSyntaxKind Kind)
	{
		return Kind != EBangoSyntaxKind::Comment && Kind != EBangoSyntaxKind::Metadata && Kind != EBangoSyntaxKind::DisconnectedMarker;
	}

	/** The bracket a token is, or 0 if it isn't one. */
	static TCHAR BracketOf(const FString& Line, const FBangoSyntaxToken& Token)
	{
		if (Token.Len != 1 || (Token.Kind != EBangoSyntaxKind::Structure && Token.Kind != EBangoSyntaxKind::Punctuation))
		{
			return 0;
		}
		const TCHAR C = Line[Token.Start];
		switch (C)
		{
		case TEXT('{'): case TEXT('}'): case TEXT('('): case TEXT(')'): case TEXT('['): case TEXT(']'):
			return C;
		default:
			return 0;
		}
	}

	static bool IsOpening(TCHAR C) { return C == TEXT('{') || C == TEXT('(') || C == TEXT('['); }

	static TCHAR PartnerOf(TCHAR C)
	{
		switch (C)
		{
		case TEXT('{'): return TEXT('}');
		case TEXT('}'): return TEXT('{');
		case TEXT('('): return TEXT(')');
		case TEXT(')'): return TEXT('(');
		case TEXT('['): return TEXT(']');
		case TEXT(']'): return TEXT('[');
		default:        return 0;
		}
	}

	/** Whether the text's last code token (not a comment or metadata) is the structure character C. */
	static bool LastCodeTokenIs(const FString& Text, TCHAR C)
	{
		TArray<FBangoSyntaxToken> Tokens;
		FBangoLineTokenizer::TokenizeLine(FStringView(Text), Tokens);
		for (int32 I = Tokens.Num() - 1; I >= 0; --I)
		{
			if (IsCode(Tokens[I].Kind))
			{
				return Tokens[I].Len == 1 && Text[Tokens[I].Start] == C && Tokens[I].Kind == EBangoSyntaxKind::Structure;
			}
		}
		return false;
	}

	/** Whether the text's first code token is the structure character C. */
	static bool FirstCodeTokenIs(const FString& Text, TCHAR C)
	{
		TArray<FBangoSyntaxToken> Tokens;
		FBangoLineTokenizer::TokenizeLine(FStringView(Text), Tokens);
		for (const FBangoSyntaxToken& Token : Tokens)
		{
			if (IsCode(Token.Kind))
			{
				return Token.Len == 1 && Text[Token.Start] == C && Token.Kind == EBangoSyntaxKind::Structure;
			}
		}
		return false;
	}
}

FBangoNewLineEdit FBangoEditingAids::NewLine(const FString& Line, int32 CursorColumn)
{
	using namespace BangoEditingAidsPrivate;

	CursorColumn = FMath::Clamp(CursorColumn, 0, Line.Len());

	// The line's indentation, up to the cursor (Enter inside the indentation keeps the rest of it on the new line).
	int32 IndentLen = 0;
	while (IndentLen < CursorColumn && IsIndent(Line[IndentLen]))
	{
		++IndentLen;
	}
	const FString Indent = Line.Mid(0, IndentLen);

	const bool bAfterOpen = LastCodeTokenIs(Line.Mid(0, CursorColumn), TEXT('{'));
	const bool bBeforeClose = FirstCodeTokenIs(Line.Mid(CursorColumn, Line.Len() - CursorColumn), TEXT('}'));

	FBangoNewLineEdit Edit;
	if (bAfterOpen)
	{
		const FString Inner = Indent + TEXT("\t");
		Edit.Insert = FString(TEXT("\n")) + Inner;
		if (bBeforeClose)
		{
			// {|} -> the '}' on its own line, the cursor on an indented line between.
			Edit.Insert += FString(TEXT("\n")) + Indent;
		}
		Edit.CursorColumn = Inner.Len();
	}
	else
	{
		Edit.Insert = FString(TEXT("\n")) + Indent;
		Edit.CursorColumn = Indent.Len();
	}
	Edit.CursorLinesDown = 1;
	return Edit;
}

int32 FBangoEditingAids::OutdentBeforeClosingBrace(const FString& Line, int32 CursorColumn)
{
	using namespace BangoEditingAidsPrivate;

	if (CursorColumn <= 0 || CursorColumn > Line.Len() || Line[CursorColumn - 1] != TEXT('\t'))
	{
		return INDEX_NONE;
	}
	for (int32 I = 0; I < CursorColumn; ++I)
	{
		if (!IsIndent(Line[I]))
		{
			return INDEX_NONE;
		}
	}
	return CursorColumn - 1;
}

FBangoBracketMatch FBangoEditingAids::FindMatchingBracket(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens,
	const FBangoTextPosition& Cursor)
{
	using namespace BangoEditingAidsPrivate;

	FBangoBracketMatch Result;
	if (Cursor.Line < 0 || Cursor.Line >= Lines.Num() || Cursor.Line >= LineTokens.Num())
	{
		return Result;
	}

	// The bracket just before the cursor, or else just after it.
	const TArray<FBangoSyntaxToken>& CursorTokens = LineTokens[Cursor.Line];
	int32 StartToken = INDEX_NONE;
	const int32 Columns[] = { Cursor.Column - 1, Cursor.Column };
	for (const int32 Column : Columns)
	{
		for (int32 I = 0; I < CursorTokens.Num() && StartToken == INDEX_NONE; ++I)
		{
			if (CursorTokens[I].Start == Column && BracketOf(Lines[Cursor.Line], CursorTokens[I]) != 0)
			{
				StartToken = I;
			}
		}
		if (StartToken != INDEX_NONE)
		{
			break;
		}
	}
	if (StartToken == INDEX_NONE)
	{
		return Result;
	}

	const FBangoSyntaxToken& Start = CursorTokens[StartToken];
	const TCHAR Bracket = BracketOf(Lines[Cursor.Line], Start);
	const TCHAR Partner = PartnerOf(Bracket);
	const bool bForward = IsOpening(Bracket);
	const bool bDisconnected = Start.bDisconnected;

	Result.bFound = true;
	Result.Bracket.Line = Cursor.Line;
	Result.Bracket.Column = Start.Start;

	// Walk away from it, counting only this kind of bracket, until the depth comes back to 0.
	int32 Depth = 0;
	int32 Line = Cursor.Line;
	int32 TokenIndex = StartToken + (bForward ? 1 : -1);
	while (Line >= 0 && Line < Lines.Num() && Line < LineTokens.Num())
	{
		const TArray<FBangoSyntaxToken>& Tokens = LineTokens[Line];
		while (TokenIndex >= 0 && TokenIndex < Tokens.Num())
		{
			const FBangoSyntaxToken& Token = Tokens[TokenIndex];
			if (Token.bDisconnected == bDisconnected)
			{
				const TCHAR C = BracketOf(Lines[Line], Token);
				if (C == Bracket)
				{
					++Depth;
				}
				else if (C == Partner)
				{
					if (Depth == 0)
					{
						Result.bMatched = true;
						Result.Match.Line = Line;
						Result.Match.Column = Token.Start;
						return Result;
					}
					--Depth;
				}
			}
			TokenIndex += bForward ? 1 : -1;
		}

		Line += bForward ? 1 : -1;
		if (Line >= 0 && Line < LineTokens.Num())
		{
			TokenIndex = bForward ? 0 : LineTokens[Line].Num() - 1;
		}
	}
	return Result;
}
