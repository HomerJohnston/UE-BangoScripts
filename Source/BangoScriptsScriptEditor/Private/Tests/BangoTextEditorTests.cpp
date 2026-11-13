// Automation tests for the Bango text editor's line tokenizer.
// Run from Session Frontend > Automation: Bango.ScriptEditor.TextEditor.LineTokenizer

#include "Misc/AutomationTest.h"
#include "BangoLineTokenizer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BangoLineTokenizerTests
{
	static const TCHAR* KindName(EBangoSyntaxKind Kind)
	{
		static const TCHAR* const Names[] =
		{
			TEXT("Default"), TEXT("Keyword"), TEXT("JumpKeyword"), TEXT("LabelKeyword"), TEXT("LabelName"), TEXT("DeclKeyword"),
			TEXT("Type"), TEXT("Constant"), TEXT("Nil"), TEXT("Preprocessor"), TEXT("Local"), TEXT("Global"), TEXT("Entity"),
			TEXT("Number"), TEXT("String"), TEXT("StringEscape"), TEXT("Placeholder"), TEXT("LocMacro"), TEXT("LocKey"),
			TEXT("LocNotes"), TEXT("VectorPart"), TEXT("Call"), TEXT("Builtin"), TEXT("Comment"), TEXT("Metadata"),
			TEXT("DisconnectedMarker"), TEXT("DisabledMarker"), TEXT("Structure"), TEXT("Operator"), TEXT("Punctuation"), TEXT("Error"),
			TEXT("Command"), TEXT("Function"), TEXT("Parameter"), TEXT("UndeclaredGlobal"),
		};
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EBangoSyntaxKind::Count), "Update the names");
		return Names[static_cast<int32>(Kind)];
	}

	/** "Kind[text] Kind~[text] Kind-[text] ..." where ~ marks a disconnected token and - a disabled one. */
	static FString Describe(const FString& Line, int32* InOutDisabledDepth = nullptr)
	{
		TArray<FBangoSyntaxToken> Tokens;
		int32 Depth = InOutDisabledDepth ? *InOutDisabledDepth : 0;
		FBangoLineTokenizer::TokenizeLine(FStringView(Line), Tokens, Depth);
		if (InOutDisabledDepth)
		{
			*InOutDisabledDepth = Depth;
		}

		FString Result;
		int32 PreviousEnd = 0;
		for (const FBangoSyntaxToken& Token : Tokens)
		{
			if (Token.Start < PreviousEnd)
			{
				Result += TEXT("!!OVERLAP ");
			}
			PreviousEnd = Token.Start + Token.Len;
			Result += FString::Printf(TEXT("%s%s%s[%s] "), KindName(Token.Kind), Token.bDisconnected ? TEXT("~") : TEXT(""), Token.bDisabled ? TEXT("-") : TEXT(""), *Line.Mid(Token.Start, Token.Len));
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoLineTokenizerTest, "Bango.ScriptEditor.TextEditor.LineTokenizer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoLineTokenizerTest::RunTest(const FString& Parameters)
{
	// Each line's description must contain the expected run of tokens.
	auto Check = [this](const TCHAR* Line, const TCHAR* Expected)
	{
		const FString Got = BangoLineTokenizerTests::Describe(Line);
		if (!Got.Contains(Expected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("Line: %s\n  want: %s\n  got:  %s"), Line, Expected, *Got));
		}
	};

	Check(TEXT("global int foundThings = 0;      //@1(0, 0)"), TEXT("DeclKeyword[global] Type[int] Global[foundThings] Operator[=] Number[0] Punctuation[;] Metadata[//@1(0, 0)]"));
	Check(TEXT("if (IsNil(foundThings) && foundThings >= 10)"), TEXT("Keyword[if] Punctuation[(] Builtin[IsNil] Punctuation[(] Global[foundThings] Punctuation[)] Operator[&&] Global[foundThings] Operator[>=] Number[10]"));
	Check(TEXT("\tHint(LOCTEXT(\"Countdown\", \"Countdown: {0}\"), 3 - _i);"), TEXT("Call[Hint] Punctuation[(] LocMacro[LOCTEXT] Punctuation[(] LocKey[\"Countdown\"] Punctuation[,] String[\"Countdown: ] Placeholder[{0}] String[\"] Punctuation[)] Punctuation[,] Number[3] Operator[-] Global[_i]"));
	Check(TEXT("Hint(NSNOTELOCTEXT(\"Ns\", \"K\", \"Hi {Name} `{\", \"note\"));"), TEXT("LocKey[\"Ns\"] Punctuation[,] LocKey[\"K\"] Punctuation[,] String[\"Hi ] Error[{Name}] String[ ] StringEscape[`{] String[\"] Punctuation[,] LocNotes[\"note\"]"));
	Check(TEXT("Hint(INVTEXT(\"Score {0}\"), 1);"), TEXT("Call[Hint] Punctuation[(] LocMacro[INVTEXT] Punctuation[(] String[\"Score ] Placeholder[{0}] String[\"] Punctuation[)] Punctuation[,] Number[1]"));
	Check(TEXT("Log(\"{0} and `{ \\n \\q {Name}\", _a);"), TEXT("String[\"] Placeholder[{0}] String[ and ] StringEscape[`{] String[ ] StringEscape[\\n] String[ ] Error[\\q] String[ {Name}\"]"));
	Check(TEXT("@john Speak(\"Hi\");"), TEXT("Entity[@john] Call[Speak]"));
	Check(TEXT("label Check;"), TEXT("LabelKeyword[label] LabelName[Check] Punctuation[;]"));
	Check(TEXT("goto Hub;"), TEXT("LabelKeyword[goto] LabelName[Hub]"));
	Check(TEXT("return;"), TEXT("JumpKeyword[return]"));
	Check(TEXT("float _h = _who GetPos().z;"), TEXT("Type[float] Global[_h] Operator[=] Global[_who] Call[GetPos] Punctuation[(] Punctuation[)] Punctuation[.] VectorPart[z]"));
	Check(TEXT("_pos.X = 1;"), TEXT("Global[_pos] Punctuation[.] Error[X]"));
	Check(TEXT("int[][] _grid = [[1, 2], [3]];"), TEXT("Type[int[][]] Global[_grid]"));
	Check(TEXT("double _d = 1. + .5 + 12abc + 2.5e-3;"), TEXT("Error[double] Global[_d] Operator[=] Error[1.] Operator[+] Error[.5] Operator[+] Error[12abc] Operator[+] Number[2.5e-3]"));
	Check(TEXT("#define params (entity _speaker, int _n = 1, entity _l = nil)"), TEXT("Preprocessor[#define] Preprocessor[params] Punctuation[(] Type[entity] Global[_speaker]"));
	Check(TEXT("#define params (entity _speaker, int _n = 1, entity _l = nil)"), TEXT("Nil[nil] Punctuation[)]"));
	Check(TEXT("#define LOCTEXT_NAMESPACE \"Quest.Intro\""), TEXT("Preprocessor[#define] Preprocessor[LOCTEXT_NAMESPACE] String[\"Quest.Intro\"]"));
	Check(TEXT("#undef LOCTEXT_NAMESPACE"), TEXT("Preprocessor[#undef] Preprocessor[LOCTEXT_NAMESPACE]"));
	Check(TEXT("//~ if (true)               //@3(20, 20)"), TEXT("DisconnectedMarker[//~] Keyword~[if] Punctuation~[(] Constant~[true] Punctuation~[)] Metadata~[//@3(20, 20)]"));
	Check(TEXT("//~"), TEXT("DisconnectedMarker[//~]"));
	Check(TEXT("                //@7(4, 9) @8(8, 13)"), TEXT("Metadata[//@7(4, 9) @8(8, 13)]"));
	Check(TEXT("// just a comment"), TEXT("Comment[// just a comment]"));
	Check(TEXT("} else if (b) {"), TEXT("Structure[}] Keyword[else] Keyword[if] Punctuation[(] Global[b] Punctuation[)] Structure[{]"));
	Check(TEXT("};"), TEXT("Structure[}] Punctuation[;]"));
	Check(TEXT("forEach (_e in questLog)"), TEXT("Keyword[forEach] Punctuation[(] Global[_e] Keyword[in] Global[questLog]"));
	Check(TEXT("questLog Push(\"x\");"), TEXT("Global[questLog] Builtin[Push]"));
	Check(TEXT("function int AddScore(int _amount)"), TEXT("DeclKeyword[function] Type[int] Call[AddScore] Punctuation[(] Type[int] Global[_amount]"));
	Check(TEXT("if (a & b | c)"), TEXT("Error[&] Global[b] Error[|]"));
	Check(TEXT("Log(\"unterminated);"), TEXT("Call[Log] Punctuation[(] Error[\"unterminated);]"));
	Check(TEXT("_x++; _y -= 2; _z *= 3;"), TEXT("Global[_x] Operator[++] Punctuation[;] Global[_y] Operator[-=]"));
	Check(TEXT("@ john"), TEXT("Error[@] Global[john]"));
	Check(TEXT("Hint(LOCTEXT(\"K\", Format(\"{0}\", 1)), \"{1}\");"), TEXT("LocKey[\"K\"] Punctuation[,] Call[Format] Punctuation[(] String[\"] Placeholder[{0}]"));
	Check(TEXT("Hint(LOCTEXT(\"K\", \"s\"), \"{1}\");"), TEXT("Punctuation[)] Punctuation[,] String[\"] Placeholder[{1}] String[\"]"));
	Check(TEXT("vector3 _p = [1, 2, 3];"), TEXT("Type[vector3] Global[_p]"));
	Check(TEXT("bool _b = !_c != false;"), TEXT("Operator[!] Global[_c] Operator[!=] Constant[false]"));
	Check(TEXT(""), TEXT(""));

	// Disabled nodes: the markers, then code as usual but marked disabled; markers in strings and comments don't count.
	Check(TEXT("/* Log(\"b\"); */    //@2(4, 0)"), TEXT("DisabledMarker[/*] Call-[Log] Punctuation-[(] String-[\"b\"] Punctuation-[)] Punctuation-[;] DisabledMarker[*/] Metadata[//@2(4, 0)]"));
	Check(TEXT("/* /* a(); */ */"), TEXT("DisabledMarker[/*] DisabledMarker[/*] Call-[a]"));
	Check(TEXT("Log(\"/* not */\"); // or /* this"), TEXT("Call[Log] Punctuation[(] String[\"/* not */\"] Punctuation[)] Punctuation[;] Comment[// or /* this]"));
	Check(TEXT("_x = 2 */ 3;"), TEXT("Operator[=] Number[2] DisabledMarker[*/] Number[3]"));
	{
		// A disabled block: the depth carries from line to line.
		int32 Depth = 0;
		const TCHAR* const Lines[] = { TEXT("/*"), TEXT("if (x)"), TEXT("\t/* y(); */"), TEXT("\tz();"), TEXT("*/"), TEXT("w();") };
		const TCHAR* const Expected[] = { TEXT("DisabledMarker[/*]"), TEXT("Keyword-[if] Punctuation-[(] Global-[x] Punctuation-[)]"),
			TEXT("DisabledMarker[/*] Call-[y]"), TEXT("Call-[z] Punctuation-[(]"), TEXT("DisabledMarker[*/]"), TEXT("Call[w] Punctuation[(]") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Lines); ++i)
		{
			const FString Got = BangoLineTokenizerTests::Describe(Lines[i], &Depth);
			if (!Got.Contains(Expected[i], ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(TEXT("Disabled block, line %d: %s\n  want: %s\n  got:  %s"), i + 1, Lines[i], Expected[i], *Got));
			}
		}
		TestEqual(TEXT("Disabled block: closed at the end"), Depth, 0);
	}

	return true;
}

