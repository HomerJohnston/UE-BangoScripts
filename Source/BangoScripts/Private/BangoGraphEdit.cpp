#include "BangoGraphEdit.h"
#include "BangoText.h"

#include "BangoAST.h"
#include "BangoLexer.h"
#include "BangoParser.h"

namespace BangoGraphEditPrivate
{
	static bool HasLineBreak(const FString& Text)
	{
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			if (Text[i] == TEXT('\n') || Text[i] == TEXT('\r'))
			{
				return true;
			}
		}
		return false;
	}

	static bool IsStatementKind(EBangoGraphNodeKind Kind)
	{
		return Kind == EBangoGraphNodeKind::Assign
			|| Kind == EBangoGraphNodeKind::IncDec || Kind == EBangoGraphNodeKind::Call;
	}

	static FString Trimmed(const FString& Value)
	{
		int32 Start = 0;
		int32 End = Value.Len();
		while (Start < End && (Value[Start] == TEXT(' ') || Value[Start] == TEXT('\t')))
		{
			++Start;
		}
		while (End > Start && (Value[End - 1] == TEXT(' ') || Value[End - 1] == TEXT('\t')))
		{
			--End;
		}
		return Value.Mid(Start, End - Start);
	}

	static const FBangoGraph* GraphAt(const FBangoGraphDocument& Document, int32 Graph)
	{
		return Graph == INDEX_NONE ? &Document.Main : (Document.Functions.IsValidIndex(Graph) ? &Document.Functions[Graph] : nullptr);
	}
}

namespace Bango
{
	bool ValidateNodeText(EBangoGraphNodeKind Kind, bool bInFunctionGraph, const FString& Text, FString& OutError)
	{
		using namespace BangoGraphEditPrivate;

		if (HasLineBreak(Text))
		{
			OutError = TEXT("A node's text must be one line.");
			return false;
		}

		FString Source;
		switch (Kind)
		{
		case EBangoGraphNodeKind::Entry:
			Source = bInFunctionGraph ? FString(TEXT("function ")) + Text + TEXT("\n{\n};") : FString(TEXT("#define params (")) + Text + TEXT(")");
			break;
		case EBangoGraphNodeKind::Assign:
		case EBangoGraphNodeKind::IncDec:
		case EBangoGraphNodeKind::Call:
			if (Text.IsEmpty())
			{
				OutError = TEXT("The statement is empty.");
				return false;
			}
			Source = Text + TEXT(";");
			break;
		case EBangoGraphNodeKind::If:      Source = FString(TEXT("if (")) + Text + TEXT(")\n{\n};"); break;
		case EBangoGraphNodeKind::While:   Source = FString(TEXT("while (")) + Text + TEXT(")\n{\n};"); break;
		case EBangoGraphNodeKind::For:     Source = FString(TEXT("for (")) + Text + TEXT(")\n{\n};"); break;
		case EBangoGraphNodeKind::ForEach: Source = FString(TEXT("forEach (")) + Text + TEXT(")\n{\n};"); break;
		case EBangoGraphNodeKind::Return:  Source = Text.IsEmpty() ? FString(TEXT("return;")) : FString(TEXT("return ")) + Text + TEXT(";"); break;
		case EBangoGraphNodeKind::Label:   Source = FString(TEXT("label ")) + Text + TEXT(";"); break;
		case EBangoGraphNodeKind::Goto:    Source = FString(TEXT("goto ")) + Text + TEXT(";"); break;
		case EBangoGraphNodeKind::Break:
		case EBangoGraphNodeKind::Continue:
		case EBangoGraphNodeKind::Reroute:
			if (!Text.IsEmpty())
			{
				OutError = TEXT("This node has no text.");
				return false;
			}
			return true;
		}

		FBangoScript Script;
		FBangoCompileError Error;
		if (!ParseScript(Source, TEXT("node"), Script, Error))
		{
			OutError = Error.Message;
			return false;
		}

		// The text must be exactly one statement of the expected kind (no `a(); b()` tricks).
		if (Kind == EBangoGraphNodeKind::Entry && bInFunctionGraph)
		{
			if (Script.Functions.Num() != 1 || Script.TopLevel.Num() != 0)
			{
				OutError = TEXT("This should be a function signature, like `int AddScore(int _amount)`.");
				return false;
			}
			return true;
		}
		if (Script.Functions.Num() != 0 || Script.TopLevel.Num() != 1)
		{
			OutError = TEXT("This should be a single statement.");
			return false;
		}
		const EBangoStmtKind Parsed = Script.TopLevel[0]->Kind;
		if (IsStatementKind(Kind))
		{
			if (Parsed == EBangoStmtKind::VarDecl)
			{
				OutError = TEXT("A node can't be a declaration: add variables in the Globals or Locals list instead.");
				return false;
			}
			if (Parsed != EBangoStmtKind::Assign && Parsed != EBangoStmtKind::IncDec && Parsed != EBangoStmtKind::Call)
			{
				OutError = TEXT("This should be an assignment, ++/-- or a call.");
				return false;
			}
		}
		return true;
	}

