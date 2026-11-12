#pragma once

// Editing (field edits, validation, compile-error placement) test cases, shared by the Unreal automation test
// (BangoGraphTests.cpp) and the stand-alone harness. Needs BangoGraphEdit.h, BangoCompiler.h and
// BangoMakeTestCommands (BangoCompilerTestCases.inl).

struct FBangoFieldEditTestCase
{
	EBangoGraphNodeKind Kind;
	bool bInFunctionGraph;
	const TCHAR* Text;
	int32 Field;
	const TCHAR* Value;
	/** The node's text after the edit, or null if the edit must be refused. */
	const TCHAR* ExpectedText;
	/** The node's kind after the edit (ignored when the edit is refused). */
	EBangoGraphNodeKind ExpectedKind;
};

static const FBangoFieldEditTestCase GBangoFieldEditTestCases[] =
{
	// Arguments
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), 0, TEXT("2.5"), TEXT("Sleep(2.5)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), 0, TEXT("  3.0 "), TEXT("Sleep(3.0)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("AwaitSpeak()"), 0, TEXT("0.5"), TEXT("AwaitSpeak(0.5)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("AwaitSpeak(0.5)"), 0, TEXT(""), TEXT("AwaitSpeak()"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Wave()"), 1, TEXT("1.5"), nullptr, EBangoGraphNodeKind::Call },   // locked: times comes first
	{ EBangoGraphNodeKind::Call, false, TEXT("Wave()"), 0, TEXT("2"), TEXT("Wave(2)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Wave(2)"), 1, TEXT("1.5"), TEXT("Wave(2, 1.5)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Wave(2, 1.5)"), 1, TEXT(""), TEXT("Wave(2)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"a\", 1, 2)"), 2, TEXT(""), TEXT("Hint(\"a\", 1)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"a\", 1, 2)"), 1, TEXT(""), nullptr, EBangoGraphNodeKind::Call },   // a value in the middle can't be removed
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"a\", 1, 2)"), 1, TEXT("_x"), TEXT("Hint(\"a\", _x, 2)"), EBangoGraphNodeKind::Call },
	// Placeholder rows without a value can be filled in, in order.
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"Test {0} {1} {2}\", 1, 2)"), 3, TEXT("3"), TEXT("Hint(\"Test {0} {1} {2}\", 1, 2, 3)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{0} {1}\")"), 1, TEXT("_a"), TEXT("Hint(\"{0} {1}\", _a)"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{0} {1}\")"), 2, TEXT("_b"), nullptr, EBangoGraphNodeKind::Call },   // locked: {0} comes first
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{0} {1}\", 1, 2)"), 2, TEXT(""), TEXT("Hint(\"{0} {1}\", 1)"), EBangoGraphNodeKind::Call },   // the row stays, now missing
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"Test {0} {1}\", 1, 2)"), 0, TEXT("\"Test {0} {1} {2}\""), TEXT("Hint(\"Test {0} {1} {2}\", 1, 2)"), EBangoGraphNodeKind::Call },

	// Targets
	{ EBangoGraphNodeKind::Call, false, TEXT("@john Speak(\"Hi\")"), 0, TEXT("@mary"), TEXT("@mary Speak(\"Hi\")"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("@john Speak(\"Hi\")"), 0, TEXT(""), TEXT("Speak(\"Hi\")"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Speak(\"x\")"), 0, TEXT("@john"), TEXT("@john Speak(\"x\")"), EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("@john Speak(\"x\")"), 0, TEXT("@ john"), nullptr, EBangoGraphNodeKind::Call },   // no space after @
	{ EBangoGraphNodeKind::Call, false, TEXT("Speak(@john)"), 1, TEXT("@bob"), TEXT("Speak(@bob)"), EBangoGraphNodeKind::Call },

	// A call node has no Result field. A line that stores a call's result is a Declare or Set node, with the call
	// as its Value.
	{ EBangoGraphNodeKind::Call, false, TEXT("Random(1.0, 2.0)"), 2, TEXT("_t"), nullptr, EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_t = Random(1.0, 2.0)"), 0, TEXT("_u[1]"), TEXT("_u[1] = Random(1.0, 2.0)"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_t = Random(1.0, 2.0)"), 1, TEXT("@john GetPos()"), TEXT("_t = @john GetPos()"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_t = Random(1.0, 2.0)"), 0, TEXT("_t = _u"), nullptr, EBangoGraphNodeKind::Assign },   // no chained assignment

	// Generic nodes
	// Local declarations have no Type field either: Name is field 0, Value field 1.
	// Global declarations have no Type field: Name is field 0, Value field 1.
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = 1"), 0, TEXT("_y"), TEXT("_y = 1"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x += 1"), 1, TEXT("_a * 2"), TEXT("_x += _a * 2"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("_x++"), 0, TEXT("_a[1]"), TEXT("_a[1]++"), EBangoGraphNodeKind::IncDec },
	{ EBangoGraphNodeKind::If, false, TEXT("_a > 1"), 0, TEXT("_a > 2 && !IsNil(g)"), TEXT("_a > 2 && !IsNil(g)"), EBangoGraphNodeKind::If },
	// A value left out reads (it fails to compile instead), but text that doesn't read is still refused.
	{ EBangoGraphNodeKind::If, false, TEXT("_a > 1"), 0, TEXT("_a >"), TEXT("_a >"), EBangoGraphNodeKind::If },
	{ EBangoGraphNodeKind::If, false, TEXT("_a > 1"), 0, TEXT("_a 1"), nullptr, EBangoGraphNodeKind::If },

	// New nodes start with their fields empty (see the add-node menu); filling them in and clearing them again.
	{ EBangoGraphNodeKind::If, false, TEXT(""), 0, TEXT("_a > 1"), TEXT("_a > 1"), EBangoGraphNodeKind::If },
	{ EBangoGraphNodeKind::If, false, TEXT("_a > 1"), 0, TEXT(""), TEXT(""), EBangoGraphNodeKind::If },
	{ EBangoGraphNodeKind::While, false, TEXT(""), 0, TEXT("true"), TEXT("true"), EBangoGraphNodeKind::While },
	{ EBangoGraphNodeKind::Assign, false, TEXT("="), 0, TEXT("_x"), TEXT("_x ="), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("="), 1, TEXT("5"), TEXT("= 5"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x ="), 1, TEXT("5"), TEXT("_x = 5"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("= 5"), 0, TEXT("_x"), TEXT("_x = 5"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("+="), 0, TEXT("_x"), TEXT("_x +="), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = 5"), 0, TEXT(""), TEXT("= 5"), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = 5"), 1, TEXT(""), TEXT("_x ="), EBangoGraphNodeKind::Assign },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("++"), 0, TEXT("_x"), TEXT("_x++"), EBangoGraphNodeKind::IncDec },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("--"), 0, TEXT("_a[1]"), TEXT("_a[1]--"), EBangoGraphNodeKind::IncDec },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("_x++"), 0, TEXT(""), TEXT("++"), EBangoGraphNodeKind::IncDec },
	{ EBangoGraphNodeKind::ForEach, false, TEXT("_item in"), 1, TEXT("_list"), TEXT("_item in _list"), EBangoGraphNodeKind::ForEach },
	{ EBangoGraphNodeKind::ForEach, false, TEXT("_item in _list"), 1, TEXT(""), TEXT("_item in"), EBangoGraphNodeKind::ForEach },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = ; _i <= ; _i++"), 1, TEXT("0"), TEXT("int _i = 0; _i <= ; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= ; _i++"), 2, TEXT("9"), TEXT("int _i = 0; _i <= 9; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = ; _i <= ; _i++"), 0, TEXT("_n"), TEXT("int _n = ; _n <= ; _n++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), 2, TEXT(""), TEXT("int _i = 0; _i <= ; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::Label, false, TEXT(""), 0, TEXT("Hub"), TEXT("Hub"), EBangoGraphNodeKind::Label },
	{ EBangoGraphNodeKind::Goto, false, TEXT(""), 0, TEXT("Hub"), TEXT("Hub"), EBangoGraphNodeKind::Goto },
	{ EBangoGraphNodeKind::Goto, false, TEXT("Hub"), 0, TEXT(""), TEXT(""), EBangoGraphNodeKind::Goto },
	// A For that isn't a simple counting loop has one Expression row, the whole header.
	{ EBangoGraphNodeKind::For, false, TEXT(";;"), 0, TEXT("int _i = 0; _i < 3; _i++"), TEXT("int _i = 0; _i < 3; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT(";;"), 0, TEXT("int _i = 0"), nullptr, EBangoGraphNodeKind::For },
	// A simple one has Var, First and Last; renaming Var renames it everywhere in the header.
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), 0, TEXT("_n"), TEXT("int _n = 0; _n <= 9; _n++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), 1, TEXT("1"), TEXT("int _i = 1; _i <= 9; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), 2, TEXT("_count - 1"), TEXT("int _i = 0; _i <= _count - 1; _i++"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("_i = 9; _i >= 0; --_i"), 0, TEXT("_k"), TEXT("_k = 9; _k >= 0; --_k"), EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), 0, TEXT("two words"), nullptr, EBangoGraphNodeKind::For },
	{ EBangoGraphNodeKind::ForEach, false, TEXT("_x in _a"), 1, TEXT("[1, 2]"), TEXT("_x in [1, 2]"), EBangoGraphNodeKind::ForEach },
	{ EBangoGraphNodeKind::Return, true, TEXT(""), 0, TEXT("1"), TEXT("1"), EBangoGraphNodeKind::Return },
	{ EBangoGraphNodeKind::Return, true, TEXT("1"), 0, TEXT(""), TEXT(""), EBangoGraphNodeKind::Return },
	{ EBangoGraphNodeKind::Label, false, TEXT("Hub"), 0, TEXT("Start"), TEXT("Start"), EBangoGraphNodeKind::Label },
	{ EBangoGraphNodeKind::Label, false, TEXT("Hub"), 0, TEXT("two words"), nullptr, EBangoGraphNodeKind::Label },

	// Entry nodes
	{ EBangoGraphNodeKind::Entry, false, TEXT("string _name, int _n"), 1, TEXT("3"), nullptr, EBangoGraphNodeKind::Entry },     // defaults are read-only here: the INPUTS panel edits them
	{ EBangoGraphNodeKind::Entry, false, TEXT("string _name, int _n = 3"), 1, TEXT(""), nullptr, EBangoGraphNodeKind::Entry },
	{ EBangoGraphNodeKind::Entry, false, TEXT("string _name, int _n = 3"), 1, TEXT("_other"), nullptr, EBangoGraphNodeKind::Entry },   // defaults are literals only
	{ EBangoGraphNodeKind::Entry, true, TEXT("void Greet()"), 0, TEXT("int"), TEXT("int Greet()"), EBangoGraphNodeKind::Entry },
	{ EBangoGraphNodeKind::Entry, true, TEXT("int Twice(int _n)"), 1, TEXT("x"), nullptr, EBangoGraphNodeKind::Entry },   // a parameter row has no value

	// Text that would break the line
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), 0, TEXT("1.0) // x"), nullptr, EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), 0, TEXT("1.0); Sleep(2.0"), nullptr, EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), 0, TEXT("1.0 //@4(1, 1)"), nullptr, EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"a\")"), 0, TEXT("\"two\nlines\""), nullptr, EBangoGraphNodeKind::Call },
	{ EBangoGraphNodeKind::If, false, TEXT("true"), 0, TEXT("true) {"), nullptr, EBangoGraphNodeKind::If },
};

