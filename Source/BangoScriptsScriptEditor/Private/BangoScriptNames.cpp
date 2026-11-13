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

void FBangoScriptNames::Build(const TArray<FString>& Lines, const TArray<TArray<FBangoSyntaxToken>>& LineTokens)
{
	using namespace BangoScriptNamesPrivate;

	Graphs.Reset();
	Graphs.SetNum(1);
	LineGraph.Reset();
	Globals.Reset();

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

			// function <type> Name(: the function's name is the header's first call.
			if (bInFunctionHeader && Token.Kind == EBangoSyntaxKind::Call && Graphs[Graph].FunctionName.IsEmpty())
			{
				Graphs[Graph].FunctionName = TokenText(Line, Token);
				continue;
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
				if (bGlobalLine)
				{
					AddUnique(Globals, Name);
				}
				else if (bParamsLine)
				{
					AddUnique(Graphs[0].Params, Name);
				}
				else if (bInFunctionHeader)
				{
					AddUnique(Graphs[Graph].Params, Name);
				}
				else
				{
					AddUnique(Graphs[Graph].Locals, Name);
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
				AddUnique(Graphs[Graph].Locals, TokenText(Line, Code[I + 2]));
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
