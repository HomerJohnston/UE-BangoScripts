// Bango text editor: autocomplete. Which word is being typed, what it could be, and the list filtered by it.
// See "Bango Text Editor Spec", section 4, Autocomplete.

#include "BangoCompletion.h"

#include "BangoTypes.h"

namespace BangoCompletionPrivate
{
	static bool IsDigit(TCHAR C)      { return C >= TEXT('0') && C <= TEXT('9'); }
	static bool IsIdentStart(TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || C == TEXT('_'); }
	static bool IsIdentChar(TCHAR C)  { return IsIdentStart(C) || IsDigit(C); }

	static TCHAR Lower(TCHAR C)
	{
		return C >= TEXT('A') && C <= TEXT('Z') ? TCHAR(C - TEXT('A') + TEXT('a')) : C;
	}

	/** Compares names ignoring case, then by case, for a stable order. */
	static bool NameLess(const FString& A, const FString& B)
	{
		const int32 Len = FMath::Min(A.Len(), B.Len());
		for (int32 I = 0; I < Len; ++I)
		{
			const TCHAR LA = Lower(A[I]);
			const TCHAR LB = Lower(B[I]);
			if (LA != LB)
			{
				return LA < LB;
			}
		}
		if (A.Len() != B.Len())
		{
			return A.Len() < B.Len();
		}
		for (int32 I = 0; I < Len; ++I)
		{
			if (A[I] != B[I])
			{
				return A[I] < B[I];
			}
		}
		return false;
	}

	static bool StartsWithIgnoringCase(const FString& Text, const FString& Prefix)
	{
		if (Prefix.Len() > Text.Len())
		{
			return false;
		}
		for (int32 I = 0; I < Prefix.Len(); ++I)
		{
			if (Lower(Text[I]) != Lower(Prefix[I]))
			{
				return false;
			}
		}
		return true;
	}

	/** Token kinds a name being typed can have: what the tokenizer makes of an unfinished name or keyword. */
	static bool IsNameLike(EBangoSyntaxKind Kind)
	{
		switch (Kind)
		{
		case EBangoSyntaxKind::Global:
		case EBangoSyntaxKind::Local:
		case EBangoSyntaxKind::Parameter:
		case EBangoSyntaxKind::UndeclaredGlobal:
		case EBangoSyntaxKind::Call:
		case EBangoSyntaxKind::Builtin:
		case EBangoSyntaxKind::Keyword:
		case EBangoSyntaxKind::JumpKeyword:
		case EBangoSyntaxKind::LabelKeyword:
		case EBangoSyntaxKind::DeclKeyword:
		case EBangoSyntaxKind::Type:
		case EBangoSyntaxKind::Constant:
		case EBangoSyntaxKind::Nil:
		case EBangoSyntaxKind::LocMacro:
			return true;
		default:
			return false;
		}
	}

	static FBangoCompletionItem MakeItem(const TCHAR* Name, EBangoCompletionKind Kind, const TCHAR* Detail, const TCHAR* Description, bool bCallable)
	{
		FBangoCompletionItem Item;
		Item.Name = Name;
		Item.Kind = Kind;
		Item.Detail = Detail;
		Item.Description = Description;
		Item.bCallable = bCallable;
		return Item;
	}
}

bool FBangoCompletion::FindWordBeforeCursor(const FString& Line, int32 CursorColumn, int32& OutWordStart)
{
	using namespace BangoCompletionPrivate;

	if (CursorColumn <= 0 || CursorColumn > Line.Len())
	{
		return false;
	}
	int32 Start = CursorColumn;
	while (Start > 0 && IsIdentChar(Line[Start - 1]))
	{
		--Start;
	}
	if (Start == CursorColumn || !IsIdentStart(Line[Start]))
	{
		return false; // nothing typed, or a number
	}
	if (Start > 0 && (Line[Start - 1] == TEXT('@') || Line[Start - 1] == TEXT('.')))
	{
		return false; // a Bango ID, or a vector part
	}

	// What the tokenizer makes of it: not in a string or a comment, not a label's name, not a name being declared.
	TArray<FBangoSyntaxToken> Tokens;
	FBangoLineTokenizer::TokenizeLine(FStringView(Line), Tokens);
	int32 WordToken = INDEX_NONE;
	for (int32 I = 0; I < Tokens.Num(); ++I)
	{
		if (Tokens[I].Start <= Start && Start < Tokens[I].Start + Tokens[I].Len)
		{
			WordToken = I;
			break;
		}
	}
	if (WordToken == INDEX_NONE || Tokens[WordToken].Start != Start || !IsNameLike(Tokens[WordToken].Kind))
	{
		return false;
	}
	for (int32 I = WordToken - 1; I >= 0; --I)
	{
		const EBangoSyntaxKind Kind = Tokens[I].Kind;
		if (Kind == EBangoSyntaxKind::Comment || Kind == EBangoSyntaxKind::Metadata || Kind == EBangoSyntaxKind::DisconnectedMarker
			|| Kind == EBangoSyntaxKind::DisabledMarker)
		{
			continue;
		}
		if (Kind == EBangoSyntaxKind::Type)
		{
			return false; // `int coun|`: a new name being declared
		}
		break;
	}

	OutWordStart = Start;
	return true;
}

