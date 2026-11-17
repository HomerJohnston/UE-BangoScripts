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
	Locals.Reset();

	int32 Graph = 0;            // the graph being read
	int32 Depth = 0;            // { } depth
	bool bInFunctionHeader = false;

	// The open blocks: the locals declared in each (they end at its `}`), and the spawn ids that start at its `}`.
	struct FBlock
	{
		TArray<int32> Locals;
		TArray<int32> StartAtClose;
	};
	TArray<FBlock> Blocks;
	bool bInLoopHeader = false;     // after `for` or `forEach`, until its block opens
	TArray<int32> PendingLoopVars;  // declared in a loop's header: they belong to its block
	TArray<int32> PendingSpawnIds;  // `int _t = spawn`: they start when the spawn's block closes
	auto AddLocal = [this](const FString& Name, const FString& Type, int32 InGraph, int32 StartLine)
	{
		FLocalName& Local = Locals.AddDefaulted_GetRef();
		Local.Name = Name;
		Local.Type = Type;
		Local.Graph = InGraph;
		Local.DeclLine = StartLine;
		Local.StartLine = StartLine;
		return Locals.Num() - 1;
	};

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
			&& TokenIs(Line, Code[0], EBangoSyntaxKind::Preprocessor, TEXT("#set"))
			&& TokenIs(Line, Code[1], EBangoSyntaxKind::Preprocessor, TEXT("inputs"));
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
				FBlock& Block = Blocks.AddDefaulted_GetRef();
				Block.Locals = MoveTemp(PendingLoopVars);
				Block.StartAtClose = MoveTemp(PendingSpawnIds);
				PendingLoopVars.Reset();
				PendingSpawnIds.Reset();
				bInLoopHeader = false;
				continue;
			}

			if (TokenIs(Line, Token, EBangoSyntaxKind::Structure, TEXT("}")))
			{
				Depth = FMath::Max(Depth - 1, 0);
				if (Blocks.Num() > 0)
				{
					FBlock Closed = Blocks.Pop();
					for (const int32 Local : Closed.Locals)
					{
						Locals[Local].EndLine = LineIndex;
					}
					for (const int32 Local : Closed.StartAtClose)
					{
						Locals[Local].StartLine = LineIndex;
						if (Blocks.Num() > 0)
						{
							Blocks.Last().Locals.Add(Local); // it's the enclosing block's
						}
					}
				}
				if (Depth == 0 && Graph != 0)
				{
					Graph = 0; // the function's body has ended; this line was its last
				}
				continue;
			}

			if (TokenIs(Line, Token, EBangoSyntaxKind::Keyword, TEXT("for")))
			{
				bInLoopHeader = true;
				continue;
			}

			// <type> name: a declaration (a global, a parameter, a local, a script variable, or a for loop's variable).
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
					const int32 Local = AddLocal(Name, Type, Graph, LineIndex);
					bool bSpawn = false;
					for (int32 J = I + 2; J < Code.Num(); ++J)
					{
						bSpawn |= TokenIs(Line, Code[J], EBangoSyntaxKind::Keyword, TEXT("spawn"));
					}
					if (bInLoopHeader)
					{
						PendingLoopVars.Add(Local);
					}
					else if (bSpawn)
					{
						Locals[Local].StartLine = MAX_int32; // not visible until the spawn's block closes
						PendingSpawnIds.Add(Local);
					}
					else if (Blocks.Num() > 0)
					{
						Blocks.Last().Locals.Add(Local);
					}
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
				PendingLoopVars.Add(AddLocal(TokenText(Line, Code[I + 2]), FString(), Graph, LineIndex));
				bInLoopHeader = true;
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
		if (FindVisibleLocal(LineIndex, Name))
		{
			return EBangoSyntaxKind::Local;
		}
	}
	return Contains(Globals, Name) ? EBangoSyntaxKind::Global : EBangoSyntaxKind::UndeclaredGlobal;
}

FString FBangoScriptNames::GetVariableKey(int32 LineIndex, const FString& Name) const
{
	const int32 Graph = GetLineGraph(LineIndex);
	if (Graphs.IsValidIndex(Graph) && Contains(Graphs[Graph].Params, Name))
	{
		return FString::Printf(TEXT("param %d %s"), Graph, *Name);
	}
	// Declared on this line (its declaration is one of its uses, even where it isn't visible yet), or visible here.
	for (int32 I = Locals.Num() - 1; I >= 0; --I)
	{
		if (Locals[I].Graph == Graph && Locals[I].DeclLine == LineIndex && Locals[I].Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("local %d"), I);
		}
	}
	for (int32 I = Locals.Num() - 1; I >= 0; --I) // the innermost visible one, as FindVisibleLocal
	{
		if (Locals[I].IsVisible(Graph, LineIndex) && Locals[I].Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("local %d"), I);
		}
	}
	return FString::Printf(TEXT("global %s"), *Name);
}

const FBangoScriptNames::FLocalName* FBangoScriptNames::FindVisibleLocal(int32 LineIndex, const FString& Name) const
{
	const int32 Graph = GetLineGraph(LineIndex);
	for (int32 I = Locals.Num() - 1; I >= 0; --I)
	{
		if (Locals[I].IsVisible(Graph, LineIndex) && Locals[I].Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return &Locals[I];
		}
	}
	return nullptr;
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
	}
	// The visible locals, in the order they're declared; of two with one name, the inner one (FindVisibleLocal).
	for (int32 I = 0; I < Locals.Num(); ++I)
	{
		if (Locals[I].IsVisible(Graph, LineIndex) && FindVisibleLocal(LineIndex, Locals[I].Name) == &Locals[I])
		{
			Add(Locals[I].Name, Locals[I].Type, EBangoSyntaxKind::Local);
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
