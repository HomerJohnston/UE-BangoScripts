#include "BangoGraphLayout.h"
#include "BangoGraphEdit.h"

#include "BangoAST.h"
#include "BangoLexer.h"
#include "BangoParser.h"

const FBangoCommandSignature* FBangoNodeLayoutContext::FindFunction(const FString& Name) const
{
	for (const FBangoCommandSignature& Function : Functions)
	{
		if (Function.Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return &Function;
		}
	}
	return nullptr;
}

bool FBangoNodeField::operator==(const FBangoNodeField& Other) const
{
	return Label.Equals(Other.Label, ESearchCase::CaseSensitive)
		&& Description.Equals(Other.Description, ESearchCase::CaseSensitive)
		&& TypeHint.Equals(Other.TypeHint, ESearchCase::CaseSensitive)
		&& Text.Equals(Other.Text, ESearchCase::CaseSensitive)
		&& Begin == Other.Begin && End == Other.End
		&& bPresent == Other.bPresent && bOptional == Other.bOptional && bHasValue == Other.bHasValue
		&& bLocked == Other.bLocked && bReadOnly == Other.bReadOnly && bPlaceholder == Other.bPlaceholder && bLocNamespace == Other.bLocNamespace
		&& bVariable == Other.bVariable && VariableType.Equals(Other.VariableType, ESearchCase::CaseSensitive)
		&& MirrorBegins == Other.MirrorBegins
		&& bHideLabel == Other.bHideLabel && bHideIcon == Other.bHideIcon
		&& SectionHeader.Equals(Other.SectionHeader, ESearchCase::CaseSensitive)
		&& OuterBegin == Other.OuterBegin && OuterEnd == Other.OuterEnd
		&& InsertPrefix.Equals(Other.InsertPrefix, ESearchCase::CaseSensitive)
		&& InsertSuffix.Equals(Other.InsertSuffix, ESearchCase::CaseSensitive);
}

bool FBangoNodeLayout::operator==(const FBangoNodeLayout& Other) const
{
	return Style == Other.Style
		&& Title.Equals(Other.Title, ESearchCase::CaseSensitive)
		&& CallName.Equals(Other.CallName, ESearchCase::CaseSensitive)
		&& CategoryName.Equals(Other.CategoryName, ESearchCase::CaseSensitive)
		&& Description.Equals(Other.Description, ESearchCase::CaseSensitive)
		&& Fields == Other.Fields
		&& Error.Equals(Other.Error, ESearchCase::CaseSensitive)
		&& bHasSimpleForm == Other.bHasSimpleForm && bShowsExpression == Other.bShowsExpression;
}

namespace BangoGraphLayoutPrivate
{
	/** The tokens of one node's text, with helpers to turn token ranges into fields. */
	struct FTokens
	{
		const FString& Text;
		TArray<FBangoToken> T; // without the end-of-file token
		bool bOk = false;

		explicit FTokens(const FString& InText) : Text(InText)
		{
			FBangoLexResult Lexed;
			FBangoCompileError Error;
			bOk = Bango::Lex(Text, Lexed, Error);
			if (bOk)
			{
				for (const FBangoToken& Token : Lexed.Tokens)
				{
					if (Token.Kind != EBangoTokenKind::EndOfFile)
					{
						T.Add(Token);
					}
				}
			}
		}

		int32 Num() const { return T.Num(); }
		bool Is(int32 Index, EBangoTokenKind Kind) const { return T.IsValidIndex(Index) && T[Index].Kind == Kind; }
		int32 StartOf(int32 Index) const { return T.IsValidIndex(Index) ? T[Index].Loc.Column - 1 : Text.Len(); }
		int32 EndOf(int32 Index) const { return T[Index].EndColumn - 1; }

		/** A field for tokens [B, E). An empty range is a field that isn't present, placed at token B. */
		FBangoNodeField Field(const FString& Label, const FString& TypeHint, int32 B, int32 E) const
		{
			FBangoNodeField F;
			F.Label = Label;
			F.TypeHint = TypeHint;
			if (E > B)
			{
				F.Begin = StartOf(B);
				F.End = EndOf(E - 1);
				F.Text = Text.Mid(F.Begin, F.End - F.Begin);
			}
			else
			{
				F.Begin = F.End = StartOf(B);
				F.bPresent = false;
			}
			F.OuterBegin = F.Begin;
			F.OuterEnd = F.End;
			return F;
		}

		/** Index of the bracket that closes the one at Open, or INDEX_NONE. */
		int32 Matching(int32 Open) const
		{
			int32 Depth = 0;
			for (int32 i = Open; i < Num(); ++i)
			{
				const EBangoTokenKind K = T[i].Kind;
				if (K == EBangoTokenKind::LParen || K == EBangoTokenKind::LBracket)
				{
					++Depth;
				}
				else if (K == EBangoTokenKind::RParen || K == EBangoTokenKind::RBracket)
				{
					if (--Depth == 0)
					{
						return i;
					}
				}
			}
			return INDEX_NONE;
		}

		/** Splits [B, E) at Separator tokens outside brackets. An empty range gives no parts if bEmptyIsNone. */
		TArray<TPair<int32, int32>> Split(int32 B, int32 E, EBangoTokenKind Separator, bool bEmptyIsNone) const
		{
			TArray<TPair<int32, int32>> Parts;
			if (B >= E && bEmptyIsNone)
			{
				return Parts;
			}
			int32 Depth = 0;
			int32 Start = B;
			for (int32 i = B; i < E; ++i)
			{
				const EBangoTokenKind K = T[i].Kind;
				if (K == EBangoTokenKind::LParen || K == EBangoTokenKind::LBracket)
				{
					++Depth;
				}
				else if (K == EBangoTokenKind::RParen || K == EBangoTokenKind::RBracket)
				{
					--Depth;
				}
				else if (K == Separator && Depth == 0)
				{
					Parts.Add(TPair<int32, int32>(Start, i));
					Start = i + 1;
				}
			}
			Parts.Add(TPair<int32, int32>(Start, E));
			return Parts;
		}

		/** First token in [B, E) outside brackets whose kind passes Test, or INDEX_NONE. */
		template <typename FTest>
		int32 FindTopLevel(int32 B, int32 E, FTest Test) const
		{
			int32 Depth = 0;
			for (int32 i = B; i < E; ++i)
			{
				const EBangoTokenKind K = T[i].Kind;
				if (K == EBangoTokenKind::LParen || K == EBangoTokenKind::LBracket)
				{
					++Depth;
				}
				else if (K == EBangoTokenKind::RParen || K == EBangoTokenKind::RBracket)
				{
					--Depth;
				}
				else if (Depth == 0 && Test(K))
				{
					return i;
				}
			}
			return INDEX_NONE;
		}
	};

	static bool IsAssignOp(EBangoTokenKind K)
	{
		return K == EBangoTokenKind::Assign || K == EBangoTokenKind::PlusAssign || K == EBangoTokenKind::MinusAssign
			|| K == EBangoTokenKind::StarAssign || K == EBangoTokenKind::SlashAssign;
	}

	static FBangoNodeField WholeText(const FString& Label, const FString& TypeHint, const FString& Text, bool bOptional)
	{
		FBangoNodeField F;
		F.Label = Label;
		F.TypeHint = TypeHint;
		F.Text = Text;
		F.Begin = 0;
		F.End = Text.Len();
		F.OuterBegin = 0;
		F.OuterEnd = Text.Len();
		F.bPresent = !Text.IsEmpty();
		F.bOptional = bOptional;
		return F;
	}

	// ---- Array commands (built into the compiler) ----

	struct FArrayCommandInfo
	{
		const TCHAR* Name;
		bool bHasTarget;
		const TCHAR* ParamNames[2];
		const TCHAR* ParamHints[2];
		int32 NumParams;
		const TCHAR* ReturnHint; // null: returns nothing
	};