void FBangoCompletion::GetBuiltinItems(TArray<FBangoCompletionItem>& OutItems)
{
	using namespace BangoCompletionPrivate;
	OutItems.Add(MakeItem(TEXT("IsNil"), EBangoCompletionKind::Builtin, TEXT("IsNil(value) -> bool"), TEXT("True if the value is nil. Never reads it, so it never stops the script."), true));
	OutItems.Add(MakeItem(TEXT("Count"), EBangoCompletionKind::Builtin, TEXT("Count(array) -> int"), TEXT("The number of slots in the array, nil slots included."), true));
	OutItems.Add(MakeItem(TEXT("Push"), EBangoCompletionKind::Builtin, TEXT("array Push(value)"), TEXT("Adds a value at the end of the array."), true));
	OutItems.Add(MakeItem(TEXT("InsertAt"), EBangoCompletionKind::Builtin, TEXT("array InsertAt(value, int Index)"), TEXT("Inserts a value at an index and moves later elements up."), true));
	OutItems.Add(MakeItem(TEXT("Pop"), EBangoCompletionKind::Builtin, TEXT("array Pop() -> element"), TEXT("Removes and returns the last element."), true));
	OutItems.Last().bNoArgs = true;
	OutItems.Add(MakeItem(TEXT("RemoveAt"), EBangoCompletionKind::Builtin, TEXT("array RemoveAt(int Index) -> element"), TEXT("Removes and returns an element, and moves later elements down."), true));
}

