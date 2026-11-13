#pragma once

#include "BangoText.h"

// Interpreter test cases, shared by the Unreal automation test (BangoInstanceTests.cpp)
// and the stand-alone test harness used during development.
// Needs: BangoCompiler.h, BangoInstance.h, Serialization/MemoryWriter.h, Serialization/MemoryReader.h

/** Actors with the Bango IDs "john" and "player" are in the level; "ghost" is not. */
class FBangoTestEntities : public IBangoEntityResolver
{
public:
	virtual bool IsEntityRegistered(const FString& Name) const override
	{
		return Name.Equals(TEXT("john"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("player"), ESearchCase::CaseSensitive);
	}
};

/** Commands for interpreter tests. Output goes to Log. */
static void BangoMakeRuntimeTestCommands(FBangoCommandTable& Table, TArray<FString>& Log)
{
	FString Error;
	const FBangoType Int = FBangoType::Int();
	const FBangoType Double = FBangoType::Double();
	const FBangoType String = FBangoType::String();
	const FBangoType Entity = FBangoType::Entity();

	FBangoCommand Hint;
	Hint.Signature = FBangoCommandSignature::Make(TEXT("Hint")).Param(TEXT("text"), String).Variadic(TEXT("{"), 0, TEXT("}"));
	Hint.Start = [&Log](FBangoCommandCall& Call)
	{
		TArray<FBangoValue> Values;
		for (int32 i = 1; i < Call.Args.Num(); ++i)
		{
			Values.Add(Call.Args[i]);
		}
		FString Text;
		FString FormatError;
		if (!Bango::FormatText(Call.Args[0].GetString(), Values, Text, FormatError))
		{
			return Call.Fail(FormatError);
		}
		Log.Add(Text);
		return EBangoCommandResult::Done;
	};
	Table.Add(Hint, Error);

	// Localized text: Say logs a text with its {0}, {1}, ... filled in.
	FBangoCommand Say;
	Say.Signature = FBangoCommandSignature::Make(TEXT("Say")).Param(TEXT("Text"), FBangoType::Text()).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}"));
	Say.Start = [&Log](FBangoCommandCall& Call)
	{
		TArray<FBangoValue> Values;
		for (int32 i = 1; i < Call.Args.Num(); ++i)
		{
			Values.Add(Call.Args[i]);
		}
		FText Text;
		FString FormatError;
		if (!Bango::FormatLocalizedText(Call.Args[0].GetText(), Values, Text, FormatError))
		{
			return Call.Fail(FormatError);
		}
		Log.Add(Text.ToString());
		return EBangoCommandResult::Done;
	};
	Table.Add(Say, Error);

	FBangoCommand FormatTextCommand;
	FormatTextCommand.Signature = FBangoCommandSignature::Make(TEXT("FormatText")).Param(TEXT("Text"), FBangoType::Text()).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).Returns(FBangoType::Text());
	FormatTextCommand.Start = [](FBangoCommandCall& Call)
	{
		TArray<FBangoValue> Values;
		for (int32 i = 1; i < Call.Args.Num(); ++i)
		{
			Values.Add(Call.Args[i]);
		}
		FText Text;
		FString FormatError;
		if (!Bango::FormatLocalizedText(Call.Args[0].GetText(), Values, Text, FormatError))
		{
			return Call.Fail(FormatError);
		}
		Call.Result = FBangoValue::MakeText(Text);
		return EBangoCommandResult::Done;
	};
	Table.Add(FormatTextCommand, Error);

	FBangoCommand ToTextCommand;
	ToTextCommand.Signature = FBangoCommandSignature::Make(TEXT("ToText")).Param(TEXT("Value"), String).Returns(FBangoType::Text());
	ToTextCommand.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeText(Bango::MakeInvariantText(Call.Args[0].GetString()));
		return EBangoCommandResult::Done;
	};
	Table.Add(ToTextCommand, Error);

	FBangoCommand Speak;
	Speak.Signature = FBangoCommandSignature::Make(TEXT("Speak")).Target(Entity).Param(TEXT("text"), String);
	Speak.Start = [&Log](FBangoCommandCall& Call)
	{
		Log.Add(Call.Target.GetEntityName() + TEXT(": ") + Call.Args[0].GetString());
		return EBangoCommandResult::Done;
	};
	Table.Add(Speak, Error);

	FBangoCommand GetName;
	GetName.Signature = FBangoCommandSignature::Make(TEXT("GetName")).Target(Entity).Returns(String);
	GetName.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeString(Call.Target.GetEntityName());
		return EBangoCommandResult::Done;
	};
	Table.Add(GetName, Error);

	// Like the built-in IsNull: true for a nil entity or one no actor has (here, anything but john and player).
	FBangoCommand IsNull;
	IsNull.Signature = FBangoCommandSignature::Make(TEXT("IsNull")).Param(TEXT("Entity"), Entity).MayBeNil().Returns(FBangoType::Bool());
	IsNull.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeBool(Call.Args[0].IsNil() || !FBangoTestEntities().IsEntityRegistered(Call.Args[0].GetEntityName()));
		return EBangoCommandResult::Done;
	};
	Table.Add(IsNull, Error);

	// Latent: waits until the given seconds have passed.
	FBangoCommand Sleep;
	Sleep.Signature = FBangoCommandSignature::Make(TEXT("Sleep")).Optional().Param(TEXT("seconds"), Double); // like the built-in Sleep
	Sleep.Start = [](FBangoCommandCall& Call)
	{
		Call.LatentState.Add(Call.Args.Num() > 0 ? Call.Args[0] : FBangoValue::MakeDouble(0.0)); // Sleep(): the next frame
		return EBangoCommandResult::Pending;
	};
	Sleep.Poll = [](FBangoCommandCall& Call)
	{
		const double Remaining = Call.LatentState[0].GetDouble() - Call.DeltaSeconds;
		Call.LatentState[0] = FBangoValue::MakeDouble(Remaining);
		return Remaining <= 0.0 ? EBangoCommandResult::Done : EBangoCommandResult::Pending;
	};
	Table.Add(Sleep, Error);

	// Latent with a result: answers 2 on the second poll.
	FBangoCommand AskChoice;
	AskChoice.Signature = FBangoCommandSignature::Make(TEXT("AskChoice")).Param(TEXT("a"), String).Param(TEXT("b"), String).Returns(Int).Discardable();
	AskChoice.Start = [](FBangoCommandCall& Call)
	{
		Call.LatentState.Add(FBangoValue::MakeInt(0));
		return EBangoCommandResult::Pending;
	};
	AskChoice.Poll = [](FBangoCommandCall& Call)
	{
		const int32 Polls = Call.LatentState[0].GetInt() + 1;
		Call.LatentState[0] = FBangoValue::MakeInt(Polls);
		if (Polls < 2)
		{
			return EBangoCommandResult::Pending;
		}
		Call.Result = FBangoValue::MakeInt(2);
		return EBangoCommandResult::Done;
	};
	Table.Add(AskChoice, Error);

	FBangoCommand Fail;
	Fail.Signature = FBangoCommandSignature::Make(TEXT("Fail"));
	Fail.Start = [](FBangoCommandCall& Call) { return Call.Fail(TEXT("boom")); };
	Table.Add(Fail, Error);

	FBangoCommand BadReturn;
	BadReturn.Signature = FBangoCommandSignature::Make(TEXT("BadReturn")).Returns(Int);
	BadReturn.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeString(TEXT("oops"));
		return EBangoCommandResult::Done;
	};
	Table.Add(BadReturn, Error);

	// A number command like the built-in Max: an int for two ints, otherwise a float.
	FBangoCommand Max;
	Max.Signature = FBangoCommandSignature::Make(TEXT("Max")).NumberParam(TEXT("A")).NumberParam(TEXT("B")).ReturnsNumber();
	Max.Start = [](FBangoCommandCall& Call)
	{
		if (Call.Args[0].GetType() == FBangoType::Int() && Call.Args[1].GetType() == FBangoType::Int())
		{
			Call.Result = FBangoValue::MakeInt(FMath::Max(Call.Args[0].GetInt(), Call.Args[1].GetInt()));
		}
		else
		{
			Call.Result = FBangoValue::MakeDouble(FMath::Max(Call.Args[0].GetNumberAsDouble(), Call.Args[1].GetNumberAsDouble()));
		}
		return EBangoCommandResult::Done;
	};
	Table.Add(Max, Error);

	// A number command that wrongly always returns a float.
	FBangoCommand BadNumber;
	BadNumber.Signature = FBangoCommandSignature::Make(TEXT("BadNumber")).NumberParam(TEXT("Value")).ReturnsNumber();
	BadNumber.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeDouble(Call.Args[0].GetNumberAsDouble());
		return EBangoCommandResult::Done;
	};
	Table.Add(BadNumber, Error);

	// A number command that wrongly returns an int for a float: turned into a float.
	FBangoCommand IntNumber;
	IntNumber.Signature = FBangoCommandSignature::Make(TEXT("IntNumber")).NumberParam(TEXT("Value")).ReturnsNumber();
	IntNumber.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeInt(3);
		return EBangoCommandResult::Done;
	};
	Table.Add(IntNumber, Error);
}