	bool GetStatementKind(const FString& Text, EBangoGraphNodeKind& OutKind)
	{
		FBangoScript Script;
		FBangoCompileError Error;
		if (BangoGraphEditPrivate::HasLineBreak(Text) || !ParseScript(Text + TEXT(";"), TEXT("node"), Script, Error) || Script.TopLevel.Num() != 1 || Script.Functions.Num() != 0)
		{
			return false;
		}
		switch (Script.TopLevel[0]->Kind)
		{
		case EBangoStmtKind::Assign:  OutKind = EBangoGraphNodeKind::Assign; return true;
		case EBangoStmtKind::IncDec:  OutKind = EBangoGraphNodeKind::IncDec; return true;
		case EBangoStmtKind::Call:    OutKind = EBangoGraphNodeKind::Call; return true;
		default: return false;
		}
	}

	bool CanEditField(const FBangoNodeLayout& Layout, int32 FieldIndex)
	{
		return Layout.Fields.IsValidIndex(FieldIndex) && Layout.Fields[FieldIndex].bHasValue && !Layout.Fields[FieldIndex].bLocked && !Layout.Fields[FieldIndex].bReadOnly;
	}

	bool SetNodeField(FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context, int32 FieldIndex, const FString& Value, FString& OutError, bool bShowExpression)
	{
		using namespace BangoGraphEditPrivate;

		const FBangoNodeLayout Layout = GetNodeLayout(Node, bInFunctionGraph, Context, bShowExpression);
		if (!Layout.Fields.IsValidIndex(FieldIndex))
		{
			OutError = TEXT("That field doesn't exist.");
			return false;
		}
		const FBangoNodeField& Field = Layout.Fields[FieldIndex];
		if (Field.bLocNamespace)
		{
			OutError = TEXT("The Namespace isn't part of the node's text.");
			return false;
		}
		if (!CanEditField(Layout, FieldIndex))
		{
			OutError = Field.bLocked ? TEXT("Fill in the values before this one first.")
				: Field.bReadOnly ? TEXT("A parameter's default is edited in the INPUTS panel.")
				: TEXT("This can't be edited here.");
			return false;
		}

		const FString NewValue = Trimmed(Value);
		FString NewText;
		if (NewValue.IsEmpty())
		{
			if (!Field.bPresent)
			{
				return true; // already empty
			}
			// Clearing removes the field with its separators.
			NewText = Node.Text.Left(Field.OuterBegin) + Node.Text.Mid(Field.OuterEnd, Node.Text.Len() - Field.OuterEnd);
		}
		else if (Field.bPresent)
		{
			// Every copy of the field changes (MirrorBegins), from the last one back so the earlier places stay put.
			TArray<int32> Begins = Field.MirrorBegins;
			Begins.Add(Field.Begin);
			Begins.Sort([](int32 A, int32 B) { return A > B; });
			const int32 Length = Field.End - Field.Begin;
			NewText = Node.Text;
			for (const int32 Begin : Begins)
			{
				NewText = NewText.Left(Begin) + NewValue + NewText.Mid(Begin + Length, NewText.Len() - Begin - Length);
			}
		}
		else
		{
			NewText = Node.Text.Left(Field.Begin) + Field.InsertPrefix + NewValue + Field.InsertSuffix + Node.Text.Mid(Field.Begin, Node.Text.Len() - Field.Begin);
		}

		// Clearing the first field (`_x = 5` to `= 5`) would leave a space in front.
		NewText = Trimmed(NewText);

		EBangoGraphNodeKind NewKind = Node.Kind;
		if (IsStatementKind(Node.Kind) && !GetStatementKind(NewText, NewKind))
		{
			NewKind = Node.Kind; // ValidateNodeText explains what's wrong
		}
		FString Problem;
		if (!ValidateNodeText(NewKind, bInFunctionGraph, NewText, Problem))
		{
			const bool bClearingMiddle = NewValue.IsEmpty() && Field.OuterBegin == Field.Begin && Field.bOptional;
			OutError = bClearingMiddle
				? FString(TEXT("Only the last value can be removed. Remove the ones after it first."))
				: FString::Printf(TEXT("%s The node would read: %s"), *Problem, *NewText);
			return false;
		}

		Node.Text = NewText;
		Node.Kind = NewKind;
		return true;
	}