	static const FArrayCommandInfo GArrayCommands[] =
	{
		{ TEXT("Count"),    false, { TEXT("Array"), nullptr },     { TEXT("array"), nullptr },       1, TEXT("int") },
		{ TEXT("Push"),     true,  { TEXT("Value"), nullptr },     { TEXT("element"), nullptr },     1, nullptr },
		{ TEXT("InsertAt"), true,  { TEXT("Value"), TEXT("Index") }, { TEXT("element"), TEXT("int") }, 2, nullptr },
		{ TEXT("Pop"),      true,  { nullptr, nullptr },           { nullptr, nullptr },             0, TEXT("element") },
		{ TEXT("RemoveAt"), true,  { TEXT("Index"), nullptr },     { TEXT("int"), nullptr },         1, TEXT("element") },
	};

	static const FArrayCommandInfo* FindArrayCommand(const FString& Name)
	{
		for (const FArrayCommandInfo& Info : GArrayCommands)
		{
			if (Name.Equals(Info.Name, ESearchCase::CaseSensitive))
			{
				return &Info;
			}
		}
		return nullptr;
	}

	/** What a dedicated call node shows, whichever kind of callee it is. */
	struct FCallee
	{
		EBangoNodeStyle Style = EBangoNodeStyle::Generic;
		bool bHasTarget = false;
		FString TargetHint;
		TArray<FString> ParamNames;
		TArray<FString> ParamHints;
		/** Per parameter; may be shorter than ParamNames (missing ones are empty). */
		TArray<FString> ParamDescriptions;
		int32 NumRequired = 0;
		bool bVariadic = false;
		FString VariadicLabelPrefix = TEXT("Value ");
		int32 VariadicFirstIndex = 1;
		FString VariadicLabelSuffix;
		int32 PlaceholderParam = INDEX_NONE;
		/** True if the placeholder parameter is text (a LOCTEXT), otherwise a string; both have {0} placeholders. */
		bool bTextPlaceholders = false;
		bool bReturnsValue = false;
		/** Its value must be used (see FBangoCommandSignature::MustUseResult), so it can't be a line of its own. */
		bool bMustUseResult = false;
		FString ReturnHint;
		FString CategoryName;
		FString Description;
	};

	static bool FindCallee(const FString& Name, const FBangoNodeLayoutContext& Context, FCallee& Out)
	{
		auto FromSignature = [&Out](const FBangoCommandSignature& Signature, EBangoNodeStyle Style)
		{
			Out.Style = Style;
			Out.bHasTarget = Signature.bHasTarget;
			Out.TargetHint = Signature.bHasTarget ? Signature.TargetType.ToString() : FString();
			for (const FBangoCommandParam& Param : Signature.Params)
			{
				Out.ParamNames.Add(Param.Name);
				Out.ParamHints.Add(Param.GetTypeName()); // "number" for a NumberParam
				Out.ParamDescriptions.Add(Param.Description);
			}
			Out.NumRequired = Signature.GetNumRequiredParams();
			Out.bVariadic = Signature.bVariadic;
			Out.VariadicLabelPrefix = Signature.VariadicLabelPrefix;
			Out.VariadicFirstIndex = Signature.VariadicFirstIndex;
			Out.VariadicLabelSuffix = Signature.VariadicLabelSuffix;
			Out.PlaceholderParam = Signature.PlaceholderParam;
			Out.bTextPlaceholders = Signature.Params.IsValidIndex(Signature.PlaceholderParam) && Signature.Params[Signature.PlaceholderParam].Type == FBangoType::Text();
			Out.bReturnsValue = Signature.bReturnsValue;
			Out.bMustUseResult = Style == EBangoNodeStyle::Command && Signature.MustUseResult(); // script functions can be thrown away
			Out.ReturnHint = Signature.bReturnsValue ? Signature.GetReturnTypeName() : FString();
			Out.CategoryName = Signature.CategoryName;
			Out.Description = Signature.CommandDescription;
		};

		if (const FBangoCommandSignature* Function = Context.FindFunction(Name))
		{
			FromSignature(*Function, EBangoNodeStyle::ScriptFunction);
			return true;
		}
		if (const FArrayCommandInfo* Info = FindArrayCommand(Name))
		{
			Out.Style = EBangoNodeStyle::ArrayCommand;
			Out.bHasTarget = Info->bHasTarget;
			Out.TargetHint = TEXT("array");
			for (int32 i = 0; i < Info->NumParams; ++i)
			{
				Out.ParamNames.Add(Info->ParamNames[i]);
				Out.ParamHints.Add(Info->ParamHints[i]);
			}
			Out.NumRequired = Info->NumParams;
			Out.bReturnsValue = Info->ReturnHint != nullptr;
			Out.bMustUseResult = !Info->bHasTarget && Info->ReturnHint != nullptr; // Count; Pop and RemoveAt can be thrown away
			Out.ReturnHint = Info->ReturnHint ? Info->ReturnHint : TEXT("");
			Out.CategoryName = TEXT("Arrays");
			return true;
		}
		if (Context.Commands)
		{
			if (const FBangoCommandSignature* Command = Context.Commands->FindCommand(Name))
			{
				FromSignature(*Command, EBangoNodeStyle::Command);
				return true;
			}
		}
		return false;
	}

	/**
	 * How argument i is cleared and filled in: clearing removes it with the `, ` before it (or everything after
	 * the `(` for the first one); filling it in adds `, ` if there are arguments before it. It's locked if an
	 * argument before it is missing too.
	 */
	static void SetArgumentEdits(const FTokens& Tok, FBangoNodeField& Field, const TArray<TPair<int32, int32>>& Args, int32 Index, int32 NameTok)
	{
		if (Index < Args.Num())
		{
			Field.OuterBegin = Index == 0 ? Tok.EndOf(NameTok + 1) : Tok.EndOf(Args[Index - 1].Value - 1);
			Field.OuterEnd = Field.End;
			// Clearing an argument in the middle would shift the ones after it; only the last one can be cleared.
			if (Index < Args.Num() - 1)
			{
				Field.OuterBegin = Field.Begin;
			}
		}
		else
		{
			Field.InsertPrefix = Args.Num() > 0 ? TEXT(", ") : TEXT("");
			Field.bLocked = Index > Args.Num();
		}
	}

	/** Placeholder rows past this (like {40} in a text) aren't shown until they have values. */
	static constexpr int32 MaxPlaceholderRows = 32;