static void BangoRunGraphEditScenarios(TArray<FString>& OutFailures)
{
	FBangoCommandTable Commands;
	BangoMakeTestCommands(Commands);
	FBangoGraphDocument FunctionsDoc;
	FBangoCompileError Error;
	Bango::BuildGraph(TEXT("function int Twice(int _n)\n{\n\treturn _n * 2;\n};\n"), TEXT("test.bango"), FunctionsDoc, Error);
	const FBangoNodeLayoutContext Context = Bango::MakeNodeLayoutContext(FunctionsDoc, &Commands);

	for (const FBangoFieldEditTestCase& Case : GBangoFieldEditTestCases)
	{
		FBangoGraphNode Node;
		Node.Kind = Case.Kind;
		Node.Text = Case.Text;
		FString EditError;
		const bool bOk = Bango::SetNodeField(Node, Case.bInFunctionGraph, Context, Case.Field, Case.Value, EditError);
		const FString What = FString::Printf(TEXT("edit `%s` field %d to `%s`"), Case.Text, Case.Field, Case.Value);
		if (!Case.ExpectedText)
		{
			if (bOk)
			{
				OutFailures.Add(What + TEXT(": should be refused, but gave `") + Node.Text + TEXT("`"));
			}
			else if (!Node.Text.Equals(Case.Text, ESearchCase::CaseSensitive) || EditError.IsEmpty())
			{
				OutFailures.Add(What + TEXT(": a refused edit must leave the node alone and say why"));
			}
			continue;
		}
		if (!bOk)
		{
			OutFailures.Add(What + TEXT(": refused: ") + EditError);
		}
		else if (!Node.Text.Equals(Case.ExpectedText, ESearchCase::CaseSensitive) || Node.Kind != Case.ExpectedKind)
		{
			OutFailures.Add(FString::Printf(TEXT("%s: expected `%s` (kind %s), got `%s` (kind %s)"), *What, Case.ExpectedText,
				*Bango::GetGraphNodeKindName(Case.ExpectedKind), *Node.Text, *Bango::GetGraphNodeKindName(Node.Kind)));
		}
	}

	// Locking: in `Wave()`, speed can't be filled in before times.
	{
		FBangoGraphNode Node;
		Node.Kind = EBangoGraphNodeKind::Call;
		Node.Text = TEXT("Wave()");
		const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, false, Context);
		if (!Bango::CanEditField(Layout, 0) || Bango::CanEditField(Layout, 1))
		{
			OutFailures.Add(TEXT("locking: in Wave(), times should be editable and speed locked"));
		}
	}

	// Localized text fields: typed as plain text; the key, namespace and notes stay when the words change.
	{
		FBangoGraphNode Node;
		Node.Kind = EBangoGraphNodeKind::Call;
		Node.Text = TEXT("Say(NOTELOCTEXT(\"Greet\", \"Hello {0}, {1}\", \"Friendly\"), 1)");
		const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, false, Context);
		const bool bRows = Layout.Fields.Num() == 3 && Layout.Fields[0].TypeHint == TEXT("text") && Layout.Fields[1].Label == TEXT("{0}")
			&& Layout.Fields[2].Label == TEXT("{1}") && Layout.Fields[2].bPlaceholder && !Layout.Fields[2].bPresent;
		if (!bRows)
		{
			OutFailures.Add(TEXT("text: Say should show Text, {0} and a missing {1} row"));
		}
		else
		{
			const FBangoNodeField& Field = Layout.Fields[0];
			if (!Bango::IsLocalizedTextField(Field) || Bango::GetFieldDisplayText(Field) != TEXT("Hello {0}, {1}") || Bango::GetFieldEditText(Field) != TEXT("Hello {0}, {1}"))
			{
				OutFailures.Add(FString(TEXT("text: the field shows and edits the words, got ")) + Bango::GetFieldDisplayText(Field));
			}
			const FString Retyped = Bango::GetFieldValueFromTyped(Field, TEXT("Bye \"{0}\""));
			if (Retyped != TEXT("NOTELOCTEXT(\"Greet\", \"Bye \\\"{0}\\\"\", \"Friendly\")"))
			{
				OutFailures.Add(FString(TEXT("text: retyping keeps the key and notes, got ")) + Retyped);
			}
			FString EditError;
			FBangoGraphNode Edited = Node;
			if (!Bango::SetNodeField(Edited, false, Context, 0, Retyped, EditError) || Edited.Text != FString(TEXT("Say(")) + Retyped + TEXT(", 1)"))
			{
				OutFailures.Add(FString(TEXT("text: the retyped text goes in the node: ")) + Edited.Text + EditError);
			}
			if (Bango::GetFieldValueFromTyped(Field, TEXT("=_line")) != TEXT("_line"))
			{
				OutFailures.Add(TEXT("text: = gives an expression"));
			}
		}

		FBangoNodeField Empty;
		Empty.TypeHint = TEXT("text");
		Empty.bPresent = false;
		FBangoTextLiteral New;
		const FString Fresh = Bango::GetFieldValueFromTyped(Empty, TEXT("New words"));
		if (!Bango::ParseTextLiteral(Fresh, New) || New.bHasNamespace || New.bHasNotes || New.Key.Len() != 32 || New.Source != TEXT("New words"))
		{
			OutFailures.Add(FString(TEXT("text: a new text gets LOCTEXT with a 32-character key, got ")) + Fresh);
		}
		FBangoTextLiteral Other;
		if (!Bango::ParseTextLiteral(Bango::GetFieldValueFromTyped(Empty, TEXT("New words")), Other) || Other.Key == New.Key)
		{
			OutFailures.Add(TEXT("text: every new text gets its own key"));
		}

		// Clearing the notes gives LOCTEXT again; a namespace gives NSLOCTEXT.
		FBangoTextLiteral Literal;
		Literal.Key = TEXT("K");
		Literal.Source = TEXT("S");
		Literal.Notes = TEXT("N");
		const FString WithNotes = Bango::MakeTextLiteral(Literal);
		Literal.Notes.Reset();
		const FString WithoutNotes = Bango::MakeTextLiteral(Literal);
		Literal.bHasNamespace = true;
		Literal.Namespace = TEXT("Ns");
		const FString WithNamespace = Bango::MakeTextLiteral(Literal);
		if (WithNotes != TEXT("NOTELOCTEXT(\"K\", \"S\", \"N\")") || WithoutNotes != TEXT("LOCTEXT(\"K\", \"S\")") || WithNamespace != TEXT("NSLOCTEXT(\"Ns\", \"K\", \"S\")"))
		{
			OutFailures.Add(FString::Printf(TEXT("text: literals written as %s, %s, %s"), *WithNotes, *WithoutNotes, *WithNamespace));
		}
		// INVTEXT: only the source.
		FBangoTextLiteral Invariant;
		if (!Bango::ParseTextLiteral(TEXT("INVTEXT(\"Hi \\\"you\\\"\")"), Invariant) || !Invariant.bInvariant || Invariant.Source != TEXT("Hi \"you\"")
			|| Bango::MakeTextLiteral(Invariant) != TEXT("INVTEXT(\"Hi \\\"you\\\"\")"))
		{
			OutFailures.Add(TEXT("text: INVTEXT is read and written with only its source"));
		}
		FBangoNodeField InvariantField;
		InvariantField.TypeHint = TEXT("text");
		InvariantField.bPresent = true;
		InvariantField.Text = TEXT("INVTEXT(\"Old\")");
		if (Bango::GetFieldValueFromTyped(InvariantField, TEXT("New")) != TEXT("INVTEXT(\"New\")") || Bango::GetFieldDisplayText(InvariantField) != TEXT("Old"))
		{
			OutFailures.Add(TEXT("text: retyping an INVTEXT keeps it invariant, and it shows its source"));
		}

		FBangoTextLiteral Bad;
		if (Bango::ParseTextLiteral(TEXT("LOCTEXT(\"K\")"), Bad) || Bango::ParseTextLiteral(TEXT("LOCTEXT(\"K\", _s)"), Bad) || Bango::ParseTextLiteral(TEXT("Hint(\"K\", \"S\")"), Bad))
		{
			OutFailures.Add(TEXT("text: only real text literals are read as one"));
		}

		// The Script Start node's Loc Namespace row.
		FBangoNodeLayout Start;
		Bango::AddLocNamespaceField(Start, true, TEXT("Bango.Intro"));
		Bango::AddLocNamespaceField(Start, false, FString());
		FBangoGraphNode Entry;
		Entry.Kind = EBangoGraphNodeKind::Entry;
		FString EntryError;
		if (Start.Fields.Num() != 2 || Bango::GetFieldDisplayText(Start.Fields[0]) != TEXT("Bango.Intro") || !Start.Fields[0].bLocNamespace || Start.Fields[1].bPresent
			|| Bango::SetNodeField(Entry, false, Context, 0, TEXT("\"X\""), EntryError))
		{
			OutFailures.Add(TEXT("text: the Loc Namespace row shows the namespace, and isn't part of the node's text"));
		}
	}

	// Text fields: typed without quotes, escaped when saved.
	{
		struct FTextCase { const TCHAR* Value; const TCHAR* Typed; };
		const FTextCase TextCases[] =
		{
			{ TEXT("Hello"), TEXT("Hello") },
			{ TEXT("say \"hi\""), TEXT("say \"hi\"") },
			{ TEXT("two\nlines"), TEXT("two\\nlines") },
			{ TEXT("a\ttab"), TEXT("a\\ttab") },
			{ TEXT("C:\\path"), TEXT("C:\\path") },
			{ TEXT("\\n, literally"), TEXT("\\\\n, literally") },
			{ TEXT("ends with \\"), TEXT("ends with \\") },
			{ TEXT("=== Act 1 ==="), TEXT("\\=== Act 1 ===") },
			{ TEXT("\\=x"), TEXT("\\\\=x") },
			{ TEXT("a = b"), TEXT("a = b") },
			{ TEXT(""), TEXT("") },
		};
		for (const FTextCase& Case : TextCases)
		{
			const FString Typed = Bango::StringToTypedText(Case.Value);
			const FString Back = Bango::TypedTextToString(Case.Typed);
			FString Literal;
			if (!Typed.Equals(Case.Typed, ESearchCase::CaseSensitive) || !Back.Equals(Case.Value, ESearchCase::CaseSensitive)
				|| !Bango::TryGetStringLiteral(Bango::MakeStringLiteral(Case.Value), Literal) || !Literal.Equals(Case.Value, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("text field: `%s` should be typed as `%s` (got `%s`, and `%s` back)"), Case.Value, Case.Typed, *Typed, *Back));
			}
		}
		if (!Bango::TypedTextToString(TEXT("\\\"quoted\\\" \\q")).Equals(TEXT("\"quoted\" \\q"), ESearchCase::CaseSensitive))
		{
			OutFailures.Add(TEXT("text field: \\\" should give a quote, and other backslashes stay"));
		}

		// Every string survives being typed and saved: random strings of the characters that matter.
		const TCHAR Alphabet[] = { TEXT('a'), TEXT('\\'), TEXT('"'), TEXT('n'), TEXT('t'), TEXT('='), TEXT('\n'), TEXT('\t'), TEXT(' '), TEXT('%') };
		uint32 Seed = 12345;
		for (int32 Run = 0; Run < 4000; ++Run)
		{
			FString Value;
			Seed = Seed * 1664525u + 1013904223u;
			const int32 Len = (int32)((Seed >> 16) % 9);
			for (int32 i = 0; i < Len; ++i)
			{
				Seed = Seed * 1664525u + 1013904223u;
				Value.AppendChar(Alphabet[(Seed >> 16) % (sizeof(Alphabet) / sizeof(Alphabet[0]))]);
			}
			FString Literal;
			if (!Bango::TypedTextToString(Bango::StringToTypedText(Value)).Equals(Value, ESearchCase::CaseSensitive)
				|| !Bango::TryGetStringLiteral(Bango::MakeStringLiteral(Value), Literal) || !Literal.Equals(Value, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("text field: a string didn't survive: `%s`"), *Value));
				break;
			}
		}

		// On a node: Hint's text, Speak's text, and a field that isn't a string.
		struct FNodeTextCase { const TCHAR* Text; int32 Field; const TCHAR* EditText; const TCHAR* Typed; const TCHAR* After; };
		const FNodeTextCase NodeCases[] =
		{
			{ TEXT("Hint(\"x\")"), 0, TEXT("x"), TEXT("Hi \"there\""), TEXT("Hint(\"Hi \\\"there\\\"\")") },
			{ TEXT("Hint(\"a\\\"b\")"), 0, TEXT("a\"b"), TEXT("two\\nlines"), TEXT("Hint(\"two\\nlines\")") },
			{ TEXT("Hint(_name)"), 0, TEXT("=_name"), TEXT("=Format(\"{0}\", 1)"), TEXT("Hint(Format(\"{0}\", 1))") },
			{ TEXT("Hint(\"x\")"), 0, TEXT("x"), TEXT("=_line"), TEXT("Hint(_line)") },
			{ TEXT("Hint(\"x\")"), 0, TEXT("x"), TEXT(""), TEXT("Hint(\"\")") },
			{ TEXT("Hint(\"x\")"), 0, TEXT("x"), TEXT("\\=x"), TEXT("Hint(\"=x\")") },
			{ TEXT("@john Speak(\"Hi\")"), 1, TEXT("Hi"), TEXT("It's \"fine\"."), TEXT("@john Speak(\"It's \\\"fine\\\".\")") },
			{ TEXT("Speak()"), 1, TEXT(""), TEXT("Hello"), TEXT("Speak(\"Hello\")") },
			{ TEXT("Sleep(1.0)"), 0, TEXT("1.0"), TEXT("2.5"), TEXT("Sleep(2.5)") },
		};
		for (const FNodeTextCase& Case : NodeCases)
		{
			FBangoGraphNode Node;
			Node.Kind = EBangoGraphNodeKind::Call;
			Node.Text = Case.Text;
			const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, false, Context);
			if (!Layout.Fields.IsValidIndex(Case.Field))
			{
				OutFailures.Add(FString::Printf(TEXT("text field on `%s`: no field %d"), Case.Text, Case.Field));
				continue;
			}
			const FBangoNodeField& Field = Layout.Fields[Case.Field];
			const FString EditText = Bango::GetFieldEditText(Field);
			FString EditError;
			const bool bSet = Bango::SetNodeField(Node, false, Context, Case.Field, Bango::GetFieldValueFromTyped(Field, Case.Typed), EditError);
			if (!EditText.Equals(Case.EditText, ESearchCase::CaseSensitive) || !bSet || !Node.Text.Equals(Case.After, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("text field on `%s`: edit text `%s` (expected `%s`); typing `%s` gave `%s` (expected `%s`) %s"),
					Case.Text, *EditText, Case.EditText, Case.Typed, *Node.Text, Case.After, *EditError));
			}
		}
	}

	// Placeholders without a value: a warning each, and saved as nil.
	{
		struct FFillCase { const TCHAR* Text; const TCHAR* Filled; };
		const FFillCase FillCases[] =
		{
			{ TEXT("Hint(\"{0} {1} {2}\", 1)"), TEXT("Hint(\"{0} {1} {2}\", 1, nil, nil)") },
			{ TEXT("Hint(\"{0}\")"), TEXT("Hint(\"{0}\", nil)") },
			{ TEXT("Hint(\"{0}\", 1)"), TEXT("Hint(\"{0}\", 1)") },
			{ TEXT("Hint(\"100% sure\")"), TEXT("Hint(\"100% sure\")") },
			{ TEXT("Hint(_text)"), TEXT("Hint(_text)") },
			{ TEXT("Collect(1)"), TEXT("Collect(1)") },
		};
		for (const FFillCase& Case : FillCases)
		{
			FBangoGraphNode Node;
			Node.Kind = EBangoGraphNodeKind::Call;
			Node.Text = Case.Text;
			const bool bChanged = Bango::FillMissingPlaceholders(Node, false, Context);
			if (!Node.Text.Equals(Case.Filled, ESearchCase::CaseSensitive) || bChanged != !FString(Case.Text).Equals(Case.Filled, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("fill placeholders of `%s`: expected `%s`, got `%s`"), Case.Text, Case.Filled, *Node.Text));
			}
		}

		FBangoGraphNode Node;
		Node.Kind = EBangoGraphNodeKind::Call;
		Node.Text = TEXT("Hint(\"{0} {1} {2}\", 1, nil)");
		TArray<Bango::FBangoFieldWarning> Warnings;
		Bango::GetNodeWarnings(Bango::GetNodeLayout(Node, false, Context), Warnings);
		if (Warnings.Num() != 2 || Warnings[0].Field != 2 || Warnings[1].Field != 3
			|| !Warnings[0].Message.Contains(TEXT("{1} is nil")) || !Warnings[1].Message.Contains(TEXT("{2} has no value")))
		{
			OutFailures.Add(FString::Printf(TEXT("warnings: expected {1} (nil) on field 2 and {2} (missing) on field 3, got %d warning(s)"), Warnings.Num()));
		}
		Node.Text = TEXT("Hint(\"{0}\", 1, nil)");
		Bango::GetNodeWarnings(Bango::GetNodeLayout(Node, false, Context), Warnings);
		if (Warnings.Num() != 0)
		{
			OutFailures.Add(TEXT("warnings: a nil past the placeholders isn't a placeholder"));
		}

		// A whole document: filled, written and compiled.
		FBangoGraphDocument Doc;
		if (!Bango::BuildGraph(TEXT("Hint(\"{0} and {1}\", 1); //@1(0, 0)\nfunction void F()\n{\n\tHint(\"{0}\"); //@2(0, 0)\n};\nF(); //@3(0, 1)\n"), TEXT("test.bango"), Doc, Error))
		{
			OutFailures.Add(FString(TEXT("fill document: build: ")) + Error.ToString());
		}
		else
		{
			const FBangoNodeLayoutContext DocContext = Bango::MakeNodeLayoutContext(Doc, &Commands);
			FString Written;
			FString WriteError;
			FBangoProgram Program;
			if (!Bango::FillMissingPlaceholders(Doc, DocContext) || !Bango::WriteGraphText(Doc, Written, WriteError)
				|| !Written.Contains(TEXT("Hint(\"{0} and {1}\", 1, nil);")) || !Written.Contains(TEXT("Hint(\"{0}\", nil);"))
				|| !Bango::CompileSource(Written, TEXT("test.bango"), Commands, Program, Error))
			{
				OutFailures.Add(FString(TEXT("fill document: expected nils that compile, got:\n")) + Written + Error.ToString());
			}
		}
	}

	// Compile errors land on the right node and field.
	{
		const TCHAR* Source =
			TEXT("Hint(\"ok\");             //@1(0, 0)\n")
			TEXT("Sleep(\"not a number\");  //@2(10, 0)\n");
		FBangoGraphDocument Doc;
		if (!Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error))
		{
			OutFailures.Add(FString(TEXT("error place: build: ")) + Error.ToString());
			return;
		}
		FString Written;
		FString WriteError;
		Bango::WriteGraphText(Doc, Written, WriteError);
		FBangoProgram Program;
		const bool bCompiled = Bango::CompileSource(Written, TEXT("test.bango"), Commands, Program, Error);
		Bango::FBangoErrorPlace Place;
		if (bCompiled || !Bango::FindErrorPlace(Doc, Written, Error, Place))
		{
			OutFailures.Add(FString(TEXT("error place: expected a compile error on node 2, got: ")) + Error.ToString());
		}
		else
		{
			const FBangoGraphNode& Node = Doc.Main.Nodes[Place.Node];
			const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, false, Context);
			const int32 Field = Bango::FindFieldAt(Layout, Place.Offset);
			if (Place.Graph != INDEX_NONE || Node.NodeID != 2 || Field != 0)
			{
				OutFailures.Add(FString::Printf(TEXT("error place: expected node 2 field 0, got graph %d node %lld field %d (offset %d) for: %s"),
					Place.Graph, Node.NodeID, Field, Place.Offset, *Error.ToString()));
			}
		}

		if (Bango::GetNextNodeID(Doc) != 3)
		{
			OutFailures.Add(TEXT("next node ID: expected 3"));
		}
	}

	// The add-node menu: every entry makes a node whose text reads; the ones with empty fields don't compile, and
	// the error is on the first empty field.
	{
		FBangoGraphDocument Doc;
		Bango::BuildGraph(TEXT("int _i = 1;\nHint(\"x\");   //@1(0, 0)\nfunction int Twice(int _n)\n{\n\treturn _n * 2; //@2(0, 0)\n};\n"), TEXT("test.bango"), Doc, Error);
		const FBangoNodeLayoutContext MenuContext = Bango::MakeNodeLayoutContext(Doc, &Commands);
		TArray<FBangoNodeMenuEntry> Entries;
		Bango::GetNodeMenuEntries(Doc, MenuContext, Entries);

		/** ErrorField: the field the compile error is on; INDEX_NONE if it compiles; -2 if it fails on the node anywhere. */
		struct FExpected { const TCHAR* Title; const TCHAR* Text; int32 ErrorField; };
		static const FExpected Expected[] =
		{
			{ TEXT("If"),          TEXT(""),                          0 },
			{ TEXT("While"),       TEXT(""),                          0 },
			{ TEXT("For"),         TEXT("int _i2 = ; _i2 <= ; _i2++"), 1 },   // `_i` is taken
			{ TEXT("For Each"),    TEXT("_item in"),                  1 },
			{ TEXT("Label"),       TEXT(""),                          0 },
			{ TEXT("Go To"),       TEXT(""),                          0 },
			{ TEXT("Set"),         TEXT("="),                         0 },
			{ TEXT("Add To"),      TEXT("+="),                        0 },
			{ TEXT("Increment"),   TEXT("++"),                        0 },
			{ TEXT("Reroute"),     TEXT(""),                          INDEX_NONE },
			{ TEXT("Twice"),       TEXT("Twice()"),                   -2 },  // fails (its argument is missing), on the node
		};
		for (const FExpected& Want : Expected)
		{
			const FBangoNodeMenuEntry* Entry = Entries.FindByPredicate([&Want](const FBangoNodeMenuEntry& E) { return E.Title == Want.Title; });
			if (!Entry)
			{
				OutFailures.Add(FString::Printf(TEXT("menu: no '%s' entry"), Want.Title));
				continue;
			}
			if (!Entry->Text.Equals(Want.Text, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("menu: '%s' should make `%s`, but makes `%s`"), Want.Title, Want.Text, *Entry->Text));
				continue;
			}
			// Put the node after the entry, write, compile and find the error.
			FBangoGraphDocument WithNode = Doc;
			FBangoGraphNode& Node = WithNode.Main.Nodes.AddDefaulted_GetRef();
			Node.Kind = Entry->Kind;
			Node.Text = Entry->Text;
			Node.NodeID = 50;
			Node.X = 20;
			WithNode.Main.Nodes[1].Next = WithNode.Main.Nodes.Num() - 1;
			FString Written;
			FString WriteError;
			FBangoProgram Program;
			FBangoCompileError CompileError;
			if (!Bango::WriteGraphText(WithNode, Written, WriteError))
			{
				OutFailures.Add(FString::Printf(TEXT("menu: '%s' doesn't write: %s"), Want.Title, *WriteError));
				continue;
			}
			const bool bCompiled = Bango::CompileSource(Written, TEXT("test.bango"), Commands, Program, CompileError);
			if (Want.ErrorField == INDEX_NONE)
			{
				if (!bCompiled)
				{
					OutFailures.Add(FString::Printf(TEXT("menu: '%s' should compile: %s\n%s"), Want.Title, *CompileError.ToString(), *Written));
				}
				continue;
			}
			Bango::FBangoErrorPlace Place;
			if (bCompiled || !Bango::FindErrorPlace(WithNode, Written, CompileError, Place) || !WithNode.Main.Nodes.IsValidIndex(Place.Node)
				|| WithNode.Main.Nodes[Place.Node].NodeID != 50)
			{
				OutFailures.Add(FString::Printf(TEXT("menu: '%s' should fail on the new node, got: %s\n%s"), Want.Title, *CompileError.ToString(), *Written));
				continue;
			}
			const int32 Field = Bango::FindFieldAt(Bango::GetNodeLayout(Node, false, MenuContext), Place.Offset);
			if (Want.ErrorField >= 0 && Field != Want.ErrorField)
			{
				OutFailures.Add(FString::Printf(TEXT("menu: '%s' error should be on field %d, but is on %d (offset %d): %s"), Want.Title, Want.ErrorField, Field, Place.Offset, *CompileError.ToString()));
			}
		}
		for (const FBangoNodeMenuEntry& Entry : Entries)
		{
			FString Problem;
			FBangoGraphNode Node;
			Node.Kind = Entry.Kind;
			Node.Text = Entry.Text;
			if (!Bango::ValidateNodeText(Entry.Kind, false, Entry.Text, Problem) || !Bango::GetNodeLayout(Node, false, MenuContext).Error.IsEmpty())
			{
				OutFailures.Add(FString::Printf(TEXT("menu: '%s' makes `%s`, which doesn't read: %s"), *Entry.Title, *Entry.Text, *Problem));
			}
		}
		const bool bHasMustUse = Entries.ContainsByPredicate([](const FBangoNodeMenuEntry& E) { return E.Title == TEXT("Count") || E.Title == TEXT("Format"); });
		if (bHasMustUse)
		{
			OutFailures.Add(TEXT("menu: commands whose value must be used (Count, Format) can't be nodes of their own"));
		}
	}
}