	int32 FindFieldAt(const FBangoNodeLayout& Layout, int32 Offset)
	{
		for (int32 i = 0; i < Layout.Fields.Num(); ++i)
		{
			const FBangoNodeField& Field = Layout.Fields[i];
			if (Field.bPresent && Offset >= Field.Begin && Offset < Field.End)
			{
				return i;
			}
		}
		// An error where an empty field goes (`if ()`, a new node not filled in yet) belongs to that field.
		for (int32 i = 0; i < Layout.Fields.Num(); ++i)
		{
			if (!Layout.Fields[i].bPresent && Layout.Fields[i].bHasValue && Offset == Layout.Fields[i].Begin)
			{
				return i;
			}
		}
		// An error right at the end of a field (like a missing ')' after it) belongs to that field too.
		for (int32 i = 0; i < Layout.Fields.Num(); ++i)
		{
			if (Layout.Fields[i].bPresent && Offset == Layout.Fields[i].End)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	bool FindErrorPlace(const FBangoGraphDocument& Document, const FString& WrittenText, const FBangoCompileError& Error, FBangoErrorPlace& OutPlace)
	{
		OutPlace = FBangoErrorPlace();
		if (Error.NodeID < 0)
		{
			return false;
		}

		bool bFound = false;
		for (int32 Graph = INDEX_NONE; Graph < Document.Functions.Num() && !bFound; ++Graph)
		{
			const FBangoGraph* Nodes = BangoGraphEditPrivate::GraphAt(Document, Graph);
			for (int32 i = 0; Nodes && i < Nodes->Nodes.Num(); ++i)
			{
				// NodeID 0 is the main graph's entry; function entries also use 0 but never get errors with that ID.
				const bool bMatch = Error.NodeID == 0 ? (Graph == INDEX_NONE && i == 0) : Nodes->Nodes[i].NodeID == Error.NodeID;
				if (bMatch)
				{
					OutPlace.Graph = Graph;
					OutPlace.Node = i;
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			return false;
		}

		// Find the node's text on the error's line to turn the column into an offset in the node's text.
		const FBangoGraphNode& Node = BangoGraphEditPrivate::GraphAt(Document, OutPlace.Graph)->Nodes[OutPlace.Node];
		int32 LineNumber = 1;
		int32 LineStart = 0;
		for (int32 i = 0; i < WrittenText.Len() && LineNumber < Error.Line; ++i)
		{
			if (WrittenText[i] == TEXT('\n'))
			{
				++LineNumber;
				LineStart = i + 1;
			}
		}
		if (LineNumber != Error.Line)
		{
			return true;
		}
		if (Node.Text.IsEmpty())
		{
			// A node with no text yet (like a new If): the error is about what's missing, which goes at the start.
			OutPlace.Offset = 0;
			return true;
		}
		int32 LineEnd = LineStart;
		while (LineEnd < WrittenText.Len() && WrittenText[LineEnd] != TEXT('\n'))
		{
			++LineEnd;
		}
		const FString Line = WrittenText.Mid(LineStart, LineEnd - LineStart);
		const int32 TextAt = Line.Find(*Node.Text, ESearchCase::CaseSensitive);
		if (TextAt != INDEX_NONE)
		{
			const int32 Offset = Error.Column - 1 - TextAt;
			if (Offset >= 0 && Offset <= Node.Text.Len())
			{
				OutPlace.Offset = Offset;
			}
		}
		return true;
	}

	namespace GraphEditPrivate
	{
		/** Where each line of Text starts, so token locations (line, column) can be turned into offsets. */
		static TArray<int32> GetLineStarts(const FString& Text)
		{
			TArray<int32> Starts;
			Starts.Add(0);
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				if (Text[i] == TEXT('\n'))
				{
					Starts.Add(i + 1);
				}
			}
			return Starts;
		}

		static int32 TokenBegin(const TArray<int32>& LineStarts, const FBangoToken& Token)
		{
			return LineStarts.IsValidIndex(Token.Loc.Line - 1) ? LineStarts[Token.Loc.Line - 1] + Token.Loc.Column - 1 : INDEX_NONE;
		}

		static int32 TokenEnd(const TArray<int32>& LineStarts, const FBangoToken& Token)
		{
			return LineStarts.IsValidIndex(Token.Loc.Line - 1) ? LineStarts[Token.Loc.Line - 1] + Token.EndColumn - 1 : INDEX_NONE;
		}

		/** The tokens of an entry text's parameter list: [OutFirst, OutEnd), and the offsets where the list starts and ends. */
		static bool FindParamList(const FString& EntryText, bool bInFunctionGraph, const FBangoLexResult& Lexed, const TArray<int32>& LineStarts,
			int32& OutFirst, int32& OutEnd, int32& OutBegin, int32& OutFinish)
		{
			const int32 NumTokens = Lexed.Tokens.Num() - 1; // without EndOfFile
			if (!bInFunctionGraph)
			{
				OutFirst = 0;
				OutEnd = NumTokens;
				OutBegin = 0;
				OutFinish = EntryText.Len();
				return true;
			}
			int32 Open = INDEX_NONE;
			for (int32 i = 0; i < NumTokens && Open == INDEX_NONE; ++i)
			{
				if (Lexed.Tokens[i].Kind == EBangoTokenKind::LParen)
				{
					Open = i;
				}
			}
			if (Open == INDEX_NONE)
			{
				return false;
			}
			int32 Depth = 0;
			for (int32 i = Open; i < NumTokens; ++i)
			{
				const EBangoTokenKind Kind = Lexed.Tokens[i].Kind;
				if (Kind == EBangoTokenKind::LParen || Kind == EBangoTokenKind::LBracket)
				{
					++Depth;
				}
				else if ((Kind == EBangoTokenKind::RParen || Kind == EBangoTokenKind::RBracket) && --Depth == 0)
				{
					OutFirst = Open + 1;
					OutEnd = i;
					OutBegin = TokenEnd(LineStarts, Lexed.Tokens[Open]);
					OutFinish = TokenBegin(LineStarts, Lexed.Tokens[i]);
					return true;
				}
			}
			return false;
		}

		static FString JoinParams(const TArray<FBangoEntryParam>& Params)
		{
			FString Result;
			for (int32 i = 0; i < Params.Num(); ++i)
			{
				if (i > 0)
				{
					Result += TEXT(", ");
				}
				Result += Params[i].Type.TrimStartAndEnd() + TEXT(" ") + Params[i].Name.TrimStartAndEnd();
				if (Params[i].bHasDefault)
				{
					Result += TEXT(" = ") + Params[i].Default.TrimStartAndEnd();
				}
			}
			return Result;
		}
	}

	bool RenameVariableInText(FString& Text, const FString& OldName, const FString& NewName)
	{
		FBangoLexResult Lexed;
		FBangoCompileError Error;
		if (OldName.IsEmpty() || OldName == NewName || !Bango::Lex(Text, Lexed, Error))
		{
			return false;
		}
		const TArray<int32> LineStarts = GraphEditPrivate::GetLineStarts(Text);
		// From the last token back, so the earlier ones' offsets stay right.
		bool bChanged = false;
		for (int32 i = Lexed.Tokens.Num() - 1; i >= 0; --i)
		{
			const FBangoToken& Token = Lexed.Tokens[i];
			if (Token.Kind == EBangoTokenKind::Identifier && Token.Text.Equals(OldName, ESearchCase::CaseSensitive)
				&& (i == 0 || Lexed.Tokens[i - 1].Kind != EBangoTokenKind::Dot)
				&& (i + 1 >= Lexed.Tokens.Num() || Lexed.Tokens[i + 1].Kind != EBangoTokenKind::LParen))
			{
				const int32 Begin = GraphEditPrivate::TokenBegin(LineStarts, Token);
				const int32 End = GraphEditPrivate::TokenEnd(LineStarts, Token);
				if (Begin >= 0 && End >= Begin && End <= Text.Len())
				{
					Text = Text.Left(Begin) + NewName + Text.Mid(End, Text.Len() - End);
					bChanged = true;
				}
			}
		}
		return bChanged;
	}

	namespace GraphEditPrivate
	{
		static bool IsFixedValueExpr(const FBangoExpr& Expr)
		{
			if (Expr.Kind == EBangoExprKind::Literal)
			{
				return true;
			}
			if (const FBangoUnaryExpr* Unary = Expr.As<FBangoUnaryExpr>())
			{
				const FBangoLiteralExpr* Operand = Unary->Operand.IsValid() ? Unary->Operand->As<FBangoLiteralExpr>() : nullptr;
				return Unary->Op == EBangoUnaryOp::Negate && Operand
					&& (Operand->LiteralKind == EBangoLiteralKind::Int || Operand->LiteralKind == EBangoLiteralKind::Double);
			}
			if (const FBangoArrayLiteralExpr* ArrayExpr = Expr.As<FBangoArrayLiteralExpr>())
			{
				for (const FBangoExprPtr& Element : ArrayExpr->Elements)
				{
					if (!Element.IsValid() || !IsFixedValueExpr(*Element))
					{
						return false;
					}
				}
				return true;
			}
			return false;
		}
	}

	bool CheckDeclValue(const FString& Type, const FString& Value, FString& OutError)
	{
		if (BangoGraphEditPrivate::HasLineBreak(Value))
		{
			OutError = TEXT("A starting value is one line.");
			return false;
		}
		FBangoScript Script;
		FBangoCompileError Error;
		const FString Source = FString::Printf(TEXT("%s _value = %s;"), *Type, *Value);
		if (!ParseScript(Source, TEXT("value"), Script, Error) || Script.TopLevel.Num() != 1 || Script.Functions.Num() != 0)
		{
			OutError = Error.Message.IsEmpty() ? FString(TEXT("This doesn't read as a value.")) : Error.Message;
			return false;
		}
		const FBangoVarDeclStmt* Decl = Script.TopLevel[0]->As<FBangoVarDeclStmt>();
		if (!Decl || !Decl->Init.IsValid() || Decl->Init->Kind == EBangoExprKind::Empty)
		{
			OutError = TEXT("This doesn't read as a value.");
			return false;
		}
		if (!GraphEditPrivate::IsFixedValueExpr(*Decl->Init))
		{
			OutError = TEXT("A starting value must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, a text, or an array of those. Set anything else in the code, with a Set node.");
			return false;
		}
		return true;
	}

	bool ReadEntryParams(const FString& EntryText, bool bInFunctionGraph, TArray<FBangoEntryParam>& OutParams)
	{
		OutParams.Reset();
		FBangoLexResult Lexed;
		FBangoCompileError Error;
		if (!Bango::Lex(EntryText, Lexed, Error))
		{
			return false;
		}
		const TArray<int32> LineStarts = GraphEditPrivate::GetLineStarts(EntryText);
		int32 First = 0, End = 0, Begin = 0, Finish = 0;
		if (!GraphEditPrivate::FindParamList(EntryText, bInFunctionGraph, Lexed, LineStarts, First, End, Begin, Finish))
		{
			return false;
		}

		// Split at the commas that aren't inside a default's brackets.
		TArray<TPair<int32, int32>> Parts;
		int32 PartStart = First;
		int32 Depth = 0;
		for (int32 i = First; i <= End; ++i)
		{
			const EBangoTokenKind Kind = i < End ? Lexed.Tokens[i].Kind : EBangoTokenKind::Comma;
			if (Kind == EBangoTokenKind::LParen || Kind == EBangoTokenKind::LBracket)
			{
				++Depth;
			}
			else if (Kind == EBangoTokenKind::RParen || Kind == EBangoTokenKind::RBracket)
			{
				--Depth;
			}
			else if (Kind == EBangoTokenKind::Comma && Depth == 0)
			{
				Parts.Add(TPair<int32, int32>(PartStart, i));
				PartStart = i + 1;
			}
		}
		if (Parts.Num() == 1 && Parts[0].Key == Parts[0].Value)
		{
			return true; // no parameters
		}

		for (const TPair<int32, int32>& Part : Parts)
		{
			int32 Assign = Part.Value;
			for (int32 i = Part.Key; i < Part.Value; ++i)
			{
				if (Lexed.Tokens[i].Kind == EBangoTokenKind::Assign)
				{
					Assign = i;
					break;
				}
			}
			const int32 NameToken = Assign - 1;
			if (NameToken <= Part.Key || Lexed.Tokens[NameToken].Kind != EBangoTokenKind::Identifier)
			{
				OutParams.Reset();
				return false; // a parameter needs a type and a name
			}
			FBangoEntryParam& Param = OutParams.AddDefaulted_GetRef();
			const int32 TypeBegin = GraphEditPrivate::TokenBegin(LineStarts, Lexed.Tokens[Part.Key]);
			const int32 NameBegin = GraphEditPrivate::TokenBegin(LineStarts, Lexed.Tokens[NameToken]);
			Param.Type = EntryText.Mid(TypeBegin, NameBegin - TypeBegin).TrimStartAndEnd();
			Param.Name = Lexed.Tokens[NameToken].Text;
			if (Assign < Part.Value)
			{
				Param.bHasDefault = true;
				if (Assign + 1 < Part.Value)
				{
					const int32 DefaultBegin = GraphEditPrivate::TokenBegin(LineStarts, Lexed.Tokens[Assign + 1]);
					const int32 DefaultEnd = GraphEditPrivate::TokenEnd(LineStarts, Lexed.Tokens[Part.Value - 1]);
					Param.Default = EntryText.Mid(DefaultBegin, DefaultEnd - DefaultBegin).TrimStartAndEnd();
				}
			}
		}
		return true;
	}

	bool WriteEntryParams(const FString& EntryText, bool bInFunctionGraph, const TArray<FBangoEntryParam>& Params, FString& OutText, FString& OutError)
	{
		FBangoLexResult Lexed;
		FBangoCompileError Error;
		if (!Bango::Lex(EntryText, Lexed, Error))
		{
			OutError = Error.Message;
			return false;
		}
		const TArray<int32> LineStarts = GraphEditPrivate::GetLineStarts(EntryText);
		int32 First = 0, End = 0, Begin = 0, Finish = 0;
		if (!GraphEditPrivate::FindParamList(EntryText, bInFunctionGraph, Lexed, LineStarts, First, End, Begin, Finish))
		{
			OutError = TEXT("The entry node's text has no parameter list.");
			return false;
		}
		const FString Text = EntryText.Left(Begin) + GraphEditPrivate::JoinParams(Params) + EntryText.Mid(Finish, EntryText.Len() - Finish);
		if (!ValidateNodeText(EBangoGraphNodeKind::Entry, bInFunctionGraph, Text, OutError))
		{
			return false;
		}
		OutText = Text;
		return true;
	}

	bool RenameFunctionInText(FString& Text, const FString& OldName, const FString& NewName)
	{
		FBangoLexResult Lexed;
		FBangoCompileError Error;
		if (OldName.IsEmpty() || !Bango::Lex(Text, Lexed, Error))
		{
			return false;
		}
		// From the last token back, so the earlier ones' columns stay right.
		bool bChanged = false;
		for (int32 i = Lexed.Tokens.Num() - 2; i >= 0; --i)
		{
			const FBangoToken& Token = Lexed.Tokens[i];
			if (Token.Kind == EBangoTokenKind::Identifier && Token.Text.Equals(OldName, ESearchCase::CaseSensitive)
				&& Lexed.Tokens[i + 1].Kind == EBangoTokenKind::LParen && Token.Loc.Line == 1)
			{
				const int32 Begin = Token.Loc.Column - 1;
				const int32 End = Token.EndColumn - 1;
				Text = Text.Left(Begin) + NewName + Text.Mid(End, Text.Len() - End);
				bChanged = true;
			}
		}
		return bChanged;
	}

	int64 GetNextNodeID(const FBangoGraphDocument& Document)
	{
		int64 Max = 0;
		auto Scan = [&Max](const FBangoGraph& Graph)
		{
			for (const FBangoGraphNode& Node : Graph.Nodes)
			{
				Max = Node.NodeID > Max ? Node.NodeID : Max;
			}
		};
		Scan(Document.Main);
		for (const FBangoGraph& Function : Document.Functions)
		{
			Scan(Function);
		}
		return Max + 1;
	}

	void GetNodeWarnings(const FBangoNodeLayout& Layout, TArray<FBangoFieldWarning>& OutWarnings)
	{
		OutWarnings.Reset();
		for (int32 i = 0; i < Layout.Fields.Num(); ++i)
		{
			const FBangoNodeField& Field = Layout.Fields[i];
			if (!Field.bPlaceholder)
			{
				continue;
			}
			if (!Field.bPresent)
			{
				FBangoFieldWarning& Warning = OutWarnings.AddDefaulted_GetRef();
				Warning.Field = i;
				Warning.Message = FString::Printf(TEXT("%s has no value. It will be saved as nil, so the text shows \"nil\" there."), *Field.Label);
			}
			else if (Field.Text.TrimStartAndEnd().Equals(TEXT("nil"), ESearchCase::CaseSensitive))
			{
				FBangoFieldWarning& Warning = OutWarnings.AddDefaulted_GetRef();
				Warning.Field = i;
				Warning.Message = FString::Printf(TEXT("%s is nil, so the text shows \"nil\" there."), *Field.Label);
			}
		}
	}

	bool FillMissingPlaceholders(FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context)
	{
		if (!Node.Text.Contains(TEXT("%")))
		{
			return false; // no placeholders: skip the layout
		}
		bool bChanged = false;
		// One value at a time, since each one is added after the last.
		for (int32 Guard = 0; Guard < 64; ++Guard)
		{
			const FBangoNodeLayout Layout = GetNodeLayout(Node, bInFunctionGraph, Context);
			int32 Missing = INDEX_NONE;
			for (int32 i = 0; i < Layout.Fields.Num(); ++i)
			{
				if (Layout.Fields[i].bPlaceholder && !Layout.Fields[i].bPresent)
				{
					Missing = i;
					break;
				}
			}
			FString Error;
			if (Missing == INDEX_NONE || !SetNodeField(Node, bInFunctionGraph, Context, Missing, TEXT("nil"), Error))
			{
				break;
			}
			bChanged = true;
		}
		return bChanged;
	}

	bool FillMissingPlaceholders(FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context)
	{
		bool bChanged = false;
		auto FillGraph = [&](FBangoGraph& Graph)
		{
			for (FBangoGraphNode& Node : Graph.Nodes)
			{
				bChanged |= FillMissingPlaceholders(Node, Graph.bIsFunction, Context);
			}
		};
		FillGraph(Document.Main);
		for (FBangoGraph& Function : Document.Functions)
		{
			FillGraph(Function);
		}
		return bChanged;
	}

	bool IsTextField(const FBangoNodeField& Field)
	{
		return Field.bHasValue && (Field.TypeHint.Equals(TEXT("string"), ESearchCase::CaseSensitive) || IsLocalizedTextField(Field));
	}

	bool IsLocalizedTextField(const FBangoNodeField& Field)
	{
		return Field.bHasValue && Field.TypeHint.Equals(TEXT("text"), ESearchCase::CaseSensitive);
	}

	bool TryGetStringLiteral(const FString& Text, FString& OutValue)
	{
		FBangoLexResult Lexed;
		FBangoCompileError Error;
		if (!Lex(Text, Lexed, Error) || Lexed.Tokens.Num() != 2 || Lexed.Tokens[0].Kind != EBangoTokenKind::StringLiteral
			|| Lexed.Comments.Num() > 0 || Lexed.MetadataLines.Num() > 0)
		{
			return false;
		}
		OutValue = Lexed.Tokens[0].Text;
		return true;
	}

	FString MakeStringLiteral(const FString& Value)
	{
		FString Out = TEXT("\"");
		for (int32 i = 0; i < Value.Len(); ++i)
		{
			const TCHAR C = Value[i];
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:         Out.AppendChar(C); break;
			}
		}
		Out += TEXT("\"");
		return Out;
	}

	FString StringToTypedText(const FString& Value)
	{
		FString Out;
		for (int32 i = 0; i < Value.Len(); ++i)
		{
			const TCHAR C = Value[i];
			const TCHAR Next = i + 1 < Value.Len() ? Value[i + 1] : TEXT('\0');
			if (C == TEXT('\n'))
			{
				Out += TEXT("\\n");
			}
			else if (C == TEXT('\t'))
			{
				Out += TEXT("\\t");
			}
			else if (C == TEXT('\\'))
			{
				// Only doubled where a single one would read as an escape (TypedTextToString's rules).
				const bool bWouldEscape = Next == TEXT('n') || Next == TEXT('t') || Next == TEXT('\\') || Next == TEXT('"')
					|| Next == TEXT('\n') || Next == TEXT('\t') || (i == 0 && Next == TEXT('='));
				Out += bWouldEscape ? TEXT("\\\\") : TEXT("\\");
			}
			else if (i == 0 && C == TEXT('='))
			{
				Out += TEXT("\\="); // a leading = would mean an expression
			}
			else
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	FString TypedTextToString(const FString& Typed)
	{
		FString Out;
		int32 i = 0;
		if (Typed.Len() >= 2 && Typed[0] == TEXT('\\') && Typed[1] == TEXT('='))
		{
			Out.AppendChar(TEXT('='));
			i = 2;
		}
		for (; i < Typed.Len(); ++i)
		{
			const TCHAR C = Typed[i];
			const TCHAR Next = i + 1 < Typed.Len() ? Typed[i + 1] : TEXT('\0');
			if (C == TEXT('\\'))
			{
				switch (Next)
				{
				case TEXT('n'):  Out.AppendChar(TEXT('\n')); ++i; continue;
				case TEXT('t'):  Out.AppendChar(TEXT('\t')); ++i; continue;
				case TEXT('\\'): Out.AppendChar(TEXT('\\')); ++i; continue;
				case TEXT('"'):  Out.AppendChar(TEXT('"')); ++i; continue;
				default: break; // any other backslash is just a backslash
				}
			}
			Out.AppendChar(C);
		}
		return Out;
	}

	FString GetFieldEditText(const FBangoNodeField& Field)
	{
		if (!IsTextField(Field))
		{
			return Field.Text;
		}
		if (!Field.bPresent)
		{
			return FString();
		}
		FString Value;
		FBangoTextLiteral Literal;
		if (IsLocalizedTextField(Field) && ParseTextLiteral(Field.Text, Literal))
		{
			return StringToTypedText(Literal.Source);
		}
		return TryGetStringLiteral(Field.Text, Value) && !IsLocalizedTextField(Field) ? StringToTypedText(Value) : FString(TEXT("=")) + Field.Text;
	}

	FString GetFieldDisplayText(const FBangoNodeField& Field)
	{
		if (!IsTextField(Field) || !Field.bPresent)
		{
			return Field.Text;
		}
		FString Value;
		FBangoTextLiteral Literal;
		if (IsLocalizedTextField(Field) && ParseTextLiteral(Field.Text, Literal))
		{
			return Literal.Source;
		}
		return TryGetStringLiteral(Field.Text, Value) && !IsLocalizedTextField(Field) ? Value : FString(TEXT("=")) + Field.Text;
	}

	FString GetFieldValueFromTyped(const FBangoNodeField& Field, const FString& Typed)
	{
		if (!IsTextField(Field))
		{
			return Typed;
		}
		if (Typed.StartsWith(TEXT("="), ESearchCase::CaseSensitive))
		{
			return Typed.Mid(1, Typed.Len() - 1); // an expression, as written
		}
		if (Typed.IsEmpty() && Field.bOptional)
		{
			return FString(); // clears it; `=""` gives an empty text
		}
		if (IsLocalizedTextField(Field))
		{
			// The same text with new words: its namespace, key and notes stay. A new text gets a new key.
			FBangoTextLiteral Literal;
			if (!Field.bPresent || !ParseTextLiteral(Field.Text, Literal))
			{
				Literal = FBangoTextLiteral();
				Literal.Key = MakeTextKey();
			}
			Literal.Source = TypedTextToString(Typed);
			return MakeTextLiteral(Literal);
		}
		return MakeStringLiteral(TypedTextToString(Typed));
	}
}