	/**
	 * A dedicated node for a call statement, `[target] Name(args)`. Returns false if the text isn't exactly one
	 * call (then the caller uses the generic layout). Sets Out.Error for an unknown callee.
	 *
	 * A declaration or assignment that stores a call's result (`double _t = Random(1.0, 2.0)`) is not a call
	 * node: storing the value is what the line does, so it's a Declare or Set node with the call as its Value.
	 */
	static bool TryCallLayout(const FBangoGraphNode& Node, const FTokens& Tok, const FBangoNodeLayoutContext& Context, FBangoNodeLayout& Out)
	{
		FBangoScript Script;
		FBangoCompileError Error;
		if (!Bango::ParseScript(Node.Text + TEXT(";"), TEXT("node"), Script, Error) || Script.TopLevel.Num() != 1)
		{
			return false;
		}

		const FBangoStmt& Stmt = *Script.TopLevel[0];
		const FBangoCallStmt* CallStmt = Stmt.As<FBangoCallStmt>();
		const FBangoCallExpr* Call = CallStmt && CallStmt->Call ? CallStmt->Call->As<FBangoCallExpr>() : nullptr;
		if (!Call)
		{
			return false;
		}

		// Find the call's name token, and check the call runs to the end of the text.
		int32 NameTok = INDEX_NONE;
		for (int32 i = 0; i + 1 < Tok.Num(); ++i)
		{
			if (Tok.Is(i, EBangoTokenKind::Identifier) && Tok.T[i].Loc.Column == Call->NameLoc.Column && Tok.Is(i + 1, EBangoTokenKind::LParen))
			{
				NameTok = i;
				break;
			}
		}
		const int32 CloseTok = NameTok == INDEX_NONE ? INDEX_NONE : Tok.Matching(NameTok + 1);
		if (CloseTok != Tok.Num() - 1)
		{
			return false;
		}

		const int32 TargetBegin = 0;

		FCallee Callee;
		if (!FindCallee(Call->Name, Context, Callee))
		{
			Out.Style = EBangoNodeStyle::Generic;
			Out.Title = Call->Name;
			Out.CallName = Call->Name;
			Out.Fields.Add(WholeText(TEXT("Call"), FString(), Node.Text, false));
			Out.Error = FString::Printf(TEXT("Unknown command or function '%s'."), *Call->Name);
			return true;
		}

		Out.Style = Callee.Style;
		Out.Title = Call->Name;
		Out.CallName = Call->Name;
		Out.CategoryName = Callee.CategoryName;
		Out.Description = Callee.Description;

		if (Callee.bHasTarget || NameTok > TargetBegin)
		{
			FBangoNodeField Target = Tok.Field(TEXT("Target"), Callee.TargetHint, TargetBegin, NameTok);
			Target.OuterEnd = Tok.StartOf(NameTok); // with the space before the name
			Target.InsertSuffix = TEXT(" ");
			if (Callee.Style == EBangoNodeStyle::ArrayCommand)
			{
				// Array commands change their target, so it's always a variable (or an element of one).
				Target.bVariable = true;
				Target.VariableType = Callee.TargetHint;
			}
			Out.Fields.Add(Target);
			if (!Callee.bHasTarget)
			{
				Out.Error = FString::Printf(TEXT("%s doesn't take a target."), *Call->Name);
			}
		}

		const TArray<TPair<int32, int32>> Args = Tok.Split(NameTok + 2, CloseTok, EBangoTokenKind::Comma, /*bEmptyIsNone*/ true);
		for (int32 i = 0; i < Callee.ParamNames.Num(); ++i)
		{
			FBangoNodeField Field = i < Args.Num()
				? Tok.Field(Callee.ParamNames[i], Callee.ParamHints[i], Args[i].Key, Args[i].Value)
				: Tok.Field(Callee.ParamNames[i], Callee.ParamHints[i], CloseTok, CloseTok);
			Field.bOptional = i >= Callee.NumRequired;
			Field.Description = Callee.ParamDescriptions.IsValidIndex(i) ? Callee.ParamDescriptions[i] : FString();
			SetArgumentEdits(Tok, Field, Args, i, NameTok);
			Out.Fields.Add(Field);
		}
		// Extra values: the ones given, plus a row for each placeholder in a literal text (like Hint's "{0} {1}")
		// that has no value yet, so it can be filled in.
		int32 NumPlaceholders = 0;
		if (Callee.bVariadic && Args.IsValidIndex(Callee.PlaceholderParam))
		{
			const TPair<int32, int32>& TextArg = Args[Callee.PlaceholderParam];
			FBangoTextLiteral TextLiteral;
			if (Callee.bTextPlaceholders)
			{
				if (Bango::ParseTextLiteral(Node.Text.Mid(Tok.StartOf(TextArg.Key), Tok.EndOf(TextArg.Value - 1) - Tok.StartOf(TextArg.Key)), TextLiteral))
				{
					NumPlaceholders = FMath::Min(FMath::Max(Bango::CountTextFormatValues(TextLiteral.Source), 0), MaxPlaceholderRows);
				}
			}
			else if (TextArg.Value - TextArg.Key == 1 && Tok.Is(TextArg.Key, EBangoTokenKind::StringLiteral))
			{
				NumPlaceholders = FMath::Min(Bango::CountFormatValues(Tok.T[TextArg.Key].Text), MaxPlaceholderRows);
			}
		}
		const int32 NumExtraRows = FMath::Max(NumPlaceholders, Args.Num() - Callee.ParamNames.Num());
		for (int32 ExtraIndex = 0; ExtraIndex < NumExtraRows; ++ExtraIndex)
		{
			const int32 i = Callee.ParamNames.Num() + ExtraIndex;
			const FString Label = Callee.bVariadic
				? FString::Printf(TEXT("%s%d%s"), *Callee.VariadicLabelPrefix, Callee.VariadicFirstIndex + ExtraIndex, *Callee.VariadicLabelSuffix)
				: FString::Printf(TEXT("Value %d"), ExtraIndex + 1);
			const bool bGiven = i < Args.Num();
			FBangoNodeField Extra = bGiven ? Tok.Field(Label, TEXT("any"), Args[i].Key, Args[i].Value) : Tok.Field(Label, TEXT("any"), CloseTok, CloseTok);
			Extra.bOptional = bGiven; // a given value can be cleared; a placeholder without one shows as missing
			Extra.bPlaceholder = ExtraIndex < NumPlaceholders;
			SetArgumentEdits(Tok, Extra, Args, i, NameTok);
			Out.Fields.Add(Extra);
			if (bGiven && !Callee.bVariadic && Out.Error.IsEmpty())
			{
				Out.Error = FString::Printf(TEXT("%s takes %s%d argument(s), but %d are given."), *Call->Name,
					Callee.NumRequired < Callee.ParamNames.Num() ? TEXT("at most ") : TEXT(""), Callee.ParamNames.Num(), Args.Num());
			}
		}
		// A command whose value must be used is never a node of its own: it goes in a Value, like `_s = Format(...)`.
		if (Callee.bMustUseResult)
		{
			Out.Error = FString::Printf(TEXT("%s gives a value that must be used, so it can't be a node of its own. Use it in the To of a Set node, like '_x = %s(...)'."), *Call->Name, *Call->Name);
		}
		return true;
	}

	static void ScriptEntryLayout(const FBangoGraphNode& Node, const FTokens& Tok, FBangoNodeLayout& Out)
	{
		Out.Style = EBangoNodeStyle::ScriptEntry;
		Out.Title = TEXT("Script Start");
		for (const TPair<int32, int32>& Part : Tok.Split(0, Tok.Num(), EBangoTokenKind::Comma, /*bEmptyIsNone*/ true))
		{
			const int32 Equals = Tok.FindTopLevel(Part.Key, Part.Value, [](EBangoTokenKind K) { return K == EBangoTokenKind::Assign; });
			const int32 NameTok = (Equals == INDEX_NONE ? Part.Value : Equals) - 1;
			if (NameTok <= Part.Key || !Tok.Is(NameTok, EBangoTokenKind::Identifier))
			{
				Out.Fields.Reset();
				Out.Fields.Add(WholeText(TEXT("Parameters"), FString(), Node.Text, true));
				Out.Fields[0].SectionHeader = TEXT("Input Params");
				return;
			}
			const FBangoNodeField Type = Tok.Field(FString(), FString(), Part.Key, NameTok);
			FBangoNodeField Field = Equals == INDEX_NONE
				? Tok.Field(Tok.T[NameTok].Text, Type.Text, Part.Value, Part.Value)
				: Tok.Field(Tok.T[NameTok].Text, Type.Text, Equals + 1, Part.Value);
			Field.bOptional = Equals != INDEX_NONE;
			// The node lists the parameters (name and type) without their defaults: those are shown and edited in the
			// editor's INPUTS panel. The field still knows where the default is, and its type.
			Field.bReadOnly = true;
			Field.bHasValue = false;
			if (Equals == INDEX_NONE)
			{
				Field.Begin = Field.End = Field.OuterBegin = Field.OuterEnd = Tok.EndOf(NameTok);
				Field.InsertPrefix = TEXT(" = ");
			}
			else
			{
				Field.OuterBegin = Tok.EndOf(NameTok);
			}
			Out.Fields.Add(Field);
		}
		if (Out.Fields.Num() > 0)
		{
			Out.Fields[0].SectionHeader = TEXT("Input Params");
		}
	}

