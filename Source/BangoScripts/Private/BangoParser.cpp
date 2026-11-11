#include "BangoParser.h"

#include "BangoLexer.h"

FString FBangoCompileError::ToString() const
{
	const FString File = FileName.IsEmpty() ? FString(TEXT("<script>")) : FileName;
	if (NodeID >= 0)
	{
		return FString::Printf(TEXT("%s(%d:%d) [node %lld]: %s"), *File, Line, Column, NodeID, *Message);
	}
	return FString::Printf(TEXT("%s(%d:%d): %s"), *File, Line, Column, *Message);
}

namespace BangoParserPrivate
{
	// Each level costs several stack frames; 64 is far more than any script needs and is safe in debug builds.
	static constexpr int32 MaxNestingDepth = 64;

	static bool IsTypeKeyword(EBangoTokenKind Kind)
	{
		switch (Kind)
		{
		case EBangoTokenKind::KwBool:
		case EBangoTokenKind::KwInt:
		case EBangoTokenKind::KwDouble:
		case EBangoTokenKind::KwString:
		case EBangoTokenKind::KwEntity:
		case EBangoTokenKind::KwText:
		case EBangoTokenKind::KwVector2:
		case EBangoTokenKind::KwVector3:
			return true;
		default:
			return false;
		}
	}

	static bool IsAssignOp(EBangoTokenKind Kind)
	{
		switch (Kind)
		{
		case EBangoTokenKind::Assign:
		case EBangoTokenKind::PlusAssign:
		case EBangoTokenKind::MinusAssign:
		case EBangoTokenKind::StarAssign:
		case EBangoTokenKind::SlashAssign:
			return true;
		default:
			return false;
		}
	}

	static EBangoAssignOp ToAssignOp(EBangoTokenKind Kind)
	{
		switch (Kind)
		{
		case EBangoTokenKind::PlusAssign:  return EBangoAssignOp::Add;
		case EBangoTokenKind::MinusAssign: return EBangoAssignOp::Subtract;
		case EBangoTokenKind::StarAssign:  return EBangoAssignOp::Multiply;
		case EBangoTokenKind::SlashAssign: return EBangoAssignOp::Divide;
		default:                           return EBangoAssignOp::Assign;
		}
	}

	static bool StartsWithUnderscore(const FString& Name)
	{
		return Name.Len() > 0 && Name[0] == TEXT('_');
	}

	/** A fixed value allowed as a params default: a literal, a negated number, or an array of those. */
	static bool IsConstantExpr(const FBangoExpr& Expr)
	{
		if (Expr.Kind == EBangoExprKind::Literal)
		{
			return true;
		}
		if (const FBangoUnaryExpr* Unary = Expr.As<FBangoUnaryExpr>())
		{
			const FBangoLiteralExpr* Operand = Unary->Operand->As<FBangoLiteralExpr>();
			return Unary->Op == EBangoUnaryOp::Negate && Operand
				&& (Operand->LiteralKind == EBangoLiteralKind::Int || Operand->LiteralKind == EBangoLiteralKind::Double);
		}
		if (const FBangoArrayLiteralExpr* ArrayExpr = Expr.As<FBangoArrayLiteralExpr>())
		{
			for (const FBangoExprPtr& Element : ArrayExpr->Elements)
			{
				if (!IsConstantExpr(*Element))
				{
					return false;
				}
			}
			return true;
		}
		return false;
	}