// ---- Hidden metadata (FBangoMetadataTracker) ----

#include "BangoTextMetadata.h"

namespace BangoTextMetadataTests
{
	/** Test text: '|' is a line break and '>' a tab. */
	static FString UV(const TCHAR* Text)
	{
		FString Result(Text);
		Result.ReplaceCharInline(TEXT('|'), TEXT('\n'), ESearchCase::CaseSensitive);
		Result.ReplaceCharInline(TEXT('>'), TEXT('\t'), ESearchCase::CaseSensitive);
		return Result;
	}

	/** The other way: line breaks as '|', tabs as '>'. */
	static FString N(const FString& Text)
	{
		FString Result = Text;
		Result.ReplaceCharInline(TEXT('\n'), TEXT('|'), ESearchCase::CaseSensitive);
		Result.ReplaceCharInline(TEXT('\t'), TEXT('>'), ESearchCase::CaseSensitive);
		Result.ReplaceInline(TEXT("\r"), TEXT(""), ESearchCase::CaseSensitive);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoTextMetadataTest, "Bango.ScriptEditor.TextEditor.HiddenMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoTextMetadataTest::RunTest(const FString& Parameters)
{
	using namespace BangoTextMetadataTests;

	auto U = [](const TCHAR* Text) { return FString(Text); };
	auto Eq = [this](const TCHAR* What, const FString& Got, const TCHAR* Want)
	{
		if (!N(Got).Equals(Want, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("%s\n  want: %s\n  got:  %s"), What, Want, *N(Got)));
		}
	};

	// A tracker, edited step by step: Edit gives the box's new text and the cursor's line, and checks the whole text after.
	struct T
	{
		TFunction<void(const TCHAR*, const FString&, const TCHAR*)> Eq;
		FBangoMetadataTracker K;
		void Start(const TCHAR* Full, const TCHAR* WantVisible, bool bRound = true)
		{
			Eq(TEXT("reset visible"), K.Reset(Full), WantVisible);
			if (bRound)
			{
				Eq(TEXT("roundtrip"), K.GetFullText(), *N(Full));
			}
		}
		void Edit(const TCHAR* What, const TCHAR* Visible, int32 Cursor, const TCHAR* WantFull)
		{
			K.OnVisibleTextChanged(UV(Visible), Cursor);
			Eq(What, K.GetFullText(), WantFull);
		}
	};

	// Split/join
	{ FBangoLineMetadata M; Eq(TEXT("split"), FBangoMetadataTracker::SplitLine(U(TEXT("Log(\"x\");    //@12(4, 7)")), M), TEXT("Log(\"x\");")); Eq(TEXT("meta"), M.Metadata, TEXT("//@12(4, 7)")); Eq(TEXT("spacing"), M.Spacing, TEXT("    ")); }
	{ FBangoLineMetadata M; Eq(TEXT("string with //@"), FBangoMetadataTracker::SplitLine(U(TEXT("Log(\"//@1(0,0)\");")), M), TEXT("Log(\"//@1(0,0)\");")); Eq(TEXT("no meta"), M.Metadata, TEXT("")); }
	{ FBangoLineMetadata M; Eq(TEXT("reroute visible"), FBangoMetadataTracker::SplitLine(U(TEXT("\t                //@7(4, 9) @8(8, 13)")), M), TEXT("")); Eq(TEXT("reroute meta"), M.Metadata, TEXT("//@7(4, 9) @8(8, 13)")); }
	{ FBangoLineMetadata M; Eq(TEXT("disconnected"), FBangoMetadataTracker::SplitLine(U(TEXT("//~ Log(\"d\");    //@2(0, 20)")), M), TEXT("//~ Log(\"d\");")); }
	{ FBangoLineMetadata M; Eq(TEXT("disc reroute"), FBangoMetadataTracker::SplitLine(U(TEXT("//~             //@9(1, 1)")), M), TEXT("//~")); }
	{ FBangoLineMetadata M; Eq(TEXT("comment only"), FBangoMetadataTracker::SplitLine(U(TEXT("// hi //@1(0,0)")), M), TEXT("// hi //@1(0,0)")); }
	{ FBangoLineMetadata M; M.Metadata=U(TEXT("//@1(0, 0)")); Eq(TEXT("join no spacing"), FBangoMetadataTracker::JoinLine(U(TEXT("a/")), M), TEXT("a/    //@1(0, 0)")); }

	T t; t.Eq = Eq;
	t.Start(TEXT("if (x)    //@1(0, 0)\n{\n\tLog(\"a\");    //@2(1, 1)\n};\nLog(\"b\");    //@3(0, 2)\n"), TEXT("if (x)|{|>Log(\"a\");|};|Log(\"b\");|"));
	Eq(TEXT("gutter"), t.K.GetGutterText(), TEXT("//@1(0, 0)||//@2(1, 1)||//@3(0, 2)|"));
	// Typing within a line keeps it.
	t.Edit(TEXT("type"), TEXT("if (x)|{|>Log(\"ab\");|};|Log(\"b\");|"), 2, TEXT("if (x)    //@1(0, 0)|{|>Log(\"ab\");    //@2(1, 1)|};|Log(\"b\");    //@3(0, 2)|"));
	// Enter at the right end of line 2: metadata stays, blank line below.
	t.Edit(TEXT("enter right"), TEXT("if (x)|{|>Log(\"ab\");||};|Log(\"b\");|"), 3, TEXT("if (x)    //@1(0, 0)|{|>Log(\"ab\");    //@2(1, 1)||};|Log(\"b\");    //@3(0, 2)|"));
	// Undo of that
	t.Edit(TEXT("undo enter"), TEXT("if (x)|{|>Log(\"ab\");|};|Log(\"b\");|"), 2, TEXT("if (x)    //@1(0, 0)|{|>Log(\"ab\");    //@2(1, 1)|};|Log(\"b\");    //@3(0, 2)|"));
	// Enter at the left of line 4's code: moves down with it.
	t.Edit(TEXT("enter left"), TEXT("if (x)|{|>Log(\"ab\");|};||Log(\"b\");|"), 5, TEXT("if (x)    //@1(0, 0)|{|>Log(\"ab\");    //@2(1, 1)|};||Log(\"b\");    //@3(0, 2)|"));
	// Enter after the indent of line 2 (left of the code): moves down with the code.
	T u; u.Eq = Eq; u.Start(TEXT("{\n\tLog(\"a\");    //@2(1, 1)\n};"), TEXT("{|>Log(\"a\");|};"));
	u.Edit(TEXT("enter after indent"), TEXT("{|>|Log(\"a\");|};"), 2, TEXT("{|>|Log(\"a\");    //@2(1, 1)|};"));
	// Enter in the middle: left half keeps it.
	T v; v.Eq = Eq; v.Start(TEXT("Log(\"a\"); Log(\"b\");    //@5(0, 0)\nX();    //@6(0, 1)"), TEXT("Log(\"a\"); Log(\"b\");|X();"));
	v.Edit(TEXT("split"), TEXT("Log(\"a\"); |Log(\"b\");|X();"), 1, TEXT("Log(\"a\");     //@5(0, 0)|Log(\"b\");|X();    //@6(0, 1)"));
	// Undo restores exactly
	v.Edit(TEXT("undo split"), TEXT("Log(\"a\"); Log(\"b\");|X();"), 0, TEXT("Log(\"a\"); Log(\"b\");    //@5(0, 0)|X();    //@6(0, 1)"));
	// Join (backspace at start of X): upper keeps, lower's dropped.
	v.Edit(TEXT("join"), TEXT("Log(\"a\"); Log(\"b\");X();"), 0, TEXT("Log(\"a\"); Log(\"b\");X();    //@5(0, 0)"));
	// Join where upper has none: lower's kept.
	T w; w.Eq = Eq; w.Start(TEXT("A();\nB();    //@9(0, 0)"), TEXT("A();|B();"));
	w.Edit(TEXT("join lower"), TEXT("A();B();"), 0, TEXT("A();B();    //@9(0, 0)"));
	// Delete a line: its metadata goes.
	T x; x.Eq = Eq; x.Start(TEXT("A();    //@1(0, 0)\nB();    //@2(0, 1)\nC();    //@3(0, 2)"), TEXT("A();|B();|C();"));
	x.Edit(TEXT("delete line"), TEXT("A();|C();"), 1, TEXT("A();    //@1(0, 0)|C();    //@3(0, 2)"));
	// Paste-replace: matching text keeps, others drop.
	T y; y.Eq = Eq; y.Start(TEXT("A();    //@1(0, 0)\nB();    //@2(0, 1)\nC();    //@3(0, 2)"), TEXT("A();|B();|C();"));
	y.Edit(TEXT("paste"), TEXT("Z();|Q();|B();|R();|C();"), 3, TEXT("Z();|Q();|B();    //@2(0, 1)|R();|C();    //@3(0, 2)"));
	// Trailing comment drops metadata.
	T z; z.Eq = Eq; z.Start(TEXT("A();    //@1(0, 0)"), TEXT("A();"));
	z.Edit(TEXT("comment"), TEXT("A(); // hi"), 0, TEXT("A(); // hi"));
	// Blank lines with reroutes: Enter at end of statement before a reroute line.
	T r; r.Eq = Eq; r.Start(TEXT("x();    //@1(0, 0)\n        //@4(2, 0)\ny();    //@2(4, 0)"), TEXT("x();||y();"));
	r.Edit(TEXT("enter before reroute"), TEXT("x();|||y();"), 1, TEXT("x();    //@1(0, 0)||        //@4(2, 0)|y();    //@2(4, 0)"));
	// Windows line endings are read too.
	T c; c.Eq = Eq; c.Start(TEXT("A();    //@1(0, 0)\r\nB();"), TEXT("A();|B();"), false); Eq(TEXT("crlf"), c.K.GetFullText(), TEXT("A();    //@1(0, 0)|B();"));
	T d; d.Eq = Eq; d.Start(TEXT("A();    //@1(0, 0)\nA();    //@2(0, 1)"), TEXT("A();|A();"));
	d.Edit(TEXT("same lines, enter at start of 2nd"), TEXT("A();||A();"), 2, TEXT("A();    //@1(0, 0)||A();    //@2(0, 1)"));
	T e; e.Eq = Eq; e.Start(TEXT("A();    //@1(0, 0)\nA();    //@2(0, 1)"), TEXT("A();|A();"));
	e.Edit(TEXT("same lines, enter at start of 1st"), TEXT("|A();|A();"), 1, TEXT("|A();    //@1(0, 0)|A();    //@2(0, 1)"));
	T f; f.Eq = Eq; f.Start(TEXT("A();    //@1(0, 0)\nB();    //@2(0, 1)\nC();    //@3(0, 2)\nD();    //@4(0, 3)"), TEXT("A();|B();|C();|D();"));
	f.Edit(TEXT("select B..C delete"), TEXT("A();|D();"), 1, TEXT("A();    //@1(0, 0)|D();    //@4(0, 3)"));

	return true;
}

// ---- Locals, parameters and globals (FBangoScriptNames) ----

#include "BangoScriptNames.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoScriptNamesTest, "Bango.ScriptEditor.TextEditor.ScriptNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoScriptNamesTest::RunTest(const FString& Parameters)
{
	const TCHAR* Source =
		TEXT("#define params (entity speaker, int _rounds = 3)\n")
		TEXT("#define LOCTEXT_NAMESPACE \"Q\"\n")
		TEXT("// ============ GLOBAL VARS ====================\n")
		TEXT("global int foundThings;\n")
		TEXT("global string[] questLog;\n")
		TEXT("int count = 0;\n")
		TEXT("string _name;\n")
		TEXT("if (IsNil(foundThings) && otherGlobal)    //@1(0, 0)\n")
		TEXT("{\n")
		TEXT("\tcount += _rounds;\n")
		TEXT("};\n")
		TEXT("forEach (entry in questLog)\n")
		TEXT("{\n")
		TEXT("\tLog(entry);\n")
		TEXT("};\n")
		TEXT("for (int i = 0; i < count; i++)\n")
		TEXT("{\n")
		TEXT("};\n")
		TEXT("function int AddScore(int amount,\n")
		TEXT("\tentity who)\n")
		TEXT("{\n")
		TEXT("\tint count;\n")
		TEXT("\tcount = amount + foundThings + speaker;\n")
		TEXT("\tif (true)\n")
		TEXT("\t{\n")
		TEXT("\t};\n")
		TEXT("\treturn count;\n")
		TEXT("};\n")
		TEXT("Log(count);\n");

	TArray<FString> Lines;
	FBangoMetadataTracker::SplitLines(Source, Lines);
	TArray<TArray<FBangoSyntaxToken>> Tokens;
	Tokens.SetNum(Lines.Num());
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[I]), Tokens[I]);
	}

	FBangoScriptNames Names;
	Names.Build(Lines, Tokens);

	// Line numbers from 1, as in an editor.
	auto Check = [this, &Names](int32 Line, const TCHAR* Name, EBangoSyntaxKind Want)
	{
		const EBangoSyntaxKind Got = Names.Classify(Line - 1, Name);
		if (Got != Want)
		{
			AddError(FString::Printf(TEXT("Line %d, '%s': want %s, got %s"), Line, Name,
				BangoLineTokenizerTests::KindName(Want), BangoLineTokenizerTests::KindName(Got)));
		}
	};

	using K = EBangoSyntaxKind;
	Check(1, TEXT("speaker"), K::Parameter);
	Check(1, TEXT("_rounds"), K::Parameter);
	Check(4, TEXT("foundThings"), K::Global);
	Check(6, TEXT("count"), K::Local);
	Check(7, TEXT("_name"), K::Local);
	Check(8, TEXT("foundThings"), K::Global);
	Check(8, TEXT("otherGlobal"), K::UndeclaredGlobal);
	Check(10, TEXT("count"), K::Local);
	Check(10, TEXT("_rounds"), K::Parameter);
	Check(12, TEXT("entry"), K::Local);
	Check(12, TEXT("questLog"), K::Global);
	Check(14, TEXT("entry"), K::Local);
	Check(16, TEXT("i"), K::Local);
	Check(19, TEXT("amount"), K::Parameter);
	Check(20, TEXT("who"), K::Parameter);                 // a function header over two lines
	Check(22, TEXT("count"), K::Local);
	Check(23, TEXT("amount"), K::Parameter);
	Check(23, TEXT("foundThings"), K::Global);
	Check(23, TEXT("speaker"), K::UndeclaredGlobal);      // functions can't see the script's parameters
	Check(27, TEXT("count"), K::Local);
	Check(29, TEXT("count"), K::Local);                   // the main graph again, after the function's };
	Check(29, TEXT("amount"), K::UndeclaredGlobal);

	return true;
}