void FBangoCompletion::GetKeywordItems(TArray<FBangoCompletionItem>& OutItems, bool bValuesOnly)
{
	using namespace BangoCompletionPrivate;
	struct FKeyword { const TCHAR* Name; const TCHAR* Detail; const TCHAR* Description; bool bValue; };
	static const FKeyword Keywords[] =
	{
		{ TEXT("if"),       TEXT("flow"),        TEXT("if (condition) { ... } runs its block when the condition is true."), false },
		{ TEXT("else"),     TEXT("flow"),        TEXT("After an if block: else { ... } or else if (...) { ... }."), false },
		{ TEXT("while"),    TEXT("flow"),        TEXT("while (condition) { ... } runs its block again and again while the condition is true."), false },
		{ TEXT("for"),      TEXT("flow"),        TEXT("for (int _i = 0; _i < 3; _i++) { ... }: a counting loop."), false },
		{ TEXT("forEach"),  TEXT("flow"),        TEXT("forEach (_x in _array) { ... } runs its block once for each element."), false },
		{ TEXT("in"),       TEXT("flow"),        TEXT("In a forEach header: forEach (_x in _array)."), false },
		{ TEXT("sequence"), TEXT("flow"),        TEXT("sequence { ... } { ... } runs its blocks one after another, each to its end."), false },
		{ TEXT("spawn"),    TEXT("flow"),        TEXT("spawn { ... } starts its block as a separate running copy of the script, and carries on straight away."), false },
		{ TEXT("waitUntil"), TEXT("flow"),       TEXT("waitUntil (condition); pauses the script until the condition is true (carries on at once if it already is)."), false },
		{ TEXT("switch"),   TEXT("flow"),        TEXT("switch (value) { case 1: { ... } default: { ... } } runs the case whose value matches."), false },
		{ TEXT("case"),     TEXT("flow"),        TEXT("In a switch: case 1, 2: { ... }."), false },
		{ TEXT("default"),  TEXT("flow"),        TEXT("In a switch, last: default: { ... } runs when no case matches."), false },
		{ TEXT("break"),    TEXT("jump"),        TEXT("Leaves the loop it's in."), false },
		{ TEXT("continue"), TEXT("jump"),        TEXT("Skips the rest of this pass through the loop it's in."), false },
		{ TEXT("return"),   TEXT("jump"),        TEXT("Ends the script, or the function it's in (with its result, if it has one)."), false },
		{ TEXT("label"),    TEXT("jump"),        TEXT("label Name; marks a place that goto can jump to."), false },
		{ TEXT("goto"),     TEXT("jump"),        TEXT("goto Name; jumps to a label."), false },
		{ TEXT("global"),   TEXT("declaration"), TEXT("global int stage; declares a global, at the top of the file."), false },
		{ TEXT("scriptVar"), TEXT("declaration"), TEXT("scriptVar int _count = 0; declares a script variable with a fixed starting value, at the top of the script or function."), false },
		{ TEXT("call"),     TEXT("call"),        TEXT("@light call TurnOn(); calls a Blueprint function of an actor: one in the category 'Bango', named exactly as written."), false },
		{ TEXT("function"), TEXT("declaration"), TEXT("function int AddScore(int _amount) { ... } defines a function, at the top level."), false },
		{ TEXT("bool"),     TEXT("type"),        TEXT("true or false."), false },
		{ TEXT("int"),      TEXT("type"),        TEXT("A whole number."), false },
		{ TEXT("float"),    TEXT("type"),        TEXT("A number with a fraction (64-bit)."), false },
		{ TEXT("string"),   TEXT("type"),        TEXT("Text that isn't translated, like a name or an ID."), false },
		{ TEXT("entity"),   TEXT("type"),        TEXT("A Bango ID, like @guard."), false },
		{ TEXT("text"),     TEXT("type"),        TEXT("Text that players read, translatable: LOCTEXT(\"Key\", \"...\")."), false },
		{ TEXT("vector2"),  TEXT("type"),        TEXT("Two numbers, like [1, 2]."), false },
		{ TEXT("vector3"),  TEXT("type"),        TEXT("Three numbers, like [1, 2, 3]."), false },
		{ TEXT("void"),     TEXT("type"),        TEXT("A function that gives back no result."), false },
		{ TEXT("true"),     TEXT("value"),       TEXT("The bool value true."), true },
		{ TEXT("false"),    TEXT("value"),       TEXT("The bool value false."), true },
		{ TEXT("nil"),      TEXT("value"),       TEXT("No value. Reading nil stops the script; check with IsNil."), true },
		{ TEXT("player"),   TEXT("entity"),      TEXT("The first local player's pawn, found each time it's used: player SetPos(...)."), true },
		{ TEXT("self"),     TEXT("entity"),      TEXT("The actor this script belongs to (its owner), by its Bango ID: self SetPos(...)."), true },
	};
	for (const FKeyword& Keyword : Keywords)
	{
		if (!bValuesOnly || Keyword.bValue)
		{
			OutItems.Add(MakeItem(Keyword.Name, EBangoCompletionKind::Keyword, Keyword.Detail, Keyword.Description, false));
		}
	}
}

void FBangoCompletion::GatherItems(const FBangoScriptNames& Names, int32 LineIndex, const TArray<FBangoCompletionItem>& CommandItems,
	TArray<FBangoCompletionItem>& OutItems)
{
	using namespace BangoCompletionPrivate;

	OutItems.Reset();
	TArray<FBangoVisibleName> Visible;
	Names.GetVisibleNames(LineIndex, Visible);
	for (const FBangoVisibleName& Name : Visible)
	{
		FBangoCompletionItem Item;
		Item.Name = Name.Name;
		Item.Detail = Name.Type;
		Item.Description = Name.Tooltip;
		switch (Name.Kind)
		{
		case EBangoSyntaxKind::Parameter: Item.Kind = EBangoCompletionKind::Parameter; break;
		case EBangoSyntaxKind::Global:    Item.Kind = EBangoCompletionKind::Global; break;
		case EBangoSyntaxKind::Function:
			// Its "type" is its signature, `int AddScore(int amount)`, which ends in `()` when it has no parameters.
			Item.Kind = EBangoCompletionKind::Function;
			Item.bCallable = true;
			Item.bNoArgs = Name.Type.EndsWith(TEXT("()"));
			break;
		default:                          Item.Kind = EBangoCompletionKind::Local; break;
		}
		OutItems.Add(Item);
	}

	TArray<FBangoCompletionItem> Builtins;
	GetBuiltinItems(Builtins);
	TArray<FBangoCompletionItem> Keywords;
	GetKeywordItems(Keywords);
	const TArray<FBangoCompletionItem>* const Groups[] = { &CommandItems, &Builtins, &Keywords };
	for (const TArray<FBangoCompletionItem>* Group : Groups)
	{
		for (const FBangoCompletionItem& Item : *Group)
		{
			OutItems.Add(Item);
		}
	}

	// Sorted by name; a name appears once (the first: a local hides a global, and so on).
	TArray<FBangoCompletionItem> Sorted;
	for (const FBangoCompletionItem& Item : OutItems)
	{
		bool bDuplicate = false;
		for (const FBangoCompletionItem& Kept : Sorted)
		{
			if (Kept.Name.Equals(Item.Name, ESearchCase::CaseSensitive))
			{
				bDuplicate = true;
				break;
			}
		}
		if (bDuplicate)
		{
			continue;
		}
		int32 At = Sorted.Num();
		while (At > 0 && NameLess(Item.Name, Sorted[At - 1].Name))
		{
			--At;
		}
		Sorted.Insert(Item, At);
	}
	OutItems = MoveTemp(Sorted);
}