	static void FunctionEntryLayout(const FBangoGraphNode& Node, const FTokens& Tok, FBangoNodeLayout& Out)
	{
		Out.Style = EBangoNodeStyle::FunctionEntry;
		int32 Open = INDEX_NONE;
		for (int32 i = 0; i < Tok.Num(); ++i)
		{
			if (Tok.Is(i, EBangoTokenKind::LParen))
			{
				Open = i;
				break;
			}
		}
		const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Tok.Matching(Open);
		if (Open < 2 || Close == INDEX_NONE || !Tok.Is(Open - 1, EBangoTokenKind::Identifier))
		{
			Out.Title = TEXT("Function");
			Out.Fields.Add(WholeText(TEXT("Signature"), FString(), Node.Text, false));
			return;
		}

		Out.Title = Tok.T[Open - 1].Text;
		Out.Fields.Add(Tok.Field(TEXT("Returns"), FString(), 0, Open - 1));
		for (const TPair<int32, int32>& Part : Tok.Split(Open + 1, Close, EBangoTokenKind::Comma, /*bEmptyIsNone*/ true))
		{
			if (Part.Value - Part.Key < 2)
			{
				continue;
			}
			const FBangoNodeField Type = Tok.Field(FString(), FString(), Part.Key, Part.Value - 1);
			FBangoNodeField Param = Tok.Field(Tok.T[Part.Value - 1].Text, Type.Text, Part.Value - 1, Part.Value);
			Param.bHasValue = false;
			Out.Fields.Add(Param);
		}
	}

	/** A type as written, without spaces: "int [ ]" -> "int[]". */
	static FString CompactTypeText(const FString& Text)
	{
		FString Out;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			if (C != TEXT(' ') && C != TEXT('\t'))
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	/** "int[]" -> "Int[]". */
	static FString CapitalizeFirst(const FString& Text)
	{
		FString Out;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			Out.AppendChar(i == 0 && C >= TEXT('a') && C <= TEXT('z') ? (TCHAR)(C - TEXT('a') + TEXT('A')) : C);
		}
		return Out;
	}

	/** Variable name -> type text ("int", "string[]"); an empty type when it can't be told. */
	using FVarScope = TMap<FString, FString>;

	static bool IsTypeKeywordKind(EBangoTokenKind Kind)
	{
		return Kind == EBangoTokenKind::KwBool || Kind == EBangoTokenKind::KwInt || Kind == EBangoTokenKind::KwDouble
			|| Kind == EBangoTokenKind::KwString || Kind == EBangoTokenKind::KwEntity || Kind == EBangoTokenKind::KwText
			|| Kind == EBangoTokenKind::KwVector2 || Kind == EBangoTokenKind::KwVector3;
	}

	/** Reads a declaration like `int _x = 5`, `global string[] log` or a for loop's `int _i = 0`. */
	static bool ReadDeclaration(const FString& Text, bool& bOutGlobal, FString& OutType, FString& OutName)
	{
		const FTokens Tok(Text);
		if (!Tok.bOk)
		{
			return false;
		}
		int32 i = 0;
		bOutGlobal = Tok.Is(0, EBangoTokenKind::KwGlobal);
		if (bOutGlobal)
		{
			++i;
		}
		const int32 TypeBegin = i;
		if (!Tok.T.IsValidIndex(i) || !IsTypeKeywordKind(Tok.T[i].Kind))
		{
			return false;
		}
		++i;
		while (Tok.Is(i, EBangoTokenKind::LBracket) && Tok.Is(i + 1, EBangoTokenKind::RBracket))
		{
			i += 2;
		}
		if (!Tok.Is(i, EBangoTokenKind::Identifier))
		{
			return false;
		}
		OutType = CompactTypeText(Tok.Field(TEXT("Type"), FString(), TypeBegin, i).Text);
		OutName = Tok.T[i].Text;
		return true;
	}

	/** Adds a variable; one already there with another type can't be told apart, so its type becomes unknown. */
	static void DeclareVariable(FVarScope& Scope, const FString& Name, const FString& Type)
	{
		if (const FString* Existing = Scope.Find(Name))
		{
			if (!Existing->Equals(Type, ESearchCase::CaseSensitive))
			{
				Scope.Add(Name, FString());
			}
			return;
		}
		Scope.Add(Name, Type);
	}

	/**
	 * The type of a variable reference like `_x`, `count` or `_grid[1][_i]` (each index takes off one []), or empty
	 * if Text isn't one or its type isn't known. A vector part at the end (`_pos.x`, `_path[2].y`) is a float.
	 */
	static FString ResolveVariableType(const FString& Text, const FVarScope& Scope)
	{
		const FTokens Tok(Text);
		if (!Tok.bOk || !Tok.Is(0, EBangoTokenKind::Identifier))
		{
			return FString();
		}
		const FString* Found = Scope.Find(Tok.T[0].Text);
		if (!Found || Found->IsEmpty())
		{
			return FString();
		}
		FString Type = *Found;
		int32 i = 1;
		while (i < Tok.Num())
		{
			if (Tok.Is(i, EBangoTokenKind::Dot) && Tok.Is(i + 1, EBangoTokenKind::Identifier) && i + 2 == Tok.Num()
				&& (Type.Equals(TEXT("vector2"), ESearchCase::CaseSensitive) || Type.Equals(TEXT("vector3"), ESearchCase::CaseSensitive)))
			{
				const FString& Part = Tok.T[i + 1].Text;
				const bool bValid = Part.Equals(TEXT("x"), ESearchCase::CaseSensitive) || Part.Equals(TEXT("y"), ESearchCase::CaseSensitive)
					|| (Part.Equals(TEXT("z"), ESearchCase::CaseSensitive) && Type.Equals(TEXT("vector3"), ESearchCase::CaseSensitive));
				return bValid ? FString(TEXT("float")) : FString();
			}
			const int32 Close = Tok.Is(i, EBangoTokenKind::LBracket) ? Tok.Matching(i) : INDEX_NONE;
			if (Close == INDEX_NONE || !Type.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
			{
				return FString();
			}
			Type.LeftChopInline(2);
			i = Close + 1;
		}
		return Type;
	}

	/** The element type of an array type ("int[][]" -> "int[]"), or empty. */
	static FString ElementTypeOf(const FString& ArrayType)
	{
		return ArrayType.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive) ? ArrayType.Left(ArrayType.Len() - 2) : FString();
	}

	/** Walks one graph's chains, recording what each node can see (FBangoNodeLayoutContext::VisibleVariables). */
	struct FScopeWalker
	{
		const FBangoGraph& Graph;
		TMap<int64, FVarScope>& Out;
		TArray<bool> Visited;

		FScopeWalker(const FBangoGraph& InGraph, TMap<int64, FVarScope>& InOut) : Graph(InGraph), Out(InOut)
		{
			Visited.Init(false, Graph.Nodes.Num());
		}

