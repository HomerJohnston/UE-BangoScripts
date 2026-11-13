// Bango text editor: the text layer of syntax highlighting.
// See "Bango Text Editor Spec", section 2.

#include "BangoLineTokenizer.h"

namespace BangoLineTokenizerPrivate
{
	static bool IsDigit(TCHAR C)      { return C >= TEXT('0') && C <= TEXT('9'); }
	static bool IsIdentStart(TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || C == TEXT('_'); }
	static bool IsIdentChar(TCHAR C)  { return IsIdentStart(C) || IsDigit(C); }
	static bool IsSpace(TCHAR C)      { return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\r'); }

	template <int32 N>
	static bool IsOneOf(FStringView Line, int32 Start, int32 Len, const TCHAR* const (&Words)[N])
	{
		for (const TCHAR* Word : Words)
		{
			int32 I = 0;
			while (Word[I] != 0 && I < Len && Line[Start + I] == Word[I])
			{
				++I;
			}
			if (Word[I] == 0 && I == Len)
			{
				return true;
			}
		}
		return false;
	}

	static const TCHAR* const FlowKeywords[]   = { TEXT("if"), TEXT("else"), TEXT("while"), TEXT("for"), TEXT("forEach"), TEXT("in") };
	static const TCHAR* const JumpKeywords[]   = { TEXT("return"), TEXT("break"), TEXT("continue") };
	static const TCHAR* const LabelKeywords[]  = { TEXT("label"), TEXT("goto") };
	static const TCHAR* const DeclKeywords[]   = { TEXT("global"), TEXT("function") };
	static const TCHAR* const TypeNames[]      = { TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("string"), TEXT("entity"), TEXT("text"), TEXT("vector2"), TEXT("vector3"), TEXT("void") };
	static const TCHAR* const Constants[]      = { TEXT("true"), TEXT("false") };
	static const TCHAR* const NilWord[]        = { TEXT("nil") };
	static const TCHAR* const PreprocWords[]   = { TEXT("params"), TEXT("LOCTEXT_NAMESPACE") };
	static const TCHAR* const Builtins[]       = { TEXT("IsNil"), TEXT("Count"), TEXT("Push"), TEXT("InsertAt"), TEXT("Pop"), TEXT("RemoveAt") };
	static const TCHAR* const InvalidWords[]   = { TEXT("double") };
	static const TCHAR* const VectorParts[]    = { TEXT("x"), TEXT("y"), TEXT("z") };

	// Localized-text macros and the role of each string argument, by position.
	enum class ELocRole : uint8 { Key, Source, Notes };

	struct FLocMacro
	{
		const TCHAR* Name;
		int32 NumArgs;
		ELocRole Roles[4];
	};

	static const FLocMacro LocMacros[] =
	{
		{ TEXT("LOCTEXT"),       2, { ELocRole::Key, ELocRole::Source } },
		{ TEXT("NSLOCTEXT"),     3, { ELocRole::Key, ELocRole::Key, ELocRole::Source } },
		{ TEXT("NOTELOCTEXT"),   3, { ELocRole::Key, ELocRole::Source, ELocRole::Notes } },
		{ TEXT("NSNOTELOCTEXT"), 4, { ELocRole::Key, ELocRole::Key, ELocRole::Source, ELocRole::Notes } },
		{ TEXT("INVTEXT"),       1, { ELocRole::Source } },
	};

	enum class EStringStyle : uint8 { Plain, LocKey, LocSource, LocNotes };

	class FLexer
	{
	public:
		FLexer(FStringView InLine, TArray<FBangoSyntaxToken>& InOut)
			: Line(InLine), Len(InLine.Len()), Out(InOut)
		{
		}

		void Run()
		{
			SkipSpaces();

			// A disconnected line: //~ then code, read as if the //~ weren't there.
			if (LookingAt(TEXT("//~")))
			{
				Emit(EBangoSyntaxKind::DisconnectedMarker, Pos, 3, false);
				Pos += 3;
				bDisconnected = true;
				SkipSpaces();
			}

			// A settings line: #define / #undef and the word after it.
			if (Pos < Len && Line[Pos] == TEXT('#'))
			{
				const int32 Start = Pos++;
				while (Pos < Len && IsIdentChar(Line[Pos]))
				{
					++Pos;
				}
				Emit(EBangoSyntaxKind::Preprocessor, Start, Pos - Start);
			}

			while (Pos < Len)
			{
				if (IsSpace(Line[Pos]))
				{
					++Pos;
					continue;
				}
				LexToken();
			}
		}

	private:
		FStringView Line;
		int32 Len;
		TArray<FBangoSyntaxToken>& Out;
		int32 Pos = 0;
		bool bDisconnected = false;

		// State that lives from one token to the next on this line.
		bool bExpectLabelName = false;
		const FLocMacro* PendingLocMacro = nullptr; // seen the name, waiting for (
		const FLocMacro* ActiveLocMacro = nullptr;  // inside its parentheses
		int32 LocDepth = 0;
		int32 LocArg = 0;
		bool bAfterDot = false;

		TCHAR At(int32 I) const { return I < Len ? Line[I] : TCHAR(0); }

		bool LookingAt(const TCHAR* S) const
		{
			for (int32 I = 0; S[I] != 0; ++I)
			{
				if (At(Pos + I) != S[I])
				{
					return false;
				}
			}
			return true;
		}

		void SkipSpaces()
		{
			while (Pos < Len && IsSpace(Line[Pos]))
			{
				++Pos;
			}
		}

		void Emit(EBangoSyntaxKind Kind, int32 Start, int32 TokenLen)
		{
			Emit(Kind, Start, TokenLen, bDisconnected);
		}

		void Emit(EBangoSyntaxKind Kind, int32 Start, int32 TokenLen, bool bDisc)
		{
			if (TokenLen <= 0)
			{
				return;
			}
			FBangoSyntaxToken Token;
			Token.Kind = Kind;
			Token.Start = Start;
			Token.Len = TokenLen;
			Token.bDisconnected = bDisc;
			Out.Add(Token);
		}

		void LexToken()
		{
			const TCHAR C = Line[Pos];

			// State that only lasts for the very next token.
			const bool bWasExpectingLabelName = bExpectLabelName;
			bExpectLabelName = false;
			const bool bWasAfterDot = bAfterDot;
			bAfterDot = false;
			const FLocMacro* LocMacroBeforeThis = PendingLocMacro;
			PendingLocMacro = nullptr;

			// Comments and metadata run to the end of the line.
			if (C == TEXT('/') && At(Pos + 1) == TEXT('/'))
			{
				const EBangoSyntaxKind Kind = At(Pos + 2) == TEXT('@') ? EBangoSyntaxKind::Metadata : EBangoSyntaxKind::Comment;
				Emit(Kind, Pos, Len - Pos);
				Pos = Len;
				return;
			}

			if (IsIdentStart(C))
			{
				LexIdentifier(bWasExpectingLabelName, bWasAfterDot);
				return;
			}

			if (IsDigit(C))
			{
				LexNumber();
				return;
			}

			if (C == TEXT('"'))
			{
				LexString(CurrentStringStyle());
				return;
			}

			if (C == TEXT('@'))
			{
				const int32 Start = Pos++;
				if (IsIdentStart(At(Pos)))
				{
					while (Pos < Len && IsIdentChar(Line[Pos]))
					{
						++Pos;
					}
					Emit(EBangoSyntaxKind::Entity, Start, Pos - Start);
				}
				else
				{
					Emit(EBangoSyntaxKind::Error, Start, 1);
				}
				return;
			}

			if (C == TEXT('.'))
			{
				if (IsDigit(At(Pos + 1)))
				{
					// .5 - a number needs a digit before the dot.
					const int32 Start = Pos++;
					while (Pos < Len && IsIdentChar(Line[Pos]))
					{
						++Pos;
					}
					Emit(EBangoSyntaxKind::Error, Start, Pos - Start);
					return;
				}
				Emit(EBangoSyntaxKind::Punctuation, Pos++, 1);
				bAfterDot = true;
				return;
			}

			LexOperatorOrPunctuation(LocMacroBeforeThis);
		}

		void LexIdentifier(bool bIsLabelName, bool bIsVectorPart)
		{
			const int32 Start = Pos;
			while (Pos < Len && IsIdentChar(Line[Pos]))
			{
				++Pos;
			}
			const int32 WordLen = Pos - Start;

			if (bIsVectorPart)
			{
				Emit(IsOneOf(Line, Start, WordLen, VectorParts) ? EBangoSyntaxKind::VectorPart : EBangoSyntaxKind::Error, Start, WordLen);
				return;
			}

			if (bIsLabelName)
			{
				Emit(EBangoSyntaxKind::LabelName, Start, WordLen);
				return;
			}

			for (const FLocMacro& Macro : LocMacros)
			{
				const TCHAR* const Names[] = { Macro.Name };
				if (IsOneOf(Line, Start, WordLen, Names))
				{
					Emit(EBangoSyntaxKind::LocMacro, Start, WordLen);
					PendingLocMacro = &Macro;
					return;
				}
			}

			if (IsOneOf(Line, Start, WordLen, TypeNames))
			{
				// Take any [] pairs written right after the type: int[][]
				while (At(Pos) == TEXT('[') && At(Pos + 1) == TEXT(']'))
				{
					Pos += 2;
				}
				Emit(EBangoSyntaxKind::Type, Start, Pos - Start);
				return;
			}

			EBangoSyntaxKind Kind;
			if (IsOneOf(Line, Start, WordLen, FlowKeywords))        { Kind = EBangoSyntaxKind::Keyword; }
			else if (IsOneOf(Line, Start, WordLen, JumpKeywords))   { Kind = EBangoSyntaxKind::JumpKeyword; }
			else if (IsOneOf(Line, Start, WordLen, LabelKeywords))  { Kind = EBangoSyntaxKind::LabelKeyword; bExpectLabelName = true; }
			else if (IsOneOf(Line, Start, WordLen, DeclKeywords))   { Kind = EBangoSyntaxKind::DeclKeyword; }
			else if (IsOneOf(Line, Start, WordLen, Constants))      { Kind = EBangoSyntaxKind::Constant; }
			else if (IsOneOf(Line, Start, WordLen, NilWord))        { Kind = EBangoSyntaxKind::Nil; }
			else if (IsOneOf(Line, Start, WordLen, PreprocWords))   { Kind = EBangoSyntaxKind::Preprocessor; }
			else if (IsOneOf(Line, Start, WordLen, Builtins))       { Kind = EBangoSyntaxKind::Builtin; }
			else if (IsOneOf(Line, Start, WordLen, InvalidWords))   { Kind = EBangoSyntaxKind::Error; }
			else if (IsFollowedByOpenParen())                       { Kind = EBangoSyntaxKind::Call; }
			// A name is the graph's local, parameter or loop variable if it has one, and otherwise a global. One line
			// can't tell, so it's a global here; FBangoScriptNames, which reads the whole file, tells them apart.
			else                                                    { Kind = EBangoSyntaxKind::Global; }

			Emit(Kind, Start, WordLen);
		}

		bool IsFollowedByOpenParen() const
		{
			int32 I = Pos;
			while (I < Len && IsSpace(Line[I]))
			{
				++I;
			}
			return At(I) == TEXT('(');
		}

		void LexNumber()
		{
			const int32 Start = Pos;
			bool bBad = false;

			while (Pos < Len && IsDigit(Line[Pos]))
			{
				++Pos;
			}

			if (At(Pos) == TEXT('.'))
			{
				++Pos;
				if (!IsDigit(At(Pos)))
				{
					bBad = true; // 1. - a number needs a digit after the dot
				}
				while (Pos < Len && IsDigit(Line[Pos]))
				{
					++Pos;
				}
			}

			if (At(Pos) == TEXT('e') || At(Pos) == TEXT('E'))
			{
				int32 I = Pos + 1;
				if (At(I) == TEXT('+') || At(I) == TEXT('-'))
				{
					++I;
				}
				if (IsDigit(At(I)))
				{
					Pos = I;
					while (Pos < Len && IsDigit(Line[Pos]))
					{
						++Pos;
					}
				}
			}

			// 12abc - a number followed directly by a letter.
			if (IsIdentChar(At(Pos)))
			{
				bBad = true;
				while (Pos < Len && IsIdentChar(Line[Pos]))
				{
					++Pos;
				}
			}

			Emit(bBad ? EBangoSyntaxKind::Error : EBangoSyntaxKind::Number, Start, Pos - Start);
		}

		EStringStyle CurrentStringStyle() const
		{
			if (ActiveLocMacro != nullptr && LocDepth == 1 && LocArg < ActiveLocMacro->NumArgs)
			{
				switch (ActiveLocMacro->Roles[LocArg])
				{
				case ELocRole::Key:    return EStringStyle::LocKey;
				case ELocRole::Source: return EStringStyle::LocSource;
				case ELocRole::Notes:  return EStringStyle::LocNotes;
				}
			}
			return EStringStyle::Plain;
		}

		void LexString(EStringStyle Style)
		{
			const EBangoSyntaxKind BaseKind =
				Style == EStringStyle::LocKey   ? EBangoSyntaxKind::LocKey :
				Style == EStringStyle::LocNotes ? EBangoSyntaxKind::LocNotes :
				EBangoSyntaxKind::String;

			// Build the pieces first: a string with no closing quote is one Error token instead.
			TArray<FBangoSyntaxToken> Pieces;
			auto Add = [&Pieces, this](EBangoSyntaxKind Kind, int32 Start, int32 PieceLen)
			{
				if (PieceLen <= 0)
				{
					return;
				}
				// Join neighbouring pieces of the same kind.
				if (Pieces.Num() > 0)
				{
					FBangoSyntaxToken& Last = Pieces[Pieces.Num() - 1];
					if (Last.Kind == Kind && Last.Start + Last.Len == Start)
					{
						Last.Len += PieceLen;
						return;
					}
				}
				FBangoSyntaxToken Token;
				Token.Kind = Kind;
				Token.Start = Start;
				Token.Len = PieceLen;
				Token.bDisconnected = bDisconnected;
				Pieces.Add(Token);
			};

			const int32 Start = Pos;
			Add(BaseKind, Pos++, 1); // opening quote

			bool bClosed = false;
			while (Pos < Len)
			{
				const TCHAR C = Line[Pos];

				if (C == TEXT('"'))
				{
					Add(BaseKind, Pos++, 1);
					bClosed = true;
					break;
				}

				if (C == TEXT('\\'))
				{
					const TCHAR N = At(Pos + 1);
					const bool bKnown = N == TEXT('"') || N == TEXT('\\') || N == TEXT('n') || N == TEXT('t');
					const int32 EscLen = N != 0 ? 2 : 1;
					Add(bKnown ? EBangoSyntaxKind::StringEscape : EBangoSyntaxKind::Error, Pos, EscLen);
					Pos += EscLen;
					continue;
				}

				// Plain strings (Log, Format, Hint) and a localized text's source have the same placeholders, as in
				// FText::Format: {0}, {1}, ..., with a backtick escaping the next character.
				if (Style == EStringStyle::LocSource || Style == EStringStyle::Plain)
				{
					// A backtick escapes the next character, as in FText::Format.
					if (C == TEXT('`') && At(Pos + 1) != 0 && At(Pos + 1) != TEXT('"'))
					{
						Add(EBangoSyntaxKind::StringEscape, Pos, 2);
						Pos += 2;
						continue;
					}
					if (C == TEXT('{'))
					{
						int32 End = Pos + 1;
						while (End < Len && Line[End] != TEXT('}') && Line[End] != TEXT('"'))
						{
							++End;
						}
						if (At(End) == TEXT('}'))
						{
							bool bAllDigits = End > Pos + 1;
							for (int32 I = Pos + 1; I < End; ++I)
							{
								bAllDigits &= IsDigit(Line[I]);
							}
							// {0} is a placeholder. {Name} isn't supported in a text (an error); in a plain string it's just
							// characters, shown as they are.
							if (bAllDigits || Style == EStringStyle::LocSource)
							{
								Add(bAllDigits ? EBangoSyntaxKind::Placeholder : EBangoSyntaxKind::Error, Pos, End + 1 - Pos);
								Pos = End + 1;
								continue;
							}
						}
					}
				}

				Add(BaseKind, Pos++, 1);
			}

			if (!bClosed)
			{
				Emit(EBangoSyntaxKind::Error, Start, Pos - Start);
				return;
			}

			for (const FBangoSyntaxToken& Piece : Pieces)
			{
				Out.Add(Piece);
			}
		}

		void LexOperatorOrPunctuation(const FLocMacro* LocMacroBeforeThis)
		{
			const TCHAR C = Line[Pos];
			const TCHAR N = At(Pos + 1);

			// Track the arguments of a localized-text macro.
			if (C == TEXT('('))
			{
				if (LocMacroBeforeThis != nullptr && ActiveLocMacro == nullptr)
				{
					ActiveLocMacro = LocMacroBeforeThis;
					LocDepth = 1;
					LocArg = 0;
				}
				else if (ActiveLocMacro != nullptr)
				{
					++LocDepth;
				}
				Emit(EBangoSyntaxKind::Punctuation, Pos++, 1);
				return;
			}
			if (C == TEXT(')'))
			{
				if (ActiveLocMacro != nullptr && --LocDepth == 0)
				{
					ActiveLocMacro = nullptr;
				}
				Emit(EBangoSyntaxKind::Punctuation, Pos++, 1);
				return;
			}
			if (C == TEXT(','))
			{
				if (ActiveLocMacro != nullptr && LocDepth == 1)
				{
					++LocArg;
				}
				Emit(EBangoSyntaxKind::Punctuation, Pos++, 1);
				return;
			}
			if (C == TEXT('[') || C == TEXT(']') || C == TEXT(';'))
			{
				Emit(EBangoSyntaxKind::Punctuation, Pos++, 1);
				return;
			}
			if (C == TEXT('{') || C == TEXT('}'))
			{
				Emit(EBangoSyntaxKind::Structure, Pos++, 1);
				return;
			}

			// The markers around disabled nodes (TokenizeLine counts them).
			if ((C == TEXT('/') && N == TEXT('*')) || (C == TEXT('*') && N == TEXT('/')))
			{
				Emit(EBangoSyntaxKind::DisabledMarker, Pos, 2);
				Pos += 2;
				return;
			}

			// Two-character operators.
			const bool bDouble =
				(C == TEXT('&') && N == TEXT('&')) ||
				(C == TEXT('|') && N == TEXT('|')) ||
				(C == TEXT('+') && (N == TEXT('+') || N == TEXT('='))) ||
				(C == TEXT('-') && (N == TEXT('-') || N == TEXT('='))) ||
				((C == TEXT('=') || C == TEXT('!') || C == TEXT('<') || C == TEXT('>') || C == TEXT('*') || C == TEXT('/')) && N == TEXT('='));
			if (bDouble)
			{
				Emit(EBangoSyntaxKind::Operator, Pos, 2);
				Pos += 2;
				return;
			}

			switch (C)
			{
			case TEXT('+'): case TEXT('-'): case TEXT('*'): case TEXT('/'): case TEXT('%'):
			case TEXT('<'): case TEXT('>'): case TEXT('='): case TEXT('!'):
				Emit(EBangoSyntaxKind::Operator, Pos++, 1);
				return;
			default:
				// A single & or |, # in the middle of a line, or any other character.
				Emit(EBangoSyntaxKind::Error, Pos++, 1);
				return;
			}
		}
	};
}

void FBangoLineTokenizer::TokenizeLine(FStringView Line, TArray<FBangoSyntaxToken>& OutTokens)
{
	int32 DisabledDepth = 0;
	TokenizeLine(Line, OutTokens, DisabledDepth);
}

void FBangoLineTokenizer::TokenizeLine(FStringView Line, TArray<FBangoSyntaxToken>& OutTokens, int32& InOutDisabledDepth)
{
	const int32 First = OutTokens.Num();
	BangoLineTokenizerPrivate::FLexer Lexer(Line, OutTokens);
	Lexer.Run();

	// Regions nest (unlike C++ comments), and the markers in strings and comments were never tokens.
	for (int32 i = First; i < OutTokens.Num(); ++i)
	{
		FBangoSyntaxToken& Token = OutTokens[i];
		if (Token.Kind == EBangoSyntaxKind::DisabledMarker)
		{
			InOutDisabledDepth = Line[Token.Start] == TEXT('/') ? InOutDisabledDepth + 1 : FMath::Max(InOutDisabledDepth - 1, 0);
		}
		else
		{
			Token.bDisabled = InOutDisabledDepth > 0;
		}
	}
}
