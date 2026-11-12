#pragma once

// Node layout (the fields the graph editor shows) test cases, shared by the Unreal automation test
// (BangoGraphTests.cpp) and the stand-alone test harness. Needs BangoGraphLayout.h and BangoMakeTestCommands
// (BangoCompilerTestCases.inl).

/**
 * One line per layout: `Title (Style) | label <type>: `text` | ...`. A field that isn't in the text shows
 * `-`; `(x)` marks a variable field, `?` an optional field and `#` a row without a value. An error is added as ` !! message`.
 */
static FString BangoDescribeLayout(const FBangoNodeLayout& Layout)
{
	static const TCHAR* Styles[] = { TEXT("ScriptEntry"), TEXT("FunctionEntry"), TEXT("Command"), TEXT("ScriptFunction"), TEXT("ArrayCommand"), TEXT("Generic") };
	FString Result = Layout.Title + TEXT(" (") + Styles[(int32)Layout.Style] + TEXT(")");
	for (const FBangoNodeField& Field : Layout.Fields)
	{
		Result += TEXT(" | ") + Field.Label;
		if (Field.bVariable)
		{
			// a field naming a variable: (x), with its type if known
			Result += Field.VariableType.IsEmpty() ? FString(TEXT(" (x)")) : FString(TEXT(" (x ")) + Field.VariableType + TEXT(")");
		}
		if (!Field.TypeHint.IsEmpty())
		{
			Result += TEXT(" <") + Field.TypeHint + (Field.bHideIcon ? TEXT(", no icon>") : TEXT(">"));
		}
		Result += Field.bPresent ? FString(TEXT(": `")) + Field.Text + TEXT("`") : FString(TEXT(": -"));
		if (Field.bOptional)
		{
			Result += TEXT("?");
		}
		if (!Field.bHasValue)
		{
			Result += TEXT("#");
		}
	}
	if (!Layout.Error.IsEmpty())
	{
		Result += TEXT(" !! ") + Layout.Error;
	}
	return Result;
}

struct FBangoLayoutTestCase
{
	EBangoGraphNodeKind Kind;
	bool bInFunctionGraph;
	const TCHAR* Text;
	const TCHAR* Expected;
};

