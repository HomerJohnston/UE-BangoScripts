// Bango text editor: which names are locals, parameters and globals, from the whole file.
// See "Bango Text Editor Spec", section 2, Meaning layer.

#include "BangoScriptNames.h"

namespace BangoScriptNamesPrivate
{
	static bool IsCode(EBangoSyntaxKind Kind)
	{
		return Kind != EBangoSyntaxKind::Comment && Kind != EBangoSyntaxKind::Metadata && Kind != EBangoSyntaxKind::DisconnectedMarker
			&& Kind != EBangoSyntaxKind::DisabledMarker;
	}

	static bool TokenIs(const FString& Line, const FBangoSyntaxToken& Token, EBangoSyntaxKind Kind, const TCHAR* Text)
	{
		if (Token.Kind != Kind)
		{
			return false;
		}
		int32 I = 0;
		for (; Text[I] != 0; ++I)
		{
			if (I >= Token.Len || Line[Token.Start + I] != Text[I])
			{
				return false;
			}
		}
		return I == Token.Len;
	}

	static FString TokenText(const FString& Line, const FBangoSyntaxToken& Token)
	{
		return Line.Mid(Token.Start, Token.Len);
	}
}

bool FBangoScriptNames::Contains(const TArray<FString>& Names, const FString& Name)
{
	for (const FString& Each : Names)
	{
		if (Each.Equals(Name, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}

void FBangoScriptNames::AddUnique(TArray<FString>& Names, const FString& Name)
{
	if (!Contains(Names, Name))
	{
		Names.Add(Name);
	}
}

void FBangoScriptNames::AddUnique(TArray<FString>& Names, TArray<FString>& Types, const FString& Name, const FString& Type)
{
	if (!Contains(Names, Name))
	{
		Names.Add(Name);
		Types.Add(Type);
	}
}

void FBangoScriptNames::Build(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens)
{
	using namespace BangoScriptNamesPrivate;

	Graphs.Reset();
	Graphs.SetNum(1);
	LineGraph.Reset();
	Globals.Reset();
	GlobalTypes.Reset();

	int32 Graph = 0;            // the graph being read
	int32 Depth = 0;            // { } depth
	bool bInFunctionHeader = false;

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString& Line = Lines[LineIndex];

		// The line's code tokens, without comments, metadata and the //~ of a disconnected line.
		TArray<FBangoSyntaxToken> Code;
		if (LineIndex < LineTokens.Num())
		{
			for (const FBangoSyntaxToken& Token : LineTokens[LineIndex])
			{
				if (IsCode(Token.Kind))
				{
					Code.Add(Token);
				}
			}
		}

		const bool bParamsLine = Code.Num() >= 2
			&& TokenIs(Line, Code[0], EBangoSyntaxKind::Preprocessor, TEXT("#define"))
			&& TokenIs(Line, Code[1], EBangoSyntaxKind::Preprocessor, TEXT("params"));
		const bool bGlobalLine = Code.Num() >= 1 && TokenIs(Line, Code[0], EBangoSyntaxKind::DeclKeyword, TEXT("global"));
		if (bParamsLine && Graphs[0].EntryLine == INDEX_NONE)
		{
			Graphs[0].EntryLine = LineIndex;
		}

		int32 LineBelongsTo = Graph;
		for (int32 I = 0; I < Code.Num(); ++I)
		{
			const FBangoSyntaxToken& Token = Code[I];
			const FBangoSyntaxToken* Next = I + 1 < Code.Num() ? &Code[I + 1] : nullptr;

			if (Depth == 0 && TokenIs(Line, Token, EBangoSyntaxKind::DeclKeyword, TEXT("function")))
			{
				Graph = Graphs.Num();
				Graphs.AddDefaulted();
				Graphs[Graph].EntryLine = LineIndex;
				LineBelongsTo = Graph;
				bInFunctionHeader = true;
				continue;
			}

			// function <type> Name(: the function's name is the header's first call, and the type before it its return type.
			if (bInFunctionHeader && Graphs[Graph].FunctionName.IsEmpty())
			{
				if (Token.Kind == EBangoSyntaxKind::Type && Next != nullptr && Next->Kind == EBangoSyntaxKind::Call)
				{
					Graphs[Graph].ReturnType = TokenText(Line, Token);
					continue;
				}
				if (Token.Kind == EBangoSyntaxKind::Call)
				{
					Graphs[Graph].FunctionName = TokenText(Line, Token);
					continue;
				}
			}

			if (TokenIs(Line, Token, EBangoSyntaxKind::Structure, TEXT("{")))
			{
				bInFunctionHeader = false;
				++Depth;
				continue;
			}

			if (TokenIs(Line, Token, EBangoSyntaxKind::Structure, TEXT("}")))
			{
				Depth = FMath::Max(Depth - 1, 0);
				if (Depth == 0 && Graph != 0)
				{
					Graph = 0; // the function's body has ended; this line was its last
				}
				continue;
			}

			// <type> name: a declaration (a global, a parameter, a local, or a for loop's variable).
			if (Token.Kind == EBangoSyntaxKind::Type && Next != nullptr && Next->Kind == EBangoSyntaxKind::Global)
			{
				const FString Name = TokenText(Line, *Next);
				const FString Type = TokenText(Line, Token);
				if (bGlobalLine)
				{
					AddUnique(Globals, GlobalTypes, Name, Type);
				}
				else if (bParamsLine)
				{
					AddUnique(Graphs[0].Params, Graphs[0].ParamTypes, Name, Type);
				}
				else if (bInFunctionHeader)
				{
					AddUnique(Graphs[Graph].Params, Graphs[Graph].ParamTypes, Name, Type);
				}
				else
				{
					AddUnique(Graphs[Graph].Locals, Graphs[Graph].LocalTypes, Name, Type);
				}
				++I;
				continue;
			}

			// forEach (name in ...): its loop variable.
			if (TokenIs(Line, Token, EBangoSyntaxKind::Keyword, TEXT("forEach"))
				&& I + 2 < Code.Num()
				&& TokenIs(Line, Code[I + 1], EBangoSyntaxKind::Punctuation, TEXT("("))
				&& Code[I + 2].Kind == EBangoSyntaxKind::Global)
			{
				AddUnique(Graphs[Graph].Locals, Graphs[Graph].LocalTypes, TokenText(Line, Code[I + 2]), FString());
				I += 2;
				continue;
			}
		}

		LineGraph.Add(LineBelongsTo);
	}
}

EBangoSyntaxKind FBangoScriptNames::Classify(int32 LineIndex, const FString& Name) const
{
	const int32 Graph = LineIndex >= 0 && LineIndex < LineGraph.Num() ? LineGraph[LineIndex] : 0;
	if (Graphs.IsValidIndex(Graph))
	{
		if (Contains(Graphs[Graph].Params, Name))
		{
			return EBangoSyntaxKind::Parameter;
		}
		if (Contains(Graphs[Graph].Locals, Name))
		{
			return EBangoSyntaxKind::Local;
		}
	}
	return Contains(Globals, Name) ? EBangoSyntaxKind::Global : EBangoSyntaxKind::UndeclaredGlobal;
}

void FBangoScriptNames::RefineTokens(int32 LineIndex, const FString& Line, TArray<FBangoSyntaxToken>& InOutTokens) const
{
	for (FBangoSyntaxToken& Token : InOutTokens)
	{
		if (Token.Kind == EBangoSyntaxKind::Global)
		{
			Token.Kind = Classify(LineIndex, Line.Mid(Token.Start, Token.Len));
		}
	}
}

int32 FBangoScriptNames::GetLineGraph(int32 LineIndex) const
{
	return LineIndex >= 0 && LineIndex < LineGraph.Num() ? LineGraph[LineIndex] : 0;
}

FString FBangoScriptNames::GetGraphFunctionName(int32 Graph) const
{
	return Graphs.IsValidIndex(Graph) ? Graphs[Graph].FunctionName : FString();
}

bool FBangoScriptNames::IsEntryLine(int32 LineIndex) const
{
	const int32 Graph = GetLineGraph(LineIndex);
	return Graphs.IsValidIndex(Graph) && Graphs[Graph].EntryLine == LineIndex && LineIndex != INDEX_NONE;
}

int32 FBangoScriptNames::FindEntryLine(const FString& FunctionName) const
{
	if (FunctionName.IsEmpty())
	{
		return Graphs.Num() > 0 ? Graphs[0].EntryLine : INDEX_NONE;
	}
	for (int32 Graph = 1; Graph < Graphs.Num(); ++Graph)
	{
		if (Graphs[Graph].FunctionName.Equals(FunctionName, ESearchCase::CaseSensitive))
		{
			return Graphs[Graph].EntryLine;
		}
	}
	return INDEX_NONE;
}

void FBangoScriptNames::GetVisibleNames(int32 LineIndex, TArray<FBangoVisibleName>& OutNames) const
{
	TArray<FString> Seen;
	auto Add = [&OutNames, &Seen](const FString& Name, const FString& Type, EBangoSyntaxKind Kind)
	{
		if (!Name.IsEmpty() && !Contains(Seen, Name))
		{
			Seen.Add(Name);
			FBangoVisibleName Visible;
			Visible.Name = Name;
			Visible.Type = Type;
			Visible.Kind = Kind;
			OutNames.Add(Visible);
		}
	};

	const int32 Graph = GetLineGraph(LineIndex);
	if (Graphs.IsValidIndex(Graph))
	{
		const FGraphNames& Names = Graphs[Graph];
		for (int32 I = 0; I < Names.Params.Num(); ++I)
		{
			Add(Names.Params[I], Names.ParamTypes.IsValidIndex(I) ? Names.ParamTypes[I] : FString(), EBangoSyntaxKind::Parameter);
		}
		for (int32 I = 0; I < Names.Locals.Num(); ++I)
		{
			Add(Names.Locals[I], Names.LocalTypes.IsValidIndex(I) ? Names.LocalTypes[I] : FString(), EBangoSyntaxKind::Local);
		}
	}

	// A local of the same name hides a global (Seen skips it).
	for (int32 I = 0; I < Globals.Num(); ++I)
	{
		Add(Globals[I], GlobalTypes.IsValidIndex(I) ? GlobalTypes[I] : FString(), EBangoSyntaxKind::Global);
	}

	// Functions, with their signature as the "type": `int AddScore(int amount, entity who)`.
	for (int32 G = 1; G < Graphs.Num(); ++G)
	{
		const FGraphNames& Function = Graphs[G];
		FString Signature = (Function.ReturnType.IsEmpty() ? FString(TEXT("void")) : Function.ReturnType) + TEXT(" ") + Function.FunctionName + TEXT("(");
		for (int32 I = 0; I < Function.Params.Num(); ++I)
		{
			if (I > 0)
			{
				Signature += TEXT(", ");
			}
			Signature += (Function.ParamTypes.IsValidIndex(I) ? Function.ParamTypes[I] + TEXT(" ") : FString()) + Function.Params[I];
		}
		Signature += TEXT(")");
		Add(Function.FunctionName, Signature, EBangoSyntaxKind::Function);
	}
}
