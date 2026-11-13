#include "BangoLexer.h"

#include "BangoParser.h"

namespace BangoLexerPrivate
{
	struct FKeyword
	{
		const TCHAR* Text;
		EBangoTokenKind Kind;
	};

	static const FKeyword Keywords[] =
	{
		{ TEXT("if"), EBangoTokenKind::KwIf },
		{ TEXT("else"), EBangoTokenKind::KwElse },
		{ TEXT("while"), EBangoTokenKind::KwWhile },
		{ TEXT("for"), EBangoTokenKind::KwFor },
		{ TEXT("forEach"), EBangoTokenKind::KwForEach },
		{ TEXT("in"), EBangoTokenKind::KwIn },
		{ TEXT("break"), EBangoTokenKind::KwBreak },
		{ TEXT("continue"), EBangoTokenKind::KwContinue },
		{ TEXT("return"), EBangoTokenKind::KwReturn },
		{ TEXT("label"), EBangoTokenKind::KwLabel },
		{ TEXT("goto"), EBangoTokenKind::KwGoto },
		{ TEXT("function"), EBangoTokenKind::KwFunction },
		{ TEXT("global"), EBangoTokenKind::KwGlobal },
		{ TEXT("true"), EBangoTokenKind::KwTrue },
		{ TEXT("false"), EBangoTokenKind::KwFalse },
		{ TEXT("nil"), EBangoTokenKind::KwNil },
		{ TEXT("bool"), EBangoTokenKind::KwBool },
		{ TEXT("int"), EBangoTokenKind::KwInt },
		{ TEXT("float"), EBangoTokenKind::KwDouble }, // the float type is a 64-bit double underneath
		{ TEXT("string"), EBangoTokenKind::KwString },
		{ TEXT("entity"), EBangoTokenKind::KwEntity },
		{ TEXT("text"), EBangoTokenKind::KwText },
		{ TEXT("vector2"), EBangoTokenKind::KwVector2 },
		{ TEXT("vector3"), EBangoTokenKind::KwVector3 },
		{ TEXT("void"), EBangoTokenKind::KwVoid },
		{ TEXT("params"), EBangoTokenKind::KwParams },
		{ TEXT("IsNil"), EBangoTokenKind::KwIsNil },
	};