bool FBangoCompletion::FindHoverItem(const FBangoScriptNames& Names, const FString& Line, int32 LineIndex, int32 Column,
	const TArray<FBangoCompletionItem>& CommandItems, FBangoCompletionItem& OutItem, int32& OutStart, int32& OutEnd)
{
	TArray<FBangoSyntaxToken> Tokens;
	FBangoLineTokenizer::TokenizeLine(FStringView(Line), Tokens);
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Tokens.Num(); ++i)
	{
		if (Column >= Tokens[i].Start && Column < Tokens[i].Start + Tokens[i].Len)
		{
			Found = i;
			break;
		}
	}
	if (Found == INDEX_NONE)
	{
		return false;
	}
	const FBangoSyntaxToken& Token = Tokens[Found];
	const bool bCall = Token.Kind == EBangoSyntaxKind::Call || Token.Kind == EBangoSyntaxKind::Builtin;
	if (!bCall && Token.Kind != EBangoSyntaxKind::Global)
	{
		return false;
	}
	// A `call`'s function is a Blueprint function of its target, not one of the script's names.
	for (int32 i = Found - 1; i >= 0; --i)
	{
		if (Tokens[i].Kind == EBangoSyntaxKind::Default && FStringView(Line).Mid(Tokens[i].Start, Tokens[i].Len).TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		if (FStringView(Line).Mid(Tokens[i].Start, Tokens[i].Len).Equals(TEXT("call"), ESearchCase::CaseSensitive))
		{
			return false;
		}
		break;
	}
	const FString Name = Line.Mid(Token.Start, Token.Len);
	TArray<FBangoCompletionItem> Items;
	GatherItems(Names, LineIndex, CommandItems, Items);
	for (const FBangoCompletionItem& Item : Items)
	{
		if (Item.Kind != EBangoCompletionKind::Keyword && Item.bCallable == bCall && Item.Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			OutItem = Item;
			OutStart = Token.Start;
			OutEnd = Token.Start + Token.Len;
			return true;
		}
	}
	return false;
}

FBangoCompletionInsert FBangoCompletion::MakeInsert(const FBangoCompletionItem& Item, const FString& Line, int32 WordEnd)
{
	FBangoCompletionInsert Insert;
	Insert.Text = Item.Name;
	WordEnd = FMath::Clamp(WordEnd, 0, Line.Len());
	if (!Item.bCallable || (WordEnd < Line.Len() && Line[WordEnd] == TEXT('(')))
	{
		return Insert;
	}
	bool bRestBlank = true;
	for (int32 I = WordEnd; I < Line.Len(); ++I)
	{
		if (Line[I] != TEXT(' ') && Line[I] != TEXT('\t'))
		{
			bRestBlank = false;
			break;
		}
	}
	if (Item.bNoArgs)
	{
		Insert.Text += TEXT("()");
	}
	else
	{
		Insert.Text += TEXT("(");
		Insert.bOpenParen = bRestBlank;
	}
	return Insert;
}

namespace BangoCompletionPrivate
{
	static bool IsStringOrCommentKind(EBangoSyntaxKind Kind)
	{
		switch (Kind)
		{
		case EBangoSyntaxKind::String:
		case EBangoSyntaxKind::StringEscape:
		case EBangoSyntaxKind::Placeholder:
		case EBangoSyntaxKind::LocKey:
		case EBangoSyntaxKind::LocNotes:
		case EBangoSyntaxKind::Comment:
		case EBangoSyntaxKind::Metadata:
			return true;
		default:
			return false;
		}
	}