	/** The first text literal in a fixed value (a literal or an array literal of them), or null. */
	static const FBangoExpr* FindTextLiteral(const FBangoExpr& Expr)
	{
		if (const FBangoLiteralExpr* Lit = Expr.As<FBangoLiteralExpr>())
		{
			return Lit->LiteralKind == EBangoLiteralKind::Text ? &Expr : nullptr;
		}
		if (const FBangoArrayLiteralExpr* ArrayLit = Expr.As<FBangoArrayLiteralExpr>())
		{
			for (const FBangoExprPtr& Element : ArrayLit->Elements)
			{
				if (const FBangoExpr* Found = Element.IsValid() ? FindTextLiteral(*Element) : nullptr)
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	/** A Name, or an Index chain whose root is a Name: x, _a[1], _g[i][j]. With bAllowPart, it may end in a vector part: _v.x, _path[2].y. */
	static bool IsPlace(const FBangoExpr& Expr, bool bAllowPart = true)
	{
		const FBangoExpr* Current = &Expr;
		if (const FBangoMemberExpr* Member = Current->As<FBangoMemberExpr>())
		{
			if (!bAllowPart)
			{
				return false;
			}
			Current = Member->Object.Get();
		}
		while (const FBangoIndexExpr* IndexExpr = Current->As<FBangoIndexExpr>())
		{
			Current = IndexExpr->Array.Get();
		}
		return Current->Kind == EBangoExprKind::Name;
	}

	class FParser
	{
	public:
		FParser(const FBangoLexResult& InLex, FBangoScript& InScript, FBangoCompileError& InError)
			: Tokens(InLex.Tokens), Lex(InLex), Script(InScript), Error(InError)
		{
		}

		bool Run()
		{
			bool bFirstItem = true;
			while (!Check(EBangoTokenKind::EndOfFile))
			{
				if (!TakeReroutes(Script.TopLevel))
				{
					return false;
				}
				if (Script.TopLevel.Num() > 0)
				{
					bFirstItem = false;
				}
				bParamsAllowed = bFirstItem;
				bFirstItem = false;
				if (Check(EBangoTokenKind::HashDefine))
				{
					if (!ParseDefine())
					{
						return false;
					}
					continue;
				}
				if (Check(EBangoTokenKind::HashUndef))
				{
					if (!ParseUndef())
					{
						return false;
					}
					continue;
				}
				if (Check(EBangoTokenKind::KwFunction))
				{
					TUniquePtr<FBangoFunction> Function = ParseFunction();
					if (!Function)
					{
						return false;
					}
					Script.Functions.Add(MoveTemp(Function));
				}
				else
				{
					FBangoStmtPtr Stmt = ParseStatement();
					if (!Stmt)
					{
						return false;
					}
					Script.TopLevel.Add(MoveTemp(Stmt));
				}
			}

			if (!TakeReroutes(Script.TopLevel))
			{
				return false;
			}

			if (Script.bHasLocNamespace && Script.LocNamespaceEndLoc.Line == 0)
			{
				return Fail(Script.LocNamespaceLoc, TEXT("A script with '#define LOCTEXT_NAMESPACE' must end with the line '#undef LOCTEXT_NAMESPACE', as Unreal's localization tools expect."));
			}

			// Every metadata comment must be on a statement line, or on a line of its own where a statement could go (a reroute).
			for (const int32 MetadataLine : Lex.MetadataLines)
			{
				if (!NodeLines.Contains(MetadataLine))
				{
					FBangoSourceLocation Loc;
					Loc.Line = MetadataLine;
					Loc.Column = 1;
					bool bLineHasCode = false;
					for (const FBangoToken& Token : Tokens)
					{
						if (Token.Loc.Line == MetadataLine && Token.Kind != EBangoTokenKind::EndOfFile)
						{
							bLineHasCode = true;
							break;
						}
					}
					return Fail(Loc, bLineHasCode
						? TEXT("This line has node metadata (//@) but no statement. Only statement lines (and reroute lines, with nothing but the //@) are graph nodes.")
						: TEXT("This line has only node metadata (//@), so it's a reroute node, but it isn't where a statement could go. A reroute can only be between statements, or at the start or end of a block."));
				}
			}
			return !bFailed;
		}

	private:
		/**
		 * Adds a reroute statement to Block for each line with only node metadata (`//@7(10, 4)`, or several: `//@7(10, 4) @8(12, 4)`) between the
		 * last token read and the next one. Called wherever a statement could start, and at the end of a block.
		 */
		bool TakeReroutes(FBangoBlock& Block)
		{
			const int32 AfterLine = Pos > 0 ? Tokens[Pos - 1].Loc.Line : 0;
			const int32 BeforeLine = Check(EBangoTokenKind::EndOfFile) ? MAX_int32 : Cur().Loc.Line;
			for (const int32 Line : Lex.MetadataLines)
			{
				if (Line <= AfterLine || Line >= BeforeLine || NodeLines.Contains(Line))
				{
					continue;
				}
				TUniquePtr<FBangoRerouteStmt> Reroute = MakeUnique<FBangoRerouteStmt>();
				Reroute->Loc.Line = Line;
				Reroute->Loc.Column = 1;
				if (!RegisterNode(*Reroute, Line))
				{
					return false;
				}
				Block.Add(MoveTemp(Reroute));

				// `//@7(4, 9) @8(8, 13)`: more reroutes on the same line, one after the other.
				if (const TArray<FBangoNodeInfo>* More = Lex.MoreMetadata.Find(Line))
				{
					for (const FBangoNodeInfo& Info : *More)
					{
						TUniquePtr<FBangoRerouteStmt> Next = MakeUnique<FBangoRerouteStmt>();
						Next->Loc.Line = Line;
						Next->Loc.Column = 1;
						Next->Node = Info;
						Next->Node.Line = Line;
						Block.Add(MoveTemp(Next));
					}
				}
			}
			return true;
		}

		/** The rest of a #define / #undef line must be empty: no ';', no metadata, nothing after it. */
		bool FinishDirectiveLine(const FBangoToken& First, const TCHAR* What)
		{
			if (Check(EBangoTokenKind::Semicolon) && Cur().Loc.Line == First.Loc.Line)
			{
				return Fail(Cur().Loc, FString::Printf(TEXT("%s lines don't end with ';' (like in C++). Remove the ';'."), What));
			}
			if (!Check(EBangoTokenKind::EndOfFile) && Cur().Loc.Line == First.Loc.Line)
			{
				return Fail(Cur().Loc, FString::Printf(TEXT("Expected the end of the %s line, but found %s."), What, *Bango::DescribeToken(Cur())));
			}
			if (Lex.MetadataLines.Contains(First.Loc.Line))
			{
				return Fail(First.Loc, FString::Printf(TEXT("The %s line can't have node metadata (//@): it isn't a graph node. Remove the //@ comment."), What));
			}
			return true;
		}

		static bool IsLocNamespaceName(const FBangoToken& Token)
		{
			return Token.Kind == EBangoTokenKind::Identifier && Token.Text.Equals(TEXT("LOCTEXT_NAMESPACE"), ESearchCase::CaseSensitive);
		}

		/** `#define params (...)` or `#define LOCTEXT_NAMESPACE "Name"` at the top of the script. */
		bool ParseDefine()
		{
			const FBangoToken& First = Cur();
			const bool bFirstItem = bParamsAllowed;
			bParamsAllowed = false;
			Advance();
			const int32 NameTok = Pos;
			const FBangoToken& Name = Cur();
			if (Name.Loc.Line != First.Loc.Line)
			{
				return Fail(First.Loc, TEXT("Expected 'params' or 'LOCTEXT_NAMESPACE' after '#define'."));
			}

			if (Name.Kind == EBangoTokenKind::KwParams)
			{
				if (!bFirstItem)
				{
					return Fail(First.Loc, TEXT("'#define params' must be the first line of the script, before any other statement or function."));
				}
				FBangoStmtPtr Stmt = ParseParams();
				if (!Stmt || !RequireOneLine(First.Loc.Line, Previous().Loc) || !FinishDirectiveLine(First, TEXT("#define params")))
				{
					return false;
				}
				Stmt->Loc = First.Loc;
				if (!RegisterNode(*Stmt, First.Loc.Line))
				{
					return false;
				}
				// Inside params ( ... )
				SetTextSpan(*Stmt, NameTok + 2, Pos - 1);
				Stmt->Node.bHasMetadata = true;
				Stmt->Node.NodeID = 0;
				Stmt->Node.X = 0;
				Stmt->Node.Y = 0;
				Script.TopLevel.Add(MoveTemp(Stmt));
				return true;
			}

			if (!IsLocNamespaceName(Name))
			{
				return Fail(Name.Loc, FString::Printf(TEXT("Bango only has '#define params (...)' and '#define LOCTEXT_NAMESPACE \"Name\"', but found %s."), *Bango::DescribeToken(Name)));
			}
			const bool bOnlyParamsBefore = Script.Functions.Num() == 0
				&& (Script.TopLevel.Num() == 0 || (Script.TopLevel.Num() == 1 && Script.GetParams()));
			if (Script.bHasLocNamespace)
			{
				return Fail(First.Loc, FString::Printf(TEXT("A script can only have one '#define LOCTEXT_NAMESPACE' (the first is on line %d)."), Script.LocNamespaceLoc.Line));
			}
			if (!bOnlyParamsBefore)
			{
				return Fail(First.Loc, TEXT("'#define LOCTEXT_NAMESPACE' must come at the top of the script: after the '#define params' line (if there is one) and before any other statement or function."));
			}
			Advance();
			const FBangoToken& Namespace = Cur();
			if (Namespace.Kind != EBangoTokenKind::StringLiteral || Namespace.Loc.Line != First.Loc.Line)
			{
				return Fail(Namespace.Loc.Line == First.Loc.Line ? Namespace.Loc : Name.Loc, TEXT("Write the namespace as a string: #define LOCTEXT_NAMESPACE \"Bango.Intro\""));
			}
			if (Namespace.Text.IsEmpty())
			{
				return Fail(Namespace.Loc, TEXT("The namespace can't be empty."));
			}
			Advance();
			if (!FinishDirectiveLine(First, TEXT("#define LOCTEXT_NAMESPACE")))
			{
				return false;
			}
			Script.bHasLocNamespace = true;
			Script.LocNamespace = Namespace.Text;
			Script.LocNamespaceLoc = First.Loc;
			return true;
		}

		/** `#undef LOCTEXT_NAMESPACE`: the last line of a script that has `#define LOCTEXT_NAMESPACE`. */
		bool ParseUndef()
		{
			const FBangoToken& First = Cur();
			bParamsAllowed = false;
			Advance();
			if (!IsLocNamespaceName(Cur()) || Cur().Loc.Line != First.Loc.Line)
			{
				return Fail(First.Loc, TEXT("The only '#undef' is '#undef LOCTEXT_NAMESPACE', at the end of a script that has '#define LOCTEXT_NAMESPACE'."));
			}
			if (!Script.bHasLocNamespace)
			{
				return Fail(First.Loc, TEXT("There's no '#define LOCTEXT_NAMESPACE' for this '#undef' to end. Remove it, or add '#define LOCTEXT_NAMESPACE \"Name\"' at the top of the script."));
			}
			if (Script.LocNamespaceEndLoc.Line != 0)
			{
				return Fail(First.Loc, FString::Printf(TEXT("A script can only have one '#undef LOCTEXT_NAMESPACE' (the first is on line %d)."), Script.LocNamespaceEndLoc.Line));
			}
			Advance();
			if (!FinishDirectiveLine(First, TEXT("#undef LOCTEXT_NAMESPACE")))
			{
				return false;
			}
			if (!Check(EBangoTokenKind::EndOfFile))
			{
				return Fail(Cur().Loc, TEXT("'#undef LOCTEXT_NAMESPACE' must be the last line of the script; nothing can come after it except comments."));
			}
			Script.LocNamespaceEndLoc = First.Loc;
			return true;
		}

		/** LOCTEXT, NSLOCTEXT, NOTELOCTEXT or NSNOTELOCTEXT, at the name: the number of strings it takes, or 0. */
		static int32 TextLiteralArgCount(const FString& Name, bool& bOutHasNamespace, bool& bOutHasNotes)
		{
			bOutHasNamespace = Name.StartsWith(TEXT("NS"), ESearchCase::CaseSensitive);
			bOutHasNotes = Name.Contains(TEXT("NOTE"), ESearchCase::CaseSensitive);
			if (Name.Equals(TEXT("LOCTEXT"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("NSLOCTEXT"), ESearchCase::CaseSensitive)
				|| Name.Equals(TEXT("NOTELOCTEXT"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("NSNOTELOCTEXT"), ESearchCase::CaseSensitive))
			{
				return 2 + (bOutHasNamespace ? 1 : 0) + (bOutHasNotes ? 1 : 0);
			}
			return 0;
		}

		/** A text literal like `LOCTEXT("Key", "Source")`, at its name. */
		FBangoExprPtr ParseTextLiteral(int32 NumArgs, bool bHasNamespace, bool bHasNotes)
		{
			const FBangoToken& Name = Cur();
			Advance(); // the name
			Advance(); // (
			const TCHAR* Usage = bHasNamespace
				? (bHasNotes ? TEXT("NSNOTELOCTEXT(\"Namespace\", \"Key\", \"Text\", \"Notes for translators\")") : TEXT("NSLOCTEXT(\"Namespace\", \"Key\", \"Text\")"))
				: (bHasNotes ? TEXT("NOTELOCTEXT(\"Key\", \"Text\", \"Notes for translators\")") : TEXT("LOCTEXT(\"Key\", \"Text\")"));
			TArray<FString> Strings;
			TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
			Lit->Loc = Name.Loc;
			Lit->LiteralKind = EBangoLiteralKind::Text;
			for (int32 i = 0; i < NumArgs; ++i)
			{
				if (i > 0 && !Check(EBangoTokenKind::Comma))
				{
					Fail(Cur().Loc, FString::Printf(TEXT("%s takes %d strings: %s"), *Name.Text, NumArgs, Usage));
					return nullptr;
				}
				if (i > 0)
				{
					Advance();
				}
				const FBangoToken& Arg = Cur();
				if (Arg.Kind != EBangoTokenKind::StringLiteral)
				{
					Fail(Arg.Loc, FString::Printf(TEXT("%s's values must be written as strings, like %s. To make text from a string worked out while the script runs, use ToText(...)."), *Name.Text, Usage));
					return nullptr;
				}
				Strings.Add(Arg.Text);
				Lit->TextArgLocs.Add(Arg.Loc);
				Advance();
			}
			if (!Check(EBangoTokenKind::RParen))
			{
				Fail(Cur().Loc, FString::Printf(TEXT("%s takes %d strings: %s"), *Name.Text, NumArgs, Usage));
				return nullptr;
			}
			Advance();

			int32 Next = 0;
			Lit->bTextHasNamespace = bHasNamespace;
			if (bHasNamespace)
			{
				Lit->TextNamespace = Strings[Next++];
			}
			Lit->TextKey = Strings[Next++];
			Lit->StringValue = Strings[Next++];
			if (bHasNotes)
			{
				Lit->TextNotes = Strings[Next++];
			}
			if (bHasNamespace && Lit->TextNamespace.IsEmpty())
			{
				Fail(Lit->TextArgLocs[0], TEXT("The namespace can't be empty."));
				return nullptr;
			}
			if (Lit->TextKey.IsEmpty())
			{
				Fail(Lit->TextArgLocs[bHasNamespace ? 1 : 0], TEXT("The key can't be empty: every text needs a key that's unique in its namespace."));
				return nullptr;
			}
			return MoveTemp(Lit);
		}

		const TArray<FBangoToken>& Tokens;
		const FBangoLexResult& Lex;
		FBangoScript& Script;
		FBangoCompileError& Error;

		int32 Pos = 0;
		int32 Depth = 0;
		bool bFailed = false;
		/** True only while parsing the first top-level statement, where `params` may appear. */
		bool bParamsAllowed = false;
		TSet<int32> NodeLines;

		// ---- Token helpers ----

		const FBangoToken& Cur() const { return Tokens[Pos]; }
		const FBangoToken& Ahead(int32 Offset) const { return Tokens[FMath::Min(Pos + Offset, Tokens.Num() - 1)]; }
		const FBangoToken& Previous() const { return Tokens[FMath::Max(Pos - 1, 0)]; }
		bool Check(EBangoTokenKind Kind) const { return Cur().Kind == Kind; }

		const FBangoToken& Advance()
		{
			const FBangoToken& Token = Tokens[Pos];
			if (Token.Kind != EBangoTokenKind::EndOfFile)
			{
				++Pos;
			}
			return Token;
		}

		bool Match(EBangoTokenKind Kind)
		{
			if (Check(Kind))
			{
				Advance();
				return true;
			}
			return false;
		}

		bool Fail(const FBangoSourceLocation& Loc, const FString& Message)
		{
			if (!bFailed)
			{
				bFailed = true;
				Error.Line = Loc.Line;
				Error.Column = Loc.Column;
				Error.Message = Message;
			}
			return false;
		}

		bool Expect(EBangoTokenKind Kind, const FString& Context)
		{
			if (Match(Kind))
			{
				return true;
			}
			return Fail(Cur().Loc, FString::Printf(TEXT("Expected %s %s, but found %s."),
				*Bango::DescribeTokenKind(Kind), *Context, *Bango::DescribeToken(Cur())));
		}

		bool EnterNesting()
		{
			if (++Depth > MaxNestingDepth)
			{
				return Fail(Cur().Loc, TEXT("This is nested too deeply."));
			}
			return true;
		}

		void LeaveNesting() { --Depth; }

		// ---- Node rules ----

		bool RequireOneLine(int32 StartLine, const FBangoSourceLocation& EndLoc)
		{
			if (EndLoc.Line != StartLine)
			{
				return Fail(EndLoc, FString::Printf(TEXT("This statement starts on line %d and continues onto line %d. Each statement must be on one line, because each line is one graph node."),
					StartLine, EndLoc.Line));
			}
			return true;
		}

		bool RegisterNode(FBangoStmt& Stmt, int32 Line)
		{
			if (NodeLines.Contains(Line))
			{
				return Fail(Stmt.Loc, TEXT("Only one statement is allowed per line, because each line is one graph node."));
			}
			NodeLines.Add(Line);

			if (Stmt.Kind != EBangoStmtKind::Reroute && Lex.MoreMetadata.Contains(Line))
			{
				return Fail(Stmt.Loc, TEXT("A statement line is one graph node, so it can only have one //@ entry. Only a reroute line (with nothing but the //@) can hold several: //@7(4, 9) @8(8, 13)."));
			}

			if (const FBangoNodeInfo* Info = Lex.Metadata.Find(Line))
			{
				Stmt.Node = *Info;
			}
			Stmt.Node.Line = Line;
			return true;
		}

		/** Records the node's editable text as the tokens [BeginTok, EndTok). */
		void SetTextSpan(FBangoStmt& Stmt, int32 BeginTok, int32 EndTok)
		{
			if (EndTok > BeginTok)
			{
				Stmt.Node.TextBegin = Tokens[BeginTok].Loc.Column;
				Stmt.Node.TextEnd = Tokens[EndTok - 1].EndColumn;
			}
			else
			{
				Stmt.Node.TextBegin = Stmt.Node.TextEnd = Tokens[BeginTok].Loc.Column;
			}
		}

		/** After the '}' of a block that ends a statement. */
		bool ExpectBlockTerminator()
		{
			if (Match(EBangoTokenKind::Semicolon))
			{
				return true;
			}
			return Fail(Previous().Loc, FString::Printf(TEXT("Expected ';' after '}', but found %s. A block that ends a statement closes with '};'."),
				*Bango::DescribeToken(Cur())));
		}

		// ---- Types and names ----

		bool ParseType(FBangoType& OutType)
		{
			const FBangoToken& TypeToken = Cur();
			EBangoBaseType Base = EBangoBaseType::Int;
			switch (TypeToken.Kind)
			{
			case EBangoTokenKind::KwBool:   Base = EBangoBaseType::Bool; break;
			case EBangoTokenKind::KwInt:    Base = EBangoBaseType::Int; break;
			case EBangoTokenKind::KwDouble: Base = EBangoBaseType::Double; break;
			case EBangoTokenKind::KwString: Base = EBangoBaseType::String; break;
			case EBangoTokenKind::KwEntity: Base = EBangoBaseType::Entity; break;
			case EBangoTokenKind::KwText:   Base = EBangoBaseType::Text; break;
			case EBangoTokenKind::KwVector2: Base = EBangoBaseType::Vector2; break;
			case EBangoTokenKind::KwVector3: Base = EBangoBaseType::Vector3; break;
			default:
				return Fail(TypeToken.Loc, FString::Printf(TEXT("Expected a type (bool, int, float, string, entity, text, vector2 or vector3), but found %s."),
					*Bango::DescribeToken(TypeToken)));
			}
			Advance();

			int32 ArrayDepth = 0;
			while (Check(EBangoTokenKind::LBracket) && Ahead(1).Kind == EBangoTokenKind::RBracket)
			{
				Advance();
				Advance();
				if (++ArrayDepth > FBangoType::MaxArrayDepth)
				{
					return Fail(TypeToken.Loc, FString::Printf(TEXT("Arrays can be nested at most %d levels deep."), (int32)FBangoType::MaxArrayDepth));
				}
			}
			if (Check(EBangoTokenKind::LBracket))
			{
				return Fail(Cur().Loc, TEXT("Array types are written with empty brackets, like int[] or int[][]."));
			}

			OutType = FBangoType(Base, (uint8)ArrayDepth);
			return true;
		}

		bool ExpectName(FString& OutName, FBangoSourceLocation& OutLoc, const TCHAR* Context)
		{
			if (!Check(EBangoTokenKind::Identifier))
			{
				if (Cur().Kind != EBangoTokenKind::EndOfFile && Cur().Kind >= EBangoTokenKind::KwIf && Cur().Kind <= EBangoTokenKind::KwIsNil)
				{
					return Fail(Cur().Loc, FString::Printf(TEXT("Expected a name %s, but found the keyword %s, which can't be used as a name."),
						Context, *Bango::DescribeToken(Cur())));
				}
				return Fail(Cur().Loc, FString::Printf(TEXT("Expected a name %s, but found %s."), Context, *Bango::DescribeToken(Cur())));
			}
			OutLoc = Cur().Loc;
			OutName = Advance().Text;
			return true;
		}

		bool ExpectLocalName(FString& OutName, FBangoSourceLocation& OutLoc, const TCHAR* Context, const TCHAR* What)
		{
			if (!ExpectName(OutName, OutLoc, Context))
			{
				return false;
			}
			if (!StartsWithUnderscore(OutName))
			{
				return Fail(OutLoc, FString::Printf(TEXT("'%s' is %s, so its name must start with '_' (for example '_%s')."), *OutName, What, *OutName));
			}
			return true;
		}

		// ---- Functions ----

		TUniquePtr<FBangoFunction> ParseFunction()
		{
			const FBangoToken& FunctionToken = Advance();
			TUniquePtr<FBangoFunction> Function = MakeUnique<FBangoFunction>();
			Function->Loc = FunctionToken.Loc;

			if (Lex.Metadata.Contains(FunctionToken.Loc.Line))
			{
				Fail(FunctionToken.Loc, TEXT("A function definition is not a graph node, so its line can't have node metadata (//@)."));
				return nullptr;
			}

			if (Match(EBangoTokenKind::KwVoid))
			{
				Function->bReturnsVoid = true;
			}
			else if (IsTypeKeyword(Cur().Kind))
			{
				Function->bReturnsVoid = false;
				if (!ParseType(Function->ReturnType))
				{
					return nullptr;
				}
			}
			else
			{
				Fail(Cur().Loc, FString::Printf(TEXT("Expected a return type (void, bool, int, float, string, entity or text) after 'function', but found %s."),
					*Bango::DescribeToken(Cur())));
				return nullptr;
			}

			FBangoSourceLocation NameLoc;
			if (!ExpectName(Function->Name, NameLoc, TEXT("for the function")))
			{
				return nullptr;
			}
			if (!Expect(EBangoTokenKind::LParen, TEXT("after the function name")))
			{
				return nullptr;
			}

			if (!Check(EBangoTokenKind::RParen))
			{
				do
				{
					FBangoParam Param;
					if (!ParseType(Param.Type)
						|| !ExpectLocalName(Param.Name, Param.Loc, TEXT("for the parameter"), TEXT("a parameter")))
					{
						return nullptr;
					}
					Function->Params.Add(MoveTemp(Param));
				}
				while (Match(EBangoTokenKind::Comma));
			}

			if (!Expect(EBangoTokenKind::RParen, TEXT("after the parameters"))
				|| !ParseBlock(Function->Body))
			{
				return nullptr;
			}
			Function->EndLine = Previous().Loc.Line;
			if (!ExpectBlockTerminator())
			{
				return nullptr;
			}
			return Function;
		}

		// ---- Statements ----

		bool ParseBlock(FBangoBlock& OutBlock)
		{
			if (!Check(EBangoTokenKind::LBrace))
			{
				return Fail(Cur().Loc, FString::Printf(TEXT("Expected '{', but found %s."), *Bango::DescribeToken(Cur())));
			}
			const FBangoSourceLocation OpenLoc = Advance().Loc;
			bParamsAllowed = false;

			if (!EnterNesting())
			{
				return false;
			}
			while (!Check(EBangoTokenKind::RBrace) && !Check(EBangoTokenKind::EndOfFile))
			{
				if (!TakeReroutes(OutBlock))
				{
					return false;
				}
				if (Check(EBangoTokenKind::KwFunction))
				{
					return Fail(Cur().Loc, TEXT("Functions can only be defined at the top level of a script, not inside a block."));
				}
				FBangoStmtPtr Stmt = ParseStatement();
				if (!Stmt)
				{
					return false;
				}
				OutBlock.Add(MoveTemp(Stmt));
			}
			if (Check(EBangoTokenKind::RBrace) && !TakeReroutes(OutBlock))
			{
				return false;
			}
			LeaveNesting();

			if (!Check(EBangoTokenKind::RBrace))
			{
				return Fail(Cur().Loc, FString::Printf(TEXT("The block opened with '{' on line %d is never closed."), OpenLoc.Line));
			}
			Advance();
			return true;
		}

		FBangoStmtPtr ParseStatement()
		{
			const int32 StartTok = Pos;
			const FBangoToken& First = Cur();
			const bool bAllowParams = bParamsAllowed;
			bParamsAllowed = false;
			switch (First.Kind)
			{
			case EBangoTokenKind::KwIf:      return ParseIf(true);
			case EBangoTokenKind::KwWhile:   return ParseWhile();
			case EBangoTokenKind::KwFor:     return ParseFor();
			case EBangoTokenKind::KwForEach: return ParseForEach();

			case EBangoTokenKind::KwElse:
				Fail(First.Loc, TEXT("'else' must come straight after the '}' of an if block, with no ';' in between."));
				return nullptr;
			case EBangoTokenKind::LBrace:
				Fail(First.Loc, TEXT("A '{' block must belong to an if, else, while, for, forEach or function."));
				return nullptr;
			case EBangoTokenKind::RBrace:
				Fail(First.Loc, TEXT("Unexpected '}' with no matching '{'."));
				return nullptr;
			case EBangoTokenKind::Semicolon:
				Fail(First.Loc, TEXT("Unexpected ';'."));
				return nullptr;
			default:
				break;
			}

			// Single-line statements ending in ';'
			FBangoStmtPtr Stmt;
			switch (First.Kind)
			{
			case EBangoTokenKind::KwGlobal:   Stmt = ParseVarDecl(true); break;
			case EBangoTokenKind::KwBreak:    Advance(); Stmt = MakeUnique<FBangoBreakStmt>(); break;
			case EBangoTokenKind::KwContinue: Advance(); Stmt = MakeUnique<FBangoContinueStmt>(); break;
			case EBangoTokenKind::KwReturn:   Stmt = ParseReturn(); break;
			case EBangoTokenKind::KwLabel:    Stmt = ParseLabelOrGoto(true); break;
			case EBangoTokenKind::KwGoto:     Stmt = ParseLabelOrGoto(false); break;
			case EBangoTokenKind::HashDefine:
				Fail(First.Loc, TEXT("'#define' lines must come at the top of the script, outside any block or function."));
				return nullptr;
			case EBangoTokenKind::HashUndef:
				Fail(First.Loc, TEXT("'#undef LOCTEXT_NAMESPACE' must be the last line of the script, outside any block or function."));
				return nullptr;
			case EBangoTokenKind::KwParams:
				Fail(First.Loc, bAllowParams
					? TEXT("The parameters line is written '#define params (...)', with no ';'.")
					: TEXT("'#define params' must be the first line of the script, before any other statement or function."));
				return nullptr;
			default:
				if (First.Kind == EBangoTokenKind::Identifier && First.Text.Equals(TEXT("locNamespace"), ESearchCase::CaseSensitive) && Tokens.IsValidIndex(Pos + 1) && Tokens[Pos + 1].Kind == EBangoTokenKind::LParen)
				{
					Fail(First.Loc, TEXT("The namespace line is now written '#define LOCTEXT_NAMESPACE \"Name\"', with '#undef LOCTEXT_NAMESPACE' as the script's last line."));
					return nullptr;
				}
				if (IsTypeKeyword(First.Kind))
				{
					Stmt = ParseVarDecl(false);
				}
				else
				{
					Stmt = ParseSimpleStatement(TEXT("statement"));
				}
				break;
			}
			if (!Stmt)
			{
				return nullptr;
			}
			Stmt->Loc = First.Loc;

			if (!Expect(EBangoTokenKind::Semicolon, TEXT("at the end of the statement"))
				|| !RequireOneLine(First.Loc.Line, Previous().Loc)
				|| !RegisterNode(*Stmt, First.Loc.Line))
			{
				return nullptr;
			}

			const int32 SemicolonTok = Pos - 1;
			switch (Stmt->Kind)
			{
			case EBangoStmtKind::Break:
			case EBangoStmtKind::Continue:
				SetTextSpan(*Stmt, SemicolonTok, SemicolonTok);
				break;
			case EBangoStmtKind::Return:
			case EBangoStmtKind::Label:
			case EBangoStmtKind::Goto:
				SetTextSpan(*Stmt, StartTok + 1, SemicolonTok);
				break;
			default:
				SetTextSpan(*Stmt, StartTok, SemicolonTok);
				break;
			}
			return Stmt;
		}

		/** `global T name [= e]` or `T _name [= e]`, without the ';'. */
		FBangoStmtPtr ParseVarDecl(bool bGlobal)
		{
			TUniquePtr<FBangoVarDeclStmt> Decl = MakeUnique<FBangoVarDeclStmt>();
			Decl->Loc = Cur().Loc;
			Decl->bGlobal = bGlobal;

			if (bGlobal)
			{
				Advance();
				if (!IsTypeKeyword(Cur().Kind))
				{
					Fail(Cur().Loc, FString::Printf(TEXT("Expected a type after 'global' (for example 'global int gold;'), but found %s."),
						*Bango::DescribeToken(Cur())));
					return nullptr;
				}
			}
			if (!ParseType(Decl->Type))
			{
				return nullptr;
			}

			FBangoSourceLocation NameLoc;
			if (!ExpectName(Decl->Name, NameLoc, TEXT("after the type")))
			{
				return nullptr;
			}
			if (bGlobal && StartsWithUnderscore(Decl->Name))
			{
				Fail(NameLoc, FString::Printf(TEXT("'%s' is declared global, so its name must not start with '_'. Names starting with '_' are locals."), *Decl->Name));
				return nullptr;
			}
			if (!bGlobal && !StartsWithUnderscore(Decl->Name))
			{
				Fail(NameLoc, FString::Printf(TEXT("'%s' is a local variable, so its name must start with '_'. To declare a global, write 'global %s %s;'."),
					*Decl->Name, *Decl->Type.ToString(), *Decl->Name));
				return nullptr;
			}

			if (Match(EBangoTokenKind::Assign))
			{
				Decl->Init = ParseExpression();
				if (!Decl->Init)
				{
					return nullptr;
				}
			}
			return MoveTemp(Decl);
		}

		/** Assignment, ++/--, or a call; without the ';'. Context is used in errors ("statement", "for step"...). */
		FBangoStmtPtr ParseSimpleStatement(const TCHAR* Context)
		{
			const FBangoSourceLocation StartLoc = Cur().Loc;

			// Prefix ++x / --x
			if (Check(EBangoTokenKind::PlusPlus) || Check(EBangoTokenKind::MinusMinus))
			{
				const bool bIncrement = Advance().Kind == EBangoTokenKind::PlusPlus;
				FBangoExprPtr Target = ParsePostfix();
				if (!Target)
				{
					return nullptr;
				}
				if (Target->Kind == EBangoExprKind::Empty)
				{
					Target->Loc = StartLoc; // `++;`: the variable goes in front, like `_x++;`
				}
				return MakeIncDec(bIncrement, MoveTemp(Target), StartLoc);
			}

			// `= 5`, `+= 2`: an assignment whose variable is left empty (a new node not filled in yet).
			FBangoExprPtr Expr = IsAssignOp(Cur().Kind) ? MakeEmpty(StartLoc) : ParseExpression();
			if (!Expr)
			{
				return nullptr;
			}

			if (IsAssignOp(Cur().Kind))
			{
				const EBangoAssignOp Op = ToAssignOp(Advance().Kind);
				if (Expr->Kind != EBangoExprKind::Empty && !IsPlace(*Expr))
				{
					Fail(Expr->Loc, TEXT("You can only assign to a variable, a global, an array element or a vector part (like _pos.x)."));
					return nullptr;
				}
				TUniquePtr<FBangoAssignStmt> Assign = MakeUnique<FBangoAssignStmt>();
				Assign->Loc = StartLoc;
				Assign->Op = Op;
				Assign->Target = MoveTemp(Expr);
				Assign->Value = ParseExpression();
				if (!Assign->Value)
				{
					return nullptr;
				}
				if (IsAssignOp(Cur().Kind))
				{
					Fail(Cur().Loc, TEXT("Assignments can't be chained. Use one assignment per line."));
					return nullptr;
				}
				if (Check(EBangoTokenKind::PlusPlus) || Check(EBangoTokenKind::MinusMinus))
				{
					Fail(Cur().Loc, TEXT("'++' and '--' are statements, not expressions. Put them on their own line."));
					return nullptr;
				}
				return MoveTemp(Assign);
			}

			if (Check(EBangoTokenKind::PlusPlus) || Check(EBangoTokenKind::MinusMinus))
			{
				const bool bIncrement = Advance().Kind == EBangoTokenKind::PlusPlus;
				return MakeIncDec(bIncrement, MoveTemp(Expr), StartLoc);
			}

			if (Expr->Kind == EBangoExprKind::Call)
			{
				TUniquePtr<FBangoCallStmt> CallStmt = MakeUnique<FBangoCallStmt>();
				CallStmt->Loc = StartLoc;
				CallStmt->Call = MoveTemp(Expr);
				return MoveTemp(CallStmt);
			}

			const FBangoLiteralExpr* EntityLiteral = Expr->As<FBangoLiteralExpr>();
			if ((Expr->Kind == EBangoExprKind::Name || (EntityLiteral && EntityLiteral->LiteralKind == EBangoLiteralKind::Entity)) && Check(EBangoTokenKind::Identifier))
			{
				Fail(Cur().Loc, FString::Printf(TEXT("Unexpected name '%s'. A command call needs parentheses, like '%s %s();'."),
					*Cur().Text, *Bango::DumpExpr(*Expr), *Cur().Text));
				return nullptr;
			}
			if (Check(EBangoTokenKind::Semicolon) || Check(EBangoTokenKind::EndOfFile))
			{
				Fail(StartLoc, FString::Printf(TEXT("This %s computes a value but doesn't do anything with it. Only declarations, assignments and calls can be statements."), Context));
				return nullptr;
			}
			Fail(Cur().Loc, FString::Printf(TEXT("Unexpected %s in this %s."), *Bango::DescribeToken(Cur()), Context));
			return nullptr;
		}

		static FBangoExprPtr MakeEmpty(const FBangoSourceLocation& Loc)
		{
			TUniquePtr<FBangoEmptyExpr> Empty = MakeUnique<FBangoEmptyExpr>();
			Empty->Loc = Loc;
			return MoveTemp(Empty);
		}

		FBangoStmtPtr MakeIncDec(bool bIncrement, FBangoExprPtr Target, const FBangoSourceLocation& Loc)
		{
			if (Target->Kind != EBangoExprKind::Empty && !IsPlace(*Target))
			{
				Fail(Target->Loc, FString::Printf(TEXT("'%s' only works on a variable, a global or an array element."),
					bIncrement ? TEXT("++") : TEXT("--")));
				return nullptr;
			}
			TUniquePtr<FBangoIncDecStmt> Stmt = MakeUnique<FBangoIncDecStmt>();
			Stmt->Loc = Loc;
			Stmt->bIncrement = bIncrement;
			Stmt->Target = MoveTemp(Target);
			return MoveTemp(Stmt);
		}

		/** `params (type _name [= literal], ...)`, without the ';'. */
		FBangoStmtPtr ParseParams()
		{
			Advance();
			TUniquePtr<FBangoParamsStmt> Stmt = MakeUnique<FBangoParamsStmt>();
			if (!Expect(EBangoTokenKind::LParen, TEXT("after 'params'")))
			{
				return nullptr;
			}

			bool bSawDefault = false;
			if (!Check(EBangoTokenKind::RParen))
			{
				do
				{
					FBangoScriptParamDecl Param;
					if (!ParseType(Param.Type)
						|| !ExpectLocalName(Param.Name, Param.Loc, TEXT("for the parameter"), TEXT("a script parameter")))
					{
						return nullptr;
					}
					if (Match(EBangoTokenKind::Assign))
					{
						Param.Default = ParseExpression();
						if (!Param.Default)
						{
							return nullptr;
						}
						if (!IsConstantExpr(*Param.Default))
						{
							Fail(Param.Default->Loc, FString::Printf(TEXT("The default for '%s' must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, or an array of those."), *Param.Name));
							return nullptr;
						}
						if (const FBangoExpr* TextLit = FindTextLiteral(*Param.Default))
						{
							Fail(TextLit->Loc, FString::Printf(TEXT("A text default can't be translated: Unreal's localization tools skip #define lines. Give '%s' the default nil, and set it in the script: if (IsNil(%s)) { %s = LOCTEXT(...); }"),
								*Param.Name, *Param.Name, *Param.Name));
							return nullptr;
						}
						bSawDefault = true;
					}
					else if (bSawDefault)
					{
						Fail(Param.Loc, FString::Printf(TEXT("'%s' has no default, but comes after a parameter with a default. Required parameters must come first; give '%s' a default (it can be '= nil') or move it earlier."),
							*Param.Name, *Param.Name));
						return nullptr;
					}
					Stmt->Params.Add(MoveTemp(Param));
				}
				while (Match(EBangoTokenKind::Comma));
			}
			if (!Expect(EBangoTokenKind::RParen, TEXT("after the parameters")))
			{
				return nullptr;
			}
			return MoveTemp(Stmt);
		}

		FBangoStmtPtr ParseReturn()
		{
			Advance();
			TUniquePtr<FBangoReturnStmt> Stmt = MakeUnique<FBangoReturnStmt>();
			if (!Check(EBangoTokenKind::Semicolon))
			{
				Stmt->Value = ParseExpression();
				if (!Stmt->Value)
				{
					return nullptr;
				}
			}
			return MoveTemp(Stmt);
		}

		FBangoStmtPtr ParseLabelOrGoto(bool bLabel)
		{
			Advance();
			FString Name;
			FBangoSourceLocation NameLoc;
			// `label ;` and `goto ;`: the name is left empty (a new node not filled in yet). It doesn't compile.
			NameLoc = Cur().Loc;
			if (!Check(EBangoTokenKind::Semicolon) && !ExpectName(Name, NameLoc, bLabel ? TEXT("after 'label'") : TEXT("after 'goto'")))
			{
				return nullptr;
			}
			if (bLabel)
			{
				TUniquePtr<FBangoLabelStmt> Stmt = MakeUnique<FBangoLabelStmt>();
				Stmt->Name = Name;
				Stmt->NameLoc = NameLoc;
				return MoveTemp(Stmt);
			}
			TUniquePtr<FBangoGotoStmt> Stmt = MakeUnique<FBangoGotoStmt>();
			Stmt->Name = Name;
			Stmt->NameLoc = NameLoc;
			return MoveTemp(Stmt);
		}

		/** `(cond)` after if/while. The header must be on one line. */
		FBangoExprPtr ParseParenCondition(int32 HeaderLine, const FString& Keyword)
		{
			if (!Expect(EBangoTokenKind::LParen, Keyword))
			{
				return nullptr;
			}
			FBangoExprPtr Condition = ParseExpression();
			if (!Condition || !Expect(EBangoTokenKind::RParen, TEXT("after the condition")) || !RequireOneLine(HeaderLine, Previous().Loc))
			{
				return nullptr;
			}
			return Condition;
		}

		/** if (...) {...} [else if (...) {...}]* [else {...}] ; — only the outermost if consumes the ';'. */
		FBangoStmtPtr ParseIf(bool bOutermost)
		{
			const int32 KeywordTok = Pos;
			const FBangoToken& IfToken = Advance();
			TUniquePtr<FBangoIfStmt> Stmt = MakeUnique<FBangoIfStmt>();
			Stmt->Loc = IfToken.Loc;

			Stmt->Condition = ParseParenCondition(IfToken.Loc.Line, TEXT("after 'if'"));
			if (!Stmt->Condition || !RegisterNode(*Stmt, IfToken.Loc.Line))
			{
				return nullptr;
			}
			SetTextSpan(*Stmt, KeywordTok + 2, Pos - 1);
			if (!ParseBlock(Stmt->Then))
			{
				return nullptr;
			}

			if (Match(EBangoTokenKind::KwElse))
			{
				Stmt->bHasElse = true;
				if (Check(EBangoTokenKind::KwIf))
				{
					if (!EnterNesting())
					{
						return nullptr;
					}
					FBangoStmtPtr ElseIf = ParseIf(false);
					LeaveNesting();
					if (!ElseIf)
					{
						return nullptr;
					}
					Stmt->Else.Add(MoveTemp(ElseIf));
				}
				else if (!ParseBlock(Stmt->Else))
				{
					return nullptr;
				}
			}

			if (bOutermost && !ExpectBlockTerminator())
			{
				return nullptr;
			}
			return MoveTemp(Stmt);
		}

		FBangoStmtPtr ParseWhile()
		{
			const int32 KeywordTok = Pos;
			const FBangoToken& WhileToken = Advance();
			TUniquePtr<FBangoWhileStmt> Stmt = MakeUnique<FBangoWhileStmt>();
			Stmt->Loc = WhileToken.Loc;

			Stmt->Condition = ParseParenCondition(WhileToken.Loc.Line, TEXT("after 'while'"));
			if (!Stmt->Condition || !RegisterNode(*Stmt, WhileToken.Loc.Line))
			{
				return nullptr;
			}
			SetTextSpan(*Stmt, KeywordTok + 2, Pos - 1);
			if (!ParseBlock(Stmt->Body)
				|| !ExpectBlockTerminator())
			{
				return nullptr;
			}
			return MoveTemp(Stmt);
		}

		FBangoStmtPtr ParseFor()
		{
			const int32 KeywordTok = Pos;
			const FBangoToken& ForToken = Advance();
			TUniquePtr<FBangoForStmt> Stmt = MakeUnique<FBangoForStmt>();
			Stmt->Loc = ForToken.Loc;

			if (!Expect(EBangoTokenKind::LParen, TEXT("after 'for'")))
			{
				return nullptr;
			}

			// Init
			if (!Check(EBangoTokenKind::Semicolon))
			{
				const FBangoSourceLocation InitLoc = Cur().Loc;
				if (Check(EBangoTokenKind::KwGlobal))
				{
					Fail(InitLoc, TEXT("A for loop can't declare a global. Declare it on its own line before the loop."));
					return nullptr;
				}
				Stmt->Init = IsTypeKeyword(Cur().Kind) ? ParseVarDecl(false) : ParseSimpleStatement(TEXT("for loop header"));
				if (!Stmt->Init)
				{
					return nullptr;
				}
				Stmt->Init->Loc = InitLoc;
				if (Stmt->Init->Kind == EBangoStmtKind::Call)
				{
					Fail(InitLoc, TEXT("The first part of a for loop must be a declaration or an assignment."));
					return nullptr;
				}
			}
			if (!Expect(EBangoTokenKind::Semicolon, TEXT("after the first part of the for loop")))
			{
				return nullptr;
			}

			// Condition
			if (!Check(EBangoTokenKind::Semicolon))
			{
				Stmt->Condition = ParseExpression();
				if (!Stmt->Condition)
				{
					return nullptr;
				}
			}
			if (!Expect(EBangoTokenKind::Semicolon, TEXT("after the for loop condition")))
			{
				return nullptr;
			}

			// Step
			if (!Check(EBangoTokenKind::RParen))
			{
				const FBangoSourceLocation StepLoc = Cur().Loc;
				if (IsTypeKeyword(Cur().Kind) || Check(EBangoTokenKind::KwGlobal))
				{
					Fail(StepLoc, TEXT("The last part of a for loop can't be a declaration. Use an assignment like '_i++'."));
					return nullptr;
				}
				Stmt->Step = ParseSimpleStatement(TEXT("for loop header"));
				if (!Stmt->Step)
				{
					return nullptr;
				}
				Stmt->Step->Loc = StepLoc;
				if (Stmt->Step->Kind == EBangoStmtKind::Call)
				{
					Fail(StepLoc, TEXT("The last part of a for loop must be an assignment, like '_i++' or '_i += 2'."));
					return nullptr;
				}
			}
			if (!Expect(EBangoTokenKind::RParen, TEXT("after the for loop header"))
				|| !RequireOneLine(ForToken.Loc.Line, Previous().Loc)
				|| !RegisterNode(*Stmt, ForToken.Loc.Line))
			{
				return nullptr;
			}
			SetTextSpan(*Stmt, KeywordTok + 2, Pos - 1);
			if (!ParseBlock(Stmt->Body)
				|| !ExpectBlockTerminator())
			{
				return nullptr;
			}
			return MoveTemp(Stmt);
		}

		FBangoStmtPtr ParseForEach()
		{
			const int32 KeywordTok = Pos;
			const FBangoToken& ForEachToken = Advance();
			TUniquePtr<FBangoForEachStmt> Stmt = MakeUnique<FBangoForEachStmt>();
			Stmt->Loc = ForEachToken.Loc;

			FBangoSourceLocation VarLoc;
			if (!Expect(EBangoTokenKind::LParen, TEXT("after 'forEach'"))
				|| !ExpectLocalName(Stmt->VarName, VarLoc, TEXT("for the forEach variable"), TEXT("the forEach loop variable"))
				|| !Expect(EBangoTokenKind::KwIn, TEXT("after the forEach variable")))
			{
				return nullptr;
			}
			Stmt->Array = ParseExpression();
			if (!Stmt->Array
				|| !Expect(EBangoTokenKind::RParen, TEXT("after the array"))
				|| !RequireOneLine(ForEachToken.Loc.Line, Previous().Loc)
				|| !RegisterNode(*Stmt, ForEachToken.Loc.Line))
			{
				return nullptr;
			}
			SetTextSpan(*Stmt, KeywordTok + 2, Pos - 1);
			if (!ParseBlock(Stmt->Body)
				|| !ExpectBlockTerminator())
			{
				return nullptr;
			}
			return MoveTemp(Stmt);
		}

		// ---- Expressions ----

		FBangoExprPtr MakeBinary(EBangoBinaryOp Op, FBangoExprPtr Left, FBangoExprPtr Right, const FBangoSourceLocation& Loc)
		{
			TUniquePtr<FBangoBinaryExpr> Expr = MakeUnique<FBangoBinaryExpr>();
			Expr->Loc = Loc;
			Expr->Op = Op;
			Expr->Left = MoveTemp(Left);
			Expr->Right = MoveTemp(Right);
			return MoveTemp(Expr);
		}

		FBangoExprPtr ParseExpression()
		{
			if (!EnterNesting())
			{
				return nullptr;
			}
			FBangoExprPtr Result = ParseOr();
			LeaveNesting();
			return Result;
		}

		FBangoExprPtr ParseOr()
		{
			FBangoExprPtr Left = ParseAnd();
			while (Left && Check(EBangoTokenKind::OrOr))
			{
				const FBangoSourceLocation OpLoc = Advance().Loc;
				FBangoExprPtr Right = ParseAnd();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(EBangoBinaryOp::Or, MoveTemp(Left), MoveTemp(Right), OpLoc);
			}
			return Left;
		}

		FBangoExprPtr ParseAnd()
		{
			FBangoExprPtr Left = ParseEquality();
			while (Left && Check(EBangoTokenKind::AndAnd))
			{
				const FBangoSourceLocation OpLoc = Advance().Loc;
				FBangoExprPtr Right = ParseEquality();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(EBangoBinaryOp::And, MoveTemp(Left), MoveTemp(Right), OpLoc);
			}
			return Left;
		}

		FBangoExprPtr ParseEquality()
		{
			FBangoExprPtr Left = ParseRelational();
			while (Left && (Check(EBangoTokenKind::EqualEqual) || Check(EBangoTokenKind::BangEqual)))
			{
				const FBangoToken& OpToken = Advance();
				const EBangoBinaryOp Op = OpToken.Kind == EBangoTokenKind::EqualEqual ? EBangoBinaryOp::Equal : EBangoBinaryOp::NotEqual;
				FBangoExprPtr Right = ParseRelational();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(Op, MoveTemp(Left), MoveTemp(Right), OpToken.Loc);
			}
			return Left;
		}

		FBangoExprPtr ParseRelational()
		{
			FBangoExprPtr Left = ParseAdditive();
			while (Left)
			{
				EBangoBinaryOp Op;
				switch (Cur().Kind)
				{
				case EBangoTokenKind::Less:         Op = EBangoBinaryOp::Less; break;
				case EBangoTokenKind::LessEqual:    Op = EBangoBinaryOp::LessEqual; break;
				case EBangoTokenKind::Greater:      Op = EBangoBinaryOp::Greater; break;
				case EBangoTokenKind::GreaterEqual: Op = EBangoBinaryOp::GreaterEqual; break;
				default: return Left;
				}
				const FBangoSourceLocation OpLoc = Advance().Loc;
				FBangoExprPtr Right = ParseAdditive();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(Op, MoveTemp(Left), MoveTemp(Right), OpLoc);
			}
			return Left;
		}

		FBangoExprPtr ParseAdditive()
		{
			FBangoExprPtr Left = ParseMultiplicative();
			while (Left && (Check(EBangoTokenKind::Plus) || Check(EBangoTokenKind::Minus)))
			{
				const FBangoToken& OpToken = Advance();
				const EBangoBinaryOp Op = OpToken.Kind == EBangoTokenKind::Plus ? EBangoBinaryOp::Add : EBangoBinaryOp::Subtract;
				FBangoExprPtr Right = ParseMultiplicative();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(Op, MoveTemp(Left), MoveTemp(Right), OpToken.Loc);
			}
			return Left;
		}

		FBangoExprPtr ParseMultiplicative()
		{
			FBangoExprPtr Left = ParseUnary();
			while (Left)
			{
				EBangoBinaryOp Op;
				switch (Cur().Kind)
				{
				case EBangoTokenKind::Star:    Op = EBangoBinaryOp::Multiply; break;
				case EBangoTokenKind::Slash:   Op = EBangoBinaryOp::Divide; break;
				case EBangoTokenKind::Percent: Op = EBangoBinaryOp::Modulo; break;
				default: return Left;
				}
				const FBangoSourceLocation OpLoc = Advance().Loc;
				FBangoExprPtr Right = ParseUnary();
				if (!Right)
				{
					return nullptr;
				}
				Left = MakeBinary(Op, MoveTemp(Left), MoveTemp(Right), OpLoc);
			}
			return Left;
		}

		FBangoExprPtr ParseUnary()
		{
			if (Check(EBangoTokenKind::Bang) || Check(EBangoTokenKind::Minus))
			{
				const FBangoToken& OpToken = Advance();
				if (!EnterNesting())
				{
					return nullptr;
				}
				FBangoExprPtr Operand = ParseUnary();
				LeaveNesting();
				if (!Operand)
				{
					return nullptr;
				}
				TUniquePtr<FBangoUnaryExpr> Expr = MakeUnique<FBangoUnaryExpr>();
				Expr->Loc = OpToken.Loc;
				Expr->Op = OpToken.Kind == EBangoTokenKind::Bang ? EBangoUnaryOp::Not : EBangoUnaryOp::Negate;
				Expr->Operand = MoveTemp(Operand);
				return MoveTemp(Expr);
			}
			if (Check(EBangoTokenKind::PlusPlus) || Check(EBangoTokenKind::MinusMinus))
			{
				Fail(Cur().Loc, TEXT("'++' and '--' are statements, not expressions. Put them on their own line."));
				return nullptr;
			}
			return ParsePostfix();
		}

		/** A primary followed by any number of [index], `.x` vector parts and `Name(args)` targeted calls. */
		FBangoExprPtr ParsePostfix()
		{
			FBangoExprPtr Expr = ParsePrimary();
			while (Expr)
			{
				if (Check(EBangoTokenKind::LBracket))
				{
					const FBangoSourceLocation BracketLoc = Advance().Loc;
					TUniquePtr<FBangoIndexExpr> IndexExpr = MakeUnique<FBangoIndexExpr>();
					IndexExpr->Loc = BracketLoc;
					IndexExpr->Array = MoveTemp(Expr);
					IndexExpr->Index = ParseExpression();
					if (!IndexExpr->Index || !Expect(EBangoTokenKind::RBracket, TEXT("after the array index")))
					{
						return nullptr;
					}
					Expr = MoveTemp(IndexExpr);
				}
				else if (Check(EBangoTokenKind::Dot))
				{
					Advance();
					if (!Check(EBangoTokenKind::Identifier))
					{
						Fail(Cur().Loc, FString::Printf(TEXT("Expected a vector part (x, y or z) after '.', but found %s."), *Bango::DescribeToken(Cur())));
						return nullptr;
					}
					const FBangoToken& PartToken = Advance();
					int32 Part = -1;
					if (PartToken.Text.Equals(TEXT("x"), ESearchCase::CaseSensitive)) Part = 0;
					else if (PartToken.Text.Equals(TEXT("y"), ESearchCase::CaseSensitive)) Part = 1;
					else if (PartToken.Text.Equals(TEXT("z"), ESearchCase::CaseSensitive)) Part = 2;
					if (Part < 0)
					{
						const bool bWrongCase = PartToken.Text.Equals(TEXT("x"), ESearchCase::IgnoreCase) || PartToken.Text.Equals(TEXT("y"), ESearchCase::IgnoreCase)
							|| PartToken.Text.Equals(TEXT("z"), ESearchCase::IgnoreCase);
						Fail(PartToken.Loc, bWrongCase
							? FString::Printf(TEXT("Vector parts are lowercase: write .%s, not .%s."), *PartToken.Text.ToLower(), *PartToken.Text)
							: FString::Printf(TEXT("'.%s' isn't a vector part. Only vectors have parts after a '.': x and y (and z for a vector3)."), *PartToken.Text));
						return nullptr;
					}
					TUniquePtr<FBangoMemberExpr> MemberExpr = MakeUnique<FBangoMemberExpr>();
					MemberExpr->Loc = Expr->Loc;
					MemberExpr->Object = MoveTemp(Expr);
					MemberExpr->Member = PartToken.Text;
					MemberExpr->Part = Part;
					MemberExpr->MemberLoc = PartToken.Loc;
					Expr = MoveTemp(MemberExpr);
				}
				else if (Check(EBangoTokenKind::Identifier) && Ahead(1).Kind == EBangoTokenKind::LParen
					&& Cur().Loc.Line == Previous().Loc.Line)
				{
					// `Target Name(args)`. The name must be on the same line as the target, so a missing ';'
					// before a call on the next line is reported as a missing ';'.
					const FBangoSourceLocation TargetLoc = Expr->Loc;
					TUniquePtr<FBangoCallExpr> Call = ParseCall();
					if (!Call)
					{
						return nullptr;
					}
					Call->Loc = TargetLoc;
					Call->Target = MoveTemp(Expr);
					Expr = MoveTemp(Call);
				}
				else
				{
					break;
				}
			}
			return Expr;
		}

		/** Name(args), at an Identifier followed by '('. */
		TUniquePtr<FBangoCallExpr> ParseCall()
		{
			const FBangoToken& NameToken = Advance();
			TUniquePtr<FBangoCallExpr> Call = MakeUnique<FBangoCallExpr>();
			Call->Loc = NameToken.Loc;
			Call->Name = NameToken.Text;
			Call->NameLoc = NameToken.Loc;

			Advance(); // (
			if (!Check(EBangoTokenKind::RParen))
			{
				do
				{
					FBangoExprPtr Arg = ParseExpression();
					if (!Arg)
					{
						return nullptr;
					}
					Call->Args.Add(MoveTemp(Arg));
				}
				while (Match(EBangoTokenKind::Comma));
			}
			if (!Expect(EBangoTokenKind::RParen, FString::Printf(TEXT("after the arguments to %s"), *Call->Name)))
			{
				return nullptr;
			}
			return Call;
		}

		FBangoExprPtr ParsePrimary()
		{
			const FBangoToken& Token = Cur();
			switch (Token.Kind)
			{
			case EBangoTokenKind::IntLiteral:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::Int;
				Lit->IntValue = Token.IntValue;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::DoubleLiteral:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::Double;
				Lit->DoubleValue = Token.DoubleValue;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::StringLiteral:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::String;
				Lit->StringValue = Token.Text;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::EntityLiteral:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::Entity;
				Lit->StringValue = Token.Text;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::KwTrue:
			case EBangoTokenKind::KwFalse:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::Bool;
				Lit->BoolValue = Token.Kind == EBangoTokenKind::KwTrue;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::KwNil:
			{
				Advance();
				TUniquePtr<FBangoLiteralExpr> Lit = MakeUnique<FBangoLiteralExpr>();
				Lit->Loc = Token.Loc;
				Lit->LiteralKind = EBangoLiteralKind::Nil;
				return MoveTemp(Lit);
			}
			case EBangoTokenKind::Identifier:
			{
				bool bTextHasNamespace = false;
				bool bTextHasNotes = false;
				const int32 TextArgs = TextLiteralArgCount(Token.Text, bTextHasNamespace, bTextHasNotes);
				if (TextArgs > 0 && Ahead(1).Kind == EBangoTokenKind::LParen)
				{
					return ParseTextLiteral(TextArgs, bTextHasNamespace, bTextHasNotes);
				}
				if (Ahead(1).Kind == EBangoTokenKind::LParen)
				{
					TUniquePtr<FBangoCallExpr> Call = ParseCall();
					if (!Call)
					{
						return nullptr;
					}
					return MoveTemp(Call);
				}
				Advance();
				TUniquePtr<FBangoNameExpr> NameExpr = MakeUnique<FBangoNameExpr>();
				NameExpr->Loc = Token.Loc;
				NameExpr->Name = Token.Text;
				return MoveTemp(NameExpr);
			}
			case EBangoTokenKind::LParen:
			{
				Advance();
				FBangoExprPtr Inner = ParseExpression();
				if (!Inner || !Expect(EBangoTokenKind::RParen, TEXT("to close '('")))
				{
					return nullptr;
				}
				return Inner;
			}
			case EBangoTokenKind::LBracket:
			{
				Advance();
				TUniquePtr<FBangoArrayLiteralExpr> ArrayExpr = MakeUnique<FBangoArrayLiteralExpr>();
				ArrayExpr->Loc = Token.Loc;
				if (!Check(EBangoTokenKind::RBracket))
				{
					do
					{
						FBangoExprPtr Element = ParseExpression();
						if (!Element)
						{
							return nullptr;
						}
						ArrayExpr->Elements.Add(MoveTemp(Element));
					}
					while (Match(EBangoTokenKind::Comma));
				}
				if (!Expect(EBangoTokenKind::RBracket, TEXT("to close the array")))
				{
					return nullptr;
				}
				return MoveTemp(ArrayExpr);
			}
			case EBangoTokenKind::KwIsNil:
			{
				Advance();
				if (!Expect(EBangoTokenKind::LParen, TEXT("after 'IsNil'")))
				{
					return nullptr;
				}
				FBangoExprPtr Operand = ParsePostfix();
				if (!Operand)
				{
					return nullptr;
				}
				if (const FBangoMemberExpr* Member = Operand->As<FBangoMemberExpr>())
				{
					Fail(Member->MemberLoc, FString::Printf(TEXT("A vector's parts are never nil on their own. Check the whole vector instead: IsNil(%s)."), *Bango::DumpExpr(*Member->Object)));
					return nullptr;
				}
				if (!IsPlace(*Operand))
				{
					Fail(Operand->Loc, TEXT("IsNil needs a variable, a global or an array element, like IsNil(_x) or IsNil(_a[2])."));
					return nullptr;
				}
				if (!Expect(EBangoTokenKind::RParen, TEXT("after the IsNil argument")))
				{
					return nullptr;
				}
				TUniquePtr<FBangoIsNilExpr> Expr = MakeUnique<FBangoIsNilExpr>();
				Expr->Loc = Token.Loc;
				Expr->Operand = MoveTemp(Operand);
				return MoveTemp(Expr);
			}
			case EBangoTokenKind::RParen:
			case EBangoTokenKind::Semicolon:
				// A value left empty (`if ()`, `_x = ;`): it parses so a new graph node can be saved, and the
				// compiler asks for it to be filled in.
				return MakeEmpty(Token.Loc);
			default:
				break;
			}

			if (IsTypeKeyword(Token.Kind) || Token.Kind == EBangoTokenKind::KwGlobal)
			{
				Fail(Token.Loc, TEXT("A declaration must be a statement on its own line, like 'int _x = 5;'."));
				return nullptr;
			}
			Fail(Token.Loc, FString::Printf(TEXT("Expected a value, but found %s."), *Bango::DescribeToken(Token)));
			return nullptr;
		}
	};
}

namespace Bango
{
	bool ParseScript(const FString& Source, const FString& FileName, FBangoScript& OutScript, FBangoCompileError& OutError)
	{
		OutError = FBangoCompileError();
		OutError.FileName = FileName;
		OutScript.FileName = FileName;

		FBangoLexResult LexResult;
		bool bOk = Lex(Source, LexResult, OutError);
		if (bOk)
		{
			BangoParserPrivate::FParser Parser(LexResult, OutScript, OutError);
			bOk = Parser.Run();
			OutScript.Comments = MoveTemp(LexResult.Comments);
		}

		if (!bOk)
		{
			if (const FBangoNodeInfo* Info = LexResult.Metadata.Find(OutError.Line))
			{
				OutError.NodeID = Info->NodeID;
			}
		}
		return bOk;
	}
}