		/** Scope is a copy: what a chain declares isn't visible outside it. */
		void Chain(int32 Index, FVarScope Scope)
		{
			while (Graph.Nodes.IsValidIndex(Index) && !Visited[Index])
			{
				Visited[Index] = true;
				const FBangoGraphNode& Node = Graph.Nodes[Index];
				if (Node.Kind != EBangoGraphNodeKind::Entry)
				{
					Out.Add(Node.NodeID, Scope);
				}
				switch (Node.Kind)
				{
				case EBangoGraphNodeKind::If:
					Chain(Node.Branches[0], Scope);
					Chain(Node.Branches[1], Scope);
					break;
				case EBangoGraphNodeKind::While:
					Chain(Node.Branches[0], Scope);
					break;
				case EBangoGraphNodeKind::Sequence:
				case EBangoGraphNodeKind::Switch:
					for (const int32 Output : Node.Branches)
					{
						Chain(Output, Scope);
					}
					break;
				case EBangoGraphNodeKind::For:
				{
					FVarScope Body = Scope;
					const FTokens Tok(Node.Text);
					const TArray<TPair<int32, int32>> Parts = Tok.bOk ? Tok.Split(0, Tok.Num(), EBangoTokenKind::Semicolon, false) : TArray<TPair<int32, int32>>();
					if (Parts.Num() == 3 && Parts[0].Value > Parts[0].Key)
					{
						bool bGlobal = false;
						FString Type;
						FString Name;
						if (ReadDeclaration(Tok.Field(TEXT("Init"), FString(), Parts[0].Key, Parts[0].Value).Text, bGlobal, Type, Name) && !bGlobal)
						{
							DeclareVariable(Body, Name, Type);
						}
					}
					Chain(Node.Branches[0], Body);
					break;
				}
				case EBangoGraphNodeKind::ForEach:
				{
					FVarScope Body = Scope;
					const FTokens Tok(Node.Text);
					if (Tok.bOk && Tok.Is(0, EBangoTokenKind::Identifier) && Tok.Is(1, EBangoTokenKind::KwIn))
					{
						const FString ArrayType = ResolveVariableType(Tok.Field(TEXT("Array"), FString(), 2, Tok.Num()).Text, Scope);
						DeclareVariable(Body, Tok.T[0].Text, ElementTypeOf(ArrayType));
					}
					Chain(Node.Branches[0], Body);
					break;
				}
				default:
					break;
				}
				Index = Node.Next;
			}
		}