	/** Calls Visit(Column, Char) for each bracket on Line that's code (not in a string or a comment), in order. */
	template <typename FVisit>
	static void ForEachCodeBracket(const FString& Line, FVisit&& Visit)
	{
		TArray<FBangoSyntaxToken> Tokens;
		FBangoLineTokenizer::TokenizeLine(FStringView(Line), Tokens);
		for (const FBangoSyntaxToken& Token : Tokens)
		{
			if (Token.Kind != EBangoSyntaxKind::Punctuation && Token.Kind != EBangoSyntaxKind::Structure)
			{
				continue;
			}
			for (int32 I = Token.Start; I < Token.Start + Token.Len && I < Line.Len(); ++I)
			{
				const TCHAR C = Line[I];
				if (C == TEXT('(') || C == TEXT(')') || C == TEXT('[') || C == TEXT(']') || C == TEXT('{') || C == TEXT('}'))
				{
					Visit(I, C);
				}
			}
		}
	}
}

void FBangoCompletion::KeepOpenParens(const FString& Line, int32 CursorColumn, TArray<int32>& InOutOpenColumns)
{
	using namespace BangoCompletionPrivate;

	struct FBracket { int32 Column; TCHAR Char; };
	TArray<FBracket> Brackets;
	ForEachCodeBracket(Line, [&Brackets](int32 Column, TCHAR C) { Brackets.Add({ Column, C }); });

	InOutOpenColumns.RemoveAll([&](int32 Open)
	{
		if (Open >= CursorColumn)
		{
			return true;
		}
		const int32 Index = Brackets.IndexOfByPredicate([Open](const FBracket& B) { return B.Column == Open; });
		if (Index == INDEX_NONE || Brackets[Index].Char != TEXT('('))
		{
			return true; // not a `(` any more
		}
		int32 Depth = 0;
		for (int32 I = Index; I < Brackets.Num(); ++I)
		{
			const TCHAR C = Brackets[I].Char;
			if (C == TEXT('(') || C == TEXT('['))
			{
				++Depth;
			}
			else if (C == TEXT(')') || C == TEXT(']'))
			{
				if (--Depth == 0)
				{
					return true; // closed
				}
			}
		}
		return false;
	});
}

bool FBangoCompletion::IsInStringOrComment(const FString& Line, int32 Column)
{
	using namespace BangoCompletionPrivate;

	// What a character typed there would be: the text before it, and one more name character.
	Column = FMath::Clamp(Column, 0, Line.Len());
	const FString Probe = Line.Left(Column) + TEXT("_");
	TArray<FBangoSyntaxToken> Tokens;
	FBangoLineTokenizer::TokenizeLine(FStringView(Probe), Tokens);
	for (const FBangoSyntaxToken& Token : Tokens)
	{
		if (Token.Start <= Column && Column < Token.Start + Token.Len)
		{
			// A string not closed yet is an error token.
			return IsStringOrCommentKind(Token.Kind) || (Token.Kind == EBangoSyntaxKind::Error && Probe[Token.Start] == TEXT('"'));
		}
	}
	return false;
}

void FBangoCompletion::Filter(const TArray<FBangoCompletionItem>& Items, const FString& Prefix, TArray<int32>& OutIndices)
{
	using namespace BangoCompletionPrivate;
	OutIndices.Reset();
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		if (StartsWithIgnoringCase(Items[I].Name, Prefix))
		{
			OutIndices.Add(I);
		}
	}
}

bool FBangoCompletion::FindCallTarget(const FString& Line, int32 WordStart, FString& OutTargetEntity)
{
	OutTargetEntity.Reset();
	auto IsNameChar = [](TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_'); };
	auto IsSpace = [](TCHAR C) { return C == TEXT(' ') || C == TEXT('\t'); };
	int32 i = FMath::Min(WordStart, Line.Len()) - 1;
	while (i >= 0 && IsSpace(Line[i]))
	{
		--i;
	}
	// `call`, as a word of its own.
	if (i < 3 || Line.Mid(i - 3, 4) != TEXT("call") || (i - 4 >= 0 && IsNameChar(Line[i - 4])) || i + 1 == WordStart)
	{
		return false;
	}
	i -= 4;
	while (i >= 0 && IsSpace(Line[i]))
	{
		--i;
	}
	// The target: `@name`, `self` or `player` is known by name; anything else is only known when it runs.
	const int32 End = i + 1;
	while (i >= 0 && IsNameChar(Line[i]))
	{
		--i;
	}
	const FString Word = Line.Mid(i + 1, End - (i + 1));
	if (i >= 0 && Line[i] == TEXT('@') && !Word.IsEmpty())
	{
		OutTargetEntity = Word;
	}
	else if (Word == TEXT("self"))
	{
		OutTargetEntity = Bango::GetSelfEntityName();
	}
	else if (Word == TEXT("player"))
	{
		OutTargetEntity = Bango::GetPlayerEntityName();
	}
	return true;
}