	static bool IsDigitChar(TCHAR C) { return C >= TEXT('0') && C <= TEXT('9'); }
	static bool IsIdentStartChar(TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || C == TEXT('_'); }
	static bool IsIdentChar(TCHAR C) { return IsIdentStartChar(C) || IsDigitChar(C); }
	static bool IsSpaceChar(TCHAR C) { return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\r') || C == TEXT('\f') || C == TEXT('\v'); }

	class FLexer
	{
	public:
		FLexer(const FString& InSource, FBangoLexResult& InResult, FBangoCompileError& InError)
			: Source(InSource), Result(InResult), Error(InError)
		{
		}

		bool Run()
		{
			while (true)
			{
				SkipSpaceAndComments();
				if (bFailed)
				{
					return false;
				}
				if (AtEnd())
				{
					FBangoToken& Eof = AddToken(EBangoTokenKind::EndOfFile, CurrentLoc());
					Eof.Text = TEXT("");
					return true;
				}
				if (!LexToken())
				{
					return false;
				}
				Result.Tokens.Last().EndColumn = CurrentLoc().Column;
				LastTokenLine = Line;
			}
		}

	private:
		const FString& Source;
		FBangoLexResult& Result;
		FBangoCompileError& Error;

		int32 Pos = 0;
		int32 Line = 1;
		int32 LineStart = 0;
		bool bFailed = false;
		TSet<int64> SeenNodeIDs;
		/** Line of the most recent token, to tell trailing comments from comments on their own line. */
		int32 LastTokenLine = 0;
		// How many disabled regions (slash-star) are open: inside one, errors add a note that disabled nodes must still be code.
		int32 DisabledDepth = 0;

		bool AtEnd() const { return Pos >= Source.Len(); }
		TCHAR Peek(int32 Offset = 0) const { return Pos + Offset < Source.Len() ? Source[Pos + Offset] : TEXT('\0'); }
		FBangoSourceLocation CurrentLoc() const { FBangoSourceLocation L; L.Line = Line; L.Column = Pos - LineStart + 1; return L; }

		void Advance()
		{
			if (Source[Pos] == TEXT('\n'))
			{
				++Line;
				LineStart = Pos + 1;
			}
			++Pos;
		}

		bool Fail(const FBangoSourceLocation& Loc, const FString& Message)
		{
			if (!bFailed)
			{
				bFailed = true;
				Error.Line = Loc.Line;
				Error.Column = Loc.Column;
				Error.Message = DisabledDepth > 0 ? Message + Bango::GetDisabledCodeNote() : Message;
			}
			return false;
		}

		FBangoToken& AddToken(EBangoTokenKind Kind, const FBangoSourceLocation& Loc)
		{
			FBangoToken Token;
			Token.Kind = Kind;
			Token.Loc = Loc;
			Result.Tokens.Add(MoveTemp(Token));
			return Result.Tokens.Last();
		}

		void SkipSpaceAndComments()
		{
			while (!AtEnd() && !bFailed)
			{
				const TCHAR C = Peek();
				if (IsSpaceChar(C) || C == TEXT('\n'))
				{
					Advance();
				}
				else if (C == TEXT('/') && Peek(1) == TEXT('/'))
				{
					if (Peek(2) == TEXT('@'))
					{
						LexMetadata();
					}
					else
					{
						FBangoSourceComment Comment;
						Comment.Line = Line;
						Comment.Column = CurrentLoc().Column;
						Comment.bOwnLine = LastTokenLine != Line;
						Advance(); Advance(); // //
						const int32 TextStart = Pos;
						while (!AtEnd() && Peek() != TEXT('\n'))
						{
							Advance();
						}
						// Carriage returns are dropped (the graph editor writes comments without them), so a comment
						// reads the same before and after saving, whatever the file's line endings.
						Comment.Text = Source.Mid(TextStart, Pos - TextStart).Replace(TEXT("\r"), TEXT(""), ESearchCase::CaseSensitive);
						Result.Comments.Add(MoveTemp(Comment));
					}
				}
				else
				{
					return;
				}
			}
		}

		void SkipMetadataSpaces()
		{
			while (!AtEnd() && IsSpaceChar(Peek()))
			{
				Advance();
			}
		}

		bool ReadMetadataInt(bool bAllowNegative, int64& OutValue, const TCHAR* What)
		{
			const FBangoSourceLocation Loc = CurrentLoc();
			bool bNegative = false;
			if (bAllowNegative && Peek() == TEXT('-'))
			{
				bNegative = true;
				Advance();
			}
			if (!IsDigitChar(Peek()))
			{
				return Fail(Loc, FString::Printf(TEXT("Bad node metadata: expected %s. The format is //@<id>(<x>, <y>)."), What));
			}
			int64 Value = 0;
			while (IsDigitChar(Peek()))
			{
				const int64 Digit = Peek() - TEXT('0');
				if (Value > (MAX_int32 - Digit) / 10)
				{
					return Fail(Loc, FString::Printf(TEXT("Bad node metadata: %s is too large."), What));
				}
				Value = Value * 10 + Digit;
				Advance();
			}
			OutValue = bNegative ? -Value : Value;
			return true;
		}

		bool ExpectMetadataChar(TCHAR Expected)
		{
			SkipMetadataSpaces();
			if (Peek() != Expected)
			{
				return Fail(CurrentLoc(), TEXT("Bad node metadata. The format is //@<id>(<x>, <y>)."));
			}
			Advance();
			return true;
		}

		/** One `<id>(<x>, <y>)` entry (spaces optional), after its `//@` or `@`. */
		bool ReadMetadataEntry(const FBangoSourceLocation& EntryLoc, FBangoNodeInfo& OutInfo)
		{
			int64 NodeID = 0;
			int64 X = 0;
			int64 Y = 0;
			SkipMetadataSpaces();
			if (!ReadMetadataInt(false, NodeID, TEXT("a node ID"))) return false;
			if (!ExpectMetadataChar(TEXT('('))) return false;
			SkipMetadataSpaces();
			if (!ReadMetadataInt(true, X, TEXT("an X coordinate"))) return false;
			if (!ExpectMetadataChar(TEXT(','))) return false;
			SkipMetadataSpaces();
			if (!ReadMetadataInt(true, Y, TEXT("a Y coordinate"))) return false;
			if (!ExpectMetadataChar(TEXT(')'))) return false;

			if (NodeID == 0)
			{
				return Fail(EntryLoc, TEXT("Node ID 0 is reserved for the script's entry node (the params line), which never has metadata. Use another ID."));
			}
			if (SeenNodeIDs.Contains(NodeID))
			{
				return Fail(EntryLoc, FString::Printf(TEXT("Node ID %lld is used more than once in this file."), NodeID));
			}
			SeenNodeIDs.Add(NodeID);

			OutInfo = FBangoNodeInfo();
			OutInfo.Line = EntryLoc.Line;
			OutInfo.bHasMetadata = true;
			OutInfo.NodeID = NodeID;
			OutInfo.X = (int32)X;
			OutInfo.Y = (int32)Y;
			return true;
		}

		/**
		 * //@<id>(<x>, <y>) — all spaces optional. A line with nothing else on it may have more entries after
		 * the first, each starting with '@': `//@7(4, 9) @8(8, 13)`, a run of reroute nodes. Nothing else may follow.
		 */
		void LexMetadata()
		{
			const FBangoSourceLocation StartLoc = CurrentLoc();
			Advance(); Advance(); Advance(); // //@

			FBangoNodeInfo Info;
			if (!ReadMetadataEntry(StartLoc, Info))
			{
				return;
			}
			Result.Metadata.Add(StartLoc.Line, Info);
			Result.MetadataLines.Add(StartLoc.Line);

			SkipMetadataSpaces();
			while (!AtEnd() && Peek() == TEXT('@'))
			{
				const FBangoSourceLocation EntryLoc = CurrentLoc();
				Advance(); // @
				FBangoNodeInfo More;
				if (!ReadMetadataEntry(EntryLoc, More))
				{
					return;
				}
				Result.MoreMetadata.FindOrAdd(StartLoc.Line).Add(More);
				SkipMetadataSpaces();
			}

			if (!AtEnd() && Peek() != TEXT('\n'))
			{
				Fail(CurrentLoc(), TEXT("Unexpected text after node metadata. The format is //@<id>(<x>, <y>), and a reroute line can have more after it: //@7(4, 9) @8(8, 13)."));
			}
		}

		bool LexToken()
		{
			const FBangoSourceLocation Loc = CurrentLoc();
			const TCHAR C = Peek();

			if (IsIdentStartChar(C))
			{
				return LexIdentifier(Loc);
			}
			if (IsDigitChar(C))
			{
				return LexNumber(Loc);
			}
			if (C == TEXT('"'))
			{
				return LexString(Loc);
			}
			if (C == TEXT('@'))
			{
				return LexEntity(Loc);
			}
			if (C == TEXT('#'))
			{
				return LexDirective(Loc);
			}
			return LexOperator(Loc);
		}

		/**
		 * `#define` or `#undef`, written like C's so Unreal's localization gatherer reads them. Must start its
		 * line and be followed by a space, as the gatherer expects.
		 */
		bool LexDirective(const FBangoSourceLocation& Loc)
		{
			static const TCHAR* Usage = TEXT("Lines starting with '#' are #define params (...), #define LOCTEXT_NAMESPACE \"Name\" and #undef LOCTEXT_NAMESPACE.");
			Advance(); // #
			const int32 Start = Pos;
			while (!AtEnd() && IsIdentChar(Peek()))
			{
				Advance();
			}
			const FString Word = Source.Mid(Start, Pos - Start);
			EBangoTokenKind Kind;
			if (Word.Equals(TEXT("define"), ESearchCase::CaseSensitive))
			{
				Kind = EBangoTokenKind::HashDefine;
			}
			else if (Word.Equals(TEXT("undef"), ESearchCase::CaseSensitive))
			{
				Kind = EBangoTokenKind::HashUndef;
			}
			else
			{
				return Fail(Loc, FString::Printf(TEXT("Unknown '#%s'. %s"), *Word, Usage));
			}
			if (Result.Tokens.Num() > 0 && Result.Tokens.Last().Loc.Line == Loc.Line)
			{
				return Fail(Loc, FString::Printf(TEXT("'#%s' must start its line."), *Word));
			}
			if (Peek() != TEXT(' ') && Peek() != TEXT('\t'))
			{
				return Fail(Loc, FString::Printf(TEXT("Put a space after '#%s'. %s"), *Word, Usage));
			}
			FBangoToken& Token = AddToken(Kind, Loc);
			Token.Text = FString(TEXT("#")) + Word;
			return true;
		}

		bool LexIdentifier(const FBangoSourceLocation& Loc)
		{
			const int32 Start = Pos;
			while (!AtEnd() && IsIdentChar(Peek()))
			{
				Advance();
			}
			const FString Text = Source.Mid(Start, Pos - Start);
			if (Text.Equals(TEXT("double"), ESearchCase::CaseSensitive))
			{
				return Fail(Loc, TEXT("The 'double' type is now called 'float' (it's still a 64-bit number). Write 'float' instead."));
			}

			EBangoTokenKind Kind = EBangoTokenKind::Identifier;
			for (const FKeyword& Keyword : Keywords)
			{
				if (Text.Equals(Keyword.Text, ESearchCase::CaseSensitive))
				{
					Kind = Keyword.Kind;
					break;
				}
			}

			FBangoToken& Token = AddToken(Kind, Loc);
			Token.Text = Text;
			return true;
		}

		/** `@john`: the `@` is followed directly by a name. Keywords are fine (`@if`), since the `@` sets them apart. */
		bool LexEntity(const FBangoSourceLocation& Loc)
		{
			Advance(); // @
			if (!IsIdentStartChar(Peek()))
			{
				return Fail(Loc, TEXT("Expected a Bango ID name right after '@', like @john (no space in between)."));
			}
			const int32 Start = Pos;
			while (!AtEnd() && IsIdentChar(Peek()))
			{
				Advance();
			}
			FBangoToken& Token = AddToken(EBangoTokenKind::EntityLiteral, Loc);
			Token.Text = Source.Mid(Start, Pos - Start);
			return true;
		}

		bool LexNumber(const FBangoSourceLocation& Loc)
		{
			const int32 Start = Pos;
			while (IsDigitChar(Peek()))
			{
				Advance();
			}

			bool bIsDouble = false;
			if (Peek() == TEXT('.'))
			{
				if (!IsDigitChar(Peek(1)))
				{
					return Fail(CurrentLoc(), TEXT("A decimal point must be followed by a digit (write 1.0, not 1.)."));
				}
				bIsDouble = true;
				Advance();
				while (IsDigitChar(Peek()))
				{
					Advance();
				}
			}
			if (Peek() == TEXT('e') || Peek() == TEXT('E'))
			{
				int32 Offset = 1;
				if (Peek(1) == TEXT('+') || Peek(1) == TEXT('-'))
				{
					Offset = 2;
				}
				if (!IsDigitChar(Peek(Offset)))
				{
					return Fail(CurrentLoc(), TEXT("An exponent must have digits (for example 1e5 or 2.5e-3)."));
				}
				bIsDouble = true;
				for (int32 i = 0; i < Offset; ++i)
				{
					Advance();
				}
				while (IsDigitChar(Peek()))
				{
					Advance();
				}
			}
			if (IsIdentChar(Peek()))
			{
				return Fail(CurrentLoc(), TEXT("A number can't be followed directly by a letter."));
			}

			const FString Text = Source.Mid(Start, Pos - Start);
			if (bIsDouble)
			{
				FBangoToken& Token = AddToken(EBangoTokenKind::DoubleLiteral, Loc);
				Token.Text = Text;
				Token.DoubleValue = FCString::Atod(*Text);
				return true;
			}

			int64 Value = 0;
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				const int64 Digit = Text[i] - TEXT('0');
				if (Value > (MAX_int32 - Digit) / 10)
				{
					return Fail(Loc, FString::Printf(TEXT("The number %s is too large for an int (the largest int is 2147483647). Use a float, like %s.0, for larger numbers."), *Text, *Text));
				}
				Value = Value * 10 + Digit;
			}
			FBangoToken& Token = AddToken(EBangoTokenKind::IntLiteral, Loc);
			Token.Text = Text;
			Token.IntValue = Value;
			return true;
		}

		bool LexString(const FBangoSourceLocation& Loc)
		{
			Advance(); // opening quote
			FString Value;
			while (true)
			{
				if (AtEnd() || Peek() == TEXT('\n'))
				{
					return Fail(Loc, TEXT("This string has no closing quote. Strings must end on the line they start."));
				}
				const TCHAR C = Peek();
				if (C == TEXT('"'))
				{
					Advance();
					break;
				}
				if (C == TEXT('\\'))
				{
					const FBangoSourceLocation EscapeLoc = CurrentLoc();
					Advance();
					const TCHAR E = Peek();
					if (E == TEXT('"')) Value.AppendChar(TEXT('"'));
					else if (E == TEXT('\\')) Value.AppendChar(TEXT('\\'));
					else if (E == TEXT('n')) Value.AppendChar(TEXT('\n'));
					else if (E == TEXT('t')) Value.AppendChar(TEXT('\t'));
					else
					{
						return Fail(EscapeLoc, TEXT("Unknown escape in string. Use \\\", \\\\, \\n or \\t."));
					}
					Advance();
					continue;
				}
				Value.AppendChar(C);
				Advance();
			}

			FBangoToken& Token = AddToken(EBangoTokenKind::StringLiteral, Loc);
			Token.Text = Value;
			return true;
		}

		bool Emit(EBangoTokenKind Kind, const FBangoSourceLocation& Loc, int32 Length)
		{
			FBangoToken& Token = AddToken(Kind, Loc);
			Token.Text = Source.Mid(Pos, Length);
			for (int32 i = 0; i < Length; ++i)
			{
				Advance();
			}
			return true;
		}

		bool LexOperator(const FBangoSourceLocation& Loc)
		{
			const TCHAR C = Peek();
			const TCHAR N = Peek(1);
			switch (C)
			{
			case TEXT('('): return Emit(EBangoTokenKind::LParen, Loc, 1);
			case TEXT(')'): return Emit(EBangoTokenKind::RParen, Loc, 1);
			case TEXT('{'): return Emit(EBangoTokenKind::LBrace, Loc, 1);
			case TEXT('}'): return Emit(EBangoTokenKind::RBrace, Loc, 1);
			case TEXT('['): return Emit(EBangoTokenKind::LBracket, Loc, 1);
			case TEXT(']'): return Emit(EBangoTokenKind::RBracket, Loc, 1);
			case TEXT(';'): return Emit(EBangoTokenKind::Semicolon, Loc, 1);
			case TEXT(','): return Emit(EBangoTokenKind::Comma, Loc, 1);
			case TEXT('%'): return Emit(EBangoTokenKind::Percent, Loc, 1);
			case TEXT('+'):
				if (N == TEXT('+')) return Emit(EBangoTokenKind::PlusPlus, Loc, 2);
				if (N == TEXT('=')) return Emit(EBangoTokenKind::PlusAssign, Loc, 2);
				return Emit(EBangoTokenKind::Plus, Loc, 1);
			case TEXT('-'):
				if (N == TEXT('-')) return Emit(EBangoTokenKind::MinusMinus, Loc, 2);
				if (N == TEXT('=')) return Emit(EBangoTokenKind::MinusAssign, Loc, 2);
				return Emit(EBangoTokenKind::Minus, Loc, 1);
			case TEXT('*'):
				if (N == TEXT('/'))
				{
					--DisabledDepth;
					return Emit(EBangoTokenKind::DisabledEnd, Loc, 2);
				}
				if (N == TEXT('=')) return Emit(EBangoTokenKind::StarAssign, Loc, 2);
				return Emit(EBangoTokenKind::Star, Loc, 1);
			case TEXT('/'):
				if (N == TEXT('*'))
				{
					++DisabledDepth;
					return Emit(EBangoTokenKind::DisabledBegin, Loc, 2);
				}
				if (N == TEXT('=')) return Emit(EBangoTokenKind::SlashAssign, Loc, 2);
				return Emit(EBangoTokenKind::Slash, Loc, 1);
			case TEXT('!'):
				if (N == TEXT('=')) return Emit(EBangoTokenKind::BangEqual, Loc, 2);
				return Emit(EBangoTokenKind::Bang, Loc, 1);
			case TEXT('='):
				if (N == TEXT('=')) return Emit(EBangoTokenKind::EqualEqual, Loc, 2);
				return Emit(EBangoTokenKind::Assign, Loc, 1);
			case TEXT('<'):
				if (N == TEXT('=')) return Emit(EBangoTokenKind::LessEqual, Loc, 2);
				return Emit(EBangoTokenKind::Less, Loc, 1);
			case TEXT('>'):
				if (N == TEXT('=')) return Emit(EBangoTokenKind::GreaterEqual, Loc, 2);
				return Emit(EBangoTokenKind::Greater, Loc, 1);
			case TEXT('&'):
				if (N == TEXT('&')) return Emit(EBangoTokenKind::AndAnd, Loc, 2);
				return Fail(Loc, TEXT("Unexpected '&'. Did you mean '&&'?"));
			case TEXT('|'):
				if (N == TEXT('|')) return Emit(EBangoTokenKind::OrOr, Loc, 2);
				return Fail(Loc, TEXT("Unexpected '|'. Did you mean '||'?"));
			default:
				break;
			}

			if (C == TEXT('.') && IsDigitChar(N))
			{
				return Fail(Loc, TEXT("A number must start with a digit (write 0.5, not .5)."));
			}
			if (C == TEXT('.'))
			{
				return Emit(EBangoTokenKind::Dot, Loc, 1);
			}
			return Fail(Loc, FString::Printf(TEXT("Unexpected character '%s'."), *Source.Mid(Pos, 1)));
		}
	};
}