		/** The entry's chain, then disconnected chains (heads first, so they see only Base). */
		void Run(const FVarScope& Base)
		{
			Chain(0, Base);
			TArray<bool> HasInput;
			HasInput.Init(false, Graph.Nodes.Num());
			for (const FBangoGraphNode& Node : Graph.Nodes)
			{
				TArray<int32> Outputs = Node.Branches;
				Outputs.Add(Node.Next);
				for (const int32 Output : Outputs)
				{
					if (HasInput.IsValidIndex(Output))
					{
						HasInput[Output] = true;
					}
				}
			}
			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				if (!Visited[i] && !HasInput[i])
				{
					Chain(i, Base);
				}
			}
			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				Chain(i, Base); // anything left (only on broken graphs)
			}
		}
	};

	/** The parameters a graph's entry declares: the script's params, or the function's. */
	static void AddEntryParams(const FBangoGraph& Graph, FVarScope& Scope)
	{
		if (Graph.Nodes.Num() == 0)
		{
			return;
		}
		FBangoScript Script;
		FBangoCompileError Error;
		if (Graph.bIsFunction)
		{
			if (Bango::ParseScript(FString(TEXT("function ")) + Graph.Nodes[0].Text + TEXT("\n{\n};"), TEXT("node"), Script, Error) && Script.Functions.Num() == 1)
			{
				for (const FBangoParam& Param : Script.Functions[0]->Params)
				{
					DeclareVariable(Scope, Param.Name, Param.Type.ToString());
				}
			}
		}
		else if (!Graph.Nodes[0].Text.IsEmpty()
			&& Bango::ParseScript(FString(TEXT("#define params (")) + Graph.Nodes[0].Text + TEXT(")"), TEXT("node"), Script, Error))
		{
			if (const FBangoParamsStmt* Params = Script.GetParams())
			{
				for (const FBangoScriptParamDecl& Param : Params->Params)
				{
					DeclareVariable(Scope, Param.Name, Param.Type.ToString());
				}
			}
		}
	}

	static void AddVisibleVariables(const FBangoGraphDocument& Document, TMap<int64, FVarScope>& Out)
	{
		// The file's globals are visible everywhere in it, and a graph's parameters and locals everywhere in it.
		FVarScope Globals;
		for (const FBangoGraphDecl& Global : Document.Globals)
		{
			DeclareVariable(Globals, Global.Name, Global.Type);
		}
		TArray<const FBangoGraph*> Graphs;
		Graphs.Add(&Document.Main);
		for (const FBangoGraph& Function : Document.Functions)
		{
			Graphs.Add(&Function);
		}
		for (const FBangoGraph* Graph : Graphs)
		{
			FVarScope Base = Globals;
			AddEntryParams(*Graph, Base);
			for (const FBangoGraphDecl& Local : Graph->Locals)
			{
				DeclareVariable(Base, Local.Name, Local.Type);
			}
			FScopeWalker(*Graph, Out).Run(Base);
		}
	}

	/** Operators that bind no tighter than `<=`: one of these at the top of a For's limit means it isn't simple. */
	static bool IsComparisonOrLogic(EBangoTokenKind Kind)
	{
		return Kind == EBangoTokenKind::Less || Kind == EBangoTokenKind::LessEqual || Kind == EBangoTokenKind::Greater
			|| Kind == EBangoTokenKind::GreaterEqual || Kind == EBangoTokenKind::EqualEqual || Kind == EBangoTokenKind::BangEqual
			|| Kind == EBangoTokenKind::AndAnd || Kind == EBangoTokenKind::OrOr;
	}

	/**
	 * A For header that's a simple counting loop, shown like Blueprint's For Loop: `int _i = A; _i <= B; _i++`
	 * (or `_i = A` for a variable declared before, `++_i`, and `>=` with `--` to count down). The loop includes
	 * B, as Blueprint's Last Index does. Fills the Var, First and Last fields; false for anything else.
	 */
	static bool SimpleForFields(const FTokens& Tok, const TArray<TPair<int32, int32>>& Parts, const FBangoNodeLayoutContext& Context, int64 NodeID,
		FBangoNodeField& OutVar, FBangoNodeField& OutFirst, FBangoNodeField& OutLast)
	{
		// Init: [int] V = A
		int32 V = Parts[0].Key;
		const bool bDeclares = Tok.Is(V, EBangoTokenKind::KwInt);
		if (bDeclares)
		{
			++V;
		}
		// First and Last may be empty (a new node not filled in yet).
		if (!Tok.Is(V, EBangoTokenKind::Identifier) || !Tok.Is(V + 1, EBangoTokenKind::Assign) || V + 2 > Parts[0].Value)
		{
			return false;
		}
		const FString& Name = Tok.T[V].Text;

		// Condition: V <= B or V >= B, with nothing in B that would bind looser than the comparison.
		const int32 C = Parts[1].Key;
		if (!Tok.Is(C, EBangoTokenKind::Identifier) || !Tok.T[C].Text.Equals(Name, ESearchCase::CaseSensitive)
			|| C + 2 > Parts[1].Value || Tok.FindTopLevel(C + 2, Parts[1].Value, IsComparisonOrLogic) != INDEX_NONE)
		{
			return false;
		}
		const bool bUp = Tok.Is(C + 1, EBangoTokenKind::LessEqual);
		if (!bUp && !Tok.Is(C + 1, EBangoTokenKind::GreaterEqual))
		{
			return false;
		}

		// Step: V++ or ++V counting up, V-- or --V counting down.
		const int32 S = Parts[2].Key;
		if (Parts[2].Value - S != 2)
		{
			return false;
		}
		const EBangoTokenKind StepOp = bUp ? EBangoTokenKind::PlusPlus : EBangoTokenKind::MinusMinus;
		const int32 StepName = Tok.Is(S, StepOp) ? S + 1 : S;
		if (!Tok.Is(StepName, EBangoTokenKind::Identifier) || !Tok.T[StepName].Text.Equals(Name, ESearchCase::CaseSensitive)
			|| !Tok.Is(StepName == S ? S + 1 : S, StepOp))
		{
			return false;
		}

		OutVar = Tok.Field(TEXT("Var"), FString(), V, V + 1);
		OutVar.bVariable = true;
		if (bDeclares)
		{
			OutVar.VariableType = TEXT("int");
		}
		else if (const TMap<FString, FString>* Visible = Context.VisibleVariables.Find(NodeID))
		{
			OutVar.VariableType = ResolveVariableType(Name, *Visible);
		}
		OutVar.MirrorBegins.Add(Tok.StartOf(C));
		OutVar.MirrorBegins.Add(Tok.StartOf(StepName));
		OutFirst = Tok.Field(TEXT("First"), TEXT("int"), V + 2, Parts[0].Value);
		OutLast = Tok.Field(TEXT("Last"), TEXT("int"), C + 2, Parts[1].Value);
		return true;
	}

	static void GenericLayout(const FBangoGraphNode& Node, bool bInFunctionGraph, const FTokens& Tok, const FBangoNodeLayoutContext& Context, bool bShowExpression, FBangoNodeLayout& Out)
	{
		Out.Style = EBangoNodeStyle::Generic;
		Out.Title = Bango::GetGraphNodeKindName(Node.Kind);
		const int32 N = Tok.Num();

		switch (Node.Kind)
		{
		case EBangoGraphNodeKind::Assign:
		{
			const int32 Op = Tok.FindTopLevel(0, N, IsAssignOp);
			if (Op < 0)
			{
				break;
			}
			switch (Tok.T[Op].Kind)
			{
			case EBangoTokenKind::PlusAssign:  Out.Title = TEXT("Add To"); break;
			case EBangoTokenKind::MinusAssign: Out.Title = TEXT("Subtract From"); break;
			case EBangoTokenKind::StarAssign:  Out.Title = TEXT("Multiply"); break;
			case EBangoTokenKind::SlashAssign: Out.Title = TEXT("Divide"); break;
			default:                           Out.Title = TEXT("Set"); break;
			}
			// Set reads as "Var To 5"; Multiply and Divide as "Var By 3"; Add To and Subtract From as "Var" and "Value".
			const bool bScales = Tok.T[Op].Kind == EBangoTokenKind::StarAssign || Tok.T[Op].Kind == EBangoTokenKind::SlashAssign;
			const bool bSet = Tok.T[Op].Kind == EBangoTokenKind::Assign;
			FBangoNodeField Var = Tok.Field(TEXT("Var"), FString(), 0, Op);
			Var.bVariable = true;
			// The variable's type, if this node can see its declaration; the value then has the same type.
			const TMap<FString, FString>* Visible = Context.VisibleVariables.Find(Node.NodeID);
			Var.VariableType = Visible ? ResolveVariableType(Var.Text, *Visible) : FString();
			Var.InsertSuffix = TEXT(" "); // `= 5` (a new node) becomes `_x = 5`
			Out.Fields.Add(Var);
			FBangoNodeField Value = Tok.Field(bScales ? TEXT("By") : (bSet ? TEXT("To") : TEXT("Value")), Var.VariableType, Op + 1, N);
			Value.InsertPrefix = TEXT(" "); // `_x =` becomes `_x = 5`
			Out.Fields.Add(Value);
			return;
		}

		case EBangoGraphNodeKind::IncDec:
		{
			if (N < 1)
			{
				break;
			}
			// `++` alone is a new node whose variable isn't filled in yet: it's filled in before the `++`.
			const bool bPrefix = N > 1 && (Tok.Is(0, EBangoTokenKind::PlusPlus) || Tok.Is(0, EBangoTokenKind::MinusMinus));
			const EBangoTokenKind OpKind = bPrefix ? Tok.T[0].Kind : Tok.T[N - 1].Kind;
			Out.Title = OpKind == EBangoTokenKind::PlusPlus ? TEXT("Increment") : TEXT("Decrement");
			FBangoNodeField Var = bPrefix ? Tok.Field(TEXT("Var"), TEXT("int"), 1, N) : Tok.Field(TEXT("Var"), TEXT("int"), 0, N - 1);
			Var.bVariable = true;
			Var.VariableType = TEXT("int");
			Out.Fields.Add(Var);
			return;
		}

		case EBangoGraphNodeKind::If:
		case EBangoGraphNodeKind::While:
		{
			FBangoNodeField Condition = WholeText(TEXT("Condition"), TEXT("bool"), Node.Text, false);
			Condition.bHideLabel = true; // the node's one row needs no name
			Condition.bHideIcon = true;  // nor an icon, like the For node's Expression row
			Out.Fields.Add(Condition);
			return;
		}

		case EBangoGraphNodeKind::Switch:
		{
			// The value's type when it's a variable this node can see (its cases' values are rows of their own).
			const TMap<FString, FString>* Visible = Context.VisibleVariables.Find(Node.NodeID);
			const FString Type = Visible ? ResolveVariableType(Node.Text, *Visible) : FString();
			Out.Fields.Add(WholeText(TEXT("On"), Type, Node.Text, false));
			return;
		}

		case EBangoGraphNodeKind::For:
		{
			const TArray<TPair<int32, int32>> Parts = Tok.Split(0, N, EBangoTokenKind::Semicolon, /*bEmptyIsNone*/ false);
			if (Parts.Num() != 3)
			{
				break;
			}
			// A simple counting loop shows as Var, First and Last, like Blueprint's For Loop; anything else (or when
			// the editor's Expression toggle asks) as the whole header in one row.
			FBangoNodeField Var;
			FBangoNodeField First;
			FBangoNodeField Last;
			Out.bHasSimpleForm = SimpleForFields(Tok, Parts, Context, Node.NodeID, Var, First, Last);
			Out.bShowsExpression = !Out.bHasSimpleForm || bShowExpression;
			if (Out.bShowsExpression)
			{
				FBangoNodeField Expression = WholeText(TEXT("Expression"), FString(), Node.Text, false);
				Expression.bHideLabel = true; // the whole header needs no name
				Out.Fields.Add(Expression);
			}
			else
			{
				Out.Fields.Add(Var);
				Out.Fields.Add(First);
				Out.Fields.Add(Last);
			}
			return;
		}

		case EBangoGraphNodeKind::ForEach:
			if (Tok.Is(0, EBangoTokenKind::Identifier) && Tok.Is(1, EBangoTokenKind::KwIn))
			{
				FBangoNodeField Variable = Tok.Field(TEXT("Variable"), FString(), 0, 1);
				Variable.bVariable = true;
				const TMap<FString, FString>* Visible = Context.VisibleVariables.Find(Node.NodeID);
				Variable.VariableType = Visible ? ElementTypeOf(ResolveVariableType(Tok.Field(TEXT("Array"), FString(), 2, N).Text, *Visible)) : FString();
				Out.Fields.Add(Variable);
				FBangoNodeField Array = Tok.Field(TEXT("Array"), TEXT("array"), 2, N);
				Array.InsertPrefix = TEXT(" "); // `_item in` (a new node) becomes `_item in _list`
				Out.Fields.Add(Array);
				return;
			}
			break;

		case EBangoGraphNodeKind::Return:
		{
			// Where the node is says whether it returns a value: the main graph's Return ends the script, a void
			// function's ends the function, and a typed function's must give a value of the function's type.
			const FString* ReturnType = bInFunctionGraph ? Context.ReturnTypes.Find(Node.NodeID) : nullptr;
			const bool bTakesValue = bInFunctionGraph && (!ReturnType || !ReturnType->IsEmpty());
			if (!bInFunctionGraph)
			{
				Out.Description = TEXT("Ends the script.");
			}
			else if (ReturnType)
			{
				Out.Description = ReturnType->IsEmpty() ? FString(TEXT("Ends the function."))
					: FString::Printf(TEXT("Ends the function, giving back its result (%s)."), **ReturnType);
			}
			if (bTakesValue)
			{
				// Required when the type is known; optional while the function's signature doesn't read.
				Out.Fields.Add(WholeText(TEXT("Value"), ReturnType ? *ReturnType : FString(), Node.Text, ReturnType == nullptr));
			}
			else if (!Node.Text.IsEmpty())
			{
				// A value where none belongs: shown so it can be cleared (compiling says why it's wrong).
				Out.Fields.Add(WholeText(TEXT("Value"), FString(), Node.Text, true));
			}
			return;
		}

		case EBangoGraphNodeKind::Label:
		case EBangoGraphNodeKind::Goto:
		{
			// The node's one row needs no name: the title already says whether it's the label or a jump to it.
			FBangoNodeField Name = WholeText(Node.Kind == EBangoGraphNodeKind::Label ? TEXT("Name") : TEXT("Label"), FString(), Node.Text, false);
			Name.bHideLabel = true;
			Out.Fields.Add(Name);
			return;
		}

		case EBangoGraphNodeKind::Break:
		case EBangoGraphNodeKind::Continue:
		case EBangoGraphNodeKind::Sequence: // no text: its outputs are its blocks
		case EBangoGraphNodeKind::Reroute:
			return;

		default:
			break;
		}

		// Anything that didn't fit its usual shape: show the whole text.
		Out.Fields.Reset();
		Out.Fields.Add(WholeText(TEXT("Text"), FString(), Node.Text, false));
	}
}