/** Globals every runtime test starts with (as if set by other scripts and Blueprints). */
static void BangoMakeRuntimeTestGlobals(FBangoGlobalStore& Globals)
{
	FString Error;
	Globals.Declare(TEXT("boss"), FBangoType::Entity(), Error);
	Globals.Set(TEXT("boss"), FBangoValue::MakeEntity(TEXT("john")), Error);
	Globals.Declare(TEXT("flag"), FBangoType::Bool(), Error);
	Globals.Set(TEXT("flag"), FBangoValue::MakeBool(true), Error);
	Globals.Declare(TEXT("lots"), FBangoType::String(), Error);
	Globals.Set(TEXT("lots"), FBangoValue::MakeString(TEXT("lots")), Error);
	Globals.Declare(TEXT("score"), FBangoType::Int(), Error);
	Globals.Set(TEXT("score"), FBangoValue::MakeInt(7), Error);
	Globals.Declare(TEXT("ratio"), FBangoType::Double(), Error);
	Globals.Set(TEXT("ratio"), FBangoValue::MakeDouble(0.5), Error);
	Globals.Declare(TEXT("spot"), FBangoType::Vector3(), Error);
	Globals.Set(TEXT("spot"), FBangoValue::MakeVector3(1.0, 2.0, 3.0), Error);
}

struct FBangoRuntimeTestResult
{
	bool bCompiled = false;
	FString CompileError;
	EBangoInstanceStatus Status = EBangoInstanceStatus::None;
	FString Error;
	TArray<FString> Log;
	int32 Resumes = 0;
};

/** Compiles and runs Source to the end (or failure), resuming every 0.5 seconds of fake time. */
static void BangoRunScript(const TCHAR* Source, FBangoRuntimeTestResult& Out, int32 MaxInstructions = 100000, const TArray<FBangoValue>& Args = TArray<FBangoValue>())
{
	FBangoCommandTable Commands;
	BangoMakeRuntimeTestCommands(Commands, Out.Log);
	FBangoGlobalStore Globals;
	BangoMakeRuntimeTestGlobals(Globals);
	FBangoTestEntities Entities;

	TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	Out.bCompiled = Bango::CompileSource(Source, TEXT("test.bango"), Commands, *Program, CompileError);
	if (!Out.bCompiled)
	{
		Out.CompileError = CompileError.ToString();
		return;
	}

	FBangoInstance Instance;
	FBangoRuntimeError StartError;
	if (!Instance.Start(Program, Commands, Globals, 1, StartError, Args))
	{
		Out.Status = Instance.GetStatus();
		Out.Error = StartError.ToString();
		return;
	}

	FBangoExecContext Context;
	Context.Globals = &Globals;
	Context.Entities = &Entities;
	Context.DeltaSeconds = 0.5f;
	Context.MaxInstructionsPerResume = MaxInstructions;
	while (Instance.IsRunning() && Out.Resumes < 1000)
	{
		Instance.Resume(Context);
		++Out.Resumes;
	}
	Out.Status = Instance.GetStatus();
	if (Out.Status == EBangoInstanceStatus::Failed)
	{
		Out.Error = Instance.GetError().ToString();
	}
}

struct FBangoRuntimeTestCase
{
	const TCHAR* Name;
	const TCHAR* Source;
	/** Lines the script must print, joined with '|'. */
	const TCHAR* ExpectedLog;
	/** For error cases: "(line:column)" (or other text) the runtime error must contain, and the message text. Null if the script must finish. */
	const TCHAR* ErrorLocation;
	const TCHAR* ErrorContains;
};