namespace Bango
{
	bool Lex(const FString& Source, FBangoLexResult& OutResult, FBangoCompileError& OutError)
	{
		BangoLexerPrivate::FLexer Lexer(Source, OutResult, OutError);
		return Lexer.Run();
	}

	FString DescribeTokenKind(EBangoTokenKind Kind)
	{
		switch (Kind)
		{
		case EBangoTokenKind::EndOfFile: return TEXT("end of file");
		case EBangoTokenKind::Identifier: return TEXT("a name");
		case EBangoTokenKind::IntLiteral: return TEXT("a number");
		case EBangoTokenKind::DoubleLiteral: return TEXT("a number");
		case EBangoTokenKind::StringLiteral: return TEXT("a string");
		case EBangoTokenKind::EntityLiteral: return TEXT("a Bango ID (@name)");
		case EBangoTokenKind::LParen: return TEXT("'('");
		case EBangoTokenKind::RParen: return TEXT("')'");
		case EBangoTokenKind::LBrace: return TEXT("'{'");
		case EBangoTokenKind::RBrace: return TEXT("'}'");
		case EBangoTokenKind::LBracket: return TEXT("'['");
		case EBangoTokenKind::RBracket: return TEXT("']'");
		case EBangoTokenKind::Semicolon: return TEXT("';'");
		case EBangoTokenKind::Comma: return TEXT("','");
		case EBangoTokenKind::Dot: return TEXT("'.'");
		case EBangoTokenKind::Assign: return TEXT("'='");
		case EBangoTokenKind::KwIn: return TEXT("'in'");
		case EBangoTokenKind::DisabledBegin: return TEXT("'/*'");
		case EBangoTokenKind::DisabledEnd: return TEXT("'*/'");
		default: break;
		}
		for (const BangoLexerPrivate::FKeyword& Keyword : BangoLexerPrivate::Keywords)
		{
			if (Keyword.Kind == Kind)
			{
				return FString::Printf(TEXT("'%s'"), Keyword.Text);
			}
		}
		return TEXT("an operator");
	}

	const TCHAR* GetDisabledCodeNote()
	{
		return TEXT(" (This is between /* and */, which mark disabled nodes: what's between them must still be Bango statements. Write notes as // comments.)");
	}

	FString DescribeToken(const FBangoToken& Token)
	{
		switch (Token.Kind)
		{
		case EBangoTokenKind::EndOfFile: return TEXT("end of file");
		case EBangoTokenKind::Identifier: return FString::Printf(TEXT("'%s'"), *Token.Text);
		case EBangoTokenKind::StringLiteral: return TEXT("a string");
		case EBangoTokenKind::EntityLiteral: return FString::Printf(TEXT("'@%s'"), *Token.Text);
		default: return FString::Printf(TEXT("'%s'"), *Token.Text);
		}
	}
}