namespace Bango
{
	void GetGraphVariables(const FBangoGraphDocument& Document, int32 GraphIndex, const FBangoNodeLayoutContext& Context, TArray<FBangoGraphVariable>& OutVariables)
	{
		using namespace BangoGraphLayoutPrivate;
		OutVariables.Reset();
		const FBangoGraph* Graph = GraphIndex == INDEX_NONE ? &Document.Main : (Document.Functions.IsValidIndex(GraphIndex) ? &Document.Functions[GraphIndex] : nullptr);
		if (!Graph || Graph->Nodes.Num() == 0)
		{
			return;
		}

		TArray<FBangoGraphVariable> Params;
		TArray<FBangoGraphVariable> Locals;
		TArray<FBangoGraphVariable> Globals;
		TSet<FString> Seen;
		TSet<FString> SeenLoops;
		auto Add = [&Seen, &SeenLoops](TArray<FBangoGraphVariable>& To, const FString& Name, const FString& Type, FBangoGraphVariable::EKind Kind, int32 DeclGraph, int32 DeclIndex)
		{
			// A name is listed once, except that every loop gets its own row: two For loops can each have an `i`.
			const bool bLoop = Kind == FBangoGraphVariable::EKind::Loop;
			if (Name.IsEmpty() || (Seen.Contains(Name) && !(bLoop && SeenLoops.Contains(Name))))
			{
				return;
			}
			Seen.Add(Name);
			if (bLoop)
			{
				SeenLoops.Add(Name);
			}
			FBangoGraphVariable& Variable = To.AddDefaulted_GetRef();
			Variable.Name = Name;
			Variable.Type = Type;
			Variable.Kind = Kind;
			Variable.DeclGraph = DeclGraph;
			Variable.Index = DeclIndex;
		};

		// Parameters, on the entry node, in order.
		TArray<FBangoEntryParam> EntryParams;
		Bango::ReadEntryParams(Graph->Nodes[0].Text, Graph->bIsFunction, EntryParams);
		for (int32 i = 0; i < EntryParams.Num(); ++i)
		{
			Add(Params, EntryParams[i].Name, EntryParams[i].Type, FBangoGraphVariable::EKind::Param, GraphIndex, i);
		}

		// The locals declared at the top of the graph, in order.
		for (int32 i = 0; i < Graph->Locals.Num(); ++i)
		{
			Add(Locals, Graph->Locals[i].Name, Graph->Locals[i].Type, FBangoGraphVariable::EKind::Local, GraphIndex, i);
		}

		// Loop variables, in node order: one for each loop, even when two loops use the same name.
		TArray<FBangoGraphVariable> Loops;
		for (int32 i = 1; i < Graph->Nodes.Num(); ++i)
		{
			const FBangoGraphNode& Node = Graph->Nodes[i];
			bool bGlobal = false;
			FString Type;
			FString Name;
			if (Node.Kind == EBangoGraphNodeKind::For && ReadDeclaration(Node.Text, bGlobal, Type, Name) && !bGlobal)
			{
				Add(Loops, Name, Type, FBangoGraphVariable::EKind::Loop, GraphIndex, i);
			}
			else if (Node.Kind == EBangoGraphNodeKind::ForEach)
			{
				const FTokens Tok(Node.Text);
				if (Tok.bOk && Tok.Is(0, EBangoTokenKind::Identifier) && Tok.Is(1, EBangoTokenKind::KwIn))
				{
					const TMap<FString, FString>* Visible = Context.VisibleVariables.Find(Node.NodeID);
					const FString ArrayText = Tok.Field(TEXT("Array"), FString(), 2, Tok.Num()).Text;
					Add(Loops, Tok.T[0].Text, Visible ? ElementTypeOf(ResolveVariableType(ArrayText, *Visible)) : FString(),
						FBangoGraphVariable::EKind::Loop, GraphIndex, i);
				}
			}
		}

		// The file's globals, in order, then the ones this graph uses but the file doesn't declare: names that
		// aren't the graph's parameters, locals or loop variables (already seen), calls (followed by `(`), vector parts
		// (after `.`), or labels (Label and Go To nodes hold only a label's name).
		for (int32 i = 0; i < Document.Globals.Num(); ++i)
		{
			Add(Globals, Document.Globals[i].Name, Document.Globals[i].Type, FBangoGraphVariable::EKind::Global, INDEX_NONE, i);
		}
		TArray<FBangoGraphVariable> Undeclared;
		for (int32 i = 1; i < Graph->Nodes.Num(); ++i)
		{
			const FBangoGraphNode& Node = Graph->Nodes[i];
			if (Node.Kind == EBangoGraphNodeKind::Label || Node.Kind == EBangoGraphNodeKind::Goto || Node.Kind == EBangoGraphNodeKind::Reroute)
			{
				continue;
			}
			const FTokens Tok(Node.Text);
			for (int32 t = 0; Tok.bOk && t < Tok.Num(); ++t)
			{
				const FString& Name = Tok.T[t].Text;
				if (!Tok.Is(t, EBangoTokenKind::Identifier) || Tok.Is(t + 1, EBangoTokenKind::LParen) || Tok.Is(t - 1, EBangoTokenKind::Dot))
				{
					continue;
				}
				Add(Undeclared, Name, FString(), FBangoGraphVariable::EKind::Global, INDEX_NONE, INDEX_NONE);
			}
		}
		auto ByName = [](const FBangoGraphVariable& A, const FBangoGraphVariable& B) { return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0; };
		Undeclared.StableSort(ByName);

		OutVariables.Append(Params);
		OutVariables.Append(Locals);
		OutVariables.Append(Loops);
		OutVariables.Append(Globals);
		OutVariables.Append(Undeclared);
	}

	FBangoNodeLayoutContext MakeNodeLayoutContext(const FBangoGraphDocument& Document, const IBangoCommandLookup* Commands)
	{
		FBangoNodeLayoutContext Context;
		Context.Commands = Commands;
		for (const FBangoGraph& Function : Document.Functions)
		{
			if (Function.Nodes.Num() == 0)
			{
				continue;
			}
			FBangoScript Script;
			FBangoCompileError Error;
			const FString Source = FString(TEXT("function ")) + Function.Nodes[0].Text + TEXT("\n{\n};");
			if (!ParseScript(Source, TEXT("node"), Script, Error) || Script.Functions.Num() != 1)
			{
				continue;
			}
			const FBangoFunction& Parsed = *Script.Functions[0];
			FBangoCommandSignature Signature = FBangoCommandSignature::Make(Parsed.Name);
			for (const FBangoParam& Param : Parsed.Params)
			{
				Signature.Param(Param.Name, Param.Type);
			}
			if (!Parsed.bReturnsVoid)
			{
				Signature.Returns(Parsed.ReturnType);
			}
			const FString ReturnType = Parsed.bReturnsVoid ? FString() : Parsed.ReturnType.ToString();
			for (const FBangoGraphNode& Node : Function.Nodes)
			{
				if (Node.Kind == EBangoGraphNodeKind::Return)
				{
					Context.ReturnTypes.Add(Node.NodeID, ReturnType);
				}
			}
			Signature.InCategory(TEXT("This Script"));
			Context.Functions.Add(Signature);
		}
		BangoGraphLayoutPrivate::AddVisibleVariables(Document, Context.VisibleVariables);
		return Context;
	}