static const FBangoRuntimeTestCase GBangoRuntimeTestCases[] =
{
	// ---- Disabled nodes ----
	{ TEXT("disabled: a simple node is skipped"), TEXT("Hint(\"a\");\n/* Hint(\"b\"); */\nHint(\"c\");"), TEXT("a|c"), nullptr, nullptr },
	{ TEXT("disabled: a block node skips its block"), TEXT("/*\nif (true)\n{\n\tHint(\"x\");\n};\n*/\nHint(\"y\");"), TEXT("y"), nullptr, nullptr },
	{ TEXT("disabled: not type-checked"), TEXT("/* Hint(nosuch + 1, @ghost); */\n/*\nwhile (\"no\")\n{\n\tbreak;\n};\n*/\nHint(\"ok\");"), TEXT("ok"), nullptr, nullptr },
	{ TEXT("disabled: a break in a loop"), TEXT("int _i = 0;\nwhile (_i < 3)\n{\n\t_i++;\n\t/* break; */\n};\nHint(\"{0}\", _i);"), TEXT("3"), nullptr, nullptr },
	{ TEXT("disabled: inside a disabled block"), TEXT("/*\nif (true)\n{\n\tHint(\"in\");\n\t/* Hint(\"off\"); */\n};\n*/\nif (true)\n{\n\t/* return; */\n\tHint(\"after\");\n};"), TEXT("after"), nullptr, nullptr },

	// ---- Vectors ----
	{ TEXT("vector literals and operators"),
		TEXT("vector3 _v1 = [1, 2, 3];\nvector3 _v2 = [0.5, 0, -1];\nvector3 _vec;\nvector2 _w;\n_vec = _v1 + _v2;\nHint(\"{0} {1} {2} {3}\", _vec, _v1 - _v2, _v1 * 2, 2 * _v1 / 4);\nHint(\"{0} {1} {2}\", -_v1, _v1 == [1, 2, 3], _v1 != _v2);\n_w = [3, 4];\nHint(\"{0} {1}\", _w, _w.x * _w.x + _w.y * _w.y);"),
		TEXT("(1.5, 2.0, 2.0) (0.5, 2.0, 4.0) (2.0, 4.0, 6.0) (0.5, 1.0, 1.5)|(-1.0, -2.0, -3.0) true true|(3.0, 4.0) 25.0"), nullptr, nullptr },
	{ TEXT("vector parts"),
		TEXT("vector3 _vec = [1, 2, 3];\nvector3[] _path;\n_vec.x = 10;\n_vec.y += 0.5;\n_vec += [1, 1, 1];\n_vec *= 2;\nHint(\"{0} {1}\", _vec, _vec.z);\n_path = [[0, 0, 0], _vec];\n_path[1].z = 9;\n_path Push(_path[0] + [0, 0, 100]);\nHint(\"{0} {1}\", _path, _path[2].z);"),
		TEXT("(22.0, 7.0, 8.0) 8.0|[(0.0, 0.0, 0.0), (22.0, 7.0, 9.0), (0.0, 0.0, 100.0)] 100.0"), nullptr, nullptr },
	{ TEXT("vector params, functions and globals"),
		TEXT("#define params (vector3 _p = [1, 2, 3], vector2 _q = [-1, 0.5])\nglobal vector3 home;\nhome = [1, 1, 1];\nhome.y = 7;\nHint(\"{0} {1} {2} {3} {4}\", _p, _q, Up(5), Up(2).z, home);\nHint(\"{0} {1}\", spot.y, spot + spot);\nfunction vector3 Up(float _h)\n{\n\treturn [0, 0, _h];\n};"),
		TEXT("(1.0, 2.0, 3.0) (-1.0, 0.5) (0.0, 0.0, 5.0) 2.0 (1.0, 7.0, 1.0)|2.0 (2.0, 4.0, 6.0)"), nullptr, nullptr },
	{ TEXT("a vector divided by zero"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", _v / 0);"), nullptr, TEXT("(2:16)"), TEXT("Division by zero") },
	{ TEXT("setting a part of a nil vector"), TEXT("vector2 _v;\n_v.x = 1;"), nullptr, TEXT("(2:1)"), TEXT("'_v' is nil, so its x can't be set on its own. Give it a whole vector first, like _v = [0, 0];") },
	{ TEXT("reading a part of a nil vector"), TEXT("vector3 _v;\nHint(\"{0}\", _v.x);"), nullptr, TEXT("(2:13)"), TEXT("'_v' is nil") },
	{ TEXT("a part of an undeclared global that isn't a vector"), TEXT("Hint(\"{0}\", lots.x);"), nullptr, TEXT("(1:13)"), TEXT("'lots' is a string, which has no 'x' part") },
	{ TEXT("arithmetic"), TEXT("Hint(\"{0} {1} {2} {3}\", 1 + 2 * 3, 7 / 2, 7.0 / 2, 7 % 3);"), TEXT("7 3 3.5 1"), nullptr, nullptr },
	{ TEXT("localized text"),
		TEXT("#define LOCTEXT_NAMESPACE \"N\"\ntext[] _all;\nSay(LOCTEXT(\"A\", \"Hello {0}, {1}\"), 3, \"you\");\nHint(\"{0}\", LOCTEXT(\"B\", \"plain\"));\nSay(FormatText(LOCTEXT(\"F\", \"{1}-{0}\"), 1, @john));\nHint(\"{0} {1}\", LOCTEXT(\"C\", \"x\") == ToText(\"x\"), LOCTEXT(\"C\", \"x\") != NSLOCTEXT(\"M\", \"C\", \"y\"));\n_all = [LOCTEXT(\"B\", \"plain\"), nil];\nHint(\"{0}\", _all);\nSay(LOCTEXT(\"E\", \"`{0`} stays\"));\n#undef LOCTEXT_NAMESPACE"),
		TEXT("Hello 3, you|plain|john-1|true true|[plain, nil]|{0} stays"), nullptr, nullptr },
	{ TEXT("invariant text"),
		TEXT("text _t = INVTEXT(\"Score: {0}\");\nSay(_t, 10);\nSay(INVTEXT(\"plain\"));\nHint(\"{0}\", INVTEXT(\"x\") == ToText(\"x\"));"),
		TEXT("Score: 10|plain|true"), nullptr, nullptr },
	{ TEXT("a text placeholder with no value while running"), TEXT("text _t;\n_t = ToText(\"{1}\");\nSay(_t, 1);"), nullptr, TEXT("(3:1)"), TEXT("uses {1}, but only 1 value(s) were given") },
	{ TEXT("a nil text"), TEXT("text _t;\nSay(_t);"), nullptr, TEXT("(2:5)"), TEXT("'_t' is nil") },
	{ TEXT("floats"), TEXT("Hint(\"{0} {1} {2}\", 10 / 4.0, -2.5 * 2, 7.5 % 2);"), TEXT("2.5 -5.0 1.5"), nullptr, nullptr },
	{ TEXT("strings"), TEXT("string _s;\n_s = \"a\" + \"b\";\n_s += \"c\";\nHint(_s);\nHint(\"100% sure\");"), TEXT("abc|100% sure"), nullptr, nullptr },
	{ TEXT("comparisons"), TEXT("Hint(\"{0} {1} {2} {3} {4}\", 1 == 1.0, \"a\" == \"a\", 2 < 1, true != false, @john == @player);"), TEXT("true true false true false"), nullptr, nullptr },
	{ TEXT("if chain"), TEXT("int _x = 2;\nif (_x == 1)\n{\n\tHint(\"one\");\n} else if (_x == 2)\n{\n\tHint(\"two\");\n} else\n{\n\tHint(\"other\");\n};"), TEXT("two"), nullptr, nullptr },
	{ TEXT("while, break, continue"), TEXT("int _i = 0;\nwhile (true)\n{\n\t_i++;\n\tif (_i == 2)\n\t{\n\t\tcontinue;\n\t};\n\tif (_i > 4)\n\t{\n\t\tbreak;\n\t};\n\tHint(\"{0}\", _i);\n};"), TEXT("1|3|4"), nullptr, nullptr },
	{ TEXT("for"), TEXT("int _sum = 0;\nfor (int _i = 1; _i <= 10; _i++)\n{\n\t_sum += _i;\n};\nHint(\"{0}\", _sum);"), TEXT("55"), nullptr, nullptr },
	{ TEXT("for continue runs the step"), TEXT("for (int _i = 0; _i < 5; _i++)\n{\n\tif (_i % 2 == 0)\n\t{\n\t\tcontinue;\n\t};\n\tHint(\"{0}\", _i);\n};"), TEXT("1|3"), nullptr, nullptr },
	{ TEXT("loop body locals reset"), TEXT("int _t;\nfor (int _i = 0; _i < 2; _i++)\n{\n\t_t = nil;\n\tHint(\"{0}\", !IsNil(_t));\n\t_t = 1;\n};"), TEXT("false|false"), nullptr, nullptr },
	{ TEXT("forEach with gaps"), TEXT("int[] _a = [1, 2];\n_a[4] = 5;\nforEach (_v in _a)\n{\n\tif (!IsNil(_v))\n\t{\n\t\tHint(\"{0}\", _v);\n\t}\n\telse\n\t{\n\t\tHint(\"gap\");\n\t};\n};"), TEXT("1|2|gap|gap|5"), nullptr, nullptr },
	{ TEXT("forEach loops over a copy"), TEXT("int[] _a = [1, 2];\nforEach (_v in _a)\n{\n\t_a Push(_v);\n};\nHint(\"{0}\", _a);"), TEXT("[1, 2, 1, 2]"), nullptr, nullptr },
	{ TEXT("forEach break and continue"), TEXT("forEach (_v in [1, 2, 3, 4])\n{\n\tif (_v == 2)\n\t{\n\t\tcontinue;\n\t};\n\tif (_v == 4)\n\t{\n\t\tbreak;\n\t};\n\tHint(\"{0}\", _v);\n};"), TEXT("1|3"), nullptr, nullptr },
	{ TEXT("nested arrays"), TEXT("int[][] _g = [[1, 2], [3]];\n_g[3][2] = 7;\n_g[0] Push(4);\n_g[0][0] += 10;\nHint(\"{0} {1}\", _g, Count(_g));\nHint(\"{0} {1}\", !IsNil(_g[2]), !IsNil(_g[3][2]));"), TEXT("[[11, 2, 4], [3], nil, [nil, nil, 7]] 4|false true"), nullptr, nullptr },
	{ TEXT("array commands"), TEXT("int[] _a = [1, 2, 3];\nint _last;\nint _first;\n_a InsertAt(0, 0);\n_last = _a Pop();\n_first = _a RemoveAt(0);\n_a InsertAt(9, 5);\nHint(\"{0} {1} {2}\", _a, _last, _first);"), TEXT("[1, 2, nil, nil, nil, 9] 3 0"), nullptr, nullptr },
	{ TEXT("arrays are values"), TEXT("int[] _a = [1];\nint[] _b;\n_b = _a;\n_b Push(2);\nHint(\"{0} {1}\", Count(_a), Count(_b));"), TEXT("1 2"), nullptr, nullptr },
	{ TEXT("Pop of nil discarded"), TEXT("int[] _a = [];\n_a[1] = 5;\n_a RemoveAt(1);\n_a Pop();\nHint(\"{0}\", Count(_a));"), TEXT("0"), nullptr, nullptr },
	{ TEXT("recursion"), TEXT("function int Fact(int _n)\n{\n\tif (_n <= 1)\n\t{\n\t\treturn 1;\n\t};\n\treturn _n * Fact(_n - 1);\n};\nHint(\"{0}\", Fact(10));"), TEXT("3628800"), nullptr, nullptr },
	{ TEXT("function arguments are copies"), TEXT("int[] _x = [];\nfunction void Grow(int[] _a)\n{\n\t_a Push(1);\n};\nGrow(_x);\nHint(\"{0}\", Count(_x));"), TEXT("0"), nullptr, nullptr },
	{ TEXT("function return conversion"), TEXT("function float Half(int _n)\n{\n\treturn _n / 2;\n};\nHint(\"{0}\", Half(3));"), TEXT("1.0"), nullptr, nullptr },
	{ TEXT("functions with globals and latent commands"), TEXT("global int gold;\ngold = 1;\nfunction void Pay(int _n)\n{\n\tSleep(1.0);\n\tgold -= _n;\n\t@john Speak(\"thanks\");\n};\nPay(1);\nHint(\"{0}\", gold);"), TEXT("john: thanks|0"), nullptr, nullptr },
	{ TEXT("goto loop"), TEXT("int _i = 0;\nlabel Top;\n_i++;\nif (_i < 3)\n{\n\tgoto Top;\n};\nHint(\"{0}\", _i);"), TEXT("3"), nullptr, nullptr },
	{ TEXT("goto out of a loop"), TEXT("while (true)\n{\n\tgoto Done;\n};\nlabel Done;\nHint(\"out\");"), TEXT("out"), nullptr, nullptr },
	{ TEXT("top-level return"), TEXT("Hint(\"a\");\nreturn;\nHint(\"b\");"), TEXT("a"), nullptr, nullptr },
	{ TEXT("globals"), TEXT("global int gold;\ngold = 5;\ngold += 2;\nHint(\"{0}\", gold);"), TEXT("7"), nullptr, nullptr },
	{ TEXT("undeclared globals"), TEXT("if (flag)\n{\n\tboss Speak(\"hi\");\n};\nHint(\"{0}\", boss GetName());"), TEXT("john: hi|john"), nullptr, nullptr },
	{ TEXT("Bango IDs"), TEXT("entity _e = @john;\n_e Speak(\"a\");\n@player Speak(\"b\");\nHint(\"{0} {1} {2} {3}\", _e, [@john, @ghost], @john == _e, @john != @player);"), TEXT("john: a|player: b|john [john, ghost] true true"), nullptr, nullptr },
	{ TEXT("Bango ID that isn't in the level, unused"), TEXT("entity _g = @ghost;\nHint(\"{0}\", _g);"), TEXT("ghost"), nullptr, nullptr },
	{ TEXT("Bango IDs as parameter defaults"), TEXT("#define params (entity _who = @john, entity[] _all = [@john, @player])\n_who Speak(\"hi\");\nforEach (_x in _all)\n{\n\t_x Speak(\"all\");\n};"), TEXT("john: hi|john: all|player: all"), nullptr, nullptr },
	{ TEXT("IsNil on globals"), TEXT("Hint(\"{0} {1}\", !IsNil(nothere), !IsNil(flag));"), TEXT("false true"), nullptr, nullptr },
	{ TEXT("short-circuit"), TEXT("bool _b;\nbool _c;\n_b = !IsNil(nothere) && nothere;\n_c = flag || nothere;\nHint(\"{0} {1}\", _b, _c);"), TEXT("false true"), nullptr, nullptr },
	{ TEXT("latent commands"), TEXT("Hint(\"start\");\nSleep(1.0);\nif (AskChoice(\"a\", \"b\") == 2)\n{\n\tHint(\"two\");\n};"), TEXT("start|two"), nullptr, nullptr },
	{ TEXT("nil as an extra value"), TEXT("Hint(\"{0} and {1}\", 1, nil);"), TEXT("1 and nil"), nullptr, nullptr },
	{ TEXT("nil assignment"), TEXT("int _x = 1;\n_x = nil;\nHint(\"{0}\", !IsNil(_x));"), TEXT("false"), nullptr, nullptr },

	{ TEXT("read nil local"), TEXT("int _x;\nHint(\"{0}\", _x);"), TEXT(""), TEXT("(2:13)"), TEXT("'_x' is nil") },
	{ TEXT("missing global"), TEXT("Hint(\"{0}\", nothere);"), TEXT(""), TEXT("(1:13)"), TEXT("Global 'nothere' doesn't exist") },
	{ TEXT("index past end"), TEXT("int[] _a = [1];\nHint(\"{0}\", _a[3]);"), TEXT(""), TEXT("(2:15)"), TEXT("'_a[3]' is past the end of '_a', which has 1 element(s)") },
	{ TEXT("negative index"), TEXT("int[] _a = [1];\nint _i = -1;\nHint(\"{0}\", _a[_i]);"), TEXT(""), TEXT("(3:15)"), TEXT("negative") },
	{ TEXT("division by zero"), TEXT("int _z = 0;\nint _x;\n_x = 1 / _z;"), TEXT(""), TEXT("(3:8)"), TEXT("Division by zero") },
	{ TEXT("modulo by zero"), TEXT("float _z = 0.0;\nfloat _x;\n_x = 1.5 % _z;"), TEXT(""), TEXT("(3:10)"), TEXT("Modulo by zero") },
	{ TEXT("integer overflow"), TEXT("int _x = 2147483647;\n_x++;"), TEXT(""), TEXT("(2:1)"), TEXT("Integer overflow") },
	{ TEXT("int range edges"), TEXT("int _min;\n_min = -2147483647 - 1;\nHint(\"{0} {1} {2}\", _min, 2147483647, _min + 2147483647);"), TEXT("-2147483648 2147483647 -1"), nullptr, nullptr },
	{ TEXT("int overflow subtract"), TEXT("int _x = -2147483647;\n_x -= 2;"), TEXT(""), TEXT("(2:1)"), TEXT("outside the int range") },
	{ TEXT("int overflow multiply"), TEXT("int _x;\n_x = 65536 * 65536;"), TEXT(""), TEXT("(2:12)"), TEXT("Integer overflow") },
	{ TEXT("int overflow divide"), TEXT("int _min;\nint _x;\n_min = -2147483647 - 1;\n_x = _min / -1;"), TEXT(""), TEXT("(4:11)"), TEXT("Integer overflow") },
	{ TEXT("int overflow negate"), TEXT("int _min;\nint _x;\n_min = -2147483647 - 1;\n_x = -_min;"), TEXT(""), TEXT("(4:6)"), TEXT("-2147483648 has no positive int") },
	{ TEXT("large numbers as float"), TEXT("float _d;\n_d = 65536 * 65536.0;\nHint(\"{0}\", _d);"), TEXT("4294967296.0"), nullptr, nullptr },
	{ TEXT("infinite loop"), TEXT("while (true)\n{\n};"), TEXT(""), TEXT("(1:"), TEXT("stuck in an infinite loop") },
	{ TEXT("runaway recursion"), TEXT("function void F()\n{\n\tF();\n};\nF();"), TEXT(""), TEXT("in F()"), TEXT("Too many nested function calls") },
	{ TEXT("missing return"), TEXT("function int F(bool _b)\n{\n\tif (_b)\n\t{\n\t\treturn 1;\n\t};\n};\nHint(\"{0}\", F(false));"), TEXT(""), TEXT("in F()"), TEXT("reached its end without returning") },
	{ TEXT("command fails"), TEXT("Hint(\"before\");\nFail();"), TEXT("before"), TEXT("(2:1)"), TEXT("Fail: boom") },
	{ TEXT("names without underscores"), TEXT("global int _shared;\nint count = 2;\nint[] list = [1, 2];\n_shared = count;\nforEach (item in list)\n{\n\tcount += item;\n};\nHint(\"{0} {1}\", count, _shared);"), TEXT("5 2"), nullptr, nullptr },
	{ TEXT("a local hides a global of the same name"), TEXT("global int same;\nint same = 7;\nHint(\"{0}\", same);"), TEXT("7"), nullptr, nullptr },
	{ TEXT("an undeclared name without underscore is a global read"), TEXT("Hint(\"{0}\", cuont);"), TEXT(""), TEXT("(1:13)"), TEXT("Global 'cuont' doesn't exist") },
	{ TEXT("string placeholders: {n}, backtick escapes, and what's left as it is"), TEXT("Hint(\"`{0`} is {0}, `` and {Name}, 100% and {1\", 5);"), TEXT("{0} is 5, ` and {Name}, 100% and {1"), nullptr, nullptr },
	{ TEXT("IsNull on nil and missing actors"), TEXT("entity _e;\nentity[] _a = [@john];\nHint(\"{0} {1} {2}\", IsNull(@john), IsNull(@ghost), IsNull(_e));\nHint(\"{0} {1} {2}\", IsNull(_a[0]), IsNull(_a[5]), IsNull(nobody));\n_e = @player;\nHint(\"{0}\", IsNull(_e));"), TEXT("false true true|false true true|false"), nullptr, nullptr },
	{ TEXT("IsNull on something that isn't an entity"), TEXT("bool _b;\n_b = IsNull(lots);"), TEXT(""), TEXT("(2:6)"), TEXT("Expected an entity here, but the value is a string") },
	{ TEXT("unregistered entity"), TEXT("@ghost Speak(\"boo\");"), TEXT(""), TEXT("(1:1)"), TEXT("no actor is registered with the Bango ID name 'ghost'") },
	{ TEXT("Bango ID written without @"), TEXT("john Speak(\"hi\");"), TEXT(""), TEXT("(1:1)"), TEXT("Global 'john' doesn't exist. There is an actor with the Bango ID 'john': write @john to use it.") },
	{ TEXT("missing global that isn't a Bango ID"), TEXT("ghost Speak(\"hi\");"), TEXT(""), TEXT("(1:1)"), TEXT("Global 'ghost' doesn't exist. No script or Blueprint has declared it.") },
	{ TEXT("undeclared global of the wrong type"), TEXT("int _x;\n_x = lots + 1;"), TEXT(""), TEXT("(2:11)"), TEXT("'+' needs two numbers or two strings, but got a string and an int") },
	{ TEXT("undeclared global as condition"), TEXT("if (lots)\n{\n};"), TEXT(""), TEXT("(1:"), TEXT("condition must be a bool") },
	{ TEXT("undeclared global as wrong target"), TEXT("lots Speak(\"x\");"), TEXT(""), TEXT("(1:1)"), TEXT("Expected an entity here, but the value is a string") },
	{ TEXT("Pop of nil used"), TEXT("int[] _a = [];\nint _x;\n_a[1] = 5;\n_a RemoveAt(1);\n_x = _a Pop();"), TEXT(""), TEXT("(5:6)"), TEXT("removed a nil element") },
	{ TEXT("nested write into nil array"), TEXT("int[][] _g;\n_g[0][0] = 1;"), TEXT(""), TEXT("(2:1)"), TEXT("Writing to '_g'") },
	{ TEXT("compound on missing inner array"), TEXT("int[][] _g = [];\n_g[1][2] += 5;"), TEXT(""), TEXT("(2:1)"), TEXT("past the end") },
	{ TEXT("runtime error node id"), TEXT("int _x;\nHint(\"{0}\", _x); //@8(0, 1)"), TEXT(""), TEXT("[node 8]"), TEXT("'_x' is nil") },
	// ---- Number commands (NumberParam, ReturnsNumber) ----
	{ TEXT("number command: int for ints, float otherwise"),
		TEXT("int _i = 3;\nfloat _f = 2.5;\nint _ri;\nfloat _rf;\n_ri = Max(_i, 9);\n_rf = Max(_i, _f);\n_rf = _rf + Max(1, 2);\nHint(\"{0} {1} {2} {3}\", _ri, _rf, Max(_f, 1), Max(Max(1, 2), 3));"),
		TEXT("9 5.0 2.5 3"), nullptr, nullptr },
	{ TEXT("number command: undeclared globals are checked when called"),
		TEXT("int _a;\nfloat _b;\n_a = Max(score, 2);\n_b = Max(ratio, 2);\nHint(\"{0} {1}\", _a, _b);"), TEXT("7 2.0"), nullptr, nullptr },
	{ TEXT("number command: an undeclared float global into an int"),
		TEXT("int _a;\n_a = Max(ratio, 2);"), TEXT(""), TEXT("(2:1)"), TEXT("Can't store a float in '_a', which holds an int.") },
	{ TEXT("number command: an undeclared global that isn't a number"),
		TEXT("Hint(\"{0}\", Max(lots, 2));"), TEXT(""), TEXT("(1:13)"), TEXT("Max: argument 1 ('A') must be a number (an int or a float), but it is a string") },
	{ TEXT("number command: a float result for ints is an error"),
		TEXT("Hint(\"{0}\", BadNumber(2));"), TEXT(""), TEXT("(1:13)"), TEXT("returned a float, but with these arguments it should return an int") },
	{ TEXT("number command: an int result for a float becomes a float"),
		TEXT("Hint(\"{0}\", IntNumber(2.5));"), TEXT("3.0"), nullptr, nullptr },
	{ TEXT("command returns the wrong type"), TEXT("int _x;\n_x = BadReturn();"), TEXT(""), TEXT("(2:6)"), TEXT("returned a string, but it should return an int") },
	{ TEXT("error inside a function"), TEXT("function void F()\n{\n\tint _x;\n\tHint(\"{0}\", _x);\n};\nF();"), TEXT(""), TEXT("(4:14) in F()"), TEXT("'_x' is nil") },
	{ TEXT("Hint with a missing value"), TEXT("Hint(\"{1}\", 5);"), TEXT(""), TEXT("(1:1)"), TEXT("uses {1}, but only 1 value(s)") },
};