// ---- Editing comforts (FBangoEditingAids) ----

#include "BangoEditingAids.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoEditingAidsTest, "Bango.ScriptEditor.TextEditor.EditingAids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoEditingAidsTest::RunTest(const FString& Parameters)
{
	using namespace BangoTextMetadataTests; // UV: '>' is a tab; N: line breaks as '|', tabs as '>'

	// Enter at a column of a line: what's typed, and the cursor's column on the new line.
	auto NewLine = [this](const TCHAR* Line, int32 Column, const TCHAR* WantInsert, int32 WantColumn)
	{
		const FBangoNewLineEdit Edit = FBangoEditingAids::NewLine(UV(Line), Column);
		if (!N(Edit.Insert).Equals(WantInsert, ESearchCase::CaseSensitive) || Edit.CursorColumn != WantColumn)
		{
			AddError(FString::Printf(TEXT("NewLine('%s', %d): want '%s' at %d, got '%s' at %d"), Line, Column, WantInsert, WantColumn, *N(Edit.Insert), Edit.CursorColumn));
		}
	};
	NewLine(TEXT(">Log(1);"), 8, TEXT("|>"), 1);          // keeps the indentation
	NewLine(TEXT(">>if (x)"), 8, TEXT("|>>"), 2);
	NewLine(TEXT(">if (x) {"), 9, TEXT("|>>"), 2);        // one tab more after {
	NewLine(TEXT("{"), 1, TEXT("|>"), 1);
	NewLine(TEXT(">{   // comment"), 2, TEXT("|>>"), 2);
	NewLine(TEXT(">{ // c"), 7, TEXT("|>>"), 2);          // a comment after the { doesn't count
	NewLine(TEXT(">{}"), 2, TEXT("|>>|>"), 2);            // {|}: the } on its own line
	NewLine(TEXT(">{};"), 2, TEXT("|>>|>"), 2);
	NewLine(TEXT(">>foo"), 1, TEXT("|>"), 1);             // inside the indentation
	NewLine(TEXT("Log(\"{\");"), 9, TEXT("|"), 0);        // a { in a string isn't one
	NewLine(TEXT("//~ >Log();"), 11, TEXT("|"), 0);       // a disconnected line has no leading indentation

	auto Outdent = [this](const TCHAR* Line, int32 Column, int32 Want)
	{
		const int32 Got = FBangoEditingAids::OutdentBeforeClosingBrace(UV(Line), Column);
		if (Got != Want)
		{
			AddError(FString::Printf(TEXT("OutdentBeforeClosingBrace('%s', %d): want %d, got %d"), Line, Column, Want, Got));
		}
	};
	Outdent(TEXT(">>"), 2, 1);
	Outdent(TEXT(">"), 1, 0);
	Outdent(TEXT(">x"), 2, INDEX_NONE);
	Outdent(TEXT(""), 0, INDEX_NONE);
	Outdent(TEXT(">  "), 3, INDEX_NONE);                 // ends in spaces, not a tab
	Outdent(TEXT("  >"), 3, 2);

	TArray<FString> Lines;
	FBangoMetadataTracker::SplitLines(UV(TEXT("if (a[1] == (2))|{|>Log(\"}\");  // }|>{|>};|};|//~ if (b)|//~ {|//~ };")), Lines);
	TArray<TArray<FBangoSyntaxToken>> Tokens;
	Tokens.SetNum(Lines.Num());
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[I]), Tokens[I]);
	}

	// The cursor at a line and column (from 0): whether it's next to a bracket, whether that has a match, and where.
	auto Bracket = [this, &Lines, &Tokens](int32 Line, int32 Column, bool bWantFound, bool bWantMatched, int32 MatchLine, int32 MatchColumn)
	{
		FBangoTextPosition Cursor;
		Cursor.Line = Line;
		Cursor.Column = Column;
		const FBangoBracketMatch Got = FBangoEditingAids::FindMatchingBracket(Lines, Tokens, Cursor);
		const bool bOk = Got.bFound == bWantFound
			&& (!bWantFound || (Got.bMatched == bWantMatched && (!bWantMatched || (Got.Match.Line == MatchLine && Got.Match.Column == MatchColumn))));
		if (!bOk)
		{
			AddError(FString::Printf(TEXT("Bracket at %d:%d: found %d, matched %d at %d:%d"), Line, Column, Got.bFound, Got.bMatched, Got.Match.Line, Got.Match.Column));
		}
	};
	Bracket(0, 3, true, true, 0, 15);    // before the ( of "if (": its )
	Bracket(0, 4, true, true, 0, 15);    // after it
	Bracket(0, 6, true, true, 0, 7);     // [ ]
	Bracket(0, 16, true, true, 0, 3);    // after the last ): back to the first (
	Bracket(1, 0, true, true, 5, 0);     // { to the }; four lines down, past the braces in a string and a comment
	Bracket(3, 2, true, true, 4, 1);     // the inner { }
	Bracket(5, 1, true, true, 1, 0);     // after the } of };
	Bracket(2, 1, false, false, 0, 0);   // no bracket next to the cursor
	Bracket(7, 5, true, true, 8, 4);     // a disconnected { matches a disconnected }

	TArray<FString> Unmatched;
	Unmatched.Add(TEXT("if ((x)"));
	TArray<TArray<FBangoSyntaxToken>> UnmatchedTokens;
	UnmatchedTokens.SetNum(1);
	FBangoLineTokenizer::TokenizeLine(FStringView(Unmatched[0]), UnmatchedTokens[0]);
	FBangoTextPosition Cursor;
	Cursor.Column = 3;
	const FBangoBracketMatch Got = FBangoEditingAids::FindMatchingBracket(Unmatched, UnmatchedTokens, Cursor);
	TestTrue(TEXT("An unclosed ( is found, with no match"), Got.bFound && !Got.bMatched);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