static const FBangoLayoutTestCase GBangoLayoutTestCases[] =
{
	// ---- Commands ----
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0)"), TEXT("Sleep (Command) | seconds <float>: `1.0`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep( 1.0 )"), TEXT("Sleep (Command) | seconds <float>: `1.0`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{0} of {1}\", _a, Random(1.0, 2.0))"), TEXT("Hint (Command) | text <string>: `\"{0} of {1}\"` | {0} <any>: `_a`? | {1} <any>: `Random(1.0, 2.0)`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Collect(1, \"x\")"), TEXT("Collect (Command) | Value 1 <any>: `1`? | Value 2 <any>: `\"x\"`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"a, b\")"), TEXT("Hint (Command) | text <string>: `\"a, b\"`") },
	// A row for every placeholder in a literal text, even ones without a value yet.
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"Test {0} {1} {2}\", 1, 2)"), TEXT("Hint (Command) | text <string>: `\"Test {0} {1} {2}\"` | {0} <any>: `1`? | {1} <any>: `2`? | {2} <any>: -") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{1} then {0}\")"), TEXT("Hint (Command) | text <string>: `\"{1} then {0}\"` | {0} <any>: - | {1} <any>: -") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"only {2}\", 0)"), TEXT("Hint (Command) | text <string>: `\"only {2}\"` | {0} <any>: `0`? | {1} <any>: - | {2} <any>: -") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"100% sure\")"), TEXT("Hint (Command) | text <string>: `\"100% sure\"`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"{0}\", 1, 2)"), TEXT("Hint (Command) | text <string>: `\"{0}\"` | {0} <any>: `1`? | {1} <any>: `2`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(_text, 1)"), TEXT("Hint (Command) | text <string>: `_text` | {0} <any>: `1`?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("@john Speak(\"Hi\")"), TEXT("Speak (Command) | Target <entity>: `@john` | text <string>: `\"Hi\"`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("_people[2] Speak(\"Hi\")"), TEXT("Speak (Command) | Target <entity>: `_people[2]` | text <string>: `\"Hi\"`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Speak(\"x\")"), TEXT("Speak (Command) | Target <entity>: - | text <string>: `\"x\"`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("AwaitSpeak()"), TEXT("AwaitSpeak (Command) | pause <float>: -?") },
	{ EBangoGraphNodeKind::Call, false, TEXT("QuitGame()"), TEXT("QuitGame (Command)") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Random(1.0, 2.0)"), TEXT("Random (Command) | min <float>: `1.0` | max <float>: `2.0` !! Random gives a value that must be used, so it can't be a node of its own. Use it in the Value of a Set node, like '_x = Random(...)'.") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = boss GetName()"), TEXT("Set (Generic) | Var (x): `_x` | Value: `boss GetName()`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_a[F(1)] = Sleep(2.0)"), TEXT("Set (Generic) | Var (x): `_a[F(1)]` | Value: `Sleep(2.0)`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(Format(\"{0}\", 1))"), TEXT("Hint (Command) | text <string>: `Format(\"{0}\", 1)`") },

	// Problems the node shows straight away
	{ EBangoGraphNodeKind::Call, false, TEXT("Sleep(1.0, 2.0)"), TEXT("Sleep (Command) | seconds <float>: `1.0`? | Value 1 <any>: `2.0`? !! Sleep takes at most 1 argument(s), but 2 are given.") },
	{ EBangoGraphNodeKind::Call, false, TEXT("@john Sleep(1.0)"), TEXT("Sleep (Command) | Target: `@john` | seconds <float>: `1.0`? !! Sleep doesn't take a target.") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Foo(1)"), TEXT("Foo (Generic) | Call: `Foo(1)` !! Unknown command or function 'Foo'.") },

	// Expressions around a call are generic nodes
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = Random(1.0, 2.0) * 2"), TEXT("Set (Generic) | Var (x): `_x` | Value: `Random(1.0, 2.0) * 2`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x += Random(1.0, 2.0)"), TEXT("Add To (Generic) | Var (x): `_x` | Value: `Random(1.0, 2.0)`") },

	// ---- Script functions (Twice is defined in the test document) ----
	{ EBangoGraphNodeKind::Call, false, TEXT("Twice(2, [])"), TEXT("Twice (ScriptFunction) | _n <int>: `2` | _s <string[]>: `[]`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("Greet()"), TEXT("Greet (ScriptFunction)") },

	// ---- Array commands ----
	{ EBangoGraphNodeKind::Call, false, TEXT("_a Push(3)"), TEXT("Push (ArrayCommand) | Target (x array) <array>: `_a` | Value <element>: `3`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("_g[1] InsertAt(4, 0)"), TEXT("InsertAt (ArrayCommand) | Target (x array) <array>: `_g[1]` | Value <element>: `4` | Index <int>: `0`") },
	{ EBangoGraphNodeKind::Call, false, TEXT("_a Pop()"), TEXT("Pop (ArrayCommand) | Target (x array) <array>: `_a`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_first = _a RemoveAt(0)"), TEXT("Set (Generic) | Var (x): `_first` | Value: `_a RemoveAt(0)`") },

	// ---- Other kinds ----
	{ EBangoGraphNodeKind::Assign, false, TEXT("_x = 1"), TEXT("Set (Generic) | Var (x): `_x` | Value: `1`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_g[1][2] *= 3"), TEXT("Multiply (Generic) | Var (x): `_g[1][2]` | By: `3`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_d /= 2.0"), TEXT("Divide (Generic) | Var (x): `_d` | By: `2.0`") },
	{ EBangoGraphNodeKind::Assign, false, TEXT("_n -= 1"), TEXT("Subtract From (Generic) | Var (x): `_n` | Value: `1`") },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("_a[2]++"), TEXT("Increment (Generic) | Var (x int) <int>: `_a[2]`") },
	{ EBangoGraphNodeKind::IncDec, false, TEXT("--_x"), TEXT("Decrement (Generic) | Var (x int) <int>: `_x`") },
	{ EBangoGraphNodeKind::If, false, TEXT("_n > 1 && !IsNil(g)"), TEXT("If (Generic) | Condition <bool, no icon>: `_n > 1 && !IsNil(g)`") },
	{ EBangoGraphNodeKind::While, false, TEXT("true"), TEXT("While (Generic) | Condition <bool, no icon>: `true`") },
	// Simple counting loops (<= with ++, >= with --) look like Blueprint's For Loop; anything else is one Expression row.
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i++"), TEXT("For (Generic) | Var (x int): `_i` | First <int>: `0` | Last <int>: `9`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 1; _i <= Count(_a); ++_i"), TEXT("For (Generic) | Var (x int): `_i` | First <int>: `1` | Last <int>: `Count(_a)`") },
	{ EBangoGraphNodeKind::For, false, TEXT("_i = 9; _i >= 0; _i--"), TEXT("For (Generic) | Var (x): `_i` | First <int>: `9` | Last <int>: `0`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i < 10; _i++"), TEXT("For (Generic) | Expression: `int _i = 0; _i < 10; _i++`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i = _i + 2"), TEXT("For (Generic) | Expression: `int _i = 0; _i <= 9; _i = _i + 2`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9; _i--"), TEXT("For (Generic) | Expression: `int _i = 0; _i <= 9; _i--`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _j <= 9; _i++"), TEXT("For (Generic) | Expression: `int _i = 0; _j <= 9; _i++`") },
	{ EBangoGraphNodeKind::For, false, TEXT("int _i = 0; _i <= 9 && _ok; _i++"), TEXT("For (Generic) | Expression: `int _i = 0; _i <= 9 && _ok; _i++`") },
	{ EBangoGraphNodeKind::For, false, TEXT("float _d = 0; _d <= 9; _d++"), TEXT("For (Generic) | Expression: `float _d = 0; _d <= 9; _d++`") },
	{ EBangoGraphNodeKind::For, false, TEXT(";;"), TEXT("For (Generic) | Expression: `;;`") },
	{ EBangoGraphNodeKind::For, false, TEXT("; F(1, 2) > 0;"), TEXT("For (Generic) | Expression: `; F(1, 2) > 0;`") },
	{ EBangoGraphNodeKind::ForEach, false, TEXT("_x in [1, 2]"), TEXT("For Each (Generic) | Variable (x): `_x` | Array <array>: `[1, 2]`") },
	{ EBangoGraphNodeKind::Return, true, TEXT(""), TEXT("Return (Generic) | Value: -?") },
	{ EBangoGraphNodeKind::Return, false, TEXT(""), TEXT("Return (Generic)") },
	{ EBangoGraphNodeKind::Return, true, TEXT("_a * 2"), TEXT("Return (Generic) | Value: `_a * 2`?") },
	{ EBangoGraphNodeKind::Label, false, TEXT("Hub"), TEXT("Label (Generic) | Name: `Hub`") },
	{ EBangoGraphNodeKind::Goto, false, TEXT("Hub"), TEXT("Go To (Generic) | Label: `Hub`") },
	{ EBangoGraphNodeKind::Break, false, TEXT(""), TEXT("Break (Generic)") },
	{ EBangoGraphNodeKind::Reroute, false, TEXT(""), TEXT("Reroute (Generic)") },

	// ---- Entry nodes ----
	{ EBangoGraphNodeKind::Entry, false, TEXT("string _name, int _times = 3, entity _who = nil"), TEXT("Script Start (ScriptEntry) | _name <string>: -# | _times <int>: `3`?# | _who <entity>: `nil`?#") },
	{ EBangoGraphNodeKind::Entry, false, TEXT("int[] _a = [1, -2], string _b"), TEXT("Script Start (ScriptEntry) | _a <int[]>: `[1, -2]`?# | _b <string>: -#") },
	{ EBangoGraphNodeKind::Entry, false, TEXT(""), TEXT("Script Start (ScriptEntry)") },
	{ EBangoGraphNodeKind::Entry, false, TEXT("entity _who = @john, entity[] _all = [@a, @b]"), TEXT("Script Start (ScriptEntry) | _who <entity>: `@john`?# | _all <entity[]>: `[@a, @b]`?#") },
	{ EBangoGraphNodeKind::Entry, true, TEXT("int Twice(int _n, string[] _s)"), TEXT("Twice (FunctionEntry) | Returns: `int` | _n <int>: `_n`# | _s <string[]>: `_s`#") },
	{ EBangoGraphNodeKind::Entry, true, TEXT("void Greet()"), TEXT("Greet (FunctionEntry) | Returns: `void`") },

	// Values that must be used: such a command can't be a node of its own. Others can.
	{ EBangoGraphNodeKind::Call, false, TEXT("Count(_a)"), TEXT("Count (ArrayCommand) | Array <array>: `_a` !! Count gives a value that must be used, so it can't be a node of its own. Use it in the Value of a Set node, like '_x = Count(...)'.") },
	{ EBangoGraphNodeKind::Call, false, TEXT("AskChoice(\"A\", \"B\")"), TEXT("AskChoice (Command) | a <string>: `\"A\"` | b <string>: `\"B\"`") },

	// Text that doesn't lex
	{ EBangoGraphNodeKind::Call, false, TEXT("Hint(\"unfinished"), TEXT("Call (Generic) | Text: `Hint(\"unfinished`") },
};

static void BangoRunGraphLayoutScenarios(TArray<FString>& OutFailures)
{
	FBangoCommandTable Commands;
	BangoMakeTestCommands(Commands);

	FBangoGraphDocument Doc;
	FBangoCompileError Error;
	if (!Bango::BuildGraph(TEXT("function int Twice(int _n, string[] _s)\n{\n\treturn _n * 2;\n};\nfunction void Greet()\n{\n};\n"), TEXT("test.bango"), Doc, Error))
	{
		OutFailures.Add(FString(TEXT("layout: test document: ")) + Error.ToString());
		return;
	}
	const FBangoNodeLayoutContext Context = Bango::MakeNodeLayoutContext(Doc, &Commands);

	// The VARIABLES list: parameters, locals and globals of one graph, with types and where each is declared.
	{
		const TCHAR* Source =
			TEXT("#define params (string _who, int _times = 1)\n")
			TEXT("global int gold;\n")
			TEXT("global string[] names;\n")
			TEXT("int _count = 0;\n")
			TEXT("int _rounds;\n")
			TEXT("forEach (_name in names)     //@3(0, 2)\n")
			TEXT("{\n")
			TEXT("\tLog(_name);                //@4(1, 3)\n")
			TEXT("};\n")
			TEXT("for (int _i = 0; _i <= 3; _i++)  //@5(0, 4)\n")
			TEXT("{\n")
			TEXT("\tbank = _who GetPos().x;   //@6(1, 5)\n")
			TEXT("};\n")
			TEXT("function void F(int _n)\n")
			TEXT("{\n")
			TEXT("\tbool _done = false;\n")
			TEXT("\tgold += _n;                //@8(0, 1)\n")
			TEXT("};\n");
		FBangoGraphDocument VarDoc;
		if (!Bango::BuildGraph(Source, TEXT("vars.bango"), VarDoc, Error))
		{
			OutFailures.Add(FString(TEXT("variables: build: ")) + Error.ToString());
		}
		else
		{
			const FBangoNodeLayoutContext VarContext = Bango::MakeNodeLayoutContext(VarDoc, &Commands);
			auto Describe = [](const TArray<FBangoGraphVariable>& Vars)
			{
				FString Out;
				for (const FBangoGraphVariable& V : Vars)
				{
					const TCHAR* Kind = V.Kind == FBangoGraphVariable::EKind::Param ? TEXT("param") : V.Kind == FBangoGraphVariable::EKind::Local ? TEXT("local")
						: V.Kind == FBangoGraphVariable::EKind::Loop ? TEXT("loop") : TEXT("global");
					Out += FString::Printf(TEXT("%s %s:%s @%d/%d; "), Kind, *V.Name, *V.Type, V.DeclGraph, V.Index);
				}
				return Out;
			};
			TArray<FBangoGraphVariable> Vars;
			Bango::GetGraphVariables(VarDoc, INDEX_NONE, VarContext, Vars);
			// Parameters and locals in order, loop variables by node, the file's globals in order, then `bank`, which
			// is used but declared nowhere in the file.
			const FString ExpectedMain = TEXT("param _who:string @-1/0; param _times:int @-1/1; local _count:int @-1/0; local _rounds:int @-1/1; loop _name:string @-1/1; loop _i:int @-1/3; global gold:int @-1/0; global names:string[] @-1/1; global bank: @-1/-1; ");
			if (Describe(Vars) != ExpectedMain)
			{
				OutFailures.Add(TEXT("variables (main): expected ") + ExpectedMain + TEXT("\n got ") + Describe(Vars));
			}
			Bango::GetGraphVariables(VarDoc, 0, VarContext, Vars);
			const FString ExpectedF = TEXT("param _n:int @0/0; local _done:bool @0/0; global gold:int @-1/0; global names:string[] @-1/1; ");
			if (Describe(Vars) != ExpectedF)
			{
				OutFailures.Add(TEXT("variables (F): expected ") + ExpectedF + TEXT("\n got ") + Describe(Vars));
			}
		}
	}

	// Renaming a function in node text: calls and its signature, not strings or longer names.
	{
		struct FRenameCase { const TCHAR* Text; const TCHAR* Expected; };
		static const FRenameCase Cases[] =
		{
			{ TEXT("Greet()"), TEXT("Hello()") },
			{ TEXT("void Greet(entity _who)"), TEXT("void Hello(entity _who)") },
			{ TEXT("int _x = Greet(Greet(1)) + 2"), TEXT("int _x = Hello(Hello(1)) + 2") },
			{ TEXT("Log(\"Greet()\", Greet)"), nullptr },        // a string, and a name that isn't a call
			{ TEXT("GreetAll()"), nullptr },
			{ TEXT("@Greet Wave()"), nullptr },
		};
		for (const FRenameCase& Case : Cases)
		{
			FString Text = Case.Text;
			const bool bChanged = Bango::RenameFunctionInText(Text, TEXT("Greet"), TEXT("Hello"));
			const FString Expected = Case.Expected ? Case.Expected : Case.Text;
			if (bChanged != (Case.Expected != nullptr) || Text != Expected)
			{
				OutFailures.Add(FString::Printf(TEXT("rename function: `%s` should become `%s`, got `%s`"), Case.Text, *Expected, *Text));
			}
		}
	}

	// Dragging a variable onto the graph: the nodes it offers, with the variable filled in, all valid node text.
	{
		struct FVarNodesCase { const TCHAR* Type; const TCHAR* Titles; };
		static const FVarNodesCase Cases[] =
		{
			{ TEXT("int"), TEXT("Set|Add To|Subtract From|Increment|Decrement|Multiply|Divide|") },
			{ TEXT("float"), TEXT("Set|Add To|Subtract From|Multiply|Divide|") },
			{ TEXT("string"), TEXT("Set|Add To|") },
			{ TEXT("vector3"), TEXT("Set|Add To|Subtract From|Multiply|Divide|") },
			{ TEXT("bool"), TEXT("Set|") },
			{ TEXT("entity"), TEXT("Set|") },
			{ TEXT(""), TEXT("Set|") },
			{ TEXT("int[]"), TEXT("Set|Push|Insert At|Pop|Remove At|For Each|") },
		};
		TSet<FString> Used;
		Used.Add(TEXT("_item"));
		for (const FVarNodesCase& Case : Cases)
		{
			TArray<FBangoNodeMenuEntry> Entries;
			Bango::GetVariableNodeEntries(TEXT("_v"), Case.Type, Used, Entries);
			FString Titles;
			for (const FBangoNodeMenuEntry& Entry : Entries)
			{
				Titles += Entry.Title + TEXT("|");
				FString Problem;
				if (!Bango::ValidateNodeText(Entry.Kind, false, Entry.Text, Problem))
				{
					OutFailures.Add(FString::Printf(TEXT("variable nodes: %s's `%s` isn't valid: %s"), *Entry.Title, *Entry.Text, *Problem));
				}
				else if (!Entry.Text.Contains(TEXT("_v")))
				{
					OutFailures.Add(FString::Printf(TEXT("variable nodes: %s's `%s` should name the variable"), *Entry.Title, *Entry.Text));
				}
			}
			if (Titles != Case.Titles)
			{
				OutFailures.Add(FString::Printf(TEXT("variable nodes for %s: expected `%s`, got `%s`"), Case.Type, Case.Titles, *Titles));
			}
		}
		// The loop variable doesn't clash with the graph's names.
		TArray<FBangoNodeMenuEntry> Entries;
		Bango::GetVariableNodeEntries(TEXT("names"), TEXT("string[]"), Used, Entries);
		if (Entries.Num() == 0 || Entries.Last().Text != TEXT("_item2 in names"))
		{
			OutFailures.Add(FString::Printf(TEXT("variable nodes: For Each should be `_item2 in names`, got `%s`"), Entries.Num() ? *Entries.Last().Text : TEXT("")));
		}
	}

	// Renaming a variable in node text: every use, but not vector parts, strings or longer names.
	{
		struct FRenameCase { const TCHAR* Text; const TCHAR* Expected; };
		static const FRenameCase Cases[] =
		{
			{ TEXT("_who Speak(_who)"), TEXT("_them Speak(_them)") },
			{ TEXT("_pos.x = _who GetPos()._who"), TEXT("_pos.x = _them GetPos()._who") },
			{ TEXT("Log(\"_who\", _whom)"), nullptr },
			{ TEXT("forEach (entity _x in _who)"), TEXT("forEach (entity _x in _them)") },
		};
		for (const FRenameCase& Case : Cases)
		{
			FString Text = Case.Text;
			const bool bChanged = Bango::RenameVariableInText(Text, TEXT("_who"), TEXT("_them"));
			const FString Expected = Case.Expected ? Case.Expected : Case.Text;
			if (bChanged != (Case.Expected != nullptr) || Text != Expected)
			{
				OutFailures.Add(FString::Printf(TEXT("rename variable: `%s` should become `%s`, got `%s`"), Case.Text, *Expected, *Text));
			}
		}
	}

	// A local's starting value, from the Locals list: it must read and be a fixed value.
	{
		struct FValueCase { const TCHAR* Type; const TCHAR* Value; bool bOk; };
		static const FValueCase Cases[] =
		{
			{ TEXT("int"), TEXT("5"), true },
			{ TEXT("float"), TEXT("-2.5"), true },
			{ TEXT("string"), TEXT("\"a = b\""), true },
			{ TEXT("int[][]"), TEXT("[[1, -2], []]"), true },
			{ TEXT("vector3"), TEXT("[0, 0, 100]"), true },
			{ TEXT("entity"), TEXT("@john"), true },
			{ TEXT("bool"), TEXT("nil"), true },
			{ TEXT("text"), TEXT("LOCTEXT(\"K\", \"Hi\")"), true },
			{ TEXT("int"), TEXT("1 + 2"), false },
			{ TEXT("float"), TEXT("Random(1.0, 2.0)"), false },
			{ TEXT("int"), TEXT("_other"), false },
			{ TEXT("int"), TEXT(""), false },
			{ TEXT("int"), TEXT("5; Hint(\"x\")"), false },
		};
		for (const FValueCase& Case : Cases)
		{
			FString Problem;
			if (Bango::CheckDeclValue(Case.Type, Case.Value, Problem) != Case.bOk)
			{
				OutFailures.Add(FString::Printf(TEXT("decl value: `%s %s` should be %s (%s)"), Case.Type, Case.Value, Case.bOk ? TEXT("fine") : TEXT("refused"), *Problem));
			}
		}
		FString Renamed = TEXT("gold = gold(1) + gold");
		Bango::RenameVariableInText(Renamed, TEXT("gold"), TEXT("coins"));
		if (Renamed != FString(TEXT("coins = gold(1) + coins")))
		{
			OutFailures.Add(FString(TEXT("rename variable: a call with the same name stays: ")) + Renamed);
		}
	}

	// Entry node parameters: read as written, and written back with the rest of the text kept.
	{
		struct FParamsCase { const TCHAR* Text; bool bFunction; const TCHAR* Read; };
		static const FParamsCase Cases[] =
		{
			{ TEXT(""), false, TEXT("") },
			{ TEXT("entity _who, int _times = 1, int[][] _grid = [[1, 2], []], text _t = nil"), false, TEXT("entity _who|int _times=1|int[][] _grid=[[1, 2], []]|text _t=nil|") },
			{ TEXT("void Greet()"), true, TEXT("") },
			{ TEXT("int[] Pick(string[]  _names, vector3 _at)"), true, TEXT("string[] _names|vector3 _at|") },
		};
		for (const FParamsCase& Case : Cases)
		{
			TArray<FBangoEntryParam> Params;
			FString Read;
			if (Bango::ReadEntryParams(Case.Text, Case.bFunction, Params))
			{
				for (const FBangoEntryParam& Param : Params)
				{
					Read += Param.Type + TEXT(" ") + Param.Name + (Param.bHasDefault ? TEXT("=") + Param.Default : FString()) + TEXT("|");
				}
			}
			else
			{
				Read = TEXT("<failed>");
			}
			if (Read != Case.Read)
			{
				OutFailures.Add(FString::Printf(TEXT("read params: `%s` should read `%s`, got `%s`"), Case.Text, Case.Read, *Read));
			}
		}

		struct FWriteCase { const TCHAR* Text; bool bFunction; const TCHAR* Expected; const TCHAR* Empty; };
		static const FWriteCase Writes[] =
		{
			{ TEXT(""), false, TEXT("bool _new"), TEXT("") },
			{ TEXT("entity _who, int _times = 1"), false, TEXT("entity _who, int _times = 1, bool _new = nil"), TEXT("") },
			{ TEXT("void Greet()"), true, TEXT("void Greet(bool _new)"), TEXT("void Greet()") },
			{ TEXT("int Add(int _a, int _b)"), true, TEXT("int Add(int _a, int _b, bool _new)"), TEXT("int Add()") },
		};
		for (const FWriteCase& Case : Writes)
		{
			TArray<FBangoEntryParam> Params;
			Bango::ReadEntryParams(Case.Text, Case.bFunction, Params);
			// As the editor adds one: optional (= nil) after an optional one, since required ones must come first.
			const bool bAfterDefault = Params.Num() > 0 && Params.Last().bHasDefault;
			FBangoEntryParam& Added = Params.AddDefaulted_GetRef();
			Added.Type = TEXT("bool");
			Added.Name = TEXT("_new");
			Added.bHasDefault = bAfterDefault;
			Added.Default = bAfterDefault ? TEXT("nil") : TEXT("");
			FString Written, WriteError;
			if (!Bango::WriteEntryParams(Case.Text, Case.bFunction, Params, Written, WriteError) || Written != Case.Expected)
			{
				OutFailures.Add(FString::Printf(TEXT("write params: `%s` should become `%s`, got `%s` (%s)"), Case.Text, Case.Expected, *Written, *WriteError));
			}
			// And removing them all again.
			Params.Reset();
			const TCHAR* Empty = Case.Empty;
			if (!Bango::WriteEntryParams(Case.Text, Case.bFunction, Params, Written, WriteError) || Written != Empty)
			{
				OutFailures.Add(FString::Printf(TEXT("write params: `%s` without params should be `%s`, got `%s` (%s)"), Case.Text, Empty, *Written, *WriteError));
			}
		}
		// Text that isn't a parameter list is refused.
		TArray<FBangoEntryParam> Bad;
		Bad.AddDefaulted();
		Bad[0].Type = TEXT("bool");
		Bad[0].Name = TEXT("not a name");
		FString Written, WriteError;
		if (Bango::WriteEntryParams(TEXT("void Greet()"), true, Bad, Written, WriteError))
		{
			OutFailures.Add(FString::Printf(TEXT("write params: a bad name should be refused, got `%s`"), *Written));
		}
	}

	// Layouts compare equal when they'd look the same (the editor uses this to rebuild only nodes that changed).
	{
		FBangoGraphNode A;
		A.Kind = EBangoGraphNodeKind::Call;
		A.Text = TEXT("Sleep(1.0)");
		FBangoGraphNode B = A;
		const FBangoNodeLayout LayoutA = Bango::GetNodeLayout(A, false, Context);
		if (LayoutA != Bango::GetNodeLayout(B, false, Context))
		{
			OutFailures.Add(TEXT("layout equality: the same node should lay out equal"));
		}
		B.Text = TEXT("Sleep(2.0)");
		if (LayoutA == Bango::GetNodeLayout(B, false, Context))
		{
			OutFailures.Add(TEXT("layout equality: a different value should lay out different"));
		}
		FBangoNodeLayout Typed = LayoutA;
		Typed.Fields[0].VariableType = TEXT("int");
		if (Typed == LayoutA)
		{
			OutFailures.Add(TEXT("layout equality: a field's variable type counts"));
		}
	}
	if (Context.Functions.Num() != 2)
	{
		OutFailures.Add(TEXT("layout: the context should have 2 script functions"));
	}

	// ---- Return: a value only where one belongs, typed like the function ----
	{
		FBangoGraphDocument ReturnDoc;
		const TCHAR* Source =
			TEXT("return;                //@1(0, 0)\n")
			TEXT("return 5;              //@2(0, 2)\n")
			TEXT("function string Name()\n{\n\treturn \"Bo\";      //@3(0, 0)\n\treturn;           //@4(0, 2)\n};\n")
			TEXT("function void Done()\n{\n\treturn;           //@5(0, 0)\n};\n");
		if (!Bango::BuildGraph(Source, TEXT("return.bango"), ReturnDoc, Error))
		{
			OutFailures.Add(FString(TEXT("return: test document: ")) + Error.ToString());
		}
		else
		{
			const FBangoNodeLayoutContext ReturnContext = Bango::MakeNodeLayoutContext(ReturnDoc, &Commands);
			struct FReturnCase { int64 NodeID; const TCHAR* Expected; };
			const FReturnCase Cases[] =
			{
				{ 1, TEXT("Return (Generic)") },
				{ 2, TEXT("Return (Generic) | Value: `5`?") },
				{ 3, TEXT("Return (Generic) | Value <string>: `\"Bo\"`") },
				{ 4, TEXT("Return (Generic) | Value <string>: -") },
				{ 5, TEXT("Return (Generic)") },
			};
			for (const FReturnCase& Case : Cases)
			{
				const FBangoGraphNode* Found = nullptr;
				bool bInFunction = false;
				for (const FBangoGraphNode& Node : ReturnDoc.Main.Nodes)
				{
					Found = Node.NodeID == Case.NodeID ? &Node : Found;
				}
				for (const FBangoGraph& Function : ReturnDoc.Functions)
				{
					for (const FBangoGraphNode& Node : Function.Nodes)
					{
						if (Node.NodeID == Case.NodeID)
						{
							Found = &Node;
							bInFunction = true;
						}
					}
				}
				const FString Got = Found ? BangoDescribeLayout(Bango::GetNodeLayout(*Found, bInFunction, ReturnContext)) : FString(TEXT("(no such node)"));
				if (!Got.Equals(Case.Expected, ESearchCase::CaseSensitive))
				{
					OutFailures.Add(FString::Printf(TEXT("return: node %d:\n  expected: %s\n  got:      %s"), (int32)Case.NodeID, Case.Expected, *Got));
				}
			}

			// Retyping the function changes the context, so its Return nodes are laid out again.
			FBangoGraphDocument Retyped = ReturnDoc;
			Retyped.Functions[0].Nodes[0].Text = TEXT("int Name()");
			if (Bango::LayoutContextsMatch(ReturnContext, Bango::MakeNodeLayoutContext(Retyped, &Commands)))
			{
				OutFailures.Add(TEXT("return: changing a function's return type should change the context"));
			}
		}
	}

	for (const FBangoLayoutTestCase& Case : GBangoLayoutTestCases)
	{
		FBangoGraphNode Node;
		Node.Kind = Case.Kind;
		Node.Text = Case.Text;
		const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, Case.bInFunctionGraph, Context);
		const FString Got = BangoDescribeLayout(Layout);
		if (!Got.Equals(Case.Expected, ESearchCase::CaseSensitive))
		{
			OutFailures.Add(FString::Printf(TEXT("layout of `%s`:\n  expected: %s\n  got:      %s"), Case.Text, Case.Expected, *Got));
		}

		// Every field's span must point at its text, and empty fields must be inside the text.
		for (const FBangoNodeField& Field : Layout.Fields)
		{
			const bool bSpanOk = Field.Begin >= 0 && Field.Begin <= Field.End && Field.End <= Node.Text.Len()
				&& Node.Text.Mid(Field.Begin, Field.End - Field.Begin).Equals(Field.Text, ESearchCase::CaseSensitive)
				&& (Field.bPresent || Field.Begin == Field.End);
			if (!bSpanOk)
			{
				OutFailures.Add(FString::Printf(TEXT("layout of `%s`: field '%s' has a bad span [%d, %d)"), Case.Text, *Field.Label, Field.Begin, Field.End));
			}
		}
	}
	// The editor's Expression toggle: a simple For shown as its whole header, and edited that way.
	{
		FBangoGraphNode Loop;
		Loop.Kind = EBangoGraphNodeKind::For;
		Loop.Text = TEXT("int _i = 0; _i <= 9; _i++");
		const FBangoNodeLayout Simple = Bango::GetNodeLayout(Loop, false, Context);
		const FBangoNodeLayout Forced = Bango::GetNodeLayout(Loop, false, Context, /*bShowExpression*/ true);
		if (!Simple.bHasSimpleForm || Simple.bShowsExpression || !Forced.bHasSimpleForm || !Forced.bShowsExpression
			|| BangoDescribeLayout(Forced) != FString(TEXT("For (Generic) | Expression: `int _i = 0; _i <= 9; _i++`")))
		{
			OutFailures.Add(FString(TEXT("For: Show Expression: ")) + BangoDescribeLayout(Forced));
		}
		FString EditError;
		if (!Bango::SetNodeField(Loop, false, Context, 0, TEXT("int _i = 0; _i < 10; _i += 2"), EditError, /*bShowExpression*/ true)
			|| Loop.Text != FString(TEXT("int _i = 0; _i < 10; _i += 2")))
		{
			OutFailures.Add(FString(TEXT("For: editing the shown expression: ")) + EditError + TEXT(" / ") + Loop.Text);
		}
		const FBangoNodeLayout Complex = Bango::GetNodeLayout(Loop, false, Context);
		if (Complex.bHasSimpleForm || !Complex.bShowsExpression)
		{
			OutFailures.Add(TEXT("For: a complex header has no simple form"));
		}
	}

	// Variable types: each node sees the params, the file's globals, the graph's locals, and the variables of the loops
	// it's inside. Set nodes and the like show the variable's type, and their value takes it too.
	{
		const TCHAR* Source =
			TEXT("#define params (int _count, string[] _names = [])\n")
			TEXT("global float speed;\n")
			TEXT("string _title = \"x\";\n")
			TEXT("int _n = 1;\n")
			TEXT("vector3 _pos = [1, 2, 3];\n")
			TEXT("_title = \"y\";    //@3(0, 2)\n")
			TEXT("speed *= 2;    //@4(0, 3)\n")
			TEXT("if (_count > 0)    //@5(0, 4)\n{\n")
			TEXT("\t_n += 2;    //@7(1, 6)\n}\nelse\n{\n")
			TEXT("\t_n = 4;    //@9(2, 6)\n};\n")
			TEXT("for (int _i = 0; _i < 3; _i++)    //@11(0, 8)\n{\n")
			TEXT("\t_names[_i] = \"z\";    //@12(1, 9)\n};\n")
			TEXT("forEach (_name in _names)    //@13(0, 10)\n{\n")
			TEXT("\t_name = \"q\";    //@14(1, 11)\n};\n")
			TEXT("_name = 3;    //@10(0, 7)\n")
			TEXT("other = 1;    //@16(0, 12)\n")
			TEXT("_pos.z = 5;    //@18(0, 14)\n")
			TEXT("_pos += [0, 0, 1];    //@19(0, 15)\n")
			TEXT("//~ _count = 5;    //@30(5, 5)\n")
			TEXT("function void F(int[][] _g)\n{\n")
			TEXT("\t_g[0] = [1];    //@20(0, 0)\n")
			TEXT("\tspeed = 2.0;    //@21(0, 1)\n")
			TEXT("\t_title = \"no\";    //@22(0, 2)\n};\n");
		struct FVarCase { int64 NodeID; const TCHAR* Expected; };
		const FVarCase VarCases[] =
		{
			{ 3,  TEXT("Set (Generic) | Var (x string): `_title` | Value <string>: `\"y\"`") },
			{ 4,  TEXT("Multiply (Generic) | Var (x float): `speed` | By <float>: `2`") },
			{ 7,  TEXT("Add To (Generic) | Var (x int): `_n` | Value <int>: `2`") },
			{ 9,  TEXT("Set (Generic) | Var (x int): `_n` | Value <int>: `4`") },
			{ 10, TEXT("Set (Generic) | Var (x): `_name` | Value: `3`") },
			{ 12, TEXT("Set (Generic) | Var (x string): `_names[_i]` | Value <string>: `\"z\"`") },
			{ 13, TEXT("For Each (Generic) | Variable (x string): `_name` | Array <array>: `_names`") },
			{ 14, TEXT("Set (Generic) | Var (x string): `_name` | Value <string>: `\"q\"`") },
			{ 16, TEXT("Set (Generic) | Var (x): `other` | Value: `1`") },
			{ 18, TEXT("Set (Generic) | Var (x float): `_pos.z` | Value <float>: `5`") },
			{ 19, TEXT("Add To (Generic) | Var (x vector3): `_pos` | Value <vector3>: `[0, 0, 1]`") },
			{ 30, TEXT("Set (Generic) | Var (x int): `_count` | Value <int>: `5`") },
			{ 20, TEXT("Set (Generic) | Var (x int[]): `_g[0]` | Value <int[]>: `[1]`") },
			{ 21, TEXT("Set (Generic) | Var (x float): `speed` | Value <float>: `2.0`") },
			{ 22, TEXT("Set (Generic) | Var (x): `_title` | Value: `\"no\"`") },
		};
		FBangoGraphDocument VarDoc;
		if (!Bango::BuildGraph(Source, TEXT("vars.bango"), VarDoc, Error))
		{
			OutFailures.Add(FString(TEXT("variable types: test document: ")) + Error.ToString());
			return;
		}
		const FBangoNodeLayoutContext VarContext = Bango::MakeNodeLayoutContext(VarDoc, &Commands);
		for (const FVarCase& VarCase : VarCases)
		{
			const FBangoGraphNode* Found = nullptr;
			bool bInFunction = false;
			for (const FBangoGraphNode& Node : VarDoc.Main.Nodes)
			{
				Found = Node.NodeID == VarCase.NodeID ? &Node : Found;
			}
			for (const FBangoGraph& Function : VarDoc.Functions)
			{
				for (const FBangoGraphNode& Node : Function.Nodes)
				{
					if (Node.NodeID == VarCase.NodeID)
					{
						Found = &Node;
						bInFunction = true;
					}
				}
			}
			const FString Got = Found ? BangoDescribeLayout(Bango::GetNodeLayout(*Found, bInFunction, VarContext)) : FString(TEXT("(no such node)"));
			if (!Got.Equals(VarCase.Expected, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("variable types: node %d:\n  expected: %s\n  got:      %s"), (int32)VarCase.NodeID, VarCase.Expected, *Got));
			}
		}

		// Changing a declaration changes the context, so the editor knows to lay everything out again.
		if (!Bango::LayoutContextsMatch(VarContext, Bango::MakeNodeLayoutContext(VarDoc, &Commands)))
		{
			OutFailures.Add(TEXT("variable types: the same document should give matching contexts"));
		}
		FBangoGraphDocument Renamed = VarDoc;
		for (FBangoGraphDecl& Local : Renamed.Main.Locals)
		{
			if (Local.Name == FString(TEXT("_title")))
			{
				Local.Type = TEXT("int"); // as the Locals list does
				Local.Value = TEXT("1");
			}
		}
		const FBangoNodeLayoutContext RenamedContext = Bango::MakeNodeLayoutContext(Renamed, &Commands);
		if (Bango::LayoutContextsMatch(VarContext, RenamedContext))
		{
			OutFailures.Add(TEXT("variable types: retyping a declaration should change the context"));
		}
		for (const FBangoGraphNode& Node : Renamed.Main.Nodes)
		{
			if (Node.NodeID == 3)
			{
				const FString Got = BangoDescribeLayout(Bango::GetNodeLayout(Node, false, RenamedContext));
				if (!Got.Equals(TEXT("Set (Generic) | Var (x int): `_title` | Value <int>: `\"y\"`"), ESearchCase::CaseSensitive))
				{
					OutFailures.Add(FString(TEXT("variable types: after retyping the declaration: ")) + Got);
				}
			}
		}
	}
}
