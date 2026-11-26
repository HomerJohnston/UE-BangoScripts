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
			TEXT("LocNotes"), TEXT("VectorPart"), TEXT("Call"), TEXT("Comment"), TEXT("Metadata"),
			TEXT("DisconnectedMarker"), TEXT("DisabledMarker"), TEXT("Structure"), TEXT("Operator"), TEXT("Punctuation"), TEXT("Error"),
			TEXT("Command"), TEXT("Function"), TEXT("Parameter"), TEXT("UndeclaredGlobal"), TEXT("Define"),
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

	Check(TEXT("global int foundThings ?= 0;"), TEXT("DeclKeyword[global] Type[int] Global[foundThings] Operator[?=] Number[0] Punctuation[;]"));
	Check(TEXT("\tglobal bool opened = true;      //@1(0, 0)"), TEXT("DeclKeyword[global] Type[bool] Global[opened] Operator[=] Constant[true] Punctuation[;] Metadata[//@1(0, 0)]"));
	Check(TEXT("if (isNil(foundThings) && foundThings >= 10)"), TEXT("Keyword[if] Punctuation[(] Keyword[isNil] Punctuation[(] Global[foundThings] Punctuation[)] Operator[&&] Global[foundThings] Operator[>=] Number[10]"));
	Check(TEXT("\tHint(TEXT(\"Countdown\", \"Countdown: {0}\"), 3 - _i);"), TEXT("Call[Hint] Punctuation[(] LocMacro[TEXT] Punctuation[(] LocKey[\"Countdown\"] Punctuation[,] String[\"Countdown: ] Placeholder[{0}] String[\"] Punctuation[)] Punctuation[,] Number[3] Operator[-] Global[_i]"));
	Check(TEXT("Hint(TEXT_NS(\"Ns\", \"K\", \"Hi {Name} `{\", \"note\"));"), TEXT("LocKey[\"Ns\"] Punctuation[,] LocKey[\"K\"] Punctuation[,] String[\"Hi ] Error[{Name}] String[ ] StringEscape[`{] String[\"] Punctuation[,] LocNotes[\"note\"]"));
	Check(TEXT("Hint(TEXT_INV(\"Score {0}\"), 1);"), TEXT("Call[Hint] Punctuation[(] LocMacro[TEXT_INV] Punctuation[(] String[\"Score ] Placeholder[{0}] String[\"] Punctuation[)] Punctuation[,] Number[1]"));
	Check(TEXT("Log(\"{0} and `{ \\n \\q {Name}\", _a);"), TEXT("String[\"] Placeholder[{0}] String[ and ] StringEscape[`{] String[ ] StringEscape[\\n] String[ ] Error[\\q] String[ {Name}\"]"));
	Check(TEXT("@john Speak(\"Hi\");"), TEXT("Entity[@john] Call[Speak]"));
	Check(TEXT("label Check;"), TEXT("LabelKeyword[label] LabelName[Check] Punctuation[;]"));
	Check(TEXT("goto Hub;"), TEXT("LabelKeyword[goto] LabelName[Hub]"));
	Check(TEXT("return;"), TEXT("JumpKeyword[return]"));
	Check(TEXT("float _h = _who GetPos().z;"), TEXT("Type[float] Global[_h] Operator[=] Global[_who] Call[GetPos] Punctuation[(] Punctuation[)] Punctuation[.] VectorPart[z]"));
	Check(TEXT("_pos.X = 1;"), TEXT("Global[_pos] Punctuation[.] Error[X]"));
	Check(TEXT("int[][] _grid = [[1, 2], [3]];"), TEXT("Type[int[][]] Global[_grid]"));
	Check(TEXT("float _d = 1. + .5 + 12abc + 2.5e-3;"), TEXT("Type[float] Global[_d] Operator[=] Error[1.] Operator[+] Error[.5] Operator[+] Error[12abc] Operator[+] Number[2.5e-3]"));
	Check(TEXT("#inputs (entity _speaker, int _n = 1, entity _l = nil)"), TEXT("Preprocessor[#inputs] Punctuation[(] Type[entity] Global[_speaker]"));
	Check(TEXT("#inputs (entity _speaker, int _n = 1, entity _l = nil)"), TEXT("Nil[nil] Punctuation[)]"));
	Check(TEXT("#TEXT_NAMESPACE \"Quest.Intro\""), TEXT("Preprocessor[#TEXT_NAMESPACE] String[\"Quest.Intro\"]"));
	Check(TEXT("#USES_GRAPH_EDITOR"), TEXT("Preprocessor[#USES_GRAPH_EDITOR]"));
	Check(TEXT("//~ if (true)               //@3(20, 20)"), TEXT("DisconnectedMarker[//~] Keyword~[if] Punctuation~[(] Constant~[true] Punctuation~[)] Metadata~[//@3(20, 20)]"));
	Check(TEXT("//~"), TEXT("DisconnectedMarker[//~]"));
	Check(TEXT("                //@7(4, 9) @8(8, 13)"), TEXT("Metadata[//@7(4, 9) @8(8, 13)]"));
	Check(TEXT("// just a comment"), TEXT("Comment[// just a comment]"));
	Check(TEXT("} else if (b) {"), TEXT("Structure[}] Keyword[else] Keyword[if] Punctuation[(] Global[b] Punctuation[)] Structure[{]"));
	Check(TEXT("};"), TEXT("Structure[}] Punctuation[;]"));
	Check(TEXT("forEach (_e in questLog)"), TEXT("Keyword[forEach] Punctuation[(] Global[_e] Keyword[in] Global[questLog]"));
	Check(TEXT("questLog Push(\"x\");"), TEXT("Global[questLog] Call[Push]"));
	Check(TEXT("function int AddScore(int _amount)"), TEXT("DeclKeyword[function] Type[int] Call[AddScore] Punctuation[(] Type[int] Global[_amount]"));
	Check(TEXT("if (a & b | c)"), TEXT("Error[&] Global[b] Error[|]"));
	Check(TEXT("Log(\"unterminated);"), TEXT("Call[Log] Punctuation[(] Error[\"unterminated);]"));
	Check(TEXT("_x++; _y -= 2; _z *= 3;"), TEXT("Global[_x] Operator[++] Punctuation[;] Global[_y] Operator[-=]"));
	Check(TEXT("@ john"), TEXT("Error[@] Global[john]"));
	Check(TEXT("Hint(TEXT(\"K\", Format(\"{0}\", 1)), \"{1}\");"), TEXT("LocKey[\"K\"] Punctuation[,] Call[Format] Punctuation[(] String[\"] Placeholder[{0}]"));
	Check(TEXT("Hint(TEXT(\"K\", \"s\"), \"{1}\");"), TEXT("Punctuation[)] Punctuation[,] String[\"] Placeholder[{1}] String[\"]"));
	Check(TEXT("vector3 _p = [1, 2, 3];"), TEXT("Type[vector3] Global[_p]"));
	Check(TEXT("bool _b = !_c != false;"), TEXT("Operator[!] Global[_c] Operator[!=] Constant[false]"));
	Check(TEXT(""), TEXT(""));
	Check(TEXT("\tcase 1, 2:"), TEXT("Keyword[case] Number[1] Punctuation[,] Number[2] Punctuation[:]"));
	Check(TEXT("\tdefault:"), TEXT("Keyword[default] Punctuation[:]"));

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
		TEXT("#inputs (entity speaker, int _rounds = 3)\n")
		TEXT("#TEXT_NAMESPACE \"Q\"\n")
		TEXT("// ============ GLOBAL VARS ====================\n")
		TEXT("global int foundThings;\n")
		TEXT("global string[] questLog;\n")
		TEXT("scriptVar int count = 0;\n")
		TEXT("string _name;\n")
		TEXT("if (isNil(foundThings) && otherGlobal)    //@1(0, 0)\n")
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
	Check(23, TEXT("speaker"), K::Parameter);             // functions see the script's parameters
	Check(27, TEXT("count"), K::Local);
	Check(29, TEXT("count"), K::Local);                   // the main graph again, after the function's };
	Check(29, TEXT("amount"), K::UndeclaredGlobal);
	Check(19, TEXT("entry"), K::UndeclaredGlobal);        // loop variables only exist in their loop
	Check(29, TEXT("i"), K::UndeclaredGlobal);

	// Block scope: a local is visible from its line to the } that closes its block; the inner one of two wins; a
	// spawn's declared id is visible after the spawn's block.
	const TCHAR* Scoped =
		TEXT("global int gold;\n")                 // 1
		TEXT("Talk(\"x\");\n")                     // 2
		TEXT("int _a = 1;\n")                      // 3
		TEXT("if (true)\n")                        // 4
		TEXT("{\n")                                // 5
		TEXT("\tfloat gold = 2.5;\n")              // 6
		TEXT("\tstring _a = \"x\";\n")             // 7
		TEXT("\tTalk(_a);\n")                      // 8
		TEXT("}\n")                                // 9
		TEXT("else\n")                             // 10
		TEXT("{\n")                                // 11
		TEXT("\tbool _b = true;\n")                // 12
		TEXT("}\n")                                // 13
		TEXT("int _t = spawn\n")                   // 14
		TEXT("{\n")                                // 15
		TEXT("\tSleep(1);\n")                      // 16
		TEXT("}\n")                                // 17
		TEXT("Talk(\"{0}\", _t);\n")               // 18
		TEXT("function void F()\n")                // 19
		TEXT("{\n")                                // 20
		TEXT("\tTalk(\"f\");\n")                   // 21
		TEXT("\tint _late = 3;\n")                 // 22
		TEXT("\tTalk(\"{0}\", _late);\n")          // 23
		TEXT("}\n");                               // 24
	FBangoMetadataTracker::SplitLines(Scoped, Lines);
	Tokens.Reset();
	Tokens.SetNum(Lines.Num());
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[I]), Tokens[I]);
	}
	Names.Build(Lines, Tokens);
	Check(2, TEXT("_a"), K::UndeclaredGlobal);            // before its declaration
	Check(3, TEXT("_a"), K::Local);
	Check(6, TEXT("gold"), K::Local);                     // a local hides a global
	Check(12, TEXT("gold"), K::Global);                   // but not in the sibling block
	Check(12, TEXT("_b"), K::Local);
	Check(14, TEXT("_b"), K::UndeclaredGlobal);
	Check(16, TEXT("_t"), K::UndeclaredGlobal);           // not in the spawn's own block
	Check(18, TEXT("_t"), K::Local);
	Check(21, TEXT("_late"), K::UndeclaredGlobal);
	Check(23, TEXT("_late"), K::Local);
	TArray<FBangoVisibleName> Visible;
	Names.GetVisibleNames(7, Visible);                    // line 8
	const FBangoVisibleName* InnerA = Visible.FindByPredicate([](const FBangoVisibleName& Name) { return Name.Name.Equals(TEXT("_a"), ESearchCase::CaseSensitive); });
	TestTrue(TEXT("The inner _a is the visible one"), InnerA != nullptr && InnerA->Type.Equals(TEXT("string"), ESearchCase::CaseSensitive));
	Names.GetVisibleNames(17, Visible);                   // line 18
	TestFalse(TEXT("A block's local isn't offered after the block"), Visible.ContainsByPredicate([](const FBangoVisibleName& Name) { return Name.Name.Equals(TEXT("_b"), ESearchCase::CaseSensitive); }));

	// Variable keys (for highlighting a variable's other uses): the same variable, not just the same name.
	auto SameVariable = [&Names](int32 LineA, int32 LineB, const TCHAR* Name)
	{
		return Names.GetVariableKey(LineA - 1, Name) == Names.GetVariableKey(LineB - 1, Name);
	};
	TestFalse(TEXT("The outer and inner _a are different variables"), SameVariable(3, 8, TEXT("_a")));
	TestTrue(TEXT("The inner _a's declaration and use are one variable"), SameVariable(7, 8, TEXT("_a")));
	TestFalse(TEXT("The local gold isn't the global gold"), SameVariable(6, 1, TEXT("gold")));
	TestTrue(TEXT("The global gold, either side of the block that hides it"), SameVariable(1, 12, TEXT("gold")));
	TestTrue(TEXT("A spawn's id: its declaration and its use after the block"), SameVariable(14, 18, TEXT("_t")));

	// Functions see the script's parameters and script variables, not its locals.
	const TCHAR* Shared =
		TEXT("#inputs (int _in)\n")                 // 1
		TEXT("scriptVar int _score = 0;\n")         // 2
		TEXT("int _plain = 1;\n")                   // 3
		TEXT("function void F(int _p)\n")           // 4
		TEXT("{\n")                                 // 5
		TEXT("\t_score += _in + _plain;\n")         // 6
		TEXT("}\n");                                // 7
	FBangoMetadataTracker::SplitLines(Shared, Lines);
	Tokens.Reset();
	Tokens.SetNum(Lines.Num());
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		FBangoLineTokenizer::TokenizeLine(FStringView(Lines[I]), Tokens[I]);
	}
	Names.Build(Lines, Tokens);
	Check(6, TEXT("_score"), K::Local);
	Check(6, TEXT("_in"), K::Parameter);
	Check(6, TEXT("_plain"), K::UndeclaredGlobal);
	TestTrue(TEXT("A script variable in a function is the script's"), SameVariable(2, 6, TEXT("_score")));
	TestTrue(TEXT("A script input in a function is the script's"), SameVariable(1, 6, TEXT("_in")));
	Visible.Reset();
	Names.GetVisibleNames(5, Visible);                    // line 6
	TestTrue(TEXT("A function is offered the script's variables"), Visible.ContainsByPredicate([](const FBangoVisibleName& Name) { return Name.Name.Equals(TEXT("_score"), ESearchCase::CaseSensitive); })
		&& Visible.ContainsByPredicate([](const FBangoVisibleName& Name) { return Name.Name.Equals(TEXT("_in"), ESearchCase::CaseSensitive); })
		&& !Visible.ContainsByPredicate([](const FBangoVisibleName& Name) { return Name.Name.Equals(TEXT("_plain"), ESearchCase::CaseSensitive); }));

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