/** Checks one case; returns an empty string if it passed. */
static FString BangoCheckRuntimeCase(const FBangoRuntimeTestCase& Case)
{
	FBangoRuntimeTestResult Result;
	BangoRunScript(Case.Source, Result, 20000);
	if (!Result.bCompiled)
	{
		return FString::Printf(TEXT("[%s] didn't compile: %s"), Case.Name, *Result.CompileError);
	}
	const FString Log = FString::Join(Result.Log, TEXT("|"));
	if (!Log.Equals(Case.ExpectedLog, ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s] output differs.\n  expected: %s\n  actual:   %s\n  error: %s"), Case.Name, Case.ExpectedLog, *Log, *Result.Error);
	}
	if (Case.ErrorContains)
	{
		if (Result.Status != EBangoInstanceStatus::Failed)
		{
			return FString::Printf(TEXT("[%s] should fail, but didn't."), Case.Name);
		}
		if (!Result.Error.Contains(Case.ErrorLocation, ESearchCase::CaseSensitive) || !Result.Error.Contains(Case.ErrorContains, ESearchCase::CaseSensitive))
		{
			return FString::Printf(TEXT("[%s]\n  expected %s containing \"%s\"\n  actual:   %s"), Case.Name, Case.ErrorLocation, Case.ErrorContains, *Result.Error);
		}
	}
	else if (Result.Status != EBangoInstanceStatus::Finished)
	{
		return FString::Printf(TEXT("[%s] didn't finish: %s"), Case.Name, *Result.Error);
	}
	return FString();
}