	bool LayoutContextsMatch(const FBangoNodeLayoutContext& A, const FBangoNodeLayoutContext& B)
	{
		if (A.Commands != B.Commands || A.Functions.Num() != B.Functions.Num() || A.VisibleVariables.Num() != B.VisibleVariables.Num()
			|| A.ReturnTypes.Num() != B.ReturnTypes.Num())
		{
			return false;
		}
		for (const TPair<int64, FString>& Entry : A.ReturnTypes)
		{
			const FString* Other = B.ReturnTypes.Find(Entry.Key);
			if (!Other || !Other->Equals(Entry.Value, ESearchCase::CaseSensitive))
			{
				return false;
			}
		}
		for (int32 i = 0; i < A.Functions.Num(); ++i)
		{
			if (!A.Functions[i].ToString().Equals(B.Functions[i].ToString(), ESearchCase::CaseSensitive))
			{
				return false;
			}
		}
		for (const TPair<int64, TMap<FString, FString>>& Entry : A.VisibleVariables)
		{
			const TMap<FString, FString>* Other = B.VisibleVariables.Find(Entry.Key);
			if (!Other || Other->Num() != Entry.Value.Num())
			{
				return false;
			}
			for (const TPair<FString, FString>& Variable : Entry.Value)
			{
				const FString* OtherType = Other->Find(Variable.Key);
				if (!OtherType || !OtherType->Equals(Variable.Value, ESearchCase::CaseSensitive))
				{
					return false;
				}
			}
		}
		return true;
	}

	void AddLocNamespaceField(FBangoNodeLayout& Layout, bool bHasLocNamespace, const FString& LocNamespace)
	{
		FBangoNodeField Field;
		Field.Label = TEXT("Namespace");
		Field.SectionHeader = TEXT("Localization");
		Field.TypeHint = TEXT("string");
		Field.Description = TEXT("The namespace of this script's LOCTEXT texts, for translators' tools. Saved as #define LOCTEXT_NAMESPACE \"...\" after the parameters, with #undef LOCTEXT_NAMESPACE at the end of the file.");
		Field.bPresent = bHasLocNamespace;
		Field.Text = bHasLocNamespace ? MakeStringLiteral(LocNamespace) : FString();
		Field.bOptional = true;
		Field.bLocNamespace = true;
		Layout.Fields.Add(Field);
	}

	bool ParseTextLiteral(const FString& Text, FBangoTextLiteral& OutLiteral)
	{
		using namespace BangoGraphLayoutPrivate;
		const FTokens Tok(Text);
		if (!Tok.bOk || Tok.Num() < 4 || !Tok.Is(0, EBangoTokenKind::Identifier) || !Tok.Is(1, EBangoTokenKind::LParen) || !Tok.Is(Tok.Num() - 1, EBangoTokenKind::RParen))
		{
			return false;
		}
		const FString& Name = Tok.T[0].Text;
		FBangoTextLiteral Literal;
		if (Name.Equals(TEXT("INVTEXT"), ESearchCase::CaseSensitive))
		{
			// INVTEXT("Source"): never translated.
			if (Tok.Num() != 4 || !Tok.Is(2, EBangoTokenKind::StringLiteral))
			{
				return false;
			}
			Literal.bInvariant = true;
			Literal.Source = Tok.T[2].Text;
			OutLiteral = Literal;
			return true;
		}
		Literal.bHasNamespace = Name.StartsWith(TEXT("NS"), ESearchCase::CaseSensitive);
		Literal.bHasNotes = Name.Contains(TEXT("NOTE"), ESearchCase::CaseSensitive);
		const bool bKnown = Name.Equals(TEXT("LOCTEXT"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("NSLOCTEXT"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("NOTELOCTEXT"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("NSNOTELOCTEXT"), ESearchCase::CaseSensitive);
		const int32 NumStrings = 2 + (Literal.bHasNamespace ? 1 : 0) + (Literal.bHasNotes ? 1 : 0);
		if (!bKnown || Tok.Num() != 2 + NumStrings * 2)
		{
			return false;
		}
		TArray<FString> Strings;
		for (int32 i = 0; i < NumStrings; ++i)
		{
			const int32 At = 2 + i * 2;
			if (!Tok.Is(At, EBangoTokenKind::StringLiteral) || !Tok.Is(At + 1, i + 1 < NumStrings ? EBangoTokenKind::Comma : EBangoTokenKind::RParen))
			{
				return false;
			}
			Strings.Add(Tok.T[At].Text);
		}
		int32 Next = 0;
		if (Literal.bHasNamespace)
		{
			Literal.Namespace = Strings[Next++];
		}
		Literal.Key = Strings[Next++];
		Literal.Source = Strings[Next++];
		if (Literal.bHasNotes)
		{
			Literal.Notes = Strings[Next++];
		}
		OutLiteral = Literal;
		return true;
	}

	FString MakeTextLiteral(const FBangoTextLiteral& Literal)
	{
		if (Literal.bInvariant)
		{
			return TEXT("INVTEXT(") + MakeStringLiteral(Literal.Source) + TEXT(")");
		}
		const bool bNotes = !Literal.Notes.IsEmpty();
		FString Result = Literal.bHasNamespace ? (bNotes ? TEXT("NSNOTELOCTEXT(") : TEXT("NSLOCTEXT(")) : (bNotes ? TEXT("NOTELOCTEXT(") : TEXT("LOCTEXT("));
		if (Literal.bHasNamespace)
		{
			Result += MakeStringLiteral(Literal.Namespace) + TEXT(", ");
		}
		Result += MakeStringLiteral(Literal.Key) + TEXT(", ") + MakeStringLiteral(Literal.Source);
		if (bNotes)
		{
			Result += TEXT(", ") + MakeStringLiteral(Literal.Notes);
		}
		return Result + TEXT(")");
	}

	FBangoNodeLayout GetNodeLayout(const FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context, bool bShowExpression)
	{
		using namespace BangoGraphLayoutPrivate;

		FBangoNodeLayout Layout;
		const FTokens Tok(Node.Text);
		if (!Tok.bOk)
		{
			Layout.Title = Node.Kind == EBangoGraphNodeKind::Entry ? FString(bInFunctionGraph ? TEXT("Function") : TEXT("Script Start")) : GetGraphNodeKindName(Node.Kind);
			Layout.Fields.Add(WholeText(TEXT("Text"), FString(), Node.Text, false));
			return Layout;
		}

		if (Node.Kind == EBangoGraphNodeKind::Entry)
		{
			if (bInFunctionGraph)
			{
				FunctionEntryLayout(Node, Tok, Layout);
			}
			else
			{
				ScriptEntryLayout(Node, Tok, Layout);
			}
			return Layout;
		}

		if (Node.Kind == EBangoGraphNodeKind::Call)
		{
			if (TryCallLayout(Node, Tok, Context, Layout))
			{
				return Layout;
			}
			Layout = FBangoNodeLayout();
		}
		GenericLayout(Node, bInFunctionGraph, Tok, Context, bShowExpression, Layout);
		return Layout;
	}
}