// ---- Autocomplete (FBangoCompletion) ----

#include "BangoCompletion.h"
#include "BangoDefines.h"
#include "BangoSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoCompletionTest, "Bango.ScriptEditor.TextEditor.Completion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoCompletionTest::RunTest(const FString& Parameters)
{
	using namespace BangoTextMetadataTests; // UV: '>' is a tab, '|' a line break

	// The word before the cursor: where it starts, or INDEX_NONE where nothing should be offered.
	auto Word = [this](const TCHAR* Line, int32 Column, int32 WantStart)
	{
		int32 Start = INDEX_NONE;
		const bool bFound = FBangoCompletion::FindWordBeforeCursor(UV(Line), Column, Start);
		const int32 Got = bFound ? Start : INDEX_NONE;
		if (Got != WantStart)
		{
			AddError(FString::Printf(TEXT("FindWordBeforeCursor('%s', %d): want %d, got %d"), Line, Column, WantStart, Got));
		}
	};
	Word(TEXT("Lo"), 2, 0);
	Word(TEXT(">Log(co"), 7, 5);
	Word(TEXT(">x = Ra"), 7, 5);
	Word(TEXT(">"), 1, INDEX_NONE);              // nothing typed
	Word(TEXT("x = 12"), 6, INDEX_NONE);         // a number
	Word(TEXT("@jo"), 3, INDEX_NONE);            // a Bango ID
	Word(TEXT("_pos.x"), 6, INDEX_NONE);         // a vector part
	Word(TEXT("Log(\"He"), 7, INDEX_NONE);       // in a string
	Word(TEXT("// Lo"), 5, INDEX_NONE);          // in a comment
	Word(TEXT("int coun"), 8, INDEX_NONE);       // a name being declared
	Word(TEXT("goto Hu"), 7, INDEX_NONE);        // a label
	Word(TEXT("questLog Pu"), 11, 9);            // a targeted command
	Word(TEXT("//~ Lo"), 6, 4);                  // a disconnected line

	// Where a waitUntil's `timeout` goes: straight after its condition's `)`, and nowhere else.
	TestTrue(TEXT("timeout is offered after a waitUntil's condition"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready) ti"), 18));
	TestTrue(TEXT("timeout is offered after a condition with brackets in it"), FBangoCompletion::IsTimeoutPlace(TEXT("\twaitUntil (Count(found) > (2 + 1))ti"), 35));
	TestFalse(TEXT("timeout isn't offered inside the condition"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready && t"), 20));
	TestFalse(TEXT("timeout isn't offered after the ';'"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready); t"), 19));
	TestFalse(TEXT("timeout isn't offered after an if"), FBangoCompletion::IsTimeoutPlace(TEXT("if (ready) t"), 11));
	TestEqual(TEXT("the timeout item"), FBangoCompletion::GetTimeoutItem().Name, FString(TEXT("timeout")));
	// Script paths, as the Script Assets pane writes them: relative in the caller's folder or below, else full.
	TestEqual(TEXT("a script beside the caller"), FBangoCompletion::MakeScriptPath(TEXT("/Game/Quests/Start"), TEXT("/Game/Quests/Intro")), FString(TEXT("Intro")));
	TestEqual(TEXT("a script below the caller's folder"), FBangoCompletion::MakeScriptPath(TEXT("/Game/Quests/Start"), TEXT("/Game/Quests/Side/Intro")), FString(TEXT("Side/Intro")));
	TestEqual(TEXT("a script elsewhere is a full path"), FBangoCompletion::MakeScriptPath(TEXT("/Game/Quests/Start"), TEXT("/Game/Shared/Greet")), FString(TEXT("/Game/Shared/Greet")));
	TestEqual(TEXT("a folder whose name only starts the same"), FBangoCompletion::MakeScriptPath(TEXT("/Game/Quests/Start"), TEXT("/Game/QuestsOld/Intro")), FString(TEXT("/Game/QuestsOld/Intro")));
	TestEqual(TEXT("from an inline script, a full path"), FBangoCompletion::MakeScriptPath(TEXT("Door.OnOpened"), TEXT("/Game/Quests/Intro")), FString(TEXT("/Game/Quests/Intro")));
	TestEqual(TEXT("a script path goes in with quotes"), FBangoCompletion::MakeScriptPathInsert(TEXT("Exec("), 5, TEXT("Intro")), FString(TEXT("\"Intro\"")));
	TestEqual(TEXT("a script path inside quotes goes in alone"), FBangoCompletion::MakeScriptPathInsert(TEXT("Exec(\"\")"), 6, TEXT("Intro")), FString(TEXT("Intro")));
	// heldFor goes straight after the condition, and timeout can follow it.
	TestTrue(TEXT("timeout is offered after a heldFor"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready) heldFor (2) ti"), 30));
	TestFalse(TEXT("timeout isn't offered inside a heldFor"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready) heldFor (t"), 27));
	TestFalse(TEXT("nothing is offered after the timeout"), FBangoCompletion::IsTimeoutPlace(TEXT("waitUntil (ready) heldFor (2) timeout (3) "), 42));
	TestTrue(TEXT("heldFor is offered after a waitUntil's condition"), FBangoCompletion::IsHoldPlace(TEXT("waitUntil (ready) he"), 18));
	TestFalse(TEXT("heldFor isn't offered after a heldFor"), FBangoCompletion::IsHoldPlace(TEXT("waitUntil (ready) heldFor (2) he"), 30));
	{
		TArray<FBangoCompletionItem> Both;
		TArray<FBangoCompletionItem> One;
		TArray<FBangoCompletionItem> None;
		FBangoCompletion::GetWaitWordItems(TEXT("waitUntil (ready) "), 18, Both);
		FBangoCompletion::GetWaitWordItems(TEXT("waitUntil (ready) heldFor (2) "), 30, One);
		FBangoCompletion::GetWaitWordItems(TEXT("waitUntil (ready && "), 19, None);
		TestTrue(TEXT("after the condition, the list is heldFor and timeout"), Both.Num() == 2 && Both[0].Name == TEXT("heldFor") && Both[1].Name == TEXT("timeout"));
		TestTrue(TEXT("after a heldFor, the list is timeout"), One.Num() == 1 && One[0].Name == TEXT("timeout"));
		TestEqual(TEXT("elsewhere, there are no such words"), None.Num(), 0);
		const FBangoCompletionInsert HoldInsert = FBangoCompletion::MakeInsert(FBangoCompletion::GetHoldItem(), TEXT("waitUntil (ready) he"), 20);
		TestTrue(TEXT("heldFor goes in with its bracket, left open"), HoldInsert.Text == TEXT("heldFor (") && HoldInsert.bOpenParen);
	}
	{
		const FBangoCompletionInsert TimeoutInsert = FBangoCompletion::MakeInsert(FBangoCompletion::GetTimeoutItem(), TEXT("waitUntil (ready) ti"), 20);
		TestTrue(TEXT("timeout goes in with its bracket, left open"), TimeoutInsert.Text == TEXT("timeout (") && TimeoutInsert.bOpenParen);
		const FBangoCompletionInsert TimeoutBefore = FBangoCompletion::MakeInsert(FBangoCompletion::GetTimeoutItem(), TEXT("waitUntil (ready) ti (5)"), 20);
		TestTrue(TEXT("timeout goes in alone before what's already there"), TimeoutBefore.Text == TEXT("timeout") && !TimeoutBefore.bOpenParen);

		// Keywords with a header go in the same way; `for` without the faint bracket, and the others as they are.
		TArray<FBangoCompletionItem> KeywordItems;
		FBangoCompletion::GetKeywordItems(KeywordItems);
		int32 NumHeaders = 0;
		for (const FBangoCompletionItem& Keyword : KeywordItems)
		{
			const FBangoCompletionInsert KeywordInsert = FBangoCompletion::MakeInsert(Keyword, Keyword.Name.Left(1), 1);
			if (Keyword.bHeader)
			{
				++NumHeaders;
				TestTrue(*FString::Printf(TEXT("'%s' goes in with its bracket"), *Keyword.Name),
					KeywordInsert.Text == Keyword.Name + TEXT(" (") && KeywordInsert.bOpenParen == (Keyword.Name != TEXT("for")));
				TestTrue(*FString::Printf(TEXT("'%s' goes in alone before a bracket"), *Keyword.Name),
					FBangoCompletion::MakeInsert(Keyword, Keyword.Name.Left(1) + TEXT("  (x)"), 1).Text == Keyword.Name);
			}
			else if (Keyword.bCallable)
			{
				// isNil and the text macros (TEXT, TEXT_NS, TEXT_INV)
				TestTrue(*FString::Printf(TEXT("'%s' goes in with its bracket, like a call"), *Keyword.Name),
					(Keyword.Name == TEXT("isNil") || Keyword.Name.StartsWith(TEXT("TEXT"), ESearchCase::CaseSensitive)) && KeywordInsert.Text == Keyword.Name + TEXT("(") && KeywordInsert.bOpenParen);
			}
			else
			{
				TestTrue(*FString::Printf(TEXT("'%s' goes in as it is"), *Keyword.Name), KeywordInsert.Text == Keyword.Name && !KeywordInsert.bOpenParen);
			}
		}
		TestEqual(TEXT("keywords with a header: if, while, for, forEach, waitUntil, switch"), NumHeaders, 6);
	}

	FBangoMetadataTracker Tracker;
	Tracker.Reset(UV(TEXT("#inputs (entity speaker, int rounds = 3)|global int foundThings;|global string count;|int count = 0;|fo|function int AddScore(int amount,|>entity who)|{|>am|};")));
	TArray<TArray<FBangoSyntaxToken>> Tokens;
	Tokens.SetNum(Tracker.GetLines().Num());
	for (int32 I = 0; I < Tracker.GetLines().Num(); ++I)
	{
		FBangoLineTokenizer::TokenizeLine(FStringView(Tracker.GetLines()[I]), Tokens[I]);
	}
	FBangoScriptNames Names;
	Names.Build(Tracker.GetLines(), Tokens);

	TArray<FBangoCompletionItem> Commands;
	const TCHAR* const CommandNames[] = { TEXT("Log"), TEXT("Format"), TEXT("foo") };
	for (const TCHAR* Name : CommandNames)
	{
		FBangoCompletionItem Item;
		Item.Name = Name;
		Item.Kind = EBangoCompletionKind::Command;
		Item.bCallable = true;
		Commands.Add(Item);
	}

	// In the main graph (line 5): "fo" is the command foo, the keywords for and forEach, the command Format and the
	// global foundThings, by name.
	TArray<FBangoCompletionItem> Items;
	FBangoCompletion::GatherItems(Names, 4, Commands, Items);
	TArray<int32> Shown;
	FBangoCompletion::Filter(Items, TEXT("fo"), Shown);
	FString Got;
	for (const int32 Index : Shown)
	{
		Got += Items[Index].Name + TEXT(" ");
	}
	TestEqual(TEXT("'fo' in the main graph"), Got, FString(TEXT("foo for forEach Format foundThings ")));

	// Keywords are listed too, and aren't calls.
	FBangoCompletion::Filter(Items, TEXT("sw"), Shown);
	TestTrue(TEXT("'sw' is the keyword switch"), Shown.Num() == 1 && Items[Shown[0]].Name.Equals(TEXT("switch"), ESearchCase::CaseSensitive)
		&& Items[Shown[0]].Kind == EBangoCompletionKind::Keyword && !Items[Shown[0]].bCallable);
	FBangoCompletion::Filter(Items, TEXT("whi"), Shown);
	TestTrue(TEXT("'whi' is the keyword while"), Shown.Num() == 1 && Items[Shown[0]].Name.Equals(TEXT("while"), ESearchCase::CaseSensitive));
	TArray<FBangoCompletionItem> Values;
	FBangoCompletion::GetKeywordItems(Values, /*bValuesOnly*/ true);
	TestEqual(TEXT("Keywords for a node's field: true, false, nil, player, self, isNil, TEXT, TEXT_NS and TEXT_INV"), Values.Num(), 9);

	// The local count hides the global count; the function is listed with its signature.
	const FBangoCompletionItem* Count = Items.FindByPredicate([](const FBangoCompletionItem& Item) { return Item.Name.Equals(TEXT("count"), ESearchCase::CaseSensitive); });
	TestTrue(TEXT("count is the local"), Count != nullptr && Count->Kind == EBangoCompletionKind::Local && Count->Detail.Equals(TEXT("int"), ESearchCase::CaseSensitive));
	const FBangoCompletionItem* AddScore = Items.FindByPredicate([](const FBangoCompletionItem& Item) { return Item.Name.Equals(TEXT("AddScore"), ESearchCase::CaseSensitive); });
	TestTrue(TEXT("AddScore is a function"), AddScore != nullptr && AddScore->Kind == EBangoCompletionKind::Function && AddScore->bCallable
		&& AddScore->Detail.Equals(TEXT("int AddScore(int amount, entity who)"), ESearchCase::CaseSensitive));

	// In the function (line 9): its parameter, and the script's too (a function sees the script's inputs, spec section 4).
	FBangoCompletion::GatherItems(Names, 8, Commands, Items);
	FBangoCompletion::Filter(Items, TEXT("am"), Shown);
	TestTrue(TEXT("'am' in the function is its parameter"), Shown.Num() == 1 && Items[Shown[0]].Name.Equals(TEXT("amount"), ESearchCase::CaseSensitive)
		&& Items[Shown[0]].Kind == EBangoCompletionKind::Parameter);
	FBangoCompletion::Filter(Items, TEXT("spea"), Shown);
	TestTrue(TEXT("The script's parameter is visible in a function"), Shown.Num() == 1 && Items[Shown[0]].Name.Equals(TEXT("speaker"), ESearchCase::CaseSensitive)
		&& Items[Shown[0]].Kind == EBangoCompletionKind::Parameter);

	// The info popup: the name under the mouse, from the same items.
	FBangoCompletionItem Hovered;
	int32 HoverStart = 0;
	int32 HoverEnd = 0;
	const FString CallLine = TEXT("Log(foundThings); // Log");
	TestTrue(TEXT("Hovering a command"), FBangoCompletion::FindHoverItem(Names, CallLine, 4, 1, Commands, Hovered, HoverStart, HoverEnd)
		&& Hovered.Name.Equals(TEXT("Log"), ESearchCase::CaseSensitive) && Hovered.bCallable && HoverStart == 0 && HoverEnd == 3);
	TestTrue(TEXT("Hovering a global"), FBangoCompletion::FindHoverItem(Names, CallLine, 4, 6, Commands, Hovered, HoverStart, HoverEnd)
		&& Hovered.Name.Equals(TEXT("foundThings"), ESearchCase::CaseSensitive) && Hovered.Detail.Equals(TEXT("int"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Hovering a comment"), FBangoCompletion::FindHoverItem(Names, CallLine, 4, 22, Commands, Hovered, HoverStart, HoverEnd));
	TestFalse(TEXT("Hovering a bracket"), FBangoCompletion::FindHoverItem(Names, CallLine, 4, 3, Commands, Hovered, HoverStart, HoverEnd));

	// Words of the language show what autocomplete says of them.
	auto HoverTitle = [&](const TCHAR* HoverLine, int32 Column) -> FString
	{
		FBangoCompletionItem Item;
		return FBangoCompletion::FindHoverItem(Names, HoverLine, 4, Column, Commands, Item, HoverStart, HoverEnd) && !Item.Description.IsEmpty()
			? FBangoCompletion::GetHoverTitle(Item) : FString();
	};
	TestEqual(TEXT("Hovering a keyword"), HoverTitle(TEXT("if (isNil(foundThings)) { }"), 0), FString(TEXT("keyword if")));
	TestEqual(TEXT("Hovering isNil"), HoverTitle(TEXT("if (isNil(foundThings)) { }"), 5), FString(TEXT("isNil(x) -> bool")));
	TestEqual(TEXT("Hovering call"), HoverTitle(TEXT("@light call TurnOn();"), 8), FString(TEXT("keyword call")));
	TestEqual(TEXT("Hovering a type"), HoverTitle(TEXT("int[] _list = [];"), 1), FString(TEXT("type int")));
	TestEqual(TEXT("Hovering a type's brackets"), HoverTitle(TEXT("int[] _list = [];"), 4), FString());
	TestEqual(TEXT("Hovering nil"), HoverTitle(TEXT("return nil;"), 8), FString(TEXT("value nil")));
	TestEqual(TEXT("Hovering player"), HoverTitle(TEXT("player SetHidden(true);"), 2), FString(TEXT("entity player")));
	TestEqual(TEXT("Hovering an @name"), HoverTitle(TEXT("@light call TurnOn();"), 2), FString());
	TestEqual(TEXT("Hovering a waitUntil's timeout"), HoverTitle(TEXT("waitUntil (ready) timeout (5)"), 20), FString(TEXT("keyword timeout")));
	TestEqual(TEXT("Hovering a waitUntil's heldFor"), HoverTitle(TEXT("waitUntil (ready) heldFor (2) timeout (5, true)"), 20), FString(TEXT("keyword heldFor")));
	TestEqual(TEXT("Hovering a timeout after a heldFor"), HoverTitle(TEXT("waitUntil (ready) heldFor (2) timeout (5, true)"), 32), FString(TEXT("keyword timeout")));

	// Defines (spec section 4, Defines): coloured as defines, offered, and hovered. The project's are put back after.
	{
		Bango::FBangoDefineText Define;
		Define.Name = TEXT("SELECT_RANDOM");
		Define.TypeName = TEXT("int");
		Define.ValueText = TEXT("3");
		Define.Description = TEXT("Pick a line at random.");
		Bango::SetProjectDefines({ Define });
		TestTrue(TEXT("A define is classed as one"), Names.Classify(4, TEXT("SELECT_RANDOM")) == EBangoSyntaxKind::Define);
		TArray<FBangoCompletionItem> DefineItems;
		FBangoCompletion::GatherItems(Names, 4, Commands, DefineItems);
		TestTrue(TEXT("Autocomplete offers a define"), DefineItems.ContainsByPredicate([](const FBangoCompletionItem& Item)
			{
				return Item.Kind == EBangoCompletionKind::Define && Item.Name == TEXT("SELECT_RANDOM") && Item.Detail == TEXT("int");
			}));
		TestEqual(TEXT("Hovering a define"), HoverTitle(TEXT("_mode = SELECT_RANDOM;"), 10), FString(TEXT("define int SELECT_RANDOM = 3")));
		Bango::ApplyDefineSettings();
	}

	return true;
}

#include "BangoValueWidgets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoActorNamePickerTest, "Bango.ScriptEditor.ValueWidgets.ActorNamePicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoActorNamePickerTest::RunTest(const FString& Parameters)
{
	// What a picked actor's name does to an entity value, and to a list of them.
	auto Add = [](const TCHAR* Type, const TCHAR* Value) { return SBangoActorNamePicker::AddNameToValue(Type, Value, TEXT("john")); };
	TestEqual(TEXT("An entity becomes the actor"), Add(TEXT("entity"), TEXT("@mary")), FString(TEXT("@john")));
	TestEqual(TEXT("An entity with no value"), Add(TEXT("entity"), TEXT("")), FString(TEXT("@john")));
	TestEqual(TEXT("A list gets it at its end"), Add(TEXT("entity[]"), TEXT("[@mary, @sue]")), FString(TEXT("[@mary, @sue, @john]")));
	TestEqual(TEXT("An empty list"), Add(TEXT("entity[]"), TEXT(" [ ] ")), FString(TEXT("[@john]")));
	TestEqual(TEXT("A list with no value"), Add(TEXT("entity[]"), TEXT("")), FString(TEXT("[@john]")));
	TestEqual(TEXT("A nil list"), Add(TEXT("entity[]"), TEXT("nil")), FString(TEXT("[@john]")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