/** Pausing, terminating, saving and loading, and start-up failures. Adds a message to OutFailures for each problem. */
static void BangoRunInstanceScenarios(TArray<FString>& OutFailures)
{
	auto Expect = [&OutFailures](bool bCondition, const TCHAR* What)
	{
		if (!bCondition)
		{
			OutFailures.Add(What);
		}
	};

	TArray<FString> Log;
	FBangoCommandTable Commands;
	BangoMakeRuntimeTestCommands(Commands, Log);
	FBangoTestEntities Entities;

	const TCHAR* Source =
		TEXT("Hint(\"start\");\n")
		TEXT("Sleep(1.0); //@5(0, 1)\n")
		TEXT("@john Speak(\"done\");");

	TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	Expect(Bango::CompileSource(Source, TEXT("scenario.bango"), Commands, *Program, CompileError), TEXT("scenario compiles"));

	FBangoExecContext Context;
	Context.Entities = &Entities;
	Context.DeltaSeconds = 0.5f;

	// ---- Pausing ----
	{
		Log.Reset();
		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Expect(Instance.Start(Program, Commands, Globals, 1, Error), TEXT("pause: starts"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("pause: waits on Sleep"));
		Expect(Log.Num() == 1, TEXT("pause: only 'start' printed before Sleep"));
		const FBangoDebugLoc Loc = Instance.GetCurrentLocation();
		Expect(Loc.Line == 2 && Loc.NodeID == 5, TEXT("pause: current location is the Sleep node"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("pause: still waiting after 0.5s"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Finished, TEXT("pause: finishes after 1s"));
		Expect(Log.Num() == 2 && Log[1].Equals(TEXT("john: done"), ESearchCase::CaseSensitive), TEXT("pause: continues after Sleep"));
	}

	// ---- Sleep() with no time waits one frame, like Sleep(0) ----
	{
		TSharedPtr<FBangoProgram> FrameProgram = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(TEXT("Hint(\"a\");\nSleep();\nHint(\"b\");\nSleep(0);\nHint(\"c\");"), TEXT("frame.bango"), Commands, *FrameProgram, CompileError),
			TEXT("Sleep(): compiles"));
		Log.Reset();
		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Expect(Instance.Start(FrameProgram, Commands, Globals, 1, Error), TEXT("Sleep(): starts"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 1, TEXT("Sleep(): waits after 'a'"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 2, TEXT("Sleep(): one frame later, 'b', then Sleep(0) waits"));
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 3, TEXT("Sleep(0): one frame later, 'c'"));
	}

	// ---- Terminate ----
	{
		Log.Reset();
		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Instance.Start(Program, Commands, Globals, 2, Error);
		Instance.Resume(Context);
		Instance.Terminate();
		Expect(Instance.GetStatus() == EBangoInstanceStatus::Terminated, TEXT("terminate: status"));
		Instance.Resume(Context);
		Instance.Resume(Context);
		Expect(Log.Num() == 1, TEXT("terminate: nothing runs afterwards"));
	}

	// ---- Save and load while paused ----
	{
		Log.Reset();
		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Instance.Start(Program, Commands, Globals, 3, Error);
		Instance.Resume(Context);
		Instance.Resume(Context);  // 0.5s of the 1s Sleep has passed

		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Globals.Serialize(Writer);
			Instance.Serialize(Writer);
		}

		FBangoGlobalStore LoadedGlobals;
		FBangoInstance Loaded;
		{
			FMemoryReader Reader(Bytes);
			LoadedGlobals.Serialize(Reader);
			Loaded.Serialize(Reader);
			Expect(!Reader.IsError(), TEXT("save/load: reads back"));
		}
		const FBangoValue* LoadedSpot = LoadedGlobals.Find(TEXT("spot"));
		Expect(LoadedSpot && *LoadedSpot == FBangoValue::MakeVector3(1.0, 2.0, 3.0), TEXT("save/load: a vector3 global keeps its value"));
		FString BindError;
		Expect(Loaded.Bind(Program, Commands, BindError), TEXT("save/load: binds"));
		Expect(Loaded.GetStatus() == EBangoInstanceStatus::Waiting && Loaded.GetId() == 3, TEXT("save/load: still waiting, same id"));

		Context.Globals = &LoadedGlobals;
		Expect(Loaded.Resume(Context) == EBangoInstanceStatus::Finished, TEXT("save/load: the remaining 0.5s finishes the Sleep"));
		Expect(Log.Num() == 2 && Log[1].Equals(TEXT("john: done"), ESearchCase::CaseSensitive), TEXT("save/load: continues where it left off"));
	}

	// ---- Save and load mid-function, mid-expression ----
	{
		Log.Reset();
		const TCHAR* Deep =
			TEXT("int[] _list = [1, 2];\nfunction int Pick(int _base)\n{\n\tint _local;\n\t_local = _base * 10;\n\treturn _local + AskChoice(\"a\", \"b\");\n};\n")
			TEXT("_list Push(Pick(4));\nHint(\"{0}\", _list);");
		TSharedPtr<FBangoProgram> DeepProgram = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(Deep, TEXT("deep.bango"), Commands, *DeepProgram, CompileError), TEXT("deep save: compiles"));

		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Instance.Start(DeepProgram, Commands, Globals, 4, Error);
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("deep save: waiting inside the function"));

		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Instance.Serialize(Writer);
		}
		FBangoInstance Loaded;
		{
			FMemoryReader Reader(Bytes);
			Loaded.Serialize(Reader);
		}
		FString BindError;
		Expect(Loaded.Bind(DeepProgram, Commands, BindError), TEXT("deep save: binds"));
		while (Loaded.IsRunning())
		{
			Loaded.Resume(Context);
		}
		Expect(Loaded.GetStatus() == EBangoInstanceStatus::Finished, TEXT("deep save: finishes"));
		Expect(Log.Num() == 1 && Log[0].Equals(TEXT("[1, 2, 42]"), ESearchCase::CaseSensitive), TEXT("deep save: the function's locals and the half-built expression survive"));
	}

	// ---- Bind refuses a different script ----
	{
		FBangoGlobalStore Globals;
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Instance.Start(Program, Commands, Globals, 5, Error);
		Instance.Resume(Context);
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Instance.Serialize(Writer);
		}
		TSharedPtr<FBangoProgram> Other = MakeShared<FBangoProgram>();
		Bango::CompileSource(TEXT("Hint(\"x\");"), TEXT("other.bango"), Commands, *Other, CompileError);
		FBangoInstance Loaded;
		{
			FMemoryReader Reader(Bytes);
			Loaded.Serialize(Reader);
		}
		FString BindError;
		Expect(!Loaded.Bind(Other, Commands, BindError), TEXT("bind: refuses state from a different script"));
	}

	// ---- Global conflict at start ----
	{
		FBangoGlobalStore Globals;
		BangoMakeRuntimeTestGlobals(Globals);  // "lots" is a string
		TSharedPtr<FBangoProgram> Conflict = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(TEXT("global int lots;\nHint(\"never runs\");"), TEXT("conflict.bango"), Commands, *Conflict, CompileError), TEXT("conflict: compiles"));
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Expect(!Instance.Start(Conflict, Commands, Globals, 6, Error), TEXT("conflict: start fails"));
		Expect(Error.Message.Contains(TEXT("already exists as string"), ESearchCase::CaseSensitive) && Error.Line == 1, TEXT("conflict: error names the existing type and the line"));
	}

	// ---- A command with no implementation ----
	{
		FBangoCommandTable SignatureOnly;
		FString AddError;
		SignatureOnly.Add(FBangoCommandSignature::Make(TEXT("Missing")), AddError);
		TSharedPtr<FBangoProgram> NoImpl = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(TEXT("Missing();"), TEXT("noimpl.bango"), SignatureOnly, *NoImpl, CompileError), TEXT("no implementation: compiles"));
		FBangoGlobalStore Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Expect(!Instance.Start(NoImpl, SignatureOnly, Globals, 7, Error), TEXT("no implementation: start fails"));
		Expect(Error.Message.Contains(TEXT("no implementation"), ESearchCase::CaseSensitive), TEXT("no implementation: says so"));
	}

	// ---- Two instances share globals ----
	{
		Log.Reset();
		FBangoGlobalStore Globals;
		Context.Globals = &Globals;
		TSharedPtr<FBangoProgram> Writer = MakeShared<FBangoProgram>();
		TSharedPtr<FBangoProgram> Reader = MakeShared<FBangoProgram>();
		Bango::CompileSource(TEXT("global string message;\nmessage = \"hello\";"), TEXT("writer.bango"), Commands, *Writer, CompileError);
		Bango::CompileSource(TEXT("Hint(message);"), TEXT("reader.bango"), Commands, *Reader, CompileError);
		FBangoInstance A;
		FBangoInstance B;
		FBangoRuntimeError Error;
		A.Start(Writer, Commands, Globals, 8, Error);
		A.Resume(Context);
		B.Start(Reader, Commands, Globals, 9, Error);
		B.Resume(Context);
		Expect(B.GetStatus() == EBangoInstanceStatus::Finished && Log.Num() == 1 && Log[0].Equals(TEXT("hello"), ESearchCase::CaseSensitive), TEXT("shared globals: one script reads what another wrote"));
	}

	// ---- Hot reload (RequestReload): switching a running script to a new version ----
	{
		auto Compile = [&](const TCHAR* ReloadSource) -> TSharedPtr<FBangoProgram>
		{
			TSharedPtr<FBangoProgram> Compiled = MakeShared<FBangoProgram>();
			FBangoCompileError ReloadError;
			if (!Bango::CompileSource(ReloadSource, TEXT("reload.bango"), Commands, *Compiled, ReloadError))
			{
				OutFailures.Add(FString::Printf(TEXT("reload: doesn't compile: %s"), *ReloadError.ToString()));
			}
			return Compiled;
		};
		struct FReloadRun
		{
			FBangoGlobalStore Globals;
			FBangoInstance Instance;
		};
		auto StartRun = [&](FReloadRun& Run, const TSharedPtr<FBangoProgram>& First) -> bool
		{
			Log.Reset();
			BangoMakeRuntimeTestGlobals(Run.Globals);
			Context.Globals = &Run.Globals;
			FBangoRuntimeError StartError;
			return Run.Instance.Start(First, Commands, Run.Globals, 1, StartError) && Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting;
		};
		auto LastLog = [&Log]() { return Log.Num() > 0 ? Log.Last() : FString(); };

		// A moving platform: the math in the loop changes while it runs, and a local is added.
		const TCHAR* PlatformV1 =
			TEXT("float _x = 0;\nfloat _speed = 1;\n")
			TEXT("while (true)    //@1(0, 0)\n{\n")
			TEXT("\t_x += _speed;    //@2(2, 0)\n")
			TEXT("\tHint(\"{0}\", _x);    //@3(4, 0)\n")
			TEXT("\tSleep(0);    //@4(6, 0)\n};");
		const TCHAR* PlatformV2 =
			TEXT("float _x = 0;\nfloat _speed = 1;\nfloat _extra = 5;\n")
			TEXT("while (true)    //@1(0, 0)\n{\n")
			TEXT("\t_x += _speed * 10;    //@2(2, 0)\n")
			TEXT("\tHint(\"{0} {1}\", _x, _extra);    //@3(4, 0)\n")
			TEXT("\tSleep(0);    //@4(6, 0)\n};");
		{
			FReloadRun Run;
			Expect(StartRun(Run, Compile(PlatformV1)) && LastLog() == TEXT("1.0"), TEXT("reload: the platform loop runs once and waits"));
			FString ReloadError;
			Expect(Run.Instance.RequestReload(Compile(PlatformV2), Commands, ReloadError), TEXT("reload: the request is accepted"));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("reload: still running"));
			Expect(LastLog() == TEXT("11.0 5.0"), TEXT("reload: the loop's new math runs, with the old _x, and the new local's starting value"));
			FString Message;
			Expect(Run.Instance.TakeReloadResult(Message) == EBangoReloadResult::Reloaded, TEXT("reload: reports that it switched"));
			Expect(Run.Instance.TakeReloadResult(Message) == EBangoReloadResult::None, TEXT("reload: reports it once"));
			Run.Instance.Resume(Context);
			Expect(LastLog() == TEXT("21.0 5.0"), TEXT("reload: keeps running the new version"));
		}

		// A local that changes type can't be carried over: it keeps the old version, and says why.
		{
			FReloadRun Run;
			StartRun(Run, Compile(PlatformV1));
			FString ReloadError;
			Run.Instance.RequestReload(Compile(TEXT("int _x = 0;\nfloat _speed = 1;\nwhile (true)    //@1(0, 0)\n{\n\t_x += 5;    //@2(2, 0)\n\tHint(\"{0}\", _x);    //@3(4, 0)\n\tSleep(0);    //@4(6, 0)\n};")), Commands, ReloadError);
			Run.Instance.Resume(Context);
			FString Message;
			const EBangoReloadResult TypeResult = Run.Instance.TakeReloadResult(Message);
			Expect(TypeResult == EBangoReloadResult::KeptOld && Message.Contains(TEXT("'_x' was float and is int")), *FString::Printf(TEXT("reload: a changed type keeps the old version (%d: %s)"), (int32)TypeResult, *Message));
			Expect(LastLog() == TEXT("2.0") && !Run.Instance.IsReloadPending(), TEXT("reload: the old version carries on"));
		}

		// Paused inside a function: the function's new code runs, and it goes back to the same call in the main graph.
		{
			FReloadRun Run;
			const TCHAR* StepV1 = TEXT("global int count;\ncount = 0;    //@1(0, 0)\nwhile (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tcount += 1;    //@10(0, 0)\n\tHint(\"{0}\", count);    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};");
			const TCHAR* StepV2 = TEXT("global int count;\ncount = 0;    //@1(0, 0)\nwhile (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tcount += 100;    //@10(0, 0)\n\tHint(\"step {0}\", count);    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};");
			Expect(StartRun(Run, Compile(StepV1)) && LastLog() == TEXT("1"), TEXT("reload: Step runs once"));
			FString ReloadError;
			Run.Instance.RequestReload(Compile(StepV2), Commands, ReloadError);
			Run.Instance.Resume(Context);
			FString Message;
			Expect(Run.Instance.TakeReloadResult(Message) == EBangoReloadResult::Reloaded, TEXT("reload: switches inside a function"));
			Expect(LastLog() == TEXT("step 101"), TEXT("reload: the function returns to the main graph's loop, which calls its new version"));
		}

		// For the debugger: where it is, and the calls on the way there.
		{
			FReloadRun Run;
			StartRun(Run, Compile(TEXT("while (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tHint(\"x\");    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};")));
			TArray<FBangoCallFrameInfo> CallStack;
			Run.Instance.GetCallStack(CallStack);
			Expect(CallStack.Num() == 2 && CallStack[0].FunctionName.IsEmpty() && CallStack[0].Loc.NodeID == 3
				&& CallStack[1].FunctionName == TEXT("Step") && CallStack[1].Loc.NodeID == 12, TEXT("debug: the call stack shows the call in the main graph, then the Sleep in Step"));
			Expect(Run.Instance.GetWaitingCommandName() == TEXT("Sleep"), TEXT("debug: it says what it's waiting on"));
		}

		// For the debugger's trail: the nodes it runs, in order.
		{
			FReloadRun Run;
			Run.Instance.SetRecordTrail(true);
			auto TrailText = [](const TArray<int64>& TrailNodes)
			{
				FString Text;
				for (const int64 Node : TrailNodes)
				{
					Text += FString::Printf(TEXT("%lld "), Node);
				}
				return Text;
			};
			StartRun(Run, Compile(TEXT("int _n = 0;\nwhile (true)    //@1(0, 0)\n{\n\t_n++;    //@2(2, 0)\n\tif (_n > 1)    //@3(4, 0)\n\t{\n\t\tHint(\"x\");    //@4(6, 0)\n\t};\n\n\tSleep(0);    //@5(8, 0)\n};")));
			TArray<int64> Nodes;
			uint64 Seen = Run.Instance.GetTrail(0, Nodes);
			Expect(Nodes == TArray<int64>({ 1, 2, 3, 5 }), *FString::Printf(TEXT("trail: the first time round, the if's Do isn't run (%s)"), *TrailText(Nodes)));
			Run.Instance.Resume(Context);
			Nodes.Reset();
			Seen = Run.Instance.GetTrail(Seen, Nodes);
			Expect(Nodes == TArray<int64>({ 1, 2, 3, 4, 5 }) && Seen == 9, *FString::Printf(TEXT("trail: the second time round it is, and only the new nodes are given (%s)"), *TrailText(Nodes)));
		}

		// Conditions (Bango::CompileCondition): a script, or one expression, that returns true or false.
		{
			struct FConditionCase
			{
				const TCHAR* Name;
				const TCHAR* Source;
				int32 Expected;              // 1 true, 0 false, -1 an error
				const TCHAR* ErrorLocation;  // for errors: like "(1:5)"
				const TCHAR* ErrorContains;
			};
			const FConditionCase Cases[] =
			{
				{ TEXT("expression"), TEXT("lots == \"lots\""), 1, nullptr, nullptr },
				{ TEXT("expression, false"), TEXT("Count([1, 2]) > 2"), 0, nullptr, nullptr },
				{ TEXT("expression with comments and a params line"), TEXT("// Is it big?\n#define params (int _n = 7)\n\n_n > 5    // more than five"), 1, nullptr, nullptr },
				{ TEXT("expression with a string holding ;"), TEXT("lots != \"a;b\" && lots != \"//\""), 1, nullptr, nullptr },
				{ TEXT("a script that returns"), TEXT("int _n = 0;\nfor (int _i = 0; _i < 4; _i++)\n{\n\t_n += _i;\n};\nreturn _n == 6;"), 1, nullptr, nullptr },
				{ TEXT("a script with a function"), TEXT("function bool Big(int _v)\n{\n\treturn _v > 10;\n};\n\nreturn Big(3);"), 0, nullptr, nullptr },
				{ TEXT("not a bool"), TEXT("3 + 4"), -1, TEXT("(1:3)"), TEXT("The condition needs a bool, but this is an int.") },
				{ TEXT("an error points into the expression as written"), TEXT("lots == missing_cmd()"), -1, TEXT("(1:9)"), TEXT("missing_cmd") },
				{ TEXT("return with no value"), TEXT("return;"), -1, TEXT("(1:1)"), TEXT("A condition must return true or false") },
				{ TEXT("reaching the end"), TEXT("int _n = 1;\nif (_n > 5)\n{\n\treturn true;\n};"), -1, nullptr, TEXT("reached its end without returning true or false") },
				{ TEXT("a runtime error points into the expression as written"), TEXT("lots == \"lots\" && nothere > 1"), -1, TEXT("(1:19)"), TEXT("nothere") },
			};
			for (const FConditionCase& Case : Cases)
			{
				TSharedPtr<FBangoProgram> Condition = MakeShared<FBangoProgram>();
				FBangoCompileError ConditionError;
				const bool bCompiled = Bango::CompileCondition(Case.Source, TEXT("cond"), Commands, *Condition, ConditionError);
				FString Outcome;
				int32 Got = -1;
				if (!bCompiled)
				{
					Outcome = ConditionError.ToString();
				}
				else
				{
					FBangoGlobalStore ConditionGlobals;
					BangoMakeRuntimeTestGlobals(ConditionGlobals);
					Context.Globals = &ConditionGlobals;
					FBangoInstance Instance;
					FBangoRuntimeError StartError;
					if (Instance.Start(Condition, Commands, ConditionGlobals, 1, StartError) && Instance.Resume(Context) == EBangoInstanceStatus::Finished
						&& Instance.GetResult().GetType() == FBangoType::Bool())
					{
						Got = Instance.GetResult().GetBool() ? 1 : 0;
					}
					Outcome = Instance.GetStatus() == EBangoInstanceStatus::Failed ? Instance.GetError().ToString() : StartError.ToString();
				}
				const bool bOk = Case.Expected >= 0
					? Got == Case.Expected
					: Got == -1 && Outcome.Contains(Case.ErrorContains) && (!Case.ErrorLocation || Outcome.Contains(Case.ErrorLocation));
				Expect(bOk, *FString::Printf(TEXT("condition: %s (got %d: %s)"), Case.Name, Got, *Outcome));
			}
		}

		// A call in the middle of a calculation can't be switched while it's under way, so the switch waits until the
		// statement it's in has finished.
		{
			FReloadRun Run;
			const TCHAR* MidV1 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 1 + Wait();    //@2(2, 0)\n\tHint(\"{0}\", _r);    //@3(4, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\treturn 1;    //@11(2, 0)\n};");
			const TCHAR* MidV2 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 1 + Wait();    //@2(2, 0)\n\tHint(\"new {0}\", _r);    //@3(4, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\treturn 1;    //@11(2, 0)\n};");
			Expect(StartRun(Run, Compile(MidV1)), TEXT("reload: waits inside Wait()"));
			FString ReloadError;
			Run.Instance.RequestReload(Compile(MidV2), Commands, ReloadError);
			Run.Instance.Resume(Context);
			FString Message;
			const EBangoReloadResult MidResult = Run.Instance.TakeReloadResult(Message);
			Expect(MidResult == EBangoReloadResult::Reloaded && Message.Contains(TEXT("after node 2")), *FString::Printf(TEXT("reload: a call mid-calculation switches after its statement (%d: %s)"), (int32)MidResult, *Message));
			Expect(LastLog() == TEXT("new 2"), *FString::Printf(TEXT("reload: and carries on in the new version (%s)"), *LastLog()));
		}

		// If it pauses again before reaching a point where the new version fits, it gives up and says why.
		{
			FReloadRun Run;
			const TCHAR* PauseV1 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 1 + Wait();    //@2(2, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\tSleep(0);    //@12(2, 0)\n\treturn 1;    //@11(4, 0)\n};");
			const TCHAR* PauseV2 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 2 + Wait();    //@2(2, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\tSleep(0);    //@12(2, 0)\n\treturn 1;    //@11(4, 0)\n};");
			StartRun(Run, Compile(PauseV1));
			FString ReloadError;
			Run.Instance.RequestReload(Compile(PauseV2), Commands, ReloadError);
			Run.Instance.Resume(Context);
			FString Message;
			const EBangoReloadResult PauseResult = Run.Instance.TakeReloadResult(Message);
			Expect(PauseResult == EBangoReloadResult::KeptOld && Message.Contains(TEXT("part of a bigger calculation")), *FString::Printf(TEXT("reload: gives up at the next pause (%d: %s)"), (int32)PauseResult, *Message));
		}

		// A command the new version calls must exist.
		{
			FReloadRun Run;
			StartRun(Run, Compile(PlatformV1));
			TSharedPtr<FBangoProgram> Unknown = MakeShared<FBangoProgram>();
			FBangoCommandTable SignatureOnly;
			SignatureOnly.Add(FBangoCommandSignature::Make(TEXT("Nothing")), CompileError.Message);
			FBangoCompileError UnknownError;
			Bango::CompileSource(TEXT("Nothing();    //@1(0, 0)"), TEXT("unknown.bango"), SignatureOnly, *Unknown, UnknownError);
			FString ReloadError;
			Expect(!Run.Instance.RequestReload(Unknown, Commands, ReloadError) && ReloadError.Contains(TEXT("'Nothing'")) && !Run.Instance.IsReloadPending(),
				TEXT("reload: refused if a command isn't registered"));
		}
	}
}

/** Spawning with arguments. Adds a message to OutFailures for each problem. */
static void BangoRunArgumentTests(TArray<FString>& OutFailures)
{
	struct FArgCase
	{
		const TCHAR* Name;
		const TCHAR* Source;
		TArray<FBangoValue> Args;
		/** Joined log when the script should finish; null when it should fail. */
		const TCHAR* ExpectedLog;
		/** Text the start or runtime error must contain; null when it should finish. */
		const TCHAR* ErrorContains;
	};

	const FBangoValue John = FBangoValue::MakeEntity(TEXT("john"));
	const TCHAR* Greet = TEXT("#define params (entity _who, string _line, int _times = 2, entity _listener = nil)\n")
		TEXT("for (int _i = 0; _i < _times; _i++)\n{\n\t_who Speak(_line);\n};\nHint(\"{0}\", !IsNil(_listener));");

	const FArgCase Cases[] =
	{
		{ TEXT("all arguments"), Greet, { John, FBangoValue::MakeString(TEXT("hi")), FBangoValue::MakeInt(1), FBangoValue::MakeEntity(TEXT("player")) }, TEXT("john: hi|true"), nullptr },
		{ TEXT("defaults for trailing arguments"), Greet, { John, FBangoValue::MakeString(TEXT("hi")) }, TEXT("john: hi|john: hi|false"), nullptr },
		{ TEXT("int argument for a float parameter"), TEXT("#define params (float _d)\nHint(\"{0}\", _d);"), { FBangoValue::MakeInt(3) }, TEXT("3.0"), nullptr },
		{ TEXT("explicit nil overrides a default"), TEXT("#define params (int _n = 5)\nHint(\"{0}\", !IsNil(_n));"), { FBangoValue::MakeNil(FBangoType::Int()) }, TEXT("false"), nullptr },
		{ TEXT("nil of any type fits any parameter"), TEXT("#define params (int _n)\nHint(\"{0}\", !IsNil(_n));"), { FBangoValue::MakeNil(FBangoType::String()) }, TEXT("false"), nullptr },
		{ TEXT("array argument"), TEXT("#define params (int[] _list)\n_list Push(3);\nHint(\"{0}\", _list);"), { FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }) }, TEXT("[1, 2, 3]"), nullptr },
		{ TEXT("array default is fresh each spawn"), TEXT("#define params (int[] _a = [1, 2])\n_a Push(3);\nHint(\"{0}\", _a);"), {}, TEXT("[1, 2, 3]"), nullptr },
		{ TEXT("no params, no arguments"), TEXT("Hint(\"ok\");"), {}, TEXT("ok"), nullptr },
		{ TEXT("empty params line"), TEXT("#define params ()\nHint(\"ok\");"), {}, TEXT("ok"), nullptr },

		{ TEXT("too few"), TEXT("#define params (int _a, int _b)\nHint(\"x\");"), { FBangoValue::MakeInt(1) }, nullptr, TEXT("Missing argument 2 ('_b', an int). This script needs at least 2 argument(s), but 1 were passed.") },
		{ TEXT("none for required"), Greet, {}, nullptr, TEXT("Missing argument 1 ('_who', an entity)") },
		{ TEXT("too many"), TEXT("#define params (int _a)\nHint(\"x\");"), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }, nullptr, TEXT("Too many arguments: this script takes at most 1, but 2 were passed.") },
		{ TEXT("arguments to a script without params"), TEXT("Hint(\"x\");"), { FBangoValue::MakeInt(1) }, nullptr, TEXT("takes no arguments") },
		{ TEXT("wrong type"), TEXT("#define params (int _a, string _b)\nHint(\"x\");"), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }, nullptr, TEXT("Argument 2 ('_b') must be a string, but it is an int.") },
		{ TEXT("float for an int"), TEXT("#define params (int _a)\nHint(\"x\");"), { FBangoValue::MakeDouble(1.5) }, nullptr, TEXT("Argument 1 ('_a') must be an int, but it is a float.") },
		{ TEXT("argument error points at the params line"), Greet, { FBangoValue::MakeString(TEXT("oops")), FBangoValue::MakeString(TEXT("hi")) }, nullptr, TEXT("test.bango(1:1) [node 0]: Argument 1 ('_who') must be an entity") },
		{ TEXT("reading a nil parameter"), TEXT("#define params (string _line = nil)\nHint(_line);"), {}, nullptr, TEXT("'_line' is nil. It's a parameter of this script; check what the caller passed.") },
		{ TEXT("reading a nil function parameter"), TEXT("function void F(string _s)\n{\n\tHint(_s);\n};\nF(nil);"), {}, nullptr, TEXT("It's a parameter of function 'F'") },
	};

	for (const FArgCase& Case : Cases)
	{
		FBangoRuntimeTestResult Result;
		BangoRunScript(Case.Source, Result, 20000, Case.Args);
		if (!Result.bCompiled)
		{
			OutFailures.Add(FString::Printf(TEXT("[args: %s] didn't compile: %s"), Case.Name, *Result.CompileError));
			continue;
		}
		if (Case.ExpectedLog)
		{
			const FString Log = FString::Join(Result.Log, TEXT("|"));
			if (Result.Status != EBangoInstanceStatus::Finished || !Log.Equals(Case.ExpectedLog, ESearchCase::CaseSensitive))
			{
				OutFailures.Add(FString::Printf(TEXT("[args: %s] expected '%s', got '%s' (%s)"), Case.Name, Case.ExpectedLog, *Log, *Result.Error));
			}
		}
		else if (Result.Status != EBangoInstanceStatus::Failed || !Result.Error.Contains(Case.ErrorContains, ESearchCase::CaseSensitive))
		{
			OutFailures.Add(FString::Printf(TEXT("[args: %s] expected an error containing '%s', got: %s"), Case.Name, Case.ErrorContains, *Result.Error));
		}
	}

	// The compiled parameter list, as the Blueprint node will read it.
	{
		TArray<FString> Log;
		FBangoCommandTable Commands;
		BangoMakeRuntimeTestCommands(Commands, Log);
		FBangoProgram Program;
		FBangoCompileError CompileError;
		if (!Bango::CompileSource(Greet, TEXT("greet.bango"), Commands, Program, CompileError))
		{
			OutFailures.Add(TEXT("[args: parameter info] doesn't compile"));
			return;
		}
		const bool bOk = Program.bHasParamsLine && Program.Params.Num() == 4 && Program.GetNumRequiredParams() == 2
			&& Program.Params[0].Name.Equals(TEXT("_who"), ESearchCase::CaseSensitive) && Program.Params[0].Type == FBangoType::Entity() && !Program.Params[0].bHasDefault
			&& Program.Params[2].bHasDefault && Program.Params[2].Default == FBangoValue::MakeInt(2)
			&& Program.Params[3].bHasDefault && Program.Params[3].Default == FBangoValue::MakeNil(FBangoType::Entity());
		if (!bOk)
		{
			OutFailures.Add(TEXT("[args: parameter info] names, types, defaults or required count are wrong"));
		}
	}
}
