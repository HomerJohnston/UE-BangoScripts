#pragma once

#include "BangoText.h"
#include "BangoConditionWatches.h"

// Interpreter test cases, shared by the Unreal automation test (BangoInstanceTests.cpp)
// and the stand-alone test harness used during development.
// Needs: BangoCompiler.h, BangoInstance.h, Serialization/MemoryWriter.h, Serialization/MemoryReader.h

/** Actors with the Bango IDs "john" and "player" are in the level; "ghost" is not. */
class FBangoTestEntities : public IBangoEntityResolver
{
public:
	virtual bool IsEntityRegistered(const FString& Name) const override
	{
		return Name.Equals(TEXT("john"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("player"), ESearchCase::CaseSensitive)
			|| Bango::IsPlayerEntityName(Name);
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

	FBangoCommand Talk;
	Talk.Signature = FBangoCommandSignature::Make(TEXT("Talk")).Param(TEXT("text"), String).Variadic(TEXT("{"), 0, TEXT("}"));
	Talk.Start = [&Log](FBangoCommandCall& Call)
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
	Table.Add(Talk, Error);

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
	const FBangoTypeVar T(TEXT("T"));
	const TArray<FBangoType> NumberTypes = { FBangoType::Int(), FBangoType::Double() };
	Max.Signature = FBangoCommandSignature::Make(TEXT("Max")).TypeParam(T, NumberTypes, TEXT("number")).Param(TEXT("A"), T).Param(TEXT("B"), T).Returns(T).Pure();
	Max.Start = [](FBangoCommandCall& Call)
	{
		if (Call.TypeArgs[0] == FBangoType::Int())
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
	BadNumber.Signature = FBangoCommandSignature::Make(TEXT("BadNumber")).TypeParam(T, NumberTypes, TEXT("number")).Param(TEXT("Value"), T).Returns(T);
	BadNumber.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeDouble(Call.Args[0].GetNumberAsDouble());
		return EBangoCommandResult::Done;
	};
	Table.Add(BadNumber, Error);

	// A number command that wrongly returns an int for a float: turned into a float.
	FBangoCommand IntNumber;
	IntNumber.Signature = FBangoCommandSignature::Make(TEXT("IntNumber")).TypeParam(T, NumberTypes, TEXT("number")).Param(TEXT("Value"), T).Returns(T);
	IntNumber.Start = [](FBangoCommandCall& Call)
	{
		Call.Result = FBangoValue::MakeInt(3);
		return EBangoCommandResult::Done;
	};
	Table.Add(IntNumber, Error);

	// Like the built-in Lerp: two floats, or two vectors of the same kind, and the result is one too.
	FBangoCommand Lerp;
	Lerp.Signature = FBangoCommandSignature::Make(TEXT("Lerp")).TypeParam(T, { FBangoType::Double(), FBangoType::Vector2(), FBangoType::Vector3() })
		.Param(TEXT("A"), T).Param(TEXT("B"), T).Param(TEXT("Alpha"), FBangoType::Double()).Returns(T).Pure();
	Lerp.Start = [](FBangoCommandCall& Call)
	{
		const double Alpha = Call.Args[2].GetDouble();
		auto Blend = [&Call, Alpha](int32 Part) { const double A = Call.Args[0].GetVectorPart(Part); return A + (Call.Args[1].GetVectorPart(Part) - A) * Alpha; };
		if (Call.TypeArgs[0] == FBangoType::Vector2())
		{
			Call.Result = FBangoValue::MakeVector2(Blend(0), Blend(1));
		}
		else if (Call.TypeArgs[0] == FBangoType::Vector3())
		{
			Call.Result = FBangoValue::MakeVector3(Blend(0), Blend(1), Blend(2));
		}
		else
		{
			const double A = Call.Args[0].GetDouble();
			Call.Result = FBangoValue::MakeDouble(A + (Call.Args[1].GetDouble() - A) * Alpha);
		}
		return EBangoCommandResult::Done;
	};
	Table.Add(Lerp, Error);

	// A latent command with a type parameter: gives its argument back a frame later, as the type it came as.
	FBangoCommand EchoLater;
	EchoLater.Signature = FBangoCommandSignature::Make(TEXT("EchoLater")).TypeParam(T, NumberTypes, TEXT("number")).Param(TEXT("Value"), T).Returns(T);
	EchoLater.Start = [](FBangoCommandCall& Call)
	{
		Call.LatentState.Add(Call.Args[0]);
		return EBangoCommandResult::Pending;
	};
	EchoLater.Poll = [](FBangoCommandCall& Call)
	{
		Call.Result = Call.LatentState[0];
		return EBangoCommandResult::Done;
	};
	Table.Add(EchoLater, Error);

	Bango::RegisterCoreCommands(Table, Error); // Count, and the array commands

	// Changes its target in place, like the array commands: adds one to an int. (A nil one is an error.)
	FBangoCommand Bump;
	Bump.Signature = FBangoCommandSignature::Make(TEXT("Bump")).Target(FBangoType::Int()).MutableTarget();
	Bump.Start = [](FBangoCommandCall& Call)
	{
		if (Call.Target.IsNil())
		{
			return Call.Fail(TEXT("there's nothing to add to (it's nil)."));
		}
		Call.Target = FBangoValue::MakeInt(Call.Target.GetInt() + 1);
		return EBangoCommandResult::Done;
	};
	Table.Add(Bump, Error);

	// Wrongly changes its target's type.
	FBangoCommand BadBump;
	BadBump.Signature = FBangoCommandSignature::Make(TEXT("BadBump")).Target(FBangoType::Int()).MutableTarget();
	BadBump.Start = [](FBangoCommandCall& Call)
	{
		Call.Target = FBangoValue::MakeString(TEXT("oops"));
		return EBangoCommandResult::Done;
	};
	Table.Add(BadBump, Error);
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
/**
 * Owner: who `self` is. The Bango ID "john" by default; null for no owner; "!message" for an owner self can't use,
 * with that message.
 */
static void BangoRunScript(const TCHAR* Source, FBangoRuntimeTestResult& Out, int32 MaxInstructions = 100000, const TArray<FBangoValue>& Args = TArray<FBangoValue>(),
	const TCHAR* Owner = TEXT("john"))
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
	if (Owner)
	{
		FBangoScriptOwner ScriptOwner;
		(Owner[0] == TEXT('!') ? ScriptOwner.Problem : ScriptOwner.BangoName) = Owner[0] == TEXT('!') ? Owner + 1 : Owner;
		Instance.SetOwner(ScriptOwner);
	}
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
	// Spawn blocks: like the script subsystem, each runs straight away until it first pauses, then with the others.
	TArray<TSharedPtr<FBangoInstance>> Spawned;
	FString SpawnedError;
	Context.SpawnBlock = [&Spawned, &Context, &SpawnedError](const FBangoInstance& Parent, int32 BlockIP, int64& OutId, FString& OutError)
	{
		if (Spawned.Num() >= 100)
		{
			OutError = TEXT("too many spawned copies");
			return false;
		}
		TSharedPtr<FBangoInstance> Child = MakeShared<FBangoInstance>();
		FBangoRuntimeError StartError;
		if (!Child->StartSpawned(Parent, BlockIP, 100 + Spawned.Num(), StartError))
		{
			OutError = StartError.Message;
			return false;
		}
		Spawned.Add(Child);
		OutId = Child->GetId();
		Child->Resume(Context);
		if (Child->GetStatus() == EBangoInstanceStatus::Failed && SpawnedError.IsEmpty())
		{
			SpawnedError = Child->GetError().ToString();
		}
		return true;
	};
	const auto AnyRunning = [&Instance, &Spawned]()
	{
		bool bRunning = Instance.IsRunning();
		for (const TSharedPtr<FBangoInstance>& Child : Spawned)
		{
			bRunning |= Child->IsRunning();
		}
		return bRunning;
	};
	while (AnyRunning() && Out.Resumes < 1000)
	{
		Instance.Resume(Context);
		for (int32 i = 0; i < Spawned.Num(); ++i)
		{
			const TSharedPtr<FBangoInstance> Child = Spawned[i];
			if (Child->IsRunning())
			{
				Child->Resume(Context);
				if (Child->GetStatus() == EBangoInstanceStatus::Failed && SpawnedError.IsEmpty())
				{
					SpawnedError = Child->GetError().ToString();
				}
			}
		}
		++Out.Resumes;
	}
	Out.Status = Instance.GetStatus();
	if (Out.Status == EBangoInstanceStatus::Failed)
	{
		Out.Error = Instance.GetError().ToString();
	}
	else if (!SpawnedError.IsEmpty())
	{
		// A spawned copy's error counts as the test's.
		Out.Status = EBangoInstanceStatus::Failed;
		Out.Error = SpawnedError;
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
	/** Who `self` is (BangoRunScript's Owner). */
	const TCHAR* Owner = TEXT("john");
};

static const FBangoRuntimeTestCase GBangoRuntimeTestCases[] =
{
	// ---- Disabled nodes ----
	{ TEXT("disabled: a simple node is skipped"), TEXT("Talk(\"a\");\n/* Talk(\"b\"); */\nTalk(\"c\");"), TEXT("a|c"), nullptr, nullptr },
	{ TEXT("disabled: a block node skips its block"), TEXT("/*\nif (true)\n{\n\tTalk(\"x\");\n};\n*/\nTalk(\"y\");"), TEXT("y"), nullptr, nullptr },
	{ TEXT("disabled: not type-checked"), TEXT("/* Talk(nosuch + 1, @ghost); */\n/*\nwhile (\"no\")\n{\n\tbreak;\n};\n*/\nTalk(\"ok\");"), TEXT("ok"), nullptr, nullptr },
	{ TEXT("disabled: a break in a loop"), TEXT("int _i = 0;\nwhile (_i < 3)\n{\n\t_i++;\n\t/* break; */\n};\nTalk(\"{0}\", _i);"), TEXT("3"), nullptr, nullptr },
	{ TEXT("disabled: inside a disabled block"), TEXT("/*\nif (true)\n{\n\tTalk(\"in\");\n\t/* Talk(\"off\"); */\n};\n*/\nif (true)\n{\n\t/* return; */\n\tTalk(\"after\");\n};"), TEXT("after"), nullptr, nullptr },

	// ---- Locals ----
	{ TEXT("locals: set each time the declaration runs"), TEXT("for (int _i = 0; _i < 3; _i++)\n{\n\tint _n = 10;\n\t_n += _i;\n\tTalk(\"{0}\", _n);\n};"), TEXT("10|11|12"), nullptr, nullptr },
	{ TEXT("locals: declared after code, worked out there"), TEXT("int _a = 2;\nTalk(\"start\");\nint _b = _a * 3;\nTalk(\"{0}\", _b);"), TEXT("start|6"), nullptr, nullptr },
	{ TEXT("locals: the same name in sibling blocks"), TEXT("if (true)\n{\n\tstring _s = \"a\";\n\tTalk(_s);\n};\nif (true)\n{\n\tint _s = 7;\n\tTalk(\"{0}\", _s);\n};"), TEXT("a|7"), nullptr, nullptr },
	{ TEXT("locals: a spawn's declared id"), TEXT("int _t = spawn\n{\n};\nTalk(\"{0}\", _t > 0);"), TEXT("true"), nullptr, nullptr },

	// ---- Vectors ----
	{ TEXT("vector literals and operators"),
		TEXT("vector3 _v1 = [1, 2, 3];\nvector3 _v2 = [0.5, 0, -1];\nvector3 _vec;\nvector2 _w;\n_vec = _v1 + _v2;\nTalk(\"{0} {1} {2} {3}\", _vec, _v1 - _v2, _v1 * 2, 2 * _v1 / 4);\nTalk(\"{0} {1} {2}\", -_v1, _v1 == [1, 2, 3], _v1 != _v2);\n_w = [3, 4];\nTalk(\"{0} {1}\", _w, _w.x * _w.x + _w.y * _w.y);"),
		TEXT("(1.5, 2.0, 2.0) (0.5, 2.0, 4.0) (2.0, 4.0, 6.0) (0.5, 1.0, 1.5)|(-1.0, -2.0, -3.0) true true|(3.0, 4.0) 25.0"), nullptr, nullptr },
	{ TEXT("vector parts"),
		TEXT("vector3 _vec = [1, 2, 3];\nvector3[] _path;\n_vec.x = 10;\n_vec.y += 0.5;\n_vec += [1, 1, 1];\n_vec *= 2;\nTalk(\"{0} {1}\", _vec, _vec.z);\n_path = [[0, 0, 0], _vec];\n_path[1].z = 9;\n_path Push(_path[0] + [0, 0, 100]);\nTalk(\"{0} {1}\", _path, _path[2].z);"),
		TEXT("(22.0, 7.0, 8.0) 8.0|[(0.0, 0.0, 0.0), (22.0, 7.0, 9.0), (0.0, 0.0, 100.0)] 100.0"), nullptr, nullptr },
	{ TEXT("vector inputs, functions and globals"),
		TEXT("#inputs (vector3 _p = [1, 2, 3], vector2 _q = [-1, 0.5])\nglobal vector3 home;\nhome = [1, 1, 1];\nhome.y = 7;\nTalk(\"{0} {1} {2} {3} {4}\", _p, _q, Up(5), Up(2).z, home);\nTalk(\"{0} {1}\", spot.y, spot + spot);\nfunction vector3 Up(float _h)\n{\n\treturn [0, 0, _h];\n};"),
		TEXT("(1.0, 2.0, 3.0) (-1.0, 0.5) (0.0, 0.0, 5.0) 2.0 (1.0, 7.0, 1.0)|2.0 (2.0, 4.0, 6.0)"), nullptr, nullptr },
	{ TEXT("a vector divided by zero"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", _v / 0);"), nullptr, TEXT("(2:16)"), TEXT("Division by zero") },
	{ TEXT("setting a part of a nil vector"), TEXT("vector2 _v;\n_v.x = 1;"), nullptr, TEXT("(2:1)"), TEXT("'_v' is nil, so its x can't be set on its own. Give it a whole vector first, like _v = [0, 0];") },
	{ TEXT("reading a part of a nil vector"), TEXT("vector3 _v;\nTalk(\"{0}\", _v.x);"), nullptr, TEXT("(2:13)"), TEXT("'_v' is nil") },
	{ TEXT("a part of an undeclared global that isn't a vector"), TEXT("Talk(\"{0}\", lots.x);"), nullptr, TEXT("(1:13)"), TEXT("'lots' is a string, which has no 'x' part") },
	{ TEXT("arithmetic"), TEXT("Talk(\"{0} {1} {2} {3}\", 1 + 2 * 3, 7 / 2, 7.0 / 2, 7 % 3);"), TEXT("7 3 3.5 1"), nullptr, nullptr },
	{ TEXT("localized text"),
		TEXT("#LOCTEXT_NAMESPACE \"N\"\ntext[] _all;\nSay(LOCTEXT(\"A\", \"Hello {0}, {1}\"), 3, \"you\");\nTalk(\"{0}\", LOCTEXT(\"B\", \"plain\"));\nSay(FormatText(LOCTEXT(\"F\", \"{1}-{0}\"), 1, @john));\nTalk(\"{0} {1}\", LOCTEXT(\"C\", \"x\") == ToText(\"x\"), LOCTEXT(\"C\", \"x\") != NSLOCTEXT(\"M\", \"C\", \"y\"));\n_all = [LOCTEXT(\"B\", \"plain\"), nil];\nTalk(\"{0}\", _all);\nSay(LOCTEXT(\"E\", \"`{0`} stays\"));"),
		TEXT("Hello 3, you|plain|john-1|true true|[plain, nil]|{0} stays"), nullptr, nullptr },
	{ TEXT("invariant text"),
		TEXT("text _t = INVTEXT(\"Score: {0}\");\nSay(_t, 10);\nSay(INVTEXT(\"plain\"));\nTalk(\"{0}\", INVTEXT(\"x\") == ToText(\"x\"));"),
		TEXT("Score: 10|plain|true"), nullptr, nullptr },
	{ TEXT("a text placeholder with no value while running"), TEXT("text _t;\n_t = ToText(\"{1}\");\nSay(_t, 1);"), nullptr, TEXT("(3:1)"), TEXT("uses {1}, but only 1 value(s) were given") },
	{ TEXT("a nil text"), TEXT("text _t;\nSay(_t);"), nullptr, TEXT("(2:5)"), TEXT("'_t' is nil") },
	{ TEXT("floats"), TEXT("Talk(\"{0} {1} {2}\", 10 / 4.0, -2.5 * 2, 7.5 % 2);"), TEXT("2.5 -5.0 1.5"), nullptr, nullptr },
	{ TEXT("strings"), TEXT("string _s;\n_s = \"a\" + \"b\";\n_s += \"c\";\nTalk(_s);\nTalk(\"100% sure\");"), TEXT("abc|100% sure"), nullptr, nullptr },
	{ TEXT("comparisons"), TEXT("Talk(\"{0} {1} {2} {3} {4}\", 1 == 1.0, \"a\" == \"a\", 2 < 1, true != false, @john == @player);"), TEXT("true true false true false"), nullptr, nullptr },
	{ TEXT("if chain"), TEXT("int _x = 2;\nif (_x == 1)\n{\n\tTalk(\"one\");\n} else if (_x == 2)\n{\n\tTalk(\"two\");\n} else\n{\n\tTalk(\"other\");\n};"), TEXT("two"), nullptr, nullptr },
	{ TEXT("while, break, continue"), TEXT("int _i = 0;\nwhile (true)\n{\n\t_i++;\n\tif (_i == 2)\n\t{\n\t\tcontinue;\n\t};\n\tif (_i > 4)\n\t{\n\t\tbreak;\n\t};\n\tTalk(\"{0}\", _i);\n};"), TEXT("1|3|4"), nullptr, nullptr },
	{ TEXT("sequence: blocks in order, then what follows"), TEXT("sequence\n{\n\tTalk(\"a\");\n\tSleep(0);\n\tTalk(\"b\");\n}\n{\n}\n{\n\tTalk(\"c\");\n}\nTalk(\"d\");"), TEXT("a|b|c|d"), nullptr, nullptr },
	{ TEXT("sequence: break and continue in a loop"), TEXT("int _i = 0;\nwhile (true)\n{\n\t_i++;\n\tsequence\n\t{\n\t\tif (_i == 2)\n\t\t{\n\t\t\tcontinue;\n\t\t}\n\t}\n\t{\n\t\tif (_i > 3)\n\t\t{\n\t\t\tbreak;\n\t\t}\n\t\tTalk(\"{0}\", _i);\n\t}\n}"), TEXT("1|3"), nullptr, nullptr },
	{ TEXT("spawn: runs on its own, the parent goes straight on"), TEXT("Talk(\"a\");\nspawn\n{\n\tTalk(\"b\");\n\tSleep(1);\n\tTalk(\"d\");\n}\nTalk(\"c\");"), TEXT("a|b|c|d"), nullptr, nullptr },
	{ TEXT("spawn: copies of the variables"), TEXT("int _x = 1;\nspawn\n{\n\tSleep(1);\n\tTalk(\"copy {0}\", _x);\n\t_x = 5;\n}\n_x = 2;\nSleep(2);\nTalk(\"parent {0}\", _x);"), TEXT("copy 1|parent 2"), nullptr, nullptr },
	{ TEXT("spawn: in a loop, each with its own copy"), TEXT("for (int _i = 0; _i < 3; _i++)\n{\n\tspawn\n\t{\n\t\tSleep(_i);\n\t\tTalk(\"{0}\", _i);\n\t}\n}\nTalk(\"loop done\");"), TEXT("loop done|0|1|2"), nullptr, nullptr },
	{ TEXT("spawn: the copy's id, kept before it runs"), TEXT("int _id = 0;\nint[] _ids = [0, 0];\n_id = spawn\n{\n\tTalk(\"copy sees {0}\", _id);\n}\n_ids[1] = spawn\n{\n}\nTalk(\"{0} {1}\", _id > 0, _ids[1] > _id);"), TEXT("copy sees 0|true true"), nullptr, nullptr },
	{ TEXT("spawn: return ends only the copy"), TEXT("spawn\n{\n\tTalk(\"in\");\n\treturn;\n\tTalk(\"never\");\n}\nTalk(\"out\");"), TEXT("in|out"), nullptr, nullptr },
	{ TEXT("spawn: in a function, the copy doesn't go back to the caller"), TEXT("function void F(int _n)\n{\n\tspawn\n\t{\n\t\tTalk(\"spawned {0}\", _n);\n\t}\n\tTalk(\"F done\");\n}\nF(7);\nTalk(\"main\");"), TEXT("spawned 7|F done|main"), nullptr, nullptr },
	{ TEXT("spawn: a disabled one"), TEXT("/*\nspawn\n{\n\tTalk(\"no\");\n}\n*/\nTalk(\"yes\");"), TEXT("yes"), nullptr, nullptr },
	{ TEXT("switch: int cases, several values, default"), TEXT("for (int _i = 0; _i < 5; _i++)\n{\n\tswitch (_i)\n\t{\n\t\tcase 0:\n\t\t{\n\t\t\tTalk(\"zero\");\n\t\t}\n\t\tcase 1, -1, 3:\n\t\t{\n\t\t\tTalk(\"odd {0}\", _i);\n\t\t}\n\t\tdefault:\n\t\t{\n\t\t\tTalk(\"other {0}\", _i);\n\t\t}\n\t}\n}\nTalk(\"end\");"), TEXT("zero|odd 1|other 2|odd 3|other 4|end"), nullptr, nullptr },
	{ TEXT("switch: strings, no default, no match"), TEXT("string _s = \"b\";\nswitch (_s)\n{\n\tcase \"a\":\n\t{\n\t\tTalk(\"a\");\n\t}\n\tcase \"B\":\n\t{\n\t\tTalk(\"B\");\n\t}\n}\nTalk(\"after\");"), TEXT("after"), nullptr, nullptr },
	{ TEXT("switch: entities, value evaluated once"), TEXT("int _n = 0;\nentity _e = @john;\nswitch (_e)\n{\n\tcase @player:\n\t{\n\t\tTalk(\"player\");\n\t}\n\tcase @john:\n\t{\n\t\tTalk(\"john\");\n\t\tSleep(0);\n\t\tTalk(\"still john\");\n\t}\n}"), TEXT("john|still john"), nullptr, nullptr },
	{ TEXT("switch: break and continue act on the loop around it"), TEXT("int _i = 0;\nwhile (true)\n{\n\t_i++;\n\tswitch (_i)\n\t{\n\t\tcase 2:\n\t\t{\n\t\t\tcontinue;\n\t\t}\n\t\tcase 4:\n\t\t{\n\t\t\tbreak;\n\t\t}\n\t}\n\tTalk(\"{0}\", _i);\n}\nTalk(\"done\");"), TEXT("1|3|done"), nullptr, nullptr },
	{ TEXT("switch: a nil value"), TEXT("int _x;\nswitch (_x)\n{\n\tcase 1:\n\t{\n\t}\n}"), nullptr, TEXT("(2:9)"), TEXT("nil") },
	{ TEXT("sequence: a disabled one"), TEXT("/*\nsequence\n{\n\tTalk(\"no\");\n}\n*/\nTalk(\"yes\");"), TEXT("yes"), nullptr, nullptr },
	{ TEXT("for"), TEXT("int _sum = 0;\nfor (int _i = 1; _i <= 10; _i++)\n{\n\t_sum += _i;\n};\nTalk(\"{0}\", _sum);"), TEXT("55"), nullptr, nullptr },
	{ TEXT("for continue runs the step"), TEXT("for (int _i = 0; _i < 5; _i++)\n{\n\tif (_i % 2 == 0)\n\t{\n\t\tcontinue;\n\t};\n\tTalk(\"{0}\", _i);\n};"), TEXT("1|3"), nullptr, nullptr },
	{ TEXT("loop body locals reset"), TEXT("int _t;\nfor (int _i = 0; _i < 2; _i++)\n{\n\t_t = nil;\n\tTalk(\"{0}\", !IsNil(_t));\n\t_t = 1;\n};"), TEXT("false|false"), nullptr, nullptr },
	{ TEXT("forEach with gaps"), TEXT("int[] _a = [1, 2];\n_a[4] = 5;\nforEach (_v in _a)\n{\n\tif (!IsNil(_v))\n\t{\n\t\tTalk(\"{0}\", _v);\n\t}\n\telse\n\t{\n\t\tTalk(\"gap\");\n\t};\n};"), TEXT("1|2|gap|gap|5"), nullptr, nullptr },
	{ TEXT("forEach loops over a copy"), TEXT("int[] _a = [1, 2];\nforEach (_v in _a)\n{\n\t_a Push(_v);\n};\nTalk(\"{0}\", _a);"), TEXT("[1, 2, 1, 2]"), nullptr, nullptr },
	{ TEXT("forEach break and continue"), TEXT("forEach (_v in [1, 2, 3, 4])\n{\n\tif (_v == 2)\n\t{\n\t\tcontinue;\n\t};\n\tif (_v == 4)\n\t{\n\t\tbreak;\n\t};\n\tTalk(\"{0}\", _v);\n};"), TEXT("1|3"), nullptr, nullptr },
	{ TEXT("nested arrays"), TEXT("int[][] _g = [[1, 2], [3]];\n_g[3][2] = 7;\n_g[0] Push(4);\n_g[0][0] += 10;\nTalk(\"{0} {1}\", _g, Count(_g));\nTalk(\"{0} {1}\", !IsNil(_g[2]), !IsNil(_g[3][2]));"), TEXT("[[11, 2, 4], [3], nil, [nil, nil, 7]] 4|false true"), nullptr, nullptr },
	{ TEXT("array commands"), TEXT("int[] _a = [1, 2, 3];\nint _last;\nint _first;\n_a InsertAt(0, 0);\n_last = _a Pop();\n_first = _a RemoveAt(0);\n_a InsertAt(9, 5);\nTalk(\"{0} {1} {2}\", _a, _last, _first);"), TEXT("[1, 2, nil, nil, nil, 9] 3 0"), nullptr, nullptr },
	// Vectors' parts and strings' characters by index, read and written.
	{ TEXT("index: read vectors and strings"), TEXT("vector3 _v = [1.5, 2, 3];\nstring _s = \"hey\";\nint _i = 2;\nTalk(\"{0} {1} {2} {3}\", _v[0], _v[_i], _s[1], _s[_i]);"), TEXT("1.5 3.0 e y"), nullptr, nullptr },
	{ TEXT("index: write vectors and strings"), TEXT("vector2 _v = [1, 2];\nstring _s = \"cat\";\n_v[1] = 7;\n_v[0] += 1;\n_s[0] = \"b\";\nTalk(\"{0} {1}\", _v, _s);"), TEXT("(2.0, 7.0) bat"), nullptr, nullptr },
	{ TEXT("index: inside arrays"), TEXT("vector2[] _path = [[1, 2], [3, 4]];\nstring[] _names = [\"ann\", \"bob\"];\n_path[1][0] = 9;\n_names[0][0] = \"A\";\nTalk(\"{0} {1} {2}\", _path[1], _names[0], _names[1][2]);"), TEXT("(9.0, 4.0) Ann b"), nullptr, nullptr },
	{ TEXT("index: Count of strings and vectors"), TEXT("string _s = \"four\";\nvector2 _v = [0, 0];\nTalk(\"{0} {1} {2}\", Count(_s), Count(_v), Count(\"\"));"), TEXT("4 2 0"), nullptr, nullptr },
	{ TEXT("index: a string past its end"), TEXT("string _s = \"ab\";\nint _i = 2;\nTalk(\"{0}\", _s[_i]);"), nullptr, TEXT("(3:15)"), TEXT("'_s[2]' is past the end of '_s', which has 2 character(s)") },
	{ TEXT("index: a vector past its end"), TEXT("vector2 _v = [1, 2];\nint _i = 5;\nTalk(\"{0}\", _v[_i]);"), nullptr, TEXT("(3:15)"), TEXT("a vector2 has 2 parts") },
	{ TEXT("index: negative"), TEXT("string _s = \"ab\";\nint _i = -1;\n_s[_i] = \"x\";"), nullptr, TEXT("(3:1)"), TEXT("Index -1 for '_s' is negative") },
	{ TEXT("index: a string's character must be one character"), TEXT("string _s = \"ab\";\n_s[0] = \"xy\";"), nullptr, TEXT("(2:1)"), TEXT("must be a string of one character, but \"xy\" has 2") },
	{ TEXT("index: writing past a string's end doesn't grow it"), TEXT("string _s = \"ab\";\n_s[2] = \"c\";"), nullptr, TEXT("(2:1)"), TEXT("past the end") },
	{ TEXT("index: a nil vector"), TEXT("vector3 _v;\n_v[0] = 1;"), nullptr, TEXT("(2:1)"), TEXT("is nil") },
	{ TEXT("index: undeclared globals' string and vector"), TEXT("Talk(\"{0} {1}\", lots[1], spot[2]);"), TEXT("o 3.0"), nullptr, nullptr },
	{ TEXT("arrays are values"), TEXT("int[] _a = [1];\nint[] _b;\n_b = _a;\n_b Push(2);\nTalk(\"{0} {1}\", Count(_a), Count(_b));"), TEXT("1 2"), nullptr, nullptr },
	{ TEXT("Pop of nil discarded"), TEXT("int[] _a = [];\n_a[1] = 5;\n_a RemoveAt(1);\n_a Pop();\nTalk(\"{0}\", Count(_a));"), TEXT("0"), nullptr, nullptr },
	{ TEXT("recursion"), TEXT("function int Fact(int _n)\n{\n\tif (_n <= 1)\n\t{\n\t\treturn 1;\n\t};\n\treturn _n * Fact(_n - 1);\n};\nTalk(\"{0}\", Fact(10));"), TEXT("3628800"), nullptr, nullptr },
	{ TEXT("function arguments are copies"), TEXT("int[] _x = [];\nfunction void Grow(int[] _a)\n{\n\t_a Push(1);\n};\nGrow(_x);\nTalk(\"{0}\", Count(_x));"), TEXT("0"), nullptr, nullptr },
	{ TEXT("function return conversion"), TEXT("function float Half(int _n)\n{\n\treturn _n / 2;\n};\nTalk(\"{0}\", Half(3));"), TEXT("1.0"), nullptr, nullptr },
	{ TEXT("functions with globals and latent commands"), TEXT("global int gold;\ngold = 1;\nfunction void Pay(int _n)\n{\n\tSleep(1.0);\n\tgold -= _n;\n\t@john Speak(\"thanks\");\n};\nPay(1);\nTalk(\"{0}\", gold);"), TEXT("john: thanks|0"), nullptr, nullptr },
	{ TEXT("goto loop"), TEXT("int _i = 0;\nlabel Top;\n_i++;\nif (_i < 3)\n{\n\tgoto Top;\n};\nTalk(\"{0}\", _i);"), TEXT("3"), nullptr, nullptr },
	{ TEXT("goto out of a loop"), TEXT("while (true)\n{\n\tgoto Done;\n};\nlabel Done;\nTalk(\"out\");"), TEXT("out"), nullptr, nullptr },
	{ TEXT("top-level return"), TEXT("Talk(\"a\");\nreturn;\nTalk(\"b\");"), TEXT("a"), nullptr, nullptr },
	{ TEXT("globals"), TEXT("global int gold;\ngold = 5;\ngold += 2;\nTalk(\"{0}\", gold);"), TEXT("7"), nullptr, nullptr },
	{ TEXT("undeclared globals"), TEXT("if (flag)\n{\n\tboss Speak(\"hi\");\n};\nTalk(\"{0}\", boss GetName());"), TEXT("john: hi|john"), nullptr, nullptr },
	{ TEXT("Bango IDs"), TEXT("entity _e = @john;\n_e Speak(\"a\");\n@player Speak(\"b\");\nTalk(\"{0} {1} {2} {3}\", _e, [@john, @ghost], @john == _e, @john != @player);"), TEXT("john: a|player: b|john [john, ghost] true true"), nullptr, nullptr },
	{ TEXT("Bango ID that isn't in the level, unused"), TEXT("entity _g = @ghost;\nTalk(\"{0}\", _g);"), TEXT("ghost"), nullptr, nullptr },
	{ TEXT("Bango IDs as parameter defaults"), TEXT("#inputs (entity _who = @john, entity[] _all = [@john, @player])\n_who Speak(\"hi\");\nforEach (_x in _all)\n{\n\t_x Speak(\"all\");\n};"), TEXT("john: hi|john: all|player: all"), nullptr, nullptr },
	{ TEXT("IsNil on globals"), TEXT("Talk(\"{0} {1}\", !IsNil(nothere), !IsNil(flag));"), TEXT("false true"), nullptr, nullptr },
	{ TEXT("short-circuit"), TEXT("bool _b;\nbool _c;\n_b = !IsNil(nothere) && nothere;\n_c = flag || nothere;\nTalk(\"{0} {1}\", _b, _c);"), TEXT("false true"), nullptr, nullptr },
	{ TEXT("latent commands"), TEXT("Talk(\"start\");\nSleep(1.0);\nif (AskChoice(\"a\", \"b\") == 2)\n{\n\tTalk(\"two\");\n};"), TEXT("start|two"), nullptr, nullptr },
	{ TEXT("nil as an extra value"), TEXT("Talk(\"{0} and {1}\", 1, nil);"), TEXT("1 and nil"), nullptr, nullptr },
	{ TEXT("nil assignment"), TEXT("int _x = 1;\n_x = nil;\nTalk(\"{0}\", !IsNil(_x));"), TEXT("false"), nullptr, nullptr },

	{ TEXT("read nil local"), TEXT("int _x;\nTalk(\"{0}\", _x);"), TEXT(""), TEXT("(2:13)"), TEXT("'_x' is nil") },
	{ TEXT("missing global"), TEXT("Talk(\"{0}\", nothere);"), TEXT(""), TEXT("(1:13)"), TEXT("Global 'nothere' doesn't exist") },
	{ TEXT("index past end"), TEXT("int[] _a = [1];\nTalk(\"{0}\", _a[3]);"), TEXT(""), TEXT("(2:15)"), TEXT("'_a[3]' is past the end of '_a', which has 1 element(s)") },
	{ TEXT("negative index"), TEXT("int[] _a = [1];\nint _i = -1;\nTalk(\"{0}\", _a[_i]);"), TEXT(""), TEXT("(3:15)"), TEXT("negative") },
	{ TEXT("division by zero"), TEXT("int _z = 0;\nint _x;\n_x = 1 / _z;"), TEXT(""), TEXT("(3:8)"), TEXT("Division by zero") },
	{ TEXT("modulo by zero"), TEXT("float _z = 0.0;\nfloat _x;\n_x = 1.5 % _z;"), TEXT(""), TEXT("(3:10)"), TEXT("Modulo by zero") },
	{ TEXT("integer overflow"), TEXT("int _x = 2147483647;\n_x++;"), TEXT(""), TEXT("(2:1)"), TEXT("Integer overflow") },
	{ TEXT("int range edges"), TEXT("int _min;\n_min = -2147483647 - 1;\nTalk(\"{0} {1} {2}\", _min, 2147483647, _min + 2147483647);"), TEXT("-2147483648 2147483647 -1"), nullptr, nullptr },
	{ TEXT("int overflow subtract"), TEXT("int _x = -2147483647;\n_x -= 2;"), TEXT(""), TEXT("(2:1)"), TEXT("outside the int range") },
	{ TEXT("int overflow multiply"), TEXT("int _x;\n_x = 65536 * 65536;"), TEXT(""), TEXT("(2:12)"), TEXT("Integer overflow") },
	{ TEXT("int overflow divide"), TEXT("int _min;\nint _x;\n_min = -2147483647 - 1;\n_x = _min / -1;"), TEXT(""), TEXT("(4:11)"), TEXT("Integer overflow") },
	{ TEXT("int overflow negate"), TEXT("int _min;\nint _x;\n_min = -2147483647 - 1;\n_x = -_min;"), TEXT(""), TEXT("(4:6)"), TEXT("-2147483648 has no positive int") },
	{ TEXT("large numbers as float"), TEXT("float _d;\n_d = 65536 * 65536.0;\nTalk(\"{0}\", _d);"), TEXT("4294967296.0"), nullptr, nullptr },
	{ TEXT("infinite loop"), TEXT("while (true)\n{\n};"), TEXT(""), TEXT("(1:"), TEXT("stuck in an infinite loop") },
	{ TEXT("runaway recursion"), TEXT("function void F()\n{\n\tF();\n};\nF();"), TEXT(""), TEXT("in F()"), TEXT("Too many nested function calls") },
	{ TEXT("missing return"), TEXT("function int F(bool _b)\n{\n\tif (_b)\n\t{\n\t\treturn 1;\n\t};\n};\nTalk(\"{0}\", F(false));"), TEXT(""), TEXT("in F()"), TEXT("reached its end without returning") },
	{ TEXT("command fails"), TEXT("Talk(\"before\");\nFail();"), TEXT("before"), TEXT("(2:1)"), TEXT("Fail: boom") },
	{ TEXT("names without underscores"), TEXT("global int _shared;\nint count = 2;\nint[] list = [1, 2];\n_shared = count;\nforEach (item in list)\n{\n\tcount += item;\n};\nTalk(\"{0} {1}\", count, _shared);"), TEXT("5 2"), nullptr, nullptr },
	{ TEXT("a local hides a global this file doesn't declare"), TEXT("int score = 3;\nTalk(\"{0}\", score);"), TEXT("3"), nullptr, nullptr },
	{ TEXT("global default: given when the global doesn't exist"), TEXT("global int fresh ?= 4;\nglobal int[] list ?= [1, 2];\nTalk(\"{0} {1}\", fresh, Count(list));"), TEXT("4 2"), nullptr, nullptr },
	{ TEXT("global default: one that exists keeps its value"), TEXT("global int score ?= 1;\nTalk(\"{0}\", score);"), TEXT("7"), nullptr, nullptr },
	{ TEXT("global default: nil"), TEXT("global int unsetYet ?= nil;\nTalk(\"{0}\", IsNil(unsetYet));"), TEXT("true"), nullptr, nullptr },
	{ TEXT("global set in the code: each time the line runs"), TEXT("global int score ?= 1;\nTalk(\"{0}\", score);\nglobal int score = 2;\nTalk(\"{0}\", score);\nfor (int _i = 0; _i < 2; _i++)\n{\n\tglobal int score = score + 10;\n};\nTalk(\"{0}\", score);"), TEXT("7|2|22"), nullptr, nullptr },
	{ TEXT("global set in the code: readable before the line, and in a function"), TEXT("Talk(\"{0}\", IsNil(later));\nSetIt();\nTalk(\"{0}\", later);\nfunction void SetIt()\n{\n\tglobal string later = \"done\";\n};"), TEXT("true|done"), nullptr, nullptr },
	{ TEXT("global set in the code: a disabled one doesn't run"), TEXT("global int score ?= 1;\n/* global int score = 50; */\nTalk(\"{0}\", score);"), TEXT("7"), nullptr, nullptr },
	{ TEXT("an undeclared name without underscore is a global read"), TEXT("Talk(\"{0}\", cuont);"), TEXT(""), TEXT("(1:13)"), TEXT("Global 'cuont' doesn't exist") },
	{ TEXT("string placeholders: {n}, backtick escapes, and what's left as it is"), TEXT("Talk(\"`{0`} is {0}, `` and {Name}, 100% and {1\", 5);"), TEXT("{0} is 5, ` and {Name}, 100% and {1"), nullptr, nullptr },
	{ TEXT("IsNull on nil and missing actors"), TEXT("entity _e;\nentity[] _a = [@john];\nTalk(\"{0} {1} {2}\", IsNull(@john), IsNull(@ghost), IsNull(_e));\nTalk(\"{0} {1} {2}\", IsNull(_a[0]), IsNull(_a[5]), IsNull(nobody));\n_e = @player;\nTalk(\"{0}\", IsNull(_e));"), TEXT("false true true|false true true|false"), nullptr, nullptr },
	{ TEXT("IsNull on something that isn't an entity"), TEXT("bool _b;\n_b = IsNull(lots);"), TEXT(""), TEXT("(2:6)"), TEXT("Expected an entity here, but the value is a string") },
	{ TEXT("the player keyword"), TEXT("entity _p = player;\nplayer Speak(\"hi\");\nTalk(\"{0} {1} {2}\", _p, _p == player, player == @player);"), TEXT("#player: hi|player true false"), nullptr, nullptr },
	// ---- self: the owner's Bango ID (john here, unless the case says otherwise) ----
	{ TEXT("self is the owner"), TEXT("entity _e = self;\nself Speak(\"hi\");\nTalk(\"{0} {1} {2} {3}\", _e, _e == @john, self == player, self);"), TEXT("john: hi|john true false john"), nullptr, nullptr },
	{ TEXT("self in arrays, switch cases and functions"),
		TEXT("entity[] _a = [self, @player];\nTalk(\"{0}\", _a);\nswitch (@john)\n{\n\tcase self:\n\t{\n\t\tTalk(\"me\");\n\t}\n}\nWho();\nfunction void Who()\n{\n\tself Speak(\"in a function\");\n};"),
		TEXT("[john, player]|me|john: in a function"), nullptr, nullptr },
	{ TEXT("self in a spawn block is the same owner"), TEXT("spawn\n{\n\tSleep(1);\n\tself Speak(\"copy\");\n}\nself Speak(\"parent\");"), TEXT("john: parent|john: copy"), nullptr, nullptr },
	{ TEXT("self as an input's default"), TEXT("#inputs (entity _who = self, entity[] _all = [self])\n_who Speak(\"x\");\nTalk(\"{0}\", _all);"), TEXT("john: x|[john]"), nullptr, nullptr },
	{ TEXT("self with no owner"), TEXT("Talk(\"a\");\nself Speak(\"x\");"), TEXT("a"), TEXT("(2:1)"), TEXT("'self' can't be used here: this script has no owner (it was started without one)"), nullptr },
	{ TEXT("self with an owner that has no Bango ID"), TEXT("entity _e = self;"), TEXT(""), TEXT("(1:"), TEXT("'self' can't be used here: Door has no Bango ID."), TEXT("!Door has no Bango ID.") },
	{ TEXT("self as a default with no owner"), TEXT("#inputs (entity _who = self)\nTalk(\"x\");"), TEXT(""), TEXT("(1:1)"), TEXT("The default for '_who' is self, but this script has no owner"), nullptr },
	{ TEXT("a script with no owner that doesn't use self runs"), TEXT("Talk(\"fine\");"), TEXT("fine"), nullptr, nullptr, nullptr },
	{ TEXT("unregistered entity"), TEXT("@ghost Speak(\"boo\");"), TEXT(""), TEXT("(1:1)"), TEXT("Speak: No actor is registered with the Bango ID name 'ghost'") },
	{ TEXT("Bango ID written without @"), TEXT("john Speak(\"hi\");"), TEXT(""), TEXT("(1:1)"), TEXT("Global 'john' doesn't exist. There is an actor with the Bango ID 'john': write @john to use it.") },
	{ TEXT("missing global that isn't a Bango ID"), TEXT("ghost Speak(\"hi\");"), TEXT(""), TEXT("(1:1)"), TEXT("Global 'ghost' doesn't exist. No script or Blueprint has declared it.") },
	{ TEXT("undeclared global of the wrong type"), TEXT("int _x;\n_x = lots + 1;"), TEXT(""), TEXT("(2:11)"), TEXT("'+' needs two numbers or two strings, but got a string and an int") },
	{ TEXT("undeclared global as condition"), TEXT("if (lots)\n{\n};"), TEXT(""), TEXT("(1:"), TEXT("condition must be a bool") },
	{ TEXT("undeclared global as wrong target"), TEXT("lots Speak(\"x\");"), TEXT(""), TEXT("(1:1)"), TEXT("Expected an entity here, but the value is a string") },
	{ TEXT("Pop of nil used"), TEXT("int[] _a = [];\nint _x;\n_a[1] = 5;\n_a RemoveAt(1);\n_x = _a Pop();"), TEXT(""), TEXT("(5:6)"), TEXT("Pop on '_a': the element it would remove is nil, so its value can't be used.") },
	{ TEXT("nested write into nil array"), TEXT("int[][] _g;\n_g[0][0] = 1;"), TEXT(""), TEXT("(2:1)"), TEXT("Writing to '_g'") },
	{ TEXT("compound on missing inner array"), TEXT("int[][] _g = [];\n_g[1][2] += 5;"), TEXT(""), TEXT("(2:1)"), TEXT("past the end") },
	{ TEXT("runtime error node id"), TEXT("int _x;\nTalk(\"{0}\", _x); //@8(0, 1)"), TEXT(""), TEXT("[node 8]"), TEXT("'_x' is nil") },
	// ---- Number commands (a type parameter that's an int or a float) ----
	{ TEXT("number command: int for ints, float otherwise"),
		TEXT("int _i = 3;\nfloat _f = 2.5;\nint _ri;\nfloat _rf;\n_ri = Max(_i, 9);\n_rf = Max(_i, _f);\n_rf = _rf + Max(1, 2);\nTalk(\"{0} {1} {2} {3}\", _ri, _rf, Max(_f, 1), Max(Max(1, 2), 3));"),
		TEXT("9 5.0 2.5 3"), nullptr, nullptr },
	{ TEXT("number command: undeclared globals are checked when called"),
		TEXT("int _a;\nfloat _b;\n_a = Max(score, 2);\n_b = Max(ratio, 2);\nTalk(\"{0} {1}\", _a, _b);"), TEXT("7 2.0"), nullptr, nullptr },
	{ TEXT("number command: an undeclared float global into an int"),
		TEXT("int _a;\n_a = Max(ratio, 2);"), TEXT(""), TEXT("(2:1)"), TEXT("Can't store a float in '_a', which holds an int.") },
	{ TEXT("number command: an undeclared global that isn't a number"),
		TEXT("Talk(\"{0}\", Max(lots, 2));"), TEXT(""), TEXT("(1:13)"), TEXT("Max: argument 1 ('A') must be a number (an int or a float), but it is a string") },
	{ TEXT("number command: a float result for ints is an error"),
		TEXT("Talk(\"{0}\", BadNumber(2));"), TEXT(""), TEXT("(1:13)"), TEXT("returned a float, but with these arguments it should return an int") },
	{ TEXT("number command: an int result for a float becomes a float"),
		TEXT("Talk(\"{0}\", IntNumber(2.5));"), TEXT("3.0"), nullptr, nullptr },
	// ---- Commands that change their target in place (MutableTarget) ----
	{ TEXT("in place: a local, an element and a declared global"),
		TEXT("global int kills = 4;\nint _n = 1;\nint[] _a = [5, 6];\nint[][] _g = [[1], [2]];\n_n Bump();\n_a[1] Bump();\n_g[1][0] Bump();\nkills Bump();\nTalk(\"{0} {1} {2} {3}\", _n, _a, _g, kills);"),
		TEXT("2 [5, 7] [[1], [3]] 5"), nullptr, nullptr },
	{ TEXT("in place: a failed call leaves the target as it was"),
		TEXT("int _n;\n_n Bump();"), TEXT(""), TEXT("(2:1)"), TEXT("Bump on '_n': there's nothing to add to (it's nil).") },
	{ TEXT("in place: an element that isn't there"),
		TEXT("int[] _a = [5];\n_a[3] Bump();"), TEXT(""), TEXT("(2:3)"), TEXT("Bump on '_a': Index [3] is out of range") },
	{ TEXT("in place: the target's type can't change"),
		TEXT("int _n = 1;\n_n BadBump();"), TEXT(""), TEXT("(2:1)"), TEXT("Command 'BadBump' changed the type of its target, '_n' (it must stay an int).") },
	{ TEXT("in place: array commands fit values to the array"),
		TEXT("float[] _f = [];\nint[][] _g = [];\n_f Push(1);\n_f InsertAt(2, 0);\n_g Push([1, 2]);\n_g[0] Push(3);\n_g Push([]);\nfloat _x = _f Pop();\nTalk(\"{0} {1} {2} {3}\", _f, _g, _x, _g[0] RemoveAt(1));\n_g[0] Pop();\nTalk(\"{0}\", _g);"),
		TEXT("[2.0] [[1, 2, 3], []] 1.0 2|[[1], []]"), nullptr, nullptr },
	{ TEXT("in place: Pop of nil on a line of its own is fine"),
		TEXT("int[] _a = [];\n_a[1] = 4;\n_a RemoveAt(0);\nTalk(\"{0}\", _a);"), TEXT("[4]"), nullptr, nullptr },

	// ---- Type parameters (TypeParam) ----
	{ TEXT("type param: Lerp on floats, ints counting as floats"),
		TEXT("Talk(\"{0} {1}\", Lerp(0, 10, 0.5), Lerp(1.0, 2, 0.5));"), TEXT("5.0 1.5"), nullptr, nullptr },
	{ TEXT("type param: Lerp on vectors"),
		TEXT("vector3 _a = [0, 0, 0];\nvector2 _w = Lerp([0, 0], [1, 1], 0.5);\nTalk(\"{0} {1} {2}\", Lerp(_a, [2, 4, 6], 0.5), _w, Lerp([2, 4, 6], _a, 1));"),
		TEXT("(1.0, 2.0, 3.0) (0.5, 0.5) (0.0, 0.0, 0.0)"), nullptr, nullptr },
	{ TEXT("type param: undeclared globals are worked out when called"),
		TEXT("float _f;\nvector3 _v;\n_f = Lerp(score, ratio, 1);\n_v = Lerp(spot, [3, 4, 5], 0.5);\n_v = Lerp(spot, _v, 0);\nTalk(\"{0} {1} {2}\", _f, _v, Lerp(spot, spot, 0.5));"),
		TEXT("0.5 (1.0, 2.0, 3.0) (1.0, 2.0, 3.0)"), nullptr, nullptr },
	{ TEXT("type param: an undeclared global of a type that isn't allowed"),
		TEXT("Talk(\"{0}\", Lerp(ratio, lots, 0.5));"), TEXT(""), TEXT("(1:13)"), TEXT("Lerp: argument 2 ('B') must be a float, a vector2 or a vector3, but it is a string.") },
	{ TEXT("type param: undeclared globals of two types"),
		TEXT("Talk(\"{0}\", Lerp(spot, ratio, 0.5));"), TEXT(""), TEXT("(1:13)"), TEXT("Lerp: 'A' and 'B' must be the same type, but they are a vector3 and a float.") },
	{ TEXT("type param: an undeclared global beside a written-out vector"),
		TEXT("vector3 _v;\n_v = Lerp(score, [1, 2, 3], 0.5);"), TEXT(""), TEXT("(2:6)"), TEXT("Expected a vector3 here, but the value is an int.") },
	{ TEXT("type param: the result's type, found when it runs"),
		TEXT("float _f;\n_f = Lerp(spot, spot, 0.5);"), TEXT(""), TEXT("(2:1)"), TEXT("Can't store a vector3 in '_f', which holds a float.") },
	{ TEXT("type param: a latent command keeps its type"),
		TEXT("int _i;\nfloat _f;\n_i = EchoLater(4);\n_f = EchoLater(2.5);\nTalk(\"{0} {1} {2}\", _i, _f, EchoLater(score));"), TEXT("4 2.5 7"), nullptr, nullptr },
	{ TEXT("Count is a command: an undeclared global that can't be counted"),
		TEXT("Talk(\"{0}\", Count(score));"), TEXT(""), TEXT("(1:13)"), TEXT("Count: argument 1 ('Value') must be an array, a vector2, a vector3 or a string, but it is an int.") },
	{ TEXT("Count is a command: undeclared globals"),
		TEXT("Talk(\"{0} {1}\", Count(lots), Count(spot));"), TEXT("4 3"), nullptr, nullptr },
	{ TEXT("command returns the wrong type"), TEXT("int _x;\n_x = BadReturn();"), TEXT(""), TEXT("(2:6)"), TEXT("returned a string, but it should return an int") },
	{ TEXT("error inside a function"), TEXT("function void F()\n{\n\tint _x;\n\tTalk(\"{0}\", _x);\n};\nF();"), TEXT(""), TEXT("(4:14) in F()"), TEXT("'_x' is nil") },
	{ TEXT("Talk with a missing value"), TEXT("Talk(\"{1}\", 5);"), TEXT(""), TEXT("(1:1)"), TEXT("uses {1}, but only 1 value(s)") },
};

/** Checks one case; returns an empty string if it passed. */
static FString BangoCheckRuntimeCase(const FBangoRuntimeTestCase& Case)
{
	FBangoRuntimeTestResult Result;
	BangoRunScript(Case.Source, Result, 20000, TArray<FBangoValue>(), Case.Owner);
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
		TEXT("Talk(\"start\");\n")
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
		Expect(Bango::CompileSource(TEXT("Talk(\"a\");\nSleep();\nTalk(\"b\");\nSleep(0);\nTalk(\"c\");"), TEXT("frame.bango"), Commands, *FrameProgram, CompileError),
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

	// ---- Save and load keep the owner (self) ----
	{
		Log.Reset();
		TSharedPtr<FBangoProgram> SelfProgram = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(TEXT("Sleep(1);\nself Speak(\"back\");"), TEXT("self.bango"), Commands, *SelfProgram, CompileError), TEXT("self save: compiles"));
		FBangoGlobalStore Globals;
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoScriptOwner Owner;
		Owner.BangoName = TEXT("john");
		Instance.SetOwner(Owner);
		FBangoRuntimeError Error;
		Instance.Start(SelfProgram, Commands, Globals, 5, Error);
		Instance.Resume(Context);

		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Instance.Serialize(Writer);
		}
		FBangoInstance Loaded;
		{
			FMemoryReader Reader(Bytes);
			Loaded.Serialize(Reader);
			Expect(!Reader.IsError(), TEXT("self save: reads back"));
		}
		Expect(Loaded.GetOwner().BangoName == TEXT("john"), TEXT("self save: the owner is loaded"));
		FString BindError;
		Expect(Loaded.Bind(SelfProgram, Commands, BindError), TEXT("self save: binds"));
		Loaded.Resume(Context);
		Loaded.Resume(Context);
		Expect(Loaded.GetStatus() == EBangoInstanceStatus::Finished && Log.Num() == 1 && Log[0] == TEXT("john: back"), TEXT("self save: self is still the owner after loading"));
	}

	// ---- Save and load keep a pending command's type arguments (what its result must be) ----
	{
		Log.Reset();
		TSharedPtr<FBangoProgram> EchoProgram = MakeShared<FBangoProgram>();
		Expect(Bango::CompileSource(TEXT("int _i;\n_i = EchoLater(4);\nTalk(\"{0}\", _i + 1);"), TEXT("echo.bango"), Commands, *EchoProgram, CompileError), TEXT("type args save: compiles"));
		FBangoGlobalStore Globals;
		Context.Globals = &Globals;
		FBangoInstance Instance;
		FBangoRuntimeError Error;
		Instance.Start(EchoProgram, Commands, Globals, 6, Error);
		Expect(Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("type args save: waits in the command"));

		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Instance.Serialize(Writer);
		}
		FBangoInstance Loaded;
		{
			FMemoryReader Reader(Bytes);
			Loaded.Serialize(Reader);
			Expect(!Reader.IsError(), TEXT("type args save: reads back"));
		}
		FString BindError;
		Expect(Loaded.Bind(EchoProgram, Commands, BindError), TEXT("type args save: binds"));
		Loaded.Resume(Context);
		Expect(Loaded.GetStatus() == EBangoInstanceStatus::Finished && Log.Num() == 1 && Log[0] == TEXT("5"), TEXT("type args save: the result is still an int after loading"));
	}

	// ---- Save and load mid-function, mid-expression ----
	{
		Log.Reset();
		const TCHAR* Deep =
			TEXT("int[] _list = [1, 2];\nfunction int Pick(int _base)\n{\n\tint _local;\n\t_local = _base * 10;\n\treturn _local + AskChoice(\"a\", \"b\");\n};\n")
			TEXT("_list Push(Pick(4));\nTalk(\"{0}\", _list);");
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
		Bango::CompileSource(TEXT("Talk(\"x\");"), TEXT("other.bango"), Commands, *Other, CompileError);
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
		Expect(Bango::CompileSource(TEXT("global int lots;\nTalk(\"never runs\");"), TEXT("conflict.bango"), Commands, *Conflict, CompileError), TEXT("conflict: compiles"));
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
		Bango::CompileSource(TEXT("Talk(message);"), TEXT("reader.bango"), Commands, *Reader, CompileError);
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

		// Global defaults across scripts: one declared without a value first leaves the global nil, and a later default
		// is given then, as it's nil; one that has a value keeps it.
		{
			FReloadRun Run;
			BangoMakeRuntimeTestGlobals(Run.Globals);
			Context.Globals = &Run.Globals;
			FBangoRuntimeError StartError;
			FBangoInstance First;
			Expect(First.Start(Compile(TEXT("global int stage;")), Commands, Run.Globals, 1, StartError), TEXT("global defaults: a value-less declaration starts"));
			const FBangoValue* Stage = Run.Globals.Find(TEXT("stage"));
			Expect(Stage && Stage->IsNil(), TEXT("global defaults: declared without a value, it's nil"));
			FBangoInstance Second;
			Expect(Second.Start(Compile(TEXT("global int stage ?= 1;")), Commands, Run.Globals, 2, StartError), TEXT("global defaults: the one with a default starts"));
			Stage = Run.Globals.Find(TEXT("stage"));
			Expect(Stage && !Stage->IsNil() && Stage->GetInt() == 1, TEXT("global defaults: a nil global gets the default"));
			FString SetError;
			Run.Globals.Set(TEXT("stage"), FBangoValue::MakeInt(5), SetError);
			FBangoInstance Third;
			Third.Start(Compile(TEXT("global int stage ?= 1;")), Commands, Run.Globals, 3, StartError);
			Stage = Run.Globals.Find(TEXT("stage"));
			Expect(Stage && !Stage->IsNil() && Stage->GetInt() == 5, TEXT("global defaults: a global with a value keeps it"));
			Run.Globals.Set(TEXT("stage"), FBangoValue::MakeNil(FBangoType::Int()), SetError);
			FBangoInstance Fourth;
			Fourth.Start(Compile(TEXT("global int stage ?= 3;")), Commands, Run.Globals, 4, StartError);
			Stage = Run.Globals.Find(TEXT("stage"));
			Expect(Stage && !Stage->IsNil() && Stage->GetInt() == 3, TEXT("global defaults: one set back to nil gets the default again"));
		}

		// A moving platform: the math in the loop changes while it runs, and a local is added.
		const TCHAR* PlatformV1 =
			TEXT("scriptVar float _x = 0;\nscriptVar float _speed = 1;\n")
			TEXT("while (true)    //@1(0, 0)\n{\n")
			TEXT("\t_x += _speed;    //@2(2, 0)\n")
			TEXT("\tTalk(\"{0}\", _x);    //@3(4, 0)\n")
			TEXT("\tSleep(0);    //@4(6, 0)\n};");
		const TCHAR* PlatformV2 =
			TEXT("scriptVar float _x = 0;\nscriptVar float _speed = 1;\nscriptVar float _extra = 5;\n")
			TEXT("while (true)    //@1(0, 0)\n{\n")
			TEXT("\t_x += _speed * 10;    //@2(2, 0)\n")
			TEXT("\tTalk(\"{0} {1}\", _x, _extra);    //@3(4, 0)\n")
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

		// Locals in a block: matched by their declaring node; one the new version adds above where it's paused starts
		// as nil, and gets its value the next time its declaration runs.
		{
			const TCHAR* LocalsV1 =
				TEXT("while (true)    //@1(0, 0)\n{\n")
				TEXT("\tfloat _d = 2;    //@5(1, 0)\n")
				TEXT("\tSleep(0);    //@4(2, 0)\n")
				TEXT("\t_d += 1;    //@6(3, 0)\n")
				TEXT("\tTalk(\"{0}\", _d);    //@7(4, 0)\n};");
			const TCHAR* LocalsV2 =
				TEXT("while (true)    //@1(0, 0)\n{\n")
				TEXT("\tfloat _d = 2;    //@5(1, 0)\n")
				TEXT("\tbool _n = true;    //@8(1, 1)\n")
				TEXT("\tSleep(0);    //@4(2, 0)\n")
				TEXT("\t_d += 1;    //@6(3, 0)\n")
				TEXT("\tTalk(\"{0} {1}\", _d, IsNil(_n));    //@7(4, 0)\n};");
			FReloadRun Run;
			Expect(StartRun(Run, Compile(LocalsV1)) && Log.Num() == 0, TEXT("reload locals: waits in the loop"));
			FString ReloadError;
			Expect(Run.Instance.RequestReload(Compile(LocalsV2), Commands, ReloadError), *(FString(TEXT("reload locals: the request is accepted: ")) + ReloadError));
			Run.Instance.Resume(Context);
			Expect(LastLog() == TEXT("3.0 true"), *(FString(TEXT("reload locals: _d kept by its node, the new _n is nil: ")) + LastLog()));
			Run.Instance.Resume(Context);
			Expect(LastLog() == TEXT("3.0 false"), *(FString(TEXT("reload locals: _n's declaration ran: ")) + LastLog()));
		}

		// A local that changes type can't be carried over: it keeps the old version, and says why.
		{
			FReloadRun Run;
			StartRun(Run, Compile(PlatformV1));
			FString ReloadError;
			Run.Instance.RequestReload(Compile(TEXT("scriptVar int _x = 0;\nscriptVar float _speed = 1;\nwhile (true)    //@1(0, 0)\n{\n\t_x += 5;    //@2(2, 0)\n\tTalk(\"{0}\", _x);    //@3(4, 0)\n\tSleep(0);    //@4(6, 0)\n};")), Commands, ReloadError);
			Run.Instance.Resume(Context);
			FString Message;
			const EBangoReloadResult TypeResult = Run.Instance.TakeReloadResult(Message);
			Expect(TypeResult == EBangoReloadResult::KeptOld && Message.Contains(TEXT("'_x' was float and is int")), *FString::Printf(TEXT("reload: a changed type keeps the old version (%d: %s)"), (int32)TypeResult, *Message));
			Expect(LastLog() == TEXT("2.0") && !Run.Instance.IsReloadPending(), TEXT("reload: the old version carries on"));
		}

		// Paused inside a function: the function's new code runs, and it goes back to the same call in the main graph.
		{
			FReloadRun Run;
			const TCHAR* StepV1 = TEXT("global int count;\ncount = 0;    //@1(0, 0)\nwhile (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tcount += 1;    //@10(0, 0)\n\tTalk(\"{0}\", count);    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};");
			const TCHAR* StepV2 = TEXT("global int count;\ncount = 0;    //@1(0, 0)\nwhile (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tcount += 100;    //@10(0, 0)\n\tTalk(\"step {0}\", count);    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};");
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
			StartRun(Run, Compile(TEXT("while (true)    //@2(2, 0)\n{\n\tStep();    //@3(4, 0)\n};\n\nfunction void Step()\n{\n\tTalk(\"x\");    //@11(2, 0)\n\tSleep(0);    //@12(4, 0)\n};")));
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
			StartRun(Run, Compile(TEXT("int _n = 0;\nwhile (true)    //@1(0, 0)\n{\n\t_n++;    //@2(2, 0)\n\tif (_n > 1)    //@3(4, 0)\n\t{\n\t\tTalk(\"x\");    //@4(6, 0)\n\t};\n\n\tSleep(0);    //@5(8, 0)\n};")));
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
				int32 Expected;              // 1 true, 0 false, 2 unset, -1 an error
				const TCHAR* ErrorLocation;  // for errors: like "(1:5)"
				const TCHAR* ErrorContains;
			};
			const FConditionCase Cases[] =
			{
				{ TEXT("expression"), TEXT("lots == \"lots\""), 1, nullptr, nullptr },
				{ TEXT("expression, false"), TEXT("Count([1, 2]) > 2"), 0, nullptr, nullptr },
				{ TEXT("expression with comments and an inputs line"), TEXT("// Is it big?\n#inputs (int _n = 7)\n\n_n > 5    // more than five"), 1, nullptr, nullptr },
				{ TEXT("expression with a string holding ;"), TEXT("lots != \"a;b\" && lots != \"//\""), 1, nullptr, nullptr },
				{ TEXT("a script that returns"), TEXT("int _n = 0;\nfor (int _i = 0; _i < 4; _i++)\n{\n\t_n += _i;\n};\nreturn _n == 6;"), 1, nullptr, nullptr },
				{ TEXT("a script with a function"), TEXT("function bool Big(int _v)\n{\n\treturn _v > 10;\n};\n\nreturn Big(3);"), 0, nullptr, nullptr },
				{ TEXT("not a bool"), TEXT("3 + 4"), -1, TEXT("(1:3)"), TEXT("The condition needs a bool, but this is an int.") },
				{ TEXT("an error points into the expression as written"), TEXT("lots == missing_cmd()"), -1, TEXT("(1:9)"), TEXT("missing_cmd") },
				{ TEXT("return with no value"), TEXT("return;"), -1, TEXT("(1:1)"), TEXT("A condition must return true or false") },
				{ TEXT("reaching the end"), TEXT("int _n = 1;\nif (_n > 5)\n{\n\treturn true;\n};"), -1, nullptr, TEXT("reached its end without returning true or false") },
				{ TEXT("an unset global points into the expression as written"), TEXT("lots == \"lots\" && nothere > 1"), 2, TEXT("(1:19)"), TEXT("nothere") },
				{ TEXT("unset: a global that's nil"), TEXT("global int empty;\nreturn empty > 1;"), 2, TEXT("(2:8)"), TEXT("'empty' is nil") },
				{ TEXT("unset: indexing a global that's nil"), TEXT("global int[] list;\nreturn list[0] > 1;"), 2, nullptr, TEXT("'list' is nil, so it can't be indexed") },
				{ TEXT("unset: the whole condition is a global that doesn't exist"), TEXT("nothere"), 2, TEXT("(1:1)"), TEXT("nothere") },
				{ TEXT("unset: a global that doesn't exist, named like an actor's Bango ID"), TEXT("john"), 2, TEXT("(1:1)"), TEXT("write @john") },
				{ TEXT("unset: not a global that doesn't exist"), TEXT("!nothere"), 2, nullptr, TEXT("nothere") },
				{ TEXT("unset: a returned global that doesn't exist"), TEXT("return nothere;"), 2, nullptr, TEXT("nothere") },
				{ TEXT("IsNil of a global that doesn't exist is true"), TEXT("IsNil(nothere)"), 1, nullptr, nullptr },
				{ TEXT("&& stops at false"), TEXT("false && nothere > 1"), 0, nullptr, nullptr },
				{ TEXT("|| stops at true"), TEXT("flag || nothere > 1"), 1, nullptr, nullptr },
				{ TEXT("an unset global behind || is still read"), TEXT("!flag || nothere > 1"), 2, nullptr, TEXT("nothere") },
				{ TEXT("past the end of a local array is an error, not unset"), TEXT("int[] _a = [1];\nreturn _a[3] > 0;"), -1, nullptr, TEXT("past the end") },
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
					if (!Instance.Start(Condition, Commands, ConditionGlobals, 1, StartError))
					{
						Outcome = StartError.ToString();
					}
					else
					{
						const EBangoConditionResult Result = Instance.FinishCondition(Instance.Resume(Context), Outcome);
						Got = Result == EBangoConditionResult::True ? 1 : Result == EBangoConditionResult::False ? 0 : Result == EBangoConditionResult::Unset ? 2 : -1;
					}
				}
				const bool bOk = Case.Expected == 0 || Case.Expected == 1
					? Got == Case.Expected
					: Got == Case.Expected && Outcome.Contains(Case.ErrorContains) && (!Case.ErrorLocation || Outcome.Contains(Case.ErrorLocation));
				Expect(bOk, *FString::Printf(TEXT("condition: %s (got %d: %s)"), Case.Name, Got, *Outcome));
			}
		}

		// Bango::ParseFixedValue: a value written as text, checked as a script variable's starting value is.
		{
			struct FFixedCase { const TCHAR* Type; const TCHAR* Text; const TCHAR* Expected; }; // Expected: its debug text and type, or "!" and part of the error
			const FFixedCase FixedCases[] = {
				{ TEXT("vector3"), TEXT("[1, 2, 3]"), TEXT("(1.0, 2.0, 3.0) vector3") },
				{ TEXT("vector2[]"), TEXT("[[1, 2]]"), TEXT("[(1.0, 2.0)] vector2[]") },
				{ TEXT("float"), TEXT(" 2 "), TEXT("2.0 float") },
				{ TEXT("int"), TEXT("-3"), TEXT("-3 int") },
				{ TEXT("float[]"), TEXT("[1, 2.5]"), TEXT("[1.0, 2.5] float[]") },
				{ TEXT("int[][]"), TEXT("[[1], []]"), TEXT("[[1], []] int[][]") },
				{ TEXT("string"), TEXT("\"hi\""), TEXT("\"hi\" string") },
				{ TEXT("bool"), TEXT("true"), TEXT("true bool") },
				{ TEXT("entity"), TEXT("@john"), TEXT("@john entity") },
				{ TEXT("entity[]"), TEXT("[@a, @b]"), TEXT("[@a, @b] entity[]") },
				{ TEXT("entity"), TEXT("player"), TEXT("player entity") },
				{ TEXT("text"), TEXT("INVTEXT(\"Hi\")"), TEXT("text(\"Hi\") text") },
				{ TEXT("text"), TEXT("NSLOCTEXT(\"Game\", \"Key\", \"Hi\")"), TEXT("text(\"Hi\") text") },
				{ TEXT("bool"), TEXT(""), TEXT("nil bool") },
				{ TEXT("vector3"), TEXT("   "), TEXT("nil vector3") },
				{ TEXT("int"), TEXT("nil"), TEXT("nil int") },
				{ TEXT("vector3"), TEXT("[1, 2]"), TEXT("!A vector3 needs exactly 3 numbers") },
				{ TEXT("int"), TEXT("2.5"), TEXT("!This value needs an int, but this is a float.") },
				{ TEXT("int"), TEXT("\"s\""), TEXT("!This value needs an int, but this is a string.") },
				{ TEXT("string"), TEXT("hi"), TEXT("!This must be a fixed value") },
				{ TEXT("int"), TEXT("1 + 2"), TEXT("!This must be a fixed value") },
				{ TEXT("int"), TEXT("Count(_x)"), TEXT("!A name, a call or a calculation isn't one.") },
				{ TEXT("int"), TEXT("1; WriteLog(\"x\")"), TEXT("!This doesn't read as one value.") },
				{ TEXT("int[]"), TEXT("[1, 2"), TEXT("!This doesn't read as a value: Expected ']'") },
				{ TEXT("int"), TEXT("1\n2"), TEXT("!A value is written on one line.") },
				{ TEXT("int"), TEXT("99999999999"), TEXT("!too large for an int") },
				{ TEXT("thing"), TEXT("1"), TEXT("!'thing' isn't a type a value can have") },
				{ TEXT("void"), TEXT("1"), TEXT("!isn't a type a value can have") },
				{ TEXT("text"), TEXT("LOCTEXT(\"Key\", \"Hi\")"), TEXT("!A LOCTEXT takes its namespace from a script") },
				{ TEXT("entity"), TEXT("self"), TEXT("!'self' is the actor a script belongs to") },
				{ TEXT("entity[]"), TEXT("[@a, self]"), TEXT("!'self' is the actor a script belongs to") },
			};
			for (const FFixedCase& Case : FixedCases)
			{
				FBangoValue Value;
				FString FixedError;
				const bool bRead = Bango::ParseFixedValue(Case.Type, Case.Text, Value, FixedError);
				const FString Got = bRead ? (Value.IsNil() ? FString(TEXT("nil")) : Value.ToDebugString()) + TEXT(" ") + Value.GetType().ToString() : FString(TEXT("!")) + FixedError;
				const bool bOk = Case.Expected[0] == TEXT('!') ? !bRead && FixedError.Contains(Case.Expected + 1) : bRead && Got == Case.Expected;
				Expect(bOk, *FString::Printf(TEXT("fixed value: %s '%s' (got %s)"), Case.Type, Case.Text, *Got));
			}
			const TArray<FString>& TypeNames = Bango::GetValueTypeNames();
			Expect(TypeNames.Num() == 16 && TypeNames[0] == TEXT("bool") && TypeNames[4] == TEXT("entity") && TypeNames[7] == TEXT("vector3") && TypeNames[8] == TEXT("bool[]") && TypeNames[15] == TEXT("vector3[]"),
				TEXT("fixed value: the types a value can have, in menu order"));
			for (const FString& TypeName : TypeNames)
			{
				FBangoValue Value;
				FString FixedError;
				Expect(Bango::ParseFixedValue(TypeName, TEXT("nil"), Value, FixedError) && Value.IsNil() && Value.GetType().ToString() == TypeName, *(FString(TEXT("fixed value: nil of ")) + TypeName));
			}
		}

		// Inputs signatures (an inline script's inputs given by its owner's C++, Bango::RegisterInputsSignature).
		{
			FBangoInputsSignature Signature;
			FString SignatureError;
			TArray<FString> Warnings;
			const TArray<FBangoInputDecl> Inputs = { { TEXT("entity _trigger"), TEXT("The trigger.") }, { TEXT("int _count") }, { TEXT("entity[] _inside") } };
			Expect(Bango::ParseInputsSignature(Inputs, Signature, SignatureError, Warnings) && Warnings.Num() == 0,
				*(FString(TEXT("signature: reads: ")) + SignatureError));
			Signature.Label = TEXT("BangoTrigger's 'Activation'");
			Expect(Signature.Params.Num() == 3 && Signature.Params[2].Type == FBangoType::ArrayOf(EBangoBaseType::Entity) && !Signature.Params[0].bHasDefault
				&& Signature.Tooltips.Num() == 3 && Signature.Tooltips[0] == TEXT("The trigger.") && Signature.Tooltips[1].IsEmpty(),
				TEXT("signature: its inputs and tooltips"));
			Expect(Signature.ToInputsText() == TEXT("entity _trigger, int _count, entity[] _inside") && Signature.GetNames() == TEXT("_trigger, _count, _inside"),
				*(FString(TEXT("signature: as text: ")) + Signature.ToInputsText()));

			struct FBadInputs { const TCHAR* Name; TArray<FBangoInputDecl> Inputs; const TCHAR* ErrorContains; };
			const FBadInputs BadCases[] = {
				{ TEXT("a default"), { { TEXT("int _n = 1") } }, TEXT("can't have a default") },
				{ TEXT("two in one entry"), { { TEXT("int _a, int _b") } }, TEXT("one input per entry") },
				{ TEXT("the same name twice"), { { TEXT("int _a") }, { TEXT("bool _a") } }, TEXT("Two inputs are named '_a'") },
				{ TEXT("not a type"), { { TEXT("thing _a") } }, TEXT("should be a type and a name") },
			};
			for (const FBadInputs& Case : BadCases)
			{
				FBangoInputsSignature Bad;
				FString BadError;
				TArray<FString> BadWarnings;
				Expect(!Bango::ParseInputsSignature(Case.Inputs, Bad, BadError, BadWarnings) && BadError.Contains(Case.ErrorContains),
					*FString::Printf(TEXT("signature: %s: %s"), Case.Name, *BadError));
			}
			{
				FBangoInputsSignature NoUnderscore;
				FString NoUnderscoreError;
				TArray<FString> NoUnderscoreWarnings;
				Expect(Bango::ParseInputsSignature({ { TEXT("int count") } }, NoUnderscore, NoUnderscoreError, NoUnderscoreWarnings)
					&& NoUnderscore.Params.Num() == 1 && NoUnderscoreWarnings.Num() == 1 && NoUnderscoreWarnings[0].Contains(TEXT("should begin with an underscore: 'count'")),
					TEXT("signature: a name without '_' works, with a warning"));
			}

			// Compiled with it: the inputs are the script's parameters, in order, and its owner passes them.
			struct FSignatureCase { const TCHAR* Name; const TCHAR* Source; int32 Expected; const TCHAR* ErrorContains; };
			const FSignatureCase SignatureCases[] = {
				{ TEXT("reads its inputs"), TEXT("_count == 3 && IsNil(_trigger) && Count(_inside) == 0"), 1, nullptr },
				{ TEXT("as a script"), TEXT("int _more = _count + 1;\nreturn _more == 4;"), 1, nullptr },
				{ TEXT("no #inputs line"), TEXT("#inputs (int _n = 1)\n_n > 0"), -1, TEXT("This script's inputs are given by BangoTrigger's 'Activation': _trigger, _count, _inside. Remove the #inputs line") },
				{ TEXT("a local can't have an input's name"), TEXT("int _count = 1;\nreturn true;"), -1, TEXT("'_count' is an input given by BangoTrigger's 'Activation'") },
				{ TEXT("a function's own local can"), TEXT("function bool F()\n{\n\tint _count = 1;\n\treturn _count > 0;\n};\nreturn F();"), 1, nullptr },
			};
			for (const FSignatureCase& Case : SignatureCases)
			{
				TSharedPtr<FBangoProgram> SignedProgram = MakeShared<FBangoProgram>();
				FBangoCompileError SignedError;
				FString Outcome;
				int32 Got = -1;
				if (!Bango::CompileCondition(Case.Source, TEXT("sig"), Commands, *SignedProgram, SignedError, &Signature))
				{
					Outcome = SignedError.ToString();
				}
				else
				{
					FBangoGlobalStore SignatureGlobals;
					Context.Globals = &SignatureGlobals;
					FBangoInstance Instance;
					FBangoRuntimeError StartError;
					const TArray<FBangoValue> Args = { FBangoValue::MakeNil(FBangoType::Entity()), FBangoValue::MakeInt(3), FBangoValue::MakeArray(FBangoType::Entity(), TArray<FBangoValue>()) };
					if (!Instance.Start(SignedProgram, Commands, SignatureGlobals, 1, StartError, Args))
					{
						Outcome = StartError.ToString();
					}
					else
					{
						const EBangoConditionResult Result = Instance.FinishCondition(Instance.Resume(Context), Outcome);
						Got = Result == EBangoConditionResult::True ? 1 : Result == EBangoConditionResult::False ? 0 : -1;
					}
				}
				const bool bOk = Case.Expected >= 0 ? Got == Case.Expected : Got == -1 && Outcome.Contains(Case.ErrorContains);
				Expect(bOk, *FString::Printf(TEXT("signature: %s (got %d: %s)"), Case.Name, Got, *Outcome));
			}

			// An object's own instance inputs (FBangoInlineScriptBase::SetInstanceInputs), after the signature's.
			{
				const TArray<FBangoInputDecl> Own = { { TEXT("vector3 _red"), TEXT("The colour when it's on.") }, { TEXT("float _speed") } };
				FBangoInputsSignature WithOwn;
				FString OwnError;
				TArray<FString> OwnWarnings;
				Expect(Bango::AddInstanceInputs(&Signature, Own, WithOwn, OwnError, OwnWarnings) && OwnWarnings.Num() == 0, *(FString(TEXT("instance inputs: read: ")) + OwnError));
				Expect(WithOwn.ToInputsText() == TEXT("entity _trigger, int _count, entity[] _inside, vector3 _red, float _speed") && WithOwn.NumInstanceInputs == 2
					&& WithOwn.Tooltips.Num() == 5 && WithOwn.Tooltips[3] == TEXT("The colour when it's on.") && WithOwn.Tooltips[0] == TEXT("The trigger."),
					*(FString(TEXT("instance inputs: after the signature's: ")) + WithOwn.ToInputsText()));
				Expect(WithOwn.Label == TEXT("BangoTrigger's 'Activation' and this object") && WithOwn.SignatureLabel == Signature.Label
					&& !WithOwn.IsInstanceInput(2) && WithOwn.IsInstanceInput(3) && WithOwn.IsInstanceInput(4) && !WithOwn.IsInstanceInput(5)
					&& WithOwn.GetSourceLabel(0) == TEXT("BangoTrigger's 'Activation'") && WithOwn.GetSourceLabel(4) == TEXT("this object (its own inputs)"),
					*(FString(TEXT("instance inputs: whose each is: ")) + WithOwn.Label));
				Expect(Signature.Params.Num() == 3 && Signature.NumInstanceInputs == 0 && Signature.GetSourceLabel(1) == Signature.Label, TEXT("instance inputs: the signature itself is untouched"));

				// Without a signature: they're all there is.
				FBangoInputsSignature OnlyOwn;
				Expect(Bango::AddInstanceInputs(nullptr, Own, OnlyOwn, OwnError, OwnWarnings) && OnlyOwn.ToInputsText() == TEXT("vector3 _red, float _speed")
					&& OnlyOwn.Label == TEXT("this object") && OnlyOwn.NumInstanceInputs == 2 && OnlyOwn.IsInstanceInput(0), TEXT("instance inputs: without a signature"));

				const FBadInputs BadOwn[] = {
					{ TEXT("a signature input's name"), { { TEXT("float _speed") }, { TEXT("int _count") } }, TEXT("This object's own input '_count' (SetInstanceInputs) has the name of an input of BangoTrigger's 'Activation'") },
					{ TEXT("the same name twice"), { { TEXT("float _speed") }, { TEXT("int _speed") } }, TEXT("This object's own inputs (SetInstanceInputs): Two inputs are named '_speed'") },
					{ TEXT("a default"), { { TEXT("float _speed = 2") } }, TEXT("can't have a default") },
					{ TEXT("doesn't read"), { { TEXT("_speed") } }, TEXT("should be a type and a name") },
				};
				for (const FBadInputs& Case : BadOwn)
				{
					FBangoInputsSignature Bad;
					FString BadError;
					TArray<FString> BadWarnings;
					Expect(!Bango::AddInstanceInputs(&Signature, Case.Inputs, Bad, BadError, BadWarnings) && BadError.Contains(Case.ErrorContains),
						*FString::Printf(TEXT("instance inputs: %s: %s"), Case.Name, *BadError));
				}
				{
					FBangoInputsSignature NoUnderscore;
					FString NoUnderscoreError;
					TArray<FString> NoUnderscoreWarnings;
					Expect(Bango::AddInstanceInputs(&Signature, { { TEXT("float speed") } }, NoUnderscore, NoUnderscoreError, NoUnderscoreWarnings)
						&& NoUnderscore.Params.Num() == 4 && NoUnderscoreWarnings.Num() == 1 && NoUnderscoreWarnings[0].Contains(TEXT("'speed'")),
						TEXT("instance inputs: a name without '_' works, with a warning"));
				}

				// Compiled with them: parameters after the signature's, passed in that order.
				const FSignatureCase OwnCases[] = {
					{ TEXT("reads both kinds"), TEXT("_count == 3 && _red.z == 9 && _speed > 1"), 1, nullptr },
					{ TEXT("as a script"), TEXT("vector3 _dim = _red * _speed;\nreturn _dim.x == 2.5 && Count(_inside) == 0;"), 1, nullptr },
					{ TEXT("no #inputs line"), TEXT("#inputs (int _n = 1)\n_n > 0"), -1, TEXT("This script's inputs are given by BangoTrigger's 'Activation' and this object: _trigger, _count, _inside, _red, _speed. Remove the #inputs line") },
					{ TEXT("a local can't have an instance input's name"), TEXT("float _speed = 1;\nreturn true;"), -1, TEXT("'_speed' is an input given by BangoTrigger's 'Activation' and this object") },
				};
				for (const FSignatureCase& Case : OwnCases)
				{
					TSharedPtr<FBangoProgram> OwnProgram = MakeShared<FBangoProgram>();
					FBangoCompileError CompileOwnError;
					FString Outcome;
					int32 Got = -1;
					if (!Bango::CompileCondition(Case.Source, TEXT("own"), Commands, *OwnProgram, CompileOwnError, &WithOwn))
					{
						Outcome = CompileOwnError.ToString();
					}
					else
					{
						FBangoGlobalStore OwnGlobals;
						Context.Globals = &OwnGlobals;
						FBangoInstance Instance;
						FBangoRuntimeError StartError;
						const TArray<FBangoValue> Args = { FBangoValue::MakeNil(FBangoType::Entity()), FBangoValue::MakeInt(3), FBangoValue::MakeArray(FBangoType::Entity(), TArray<FBangoValue>()),
							FBangoValue::MakeVector3(1, 0, 9), FBangoValue::MakeDouble(2.5) };
						if (!Instance.Start(OwnProgram, Commands, OwnGlobals, 1, StartError, Args))
						{
							Outcome = StartError.ToString();
						}
						else
						{
							const EBangoConditionResult Result = Instance.FinishCondition(Instance.Resume(Context), Outcome);
							Got = Result == EBangoConditionResult::True ? 1 : Result == EBangoConditionResult::False ? 0 : -1;
						}
					}
					const bool bOk = Case.Expected >= 0 ? Got == Case.Expected : Got == -1 && Outcome.Contains(Case.ErrorContains);
					Expect(bOk, *FString::Printf(TEXT("instance inputs: %s (got %d: %s)"), Case.Name, Got, *Outcome));
				}
			}
		}

		// `target call Name(args)`: handed to the host (Context.CallReflected), which calls the Blueprint function.
		{
			TArray<FString> Calls;
			Context.CallReflected = [&Calls](const FBangoValue& Target, const FString& Name, const TArray<FBangoValue>& Args, bool bWantResult, FBangoValue& OutResult, FString& OutError)
			{
				FString Call = Target.GetEntityName() + TEXT(" ") + Name + TEXT("(");
				for (int32 i = 0; i < Args.Num(); ++i)
				{
					Call += (i > 0 ? TEXT(", ") : TEXT("")) + (Args[i].IsNil() ? FString(TEXT("nil")) : Args[i].ToDisplayString());
				}
				Calls.Add(Call + (bWantResult ? TEXT(") +result") : TEXT(")")));
				if (Name == TEXT("Broken"))
				{
					OutError = TEXT("'call Broken': it broke.");
					return false;
				}
				OutResult = FBangoValue::MakeInt(5);
				return true;
			};
			struct FCallCase { const TCHAR* Name; const TCHAR* Source; int32 Expected; const TCHAR* ErrorOrCalls; };
			const FCallCase CallCases[] = {
				{ TEXT("arguments and a result"), TEXT("@lamp call TurnOn(2, \"x\", nil);\nint _n = @lamp call GetCount() + 1;\nreturn _n == 6;"), 1,
					TEXT("lamp TurnOn(2, x, nil) | lamp GetCount() +result") },
				{ TEXT("its result is checked when stored"), TEXT("bool _b = @lamp call GetCount();\nreturn true;"), -1, TEXT("Can't store") },
				{ TEXT("an error stops it"), TEXT("@lamp call Broken();\nreturn true;"), -1, TEXT("'call Broken': it broke.") },
				{ TEXT("a nil target"), TEXT("nil call TurnOn();\nreturn true;"), -1, TEXT("'call TurnOn': the target is nil.") },
			};
			for (const FCallCase& Case : CallCases)
			{
				Calls.Reset();
				TSharedPtr<FBangoProgram> CallProgram = MakeShared<FBangoProgram>();
				FBangoCompileError CallError;
				FString Outcome;
				int32 Got = -1;
				if (!Bango::CompileCondition(Case.Source, TEXT("call"), Commands, *CallProgram, CallError))
				{
					Outcome = CallError.ToString();
				}
				else
				{
					FBangoGlobalStore CallGlobals;
					Context.Globals = &CallGlobals;
					FBangoInstance Instance;
					FBangoRuntimeError StartError;
					if (Instance.Start(CallProgram, Commands, CallGlobals, 1, StartError))
					{
						const EBangoConditionResult Result = Instance.FinishCondition(Instance.Resume(Context), Outcome);
						Got = Result == EBangoConditionResult::True ? 1 : Result == EBangoConditionResult::False ? 0 : -1;
					}
					else
					{
						Outcome = StartError.ToString();
					}
				}
				const bool bOk = Case.Expected >= 0
					? Got == Case.Expected && FString::Join(Calls, TEXT(" | ")) == Case.ErrorOrCalls
					: Got == -1 && Outcome.Contains(Case.ErrorOrCalls);
				Expect(bOk, *FString::Printf(TEXT("call: %s (got %d: %s; calls: %s)"), Case.Name, Got, *Outcome, *FString::Join(Calls, TEXT(" | "))));
			}
			// The compiler keeps each call's site (FBangoProgram::ReflectedCalls), which the editor checks when it knows the target.
			{
				FBangoProgram SiteProgram;
				FBangoCompileError SiteError;
				const bool bCompiled = Bango::CompileSource(TEXT("entity _e = @x;\n@light call TurnOn(1);\nint[] _a = [@x call A(), 2];\nself call B();\n_e call C(1, 2);"),
					TEXT("sites"), Commands, SiteProgram, SiteError);
				FString Sites;
				for (const FBangoReflectedCallSite& Site : SiteProgram.ReflectedCalls)
				{
					Sites += FString::Printf(TEXT("%s.%s/%d%s@%d:%d "), *Site.TargetEntity, *Site.Name, Site.NumArgs, Site.bResultUsed ? TEXT("+result") : TEXT(""), Site.Line, Site.Column);
				}
				const FString WantSites = FString::Printf(TEXT("light.TurnOn/1@2:13 x.A/0+result@3:21 %s.B/0@4:11 .C/2@5:9 "), *Bango::GetSelfEntityName());
				Expect(bCompiled && Sites == WantSites, *FString::Printf(TEXT("call: sites: %s (want %s) %s"), *Sites, *WantSites, *SiteError.ToString()));

				FBangoCallableFunction TurnOn;
				TurnOn.Name = TEXT("TurnOn");
				TurnOn.ParamNames = { TEXT("Brightness") };
				TurnOn.ParamTypes = { FBangoType::Double() };
				TurnOn.Signature = TEXT("TurnOn(float Brightness)");
				const TArray<FBangoCallableFunction> Functions = { TurnOn };
				struct FCheckCase { const TCHAR* What; const TCHAR* Name; int32 NumArgs; bool bResultUsed; const TCHAR* ErrorContains; };
				const FCheckCase CheckCases[] = {
					{ TEXT("fits"), TEXT("TurnOn"), 1, false, nullptr },
					{ TEXT("no such function"), TEXT("TurnOff"), 0, false, TEXT("@light (BP_Light) has no Bango function named 'TurnOff'") },
					{ TEXT("the wrong case"), TEXT("turnOn"), 1, false, TEXT("Did you mean 'TurnOn'?") },
					{ TEXT("the wrong count"), TEXT("TurnOn"), 2, false, TEXT("takes 1 argument(s), but 2 were given: TurnOn(float Brightness)") },
					{ TEXT("no result"), TEXT("TurnOn"), 1, true, TEXT("doesn't return a value a script can use") },
				};
				for (const FCheckCase& Case : CheckCases)
				{
					FBangoReflectedCallSite Site;
					Site.Name = Case.Name;
					Site.NumArgs = Case.NumArgs;
					Site.bResultUsed = Case.bResultUsed;
					FString CheckError;
					const bool bOk = Bango::CheckReflectedCall(Site, Functions, TEXT("@light (BP_Light)"), CheckError);
					Expect(Case.ErrorContains ? !bOk && CheckError.Contains(Case.ErrorContains) : bOk, *FString::Printf(TEXT("call: check: %s: %s"), Case.What, *CheckError));
				}
			}

			Context.CallReflected = nullptr;
			{
				TSharedPtr<FBangoProgram> NoHostProgram = MakeShared<FBangoProgram>();
				FBangoCompileError NoHostError;
				FString Outcome;
				FBangoGlobalStore NoHostGlobals;
				Context.Globals = &NoHostGlobals;
				FBangoInstance Instance;
				FBangoRuntimeError StartError;
				const bool bRan = Bango::CompileCondition(TEXT("@lamp call TurnOn();\nreturn true;"), TEXT("call"), Commands, *NoHostProgram, NoHostError)
					&& Instance.Start(NoHostProgram, Commands, NoHostGlobals, 1, StartError)
					&& Instance.FinishCondition(Instance.Resume(Context), Outcome) == EBangoConditionResult::Error;
				Expect(bRan && Outcome.Contains(TEXT("Blueprint functions can't be called here")), *(FString(TEXT("call: without a host: ")) + Outcome));
			}
		}

		// A single-expression condition written as a script (the graph editor's "Write as 'return ...;'").
		{
			struct FToScriptCase { const TCHAR* Source; const TCHAR* Want; };
			const FToScriptCase ToScriptCases[] = {
				{ TEXT("true"), TEXT("return true;") },
				{ TEXT("globalVar1 && globalVar2"), TEXT("return globalVar1 && globalVar2;") },
				{ TEXT("#inputs (int _count)\n// enough?\n_count >= 3   // three\n"), TEXT("#inputs (int _count)\n// enough?\nreturn _count >= 3;   // three\n") },
				{ TEXT("return true;"), nullptr },             // already a script
				{ TEXT("Talk(\"x\");\nreturn true;"), nullptr },
			};
			for (const FToScriptCase& Case : ToScriptCases)
			{
				FString AsScript = TEXT("(untouched)");
				const bool bConverted = Bango::ConditionExpressionToScript(Case.Source, AsScript);
				Expect(Case.Want ? (bConverted && AsScript == Case.Want) : (!bConverted && AsScript == TEXT("(untouched)")),
					*FString::Printf(TEXT("condition to script: %s (got %s)"), Case.Source, bConverted ? *AsScript : TEXT("not converted")));
				// What it gives compiles as a condition, as the expression did.
				if (bConverted)
				{
					FBangoProgram ConvertedProgram;
					FBangoCompileError ConvertedError;
					Expect(Bango::CompileCondition(AsScript, TEXT("cond"), Commands, ConvertedProgram, ConvertedError) && !AsScript.Contains(TEXT("return return")),
						*FString::Printf(TEXT("condition to script compiles: %s %s"), *AsScript, *ConvertedError.ToString()));
				}
			}
		}

		// A call in the middle of a calculation can't be switched while it's under way, so the switch waits until the
		// statement it's in has finished.
		{
			FReloadRun Run;
			const TCHAR* MidV1 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 1 + Wait();    //@2(2, 0)\n\tTalk(\"{0}\", _r);    //@3(4, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\treturn 1;    //@11(2, 0)\n};");
			const TCHAR* MidV2 = TEXT("int _r;\nwhile (true)    //@1(0, 0)\n{\n\t_r = 1 + Wait();    //@2(2, 0)\n\tTalk(\"new {0}\", _r);    //@3(4, 0)\n};\n\nfunction int Wait()\n{\n\tSleep(0);    //@10(0, 0)\n\treturn 1;    //@11(2, 0)\n};");
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

		// ---- waitUntil ----
		auto Set = [](FBangoGlobalStore& Store, const TCHAR* Name, const FBangoValue& Value)
		{
			FString SetError;
			Store.Set(Name, Value, SetError);
		};
		// Waits until a global it reads changes, then checks again.
		{
			FReloadRun Run;
			Expect(StartRun(Run, Compile(TEXT("Talk(\"a\");\nwaitUntil (score > 10);\nTalk(\"b\");"))) && Log.Num() == 1, TEXT("waitUntil: pauses while false"));
			const FBangoWaitSet* WaitSet = Run.Instance.GetWaitSet();
			Expect(WaitSet && WaitSet->Globals.Num() == 1 && WaitSet->Globals[0] == TEXT("score") && WaitSet->PollReason.IsEmpty(), TEXT("waitUntil: its set is the global it reads"));
			Expect(Run.Instance.GetWaitingCommandName() == TEXT("waitUntil") && Run.Instance.GetCurrentLocation().Line == 2, TEXT("waitUntil: the debugger shows it, on its line"));
			Expect(!Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil: not due until something changes"));
			Set(Run.Globals, TEXT("ratio"), FBangoValue::MakeDouble(2.0));
			Expect(!Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil: another global changing doesn't make it due"));
			Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(7));
			Expect(!Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil: setting the same value isn't a change"));
			Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(8));
			Expect(Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil: a change makes it due"));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 1 && !Run.Instance.IsWaitDue(Run.Globals),
				TEXT("waitUntil: still false, it waits again"));
			Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(11));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("b"), TEXT("waitUntil: true, it carries on"));
		}
		// Already true: carries on without pausing.
		{
			FReloadRun Run;
			BangoMakeRuntimeTestGlobals(Run.Globals);
			Context.Globals = &Run.Globals;
			Log.Reset();
			FBangoRuntimeError StartError;
			Run.Instance.Start(Compile(TEXT("waitUntil (score == 7);\nTalk(\"b\");")), Commands, Run.Globals, 1, StartError);
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("b"), TEXT("waitUntil: already true, no pause"));
		}
		// A global that isn't set up yet (missing or nil) is false, not an error, in the condition and in functions it calls.
		{
			FReloadRun Run;
			Expect(StartRun(Run, Compile(TEXT("function bool Ready()\n{\n\treturn later > 1;\n};\nwaitUntil (missing > 3 || Ready());\nTalk(\"b\");"))),
				TEXT("waitUntil: an unset global waits"));
			Expect(Run.Instance.GetError().Message.IsEmpty(), TEXT("waitUntil: an unset global isn't an error"));
			FString DeclareError;
			Run.Globals.Declare(TEXT("missing"), FBangoType::Int(), DeclareError);
			Expect(Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil: creating the global makes it due"));
			Set(Run.Globals, TEXT("missing"), FBangoValue::MakeInt(1));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("waitUntil: unset in a function it calls, it waits (backing out of the call)"));
			Run.Globals.Declare(TEXT("later"), FBangoType::Int(), DeclareError);
			Set(Run.Globals, TEXT("later"), FBangoValue::MakeInt(2));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("b"), TEXT("waitUntil: the function's global set, it carries on"));
		}
		// A command in the condition: checked every frame.
		{
			FReloadRun Run;
			Expect(StartRun(Run, Compile(TEXT("waitUntil (!IsNull(@ghost) || score > 100);"))), TEXT("waitUntil polled: waits"));
			const FBangoWaitSet* WaitSet = Run.Instance.GetWaitSet();
			Expect(WaitSet && WaitSet->PollReason.Contains(TEXT("IsNull")) && Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil polled: always due"));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil polled: still due after a check"));
		}
		// The condition can't wait.
		{
			FReloadRun Run;
			StartRun(Run, Compile(TEXT("waitUntil (AskChoice(\"a\", \"b\") == 1);")));
			Expect(Run.Instance.GetStatus() == EBangoInstanceStatus::Failed && Run.Instance.GetError().Message.Contains(TEXT("must answer at once, but 'AskChoice' waits")),
				TEXT("waitUntil: a command that waits, in its condition, is an error"));
		}
		// Changing an array global in place (Push) is a change.
		{
			FReloadRun Run;
			FString DeclareError;
			Run.Globals.Declare(TEXT("found"), FBangoType::ArrayOf(EBangoBaseType::Int), DeclareError);
			Set(Run.Globals, TEXT("found"), FBangoValue::MakeArray(FBangoType::Int(), TArray<FBangoValue>()));
			Expect(StartRun(Run, Compile(TEXT("global int[] found;\nwaitUntil (Count(found) >= 2);\nTalk(\"b\");"))), TEXT("waitUntil in place: waits"));
			FBangoInstance Pusher;
			FBangoRuntimeError PushError;
			Pusher.Start(Compile(TEXT("global int[] found;\nfound Push(1);")), Commands, Run.Globals, 2, PushError);
			Pusher.Resume(Context);
			Expect(Run.Instance.IsWaitDue(Run.Globals) && Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting, TEXT("waitUntil in place: one Push makes it due, still false"));
			Pusher.Start(Compile(TEXT("global int[] found;\nfound Push(2);")), Commands, Run.Globals, 3, PushError);
			Pusher.Resume(Context);
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished, TEXT("waitUntil in place: the second Push, it carries on"));
		}
		// Saved and loaded while waiting: checked again straight away.
		{
			FReloadRun Run;
			TSharedPtr<FBangoProgram> WaitProgram = Compile(TEXT("waitUntil (score > 10);\nTalk(\"b\");"));
			StartRun(Run, WaitProgram);
			TArray<uint8> Bytes;
			{
				FMemoryWriter Writer(Bytes);
				Run.Globals.Serialize(Writer);
				Run.Instance.Serialize(Writer);
			}
			FBangoGlobalStore LoadedGlobals;
			FBangoInstance Loaded;
			{
				FMemoryReader Reader(Bytes);
				LoadedGlobals.Serialize(Reader);
				Loaded.Serialize(Reader);
			}
			FString BindError;
			Expect(Loaded.Bind(WaitProgram, Commands, BindError) && Loaded.GetWaitSet() && Loaded.IsWaitDue(LoadedGlobals), TEXT("waitUntil save/load: due after loading"));
			Context.Globals = &LoadedGlobals;
			Expect(Loaded.Resume(Context) == EBangoInstanceStatus::Waiting && !Loaded.IsWaitDue(LoadedGlobals), TEXT("waitUntil save/load: still false, waits"));
			Set(LoadedGlobals, TEXT("score"), FBangoValue::MakeInt(20));
			Expect(Loaded.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("b"), TEXT("waitUntil save/load: carries on"));
		}
		// Hot reload while waiting: it switches there, and checks the new condition.
		{
			FReloadRun Run;
			Expect(StartRun(Run, Compile(TEXT("waitUntil (score > 100);    //@1(0, 0)\nTalk(\"v1\");    //@2(0, 2)"))), TEXT("waitUntil reload: waits"));
			FString ReloadError;
			Expect(Run.Instance.RequestReload(Compile(TEXT("waitUntil (score > 5);    //@1(0, 0)\nTalk(\"v2\");    //@2(0, 2)")), Commands, ReloadError)
				&& Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil reload: a pending reload makes it due"));
			Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("v2"), TEXT("waitUntil reload: the new condition is true"));
			FString ReloadMessage;
			Expect(Run.Instance.TakeReloadResult(ReloadMessage) == EBangoReloadResult::Reloaded, *(FString(TEXT("waitUntil reload: it switched: ")) + ReloadMessage));
		}
		// ---- waitUntil with a timeout ----
		{
			const float DeltaBefore = Context.DeltaSeconds;
			const TCHAR* Timed = TEXT("global int score;\nwaitUntil (score > 10) timeout (2)\n{\n\tTalk(\"late\");\n}\nTalk(\"after\");");
			// The time runs out while the global it reads isn't set: its block runs, then what follows.
			{
				FReloadRun Run;
				Expect(StartRun(Run, Compile(Timed)) && Log.Num() == 0 && Run.Instance.GetWaitingCommandName() == TEXT("waitUntil"), TEXT("waitUntil timeout: waits"));
				Expect(Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil timeout: resumed every frame, to count its time"));
				Context.DeltaSeconds = 1.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 0, TEXT("waitUntil timeout: one second in, still waiting"));
				Context.DeltaSeconds = 1.5f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 2 && Log[0] == TEXT("late") && Log[1] == TEXT("after"),
					TEXT("waitUntil timeout: time up (the global unset), its block runs, then what follows"));
			}
			// The same with the global set, but the condition false.
			{
				FReloadRun Run;
				StartRun(Run, Compile(Timed));
				Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(5));
				Context.DeltaSeconds = 0.5f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 0, TEXT("waitUntil timeout: a change, still false, it waits on"));
				Context.DeltaSeconds = 1.5f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 2 && Log[0] == TEXT("late") && Log[1] == TEXT("after"),
					TEXT("waitUntil timeout: time up (the condition false), its block runs, then what follows"));
			}
			// True in time: the block is skipped. True in the very frame the time runs out counts as in time.
			{
				FReloadRun Run;
				StartRun(Run, Compile(Timed));
				Context.DeltaSeconds = 1.0f;
				Run.Instance.Resume(Context);
				Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(20));
				Context.DeltaSeconds = 5.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 1 && Log[0] == TEXT("after"), TEXT("waitUntil timeout: true in time, its block is skipped"));
			}
			// Already true: no wait, no block. A timeout of 0 with it false: straight to the block.
			{
				FReloadRun Run;
				Log.Reset();
				BangoMakeRuntimeTestGlobals(Run.Globals);
				Context.Globals = &Run.Globals;
				Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(20));
				FBangoRuntimeError StartError;
				Run.Instance.Start(Compile(Timed), Commands, Run.Globals, 1, StartError);
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 1 && Log[0] == TEXT("after"), TEXT("waitUntil timeout: already true, no wait"));

				FReloadRun Zero;
				Log.Reset();
				BangoMakeRuntimeTestGlobals(Zero.Globals);
				Context.Globals = &Zero.Globals;
				Zero.Instance.Start(Compile(TEXT("global int score;\nwaitUntil (score > 10) timeout (0)\n{\n\tTalk(\"late\");\n}\nTalk(\"after\");")), Commands, Zero.Globals, 1, StartError);
				Expect(Zero.Instance.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 2 && Log[0] == TEXT("late"), TEXT("waitUntil timeout: 0 seconds and false, straight to its block"));
			}
			// Only as long as it's waiting there: a waitUntil without one, after it, waits as long as it takes.
			{
				FReloadRun Run;
				StartRun(Run, Compile(TEXT("global int score;\nint _t = 1;\nwaitUntil (score > 10) timeout (_t * 2)\n{\n}\nwaitUntil (score > 10);\nTalk(\"after\");")));
				Context.DeltaSeconds = 3.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && !Run.Instance.IsWaitDue(Run.Globals), TEXT("waitUntil timeout: the next waitUntil has none"));
				Context.DeltaSeconds = 100.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 0, TEXT("waitUntil timeout: and waits as long as it takes"));
				Set(Run.Globals, TEXT("score"), FBangoValue::MakeInt(20));
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("after"), TEXT("waitUntil timeout: until it's true"));
			}
			// In a loop, the time starts again each time round. The block can leave the loop.
			{
				FReloadRun Run;
				StartRun(Run, Compile(TEXT("global int score;\nint _n = 0;\nwhile (true)\n{\n\twaitUntil (score > 10) timeout (1)\n\t{\n\t\t_n++;\n\t\tif (_n == 2)\n\t\t{\n\t\t\tbreak;\n\t\t}\n\t\tTalk(\"again\");\n\t}\n}\nTalk(\"out\");")));
				Context.DeltaSeconds = 1.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 1 && Log[0] == TEXT("again"), TEXT("waitUntil timeout in a loop: timed out once, waiting again"));
				Context.DeltaSeconds = 0.5f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 1, TEXT("waitUntil timeout in a loop: its time started again"));
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("out"), TEXT("waitUntil timeout in a loop: the block's break leaves the loop"));
			}
			// A negative or nil timeout stops the script.
			{
				FReloadRun Run;
				StartRun(Run, Compile(TEXT("global bool go;\nfloat _t = -1;\nwaitUntil (go) timeout (_t)\n{\n}")));
				Expect(Run.Instance.GetStatus() == EBangoInstanceStatus::Failed && Run.Instance.GetError().Message.Contains(TEXT("A timeout can't be negative")), TEXT("waitUntil timeout: negative is an error"));
				FReloadRun Nil;
				StartRun(Nil, Compile(TEXT("global bool go;\nfloat _t;\nwaitUntil (go) timeout (_t)\n{\n}")));
				Expect(Nil.Instance.GetStatus() == EBangoInstanceStatus::Failed && Nil.Instance.GetError().Message.Contains(TEXT("'_t' is nil")), TEXT("waitUntil timeout: nil is an error"));
			}
			// Saved and loaded while waiting: the time it has left is kept.
			{
				FReloadRun Run;
				TSharedPtr<FBangoProgram> TimedProgram = Compile(Timed);
				StartRun(Run, TimedProgram);
				Context.DeltaSeconds = 1.5f;
				Run.Instance.Resume(Context);
				TArray<uint8> Bytes;
				{
					FMemoryWriter Writer(Bytes);
					Run.Globals.Serialize(Writer);
					Run.Instance.Serialize(Writer);
				}
				FBangoGlobalStore LoadedGlobals;
				FBangoInstance Loaded;
				{
					FMemoryReader Reader(Bytes);
					LoadedGlobals.Serialize(Reader);
					Loaded.Serialize(Reader);
				}
				FString BindError;
				Expect(Loaded.Bind(TimedProgram, Commands, BindError) && Loaded.GetWaitSet() && Loaded.IsWaitDue(LoadedGlobals), TEXT("waitUntil timeout save/load: waiting after loading"));
				Context.Globals = &LoadedGlobals;
				Log.Reset();
				Context.DeltaSeconds = 0.25f;
				Expect(Loaded.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 0, TEXT("waitUntil timeout save/load: not up yet (0.25 s left)"));
				Context.DeltaSeconds = 0.5f;
				Expect(Loaded.Resume(Context) == EBangoInstanceStatus::Finished && Log.Num() == 2 && Log[0] == TEXT("late"), TEXT("waitUntil timeout save/load: its time left was kept"));
			}
			// Hot reload while waiting there: the time it has left is kept, and the new version's block runs.
			{
				FReloadRun Run;
				StartRun(Run, Compile(TEXT("global int score;\nwaitUntil (score > 10) timeout (2)    //@1(0, 0)\n{\n\tTalk(\"v1\");    //@2(0, 2)\n}")));
				FString ReloadError;
				Run.Instance.RequestReload(Compile(TEXT("global int score;\nwaitUntil (score > 50) timeout (99)    //@1(0, 0)\n{\n\tTalk(\"v2\");    //@2(0, 2)\n}")), Commands, ReloadError);
				Context.DeltaSeconds = 1.0f;
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Waiting && Log.Num() == 0, TEXT("waitUntil timeout reload: switched, still waiting"));
				FString ReloadMessage;
				Expect(Run.Instance.TakeReloadResult(ReloadMessage) == EBangoReloadResult::Reloaded, *(FString(TEXT("waitUntil timeout reload: it switched: ")) + ReloadMessage));
				Expect(Run.Instance.Resume(Context) == EBangoInstanceStatus::Finished && LastLog() == TEXT("v2"), TEXT("waitUntil timeout reload: the time left was kept, the new block runs"));
			}
			Context.DeltaSeconds = DeltaBefore;
		}
		// Not in a condition.
		{
			FBangoProgram ConditionProgram;
			FBangoCompileError ConditionError;
			Expect(!Bango::CompileCondition(TEXT("waitUntil (true);\nreturn true;"), TEXT("c"), Commands, ConditionProgram, ConditionError)
				&& ConditionError.Message.Contains(TEXT("can't use waitUntil")), TEXT("waitUntil: not in a condition"));
		}
		// What a condition depends on: globals (in its functions too), the inputs it reads, and commands.
		{
			FBangoProgram ConditionProgram;
			FBangoCompileError ConditionError;
			const bool bCompiled = Bango::CompileCondition(TEXT("#inputs (entity _activator, entity[] _actual, entity[] _potential)\nglobal int stage;\nfunction bool Far()\n{\n\treturn range > 2;\n};\nreturn Count(_actual) < 3 && stage == 2 && Far();"),
				TEXT("c"), Commands, ConditionProgram, ConditionError);
			const FBangoDependencies& Deps = ConditionProgram.ConditionDependencies;
			Expect(bCompiled && Deps.Globals == TArray<FString>({ TEXT("range"), TEXT("stage") }) && Deps.Inputs == TArray<FString>({ TEXT("_actual") }) && Deps.PollReason.IsEmpty(),
				*(FString(TEXT("condition dependencies: globals and inputs ")) + ConditionError.ToString()));
			FBangoProgram Polled;
			Expect(Bango::CompileCondition(TEXT("#inputs (entity _activator)\n!IsNull(_activator) && _activator != @ghost"), TEXT("c"), Commands, Polled, ConditionError)
				&& Polled.ConditionDependencies.Inputs == TArray<FString>({ TEXT("_activator") }) && Polled.ConditionDependencies.PollReason == TEXT("it calls IsNull"),
				TEXT("condition dependencies: a command makes it polled"));
			FBangoProgram Script;
			Expect(Bango::CompileSource(TEXT("Talk(\"a\");"), TEXT("s"), Commands, Script, ConditionError) && Script.ConditionDependencies.PollReason.IsEmpty() && Script.ConditionDependencies.Inputs.IsEmpty(),
				TEXT("condition dependencies: not worked out for a script"));
		}

		// ---- Watching conditions (FBangoWatchRegistry) ----
		{
			FBangoGlobalStore WatchGlobals;
			BangoMakeRuntimeTestGlobals(WatchGlobals);
			Context.Globals = &WatchGlobals;
			TArray<FString> Reported;
			FBangoWatchRegistry Registry(WatchGlobals,
				[&](const TSharedPtr<const FBangoProgram>& CheckProgram, const TArray<FBangoValue>& CheckArgs, FString& OutError)
				{
					FBangoInstance Check;
					FBangoRuntimeError CheckError;
					if (!Check.Start(CheckProgram, Commands, WatchGlobals, 99, CheckError, CheckArgs))
					{
						OutError = CheckError.ToString();
						return EBangoConditionResult::Error;
					}
					return Check.FinishCondition(Check.Resume(Context), OutError);
				},
				[&Reported](const FString& GroupName, const FString& WatchError) { Reported.Add(GroupName + TEXT(": ") + WatchError); });
			auto CompileWatched = [&](const TCHAR* WatchedSource) -> TSharedPtr<const FBangoProgram>
			{
				TSharedPtr<FBangoProgram> WatchedProgram = MakeShared<FBangoProgram>();
				FBangoCompileError WatchedError;
				if (!Bango::CompileCondition(WatchedSource, TEXT("watched"), Commands, *WatchedProgram, WatchedError))
				{
					Expect(false, *(FString(TEXT("watch: condition compiles: ")) + WatchedError.ToString()));
					return TSharedPtr<const FBangoProgram>();
				}
				return WatchedProgram;
			};
			auto Entity = [](const TCHAR* Name) { return FBangoValue::MakeEntity(Name); };
			auto MakeEntities = [](std::initializer_list<const TCHAR*> Names)
			{
				TArray<FBangoValue> Elements;
				for (const TCHAR* Name : Names)
				{
					Elements.Add(FBangoValue::MakeEntity(Name));
				}
				return FBangoValue::MakeArray(FBangoType::Entity(), Elements);
			};
			struct FCall
			{
				int64 Group = 0;
				TArray<FBangoWatchChange> Changes;
			};
			TArray<FCall> Calls;
			auto Recorder = [&Calls](FBangoWatchGroupHandle* Group)
			{
				return [&Calls, Group](TConstArrayView<FBangoWatchChange> Changes)
				{
					FCall& Call = Calls.AddDefaulted_GetRef();
					Call.Group = Group->Id;
					for (const FBangoWatchChange& Change : Changes)
					{
						Call.Changes.Add(Change);
					}
				};
			};
			const TCHAR* Inputs = TEXT("#inputs (entity _activator, entity[] _actual)\n");
			double Now = 0.0;

			// Globals: checked again only when one it reads changes; only changes reported, together.
			FBangoWatchGroupHandle Scores;
			Scores = Registry.CreateGroup(CompileWatched(*(FString(Inputs) + TEXT("score > 10 && _activator != @ghost"))), 0.5f, TEXT("Scores"), Recorder(&Scores));
			FBangoWatchHandle A, Ghost;
			Expect(!Registry.StartWatch(Scores, { { TEXT("_activator"), Entity(TEXT("a")) } }, A) && A.IsValid(), TEXT("watch: the first answer at once (false)"));
			Registry.StartWatch(Scores, { { TEXT("_activator"), Entity(TEXT("ghost")) } }, Ghost);
			int64 Checks = Registry.GetNumChecks();
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks && Calls.Num() == 0, TEXT("watch: nothing changed, nothing checked"));
			Set(WatchGlobals, TEXT("ratio"), FBangoValue::MakeDouble(3.0));
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks, TEXT("watch: a global it doesn't read changing isn't checked"));
			Set(WatchGlobals, TEXT("score"), FBangoValue::MakeInt(11));
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks + 2 && Calls.Num() == 1 && Calls[0].Group == Scores.Id && Calls[0].Changes.Num() == 1
				&& Calls[0].Changes[0].Watch == A && Calls[0].Changes[0].bSatisfied, TEXT("watch: a global it reads: both checked, the one that changed reported"));
			Checks = Registry.GetNumChecks();
			FString InputError;
			Expect(Registry.SetGroupInputs(Scores, { { TEXT("_actual"), MakeEntities({ TEXT("a") }) } }, InputError), TEXT("watch: group inputs set"));
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks, TEXT("watch: an input it doesn't read changing isn't checked"));
			Expect(!Registry.SetGroupInputs(Scores, { { TEXT("_nope"), Entity(TEXT("a")) } }, InputError) && InputError.Contains(TEXT("no input '_nope'")), TEXT("watch: an unknown group input"));
			FBangoWatchHandle Bad;
			Expect(!Registry.StartWatch(Scores, { { TEXT("_who"), Entity(TEXT("a")) } }, Bad) && !Bad.IsValid() && Reported.Num() == 1 && Reported[0].StartsWith(TEXT("Scores: "), ESearchCase::CaseSensitive),
				TEXT("watch: an unknown watch input is reported"));
			Reported.Reset();
			Calls.Reset();

			// Inputs: checked again when one it reads is set to a different value.
			FBangoWatchGroupHandle Crowd;
			Crowd = Registry.CreateGroup(CompileWatched(*(FString(Inputs) + TEXT("Count(_actual) < 2"))), 0.5f, TEXT("Crowd"), Recorder(&Crowd));
			FBangoWatchHandle C;
			Registry.SetGroupInputs(Crowd, { { TEXT("_actual"), MakeEntities({}) } }, InputError);
			Expect(Registry.StartWatch(Crowd, { { TEXT("_activator"), Entity(TEXT("c")) } }, C), TEXT("watch: an input-only condition, true at first"));
			Checks = Registry.GetNumChecks();
			Registry.SetGroupInputs(Crowd, { { TEXT("_actual"), MakeEntities({ TEXT("a"), TEXT("b") }) } }, InputError);
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks + 1 && Calls.Num() == 1 && Calls[0].Group == Crowd.Id && !Calls[0].Changes[0].bSatisfied,
				TEXT("watch: an input it reads changing: checked, unsatisfied"));
			Checks = Registry.GetNumChecks();
			Registry.SetGroupInputs(Crowd, { { TEXT("_actual"), MakeEntities({ TEXT("a"), TEXT("b") }) } }, InputError);
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks, TEXT("watch: the same input values again: not checked"));
			Calls.Reset();

			// Polled: a command in it. Checked every Poll Interval, spread out, whatever changes.
			FBangoWatchGroupHandle Polled;
			Polled = Registry.CreateGroup(CompileWatched(TEXT("#inputs (entity _activator)\n!IsNull(_activator)")), 1.0f, TEXT("Polled"), Recorder(&Polled));
			FBangoWatchHandle P;
			Expect(Registry.StartWatch(Polled, { { TEXT("_activator"), Entity(TEXT("john")) } }, P), TEXT("watch polled: true at first"));
			Checks = Registry.GetNumChecks();
			for (int32 Frame = 0; Frame < 12; ++Frame)
			{
				Registry.Update(Now += 0.25); // 3 seconds
			}
			const int64 Polls = Registry.GetNumChecks() - Checks;
			Expect(Polls >= 3 && Polls <= 4 && Calls.Num() == 0, *FString::Printf(TEXT("watch polled: once a second (%d checks in 3 s)"), (int32)Polls));
			FBangoWatchGroupHandle EveryFrame = Registry.CreateGroup(CompileWatched(TEXT("#inputs (entity _activator)\n!IsNull(_activator)")), 0.0f, TEXT("Every frame"), nullptr);
			FBangoWatchHandle E;
			Registry.StartWatch(EveryFrame, { { TEXT("_activator"), Entity(TEXT("john")) } }, E);
			Registry.StopWatch(P);
			Checks = Registry.GetNumChecks();
			Registry.Update(Now += 0.01);
			Registry.Update(Now += 0.01);
			Expect(Registry.GetNumChecks() == Checks + 2, TEXT("watch polled: interval 0, every Update"));
			Registry.DestroyGroup(EveryFrame);
			Registry.DestroyGroup(Polled);

			// Errors: unsatisfied, shown once until it answers true or false again. Unset is unsatisfied, quietly.
			FBangoWatchGroupHandle Broken;
			Broken = Registry.CreateGroup(CompileWatched(TEXT("#inputs (entity _activator)\nscore / zero > 1 || IsNil(later) == false && later > 0")), 0.5f, TEXT("Broken"), Recorder(&Broken));
			FBangoWatchHandle Br;
			FString DeclareError;
			WatchGlobals.Declare(TEXT("zero"), FBangoType::Int(), DeclareError);
			Set(WatchGlobals, TEXT("zero"), FBangoValue::MakeInt(0));
			Expect(!Registry.StartWatch(Broken, { { TEXT("_activator"), Entity(TEXT("a")) } }, Br) && Reported.Num() == 1 && Reported[0].Contains(TEXT("Division by zero")),
				*(FString(TEXT("watch error: reported at the start: ")) + (Reported.Num() > 0 ? Reported[0] : FString())));
			Set(WatchGlobals, TEXT("score"), FBangoValue::MakeInt(12));
			Registry.Update(Now += 0.1);
			Expect(Reported.Num() == 1, TEXT("watch error: not shown again"));
			Set(WatchGlobals, TEXT("zero"), FBangoValue::MakeInt(100));
			Registry.Update(Now += 0.1);
			Set(WatchGlobals, TEXT("zero"), FBangoValue::MakeInt(0));
			Registry.Update(Now += 0.1);
			Expect(Reported.Num() == 2, TEXT("watch error: shown again after a false in between"));
			Registry.DestroyGroup(Broken);
			Reported.Reset();
			Calls.Reset();

			// Callbacks can stop watches, start them and destroy groups, their own too.
			Registry.DestroyGroup(Scores);
			Registry.DestroyGroup(Crowd);
			FBangoWatchGroupHandle First, Second;
			FBangoWatchHandle F1, S1;
			First = Registry.CreateGroup(CompileWatched(*(FString(Inputs) + TEXT("score > 20"))), 0.5f, TEXT("First"),
				[&](TConstArrayView<FBangoWatchChange> Changes)
				{
					Calls.AddDefaulted_GetRef().Group = First.Id;
					Registry.StopWatch(S1);
					Registry.DestroyGroup(First);
				});
			Second = Registry.CreateGroup(CompileWatched(*(FString(Inputs) + TEXT("score > 20"))), 0.5f, TEXT("Second"), Recorder(&Second));
			Registry.StartWatch(First, { { TEXT("_activator"), Entity(TEXT("a")) } }, F1);
			Registry.StartWatch(Second, { { TEXT("_activator"), Entity(TEXT("a")) } }, S1);
			FBangoWatchHandle S2;
			Registry.StartWatch(Second, { { TEXT("_activator"), Entity(TEXT("b")) } }, S2);
			const int32 GroupsBefore = Registry.GetNumGroups();
			const int64 FirstId = First.Id;
			Set(WatchGlobals, TEXT("score"), FBangoValue::MakeInt(25));
			Registry.Update(Now += 0.1);
			Expect(Calls.Num() == 2 && Calls[0].Group == FirstId && Calls[1].Group == Second.Id && Calls[1].Changes.Num() == 1 && Calls[1].Changes[0].Watch == S2
				&& Registry.GetNumGroups() == GroupsBefore - 1 && !Registry.IsWatching(F1), TEXT("watch callbacks: a watch stopped by an earlier callback isn't reported; a group can destroy itself"));
			Checks = Registry.GetNumChecks();
			Set(WatchGlobals, TEXT("score"), FBangoValue::MakeInt(5));
			Registry.Update(Now += 0.1);
			Expect(Registry.GetNumChecks() == Checks + 1, TEXT("watch callbacks: only what's left is checked"));
			FBangoWatchGroupHandle Gone;
			Registry.DestroyGroup(Gone);
			FBangoWatchHandle GoneWatch;
			Registry.StopWatch(GoneWatch);
			Expect(!Registry.StartWatch(First, { { TEXT("_activator"), Entity(TEXT("a")) } }, GoneWatch) && !GoneWatch.IsValid(), TEXT("watch: a destroyed group can't start watches"));
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
	const TCHAR* Greet = TEXT("#inputs (entity _who, string _line, int _times = 2, entity _listener = nil)\n")
		TEXT("for (int _i = 0; _i < _times; _i++)\n{\n\t_who Speak(_line);\n};\nTalk(\"{0}\", !IsNil(_listener));");

	const FArgCase Cases[] =
	{
		{ TEXT("all arguments"), Greet, { John, FBangoValue::MakeString(TEXT("hi")), FBangoValue::MakeInt(1), FBangoValue::MakeEntity(TEXT("player")) }, TEXT("john: hi|true"), nullptr },
		{ TEXT("defaults for trailing arguments"), Greet, { John, FBangoValue::MakeString(TEXT("hi")) }, TEXT("john: hi|john: hi|false"), nullptr },
		{ TEXT("int argument for a float parameter"), TEXT("#inputs (float _d)\nTalk(\"{0}\", _d);"), { FBangoValue::MakeInt(3) }, TEXT("3.0"), nullptr },
		{ TEXT("explicit nil overrides a default"), TEXT("#inputs (int _n = 5)\nTalk(\"{0}\", !IsNil(_n));"), { FBangoValue::MakeNil(FBangoType::Int()) }, TEXT("false"), nullptr },
		{ TEXT("nil of any type fits any parameter"), TEXT("#inputs (int _n)\nTalk(\"{0}\", !IsNil(_n));"), { FBangoValue::MakeNil(FBangoType::String()) }, TEXT("false"), nullptr },
		{ TEXT("array argument"), TEXT("#inputs (int[] _list)\n_list Push(3);\nTalk(\"{0}\", _list);"), { FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }) }, TEXT("[1, 2, 3]"), nullptr },
		{ TEXT("array default is fresh each spawn"), TEXT("#inputs (int[] _a = [1, 2])\n_a Push(3);\nTalk(\"{0}\", _a);"), {}, TEXT("[1, 2, 3]"), nullptr },
		{ TEXT("no inputs, no arguments"), TEXT("Talk(\"ok\");"), {}, TEXT("ok"), nullptr },
		{ TEXT("empty inputs line"), TEXT("#inputs ()\nTalk(\"ok\");"), {}, TEXT("ok"), nullptr },

		{ TEXT("too few"), TEXT("#inputs (int _a, int _b)\nTalk(\"x\");"), { FBangoValue::MakeInt(1) }, nullptr, TEXT("Missing argument 2 ('_b', an int). This script needs at least 2 argument(s), but 1 were passed.") },
		{ TEXT("none for required"), Greet, {}, nullptr, TEXT("Missing argument 1 ('_who', an entity)") },
		{ TEXT("extra values are left unused"), TEXT("#inputs (int _a)\nTalk(\"{0}\", _a);"), { FBangoValue::MakeInt(1), FBangoValue::MakeString(TEXT("unused")) }, TEXT("1"), nullptr },
		{ TEXT("a script without inputs leaves them all unused"), TEXT("Talk(\"x\");"), { FBangoValue::MakeInt(1) }, TEXT("x"), nullptr },
		{ TEXT("extra values aren't checked"), TEXT("#inputs (int _a = 4)\nTalk(\"{0}\", _a);"), { FBangoValue::MakeInt(2), FBangoValue::MakeEntity(TEXT("nobody")), FBangoValue::MakeNil(FBangoType::Bool()) }, TEXT("2"), nullptr },
		{ TEXT("wrong type"), TEXT("#inputs (int _a, string _b)\nTalk(\"x\");"), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }, nullptr, TEXT("Argument 2 ('_b') must be a string, but it is an int.") },
		{ TEXT("float for an int"), TEXT("#inputs (int _a)\nTalk(\"x\");"), { FBangoValue::MakeDouble(1.5) }, nullptr, TEXT("Argument 1 ('_a') must be an int, but it is a float.") },
		{ TEXT("argument error points at the inputs line"), Greet, { FBangoValue::MakeString(TEXT("oops")), FBangoValue::MakeString(TEXT("hi")) }, nullptr, TEXT("test.bango(1:1) [node 0]: Argument 1 ('_who') must be an entity") },
		{ TEXT("reading a nil parameter"), TEXT("#inputs (string _line = nil)\nTalk(_line);"), {}, nullptr, TEXT("'_line' is nil. It's a parameter of this script; check what the caller passed.") },
		{ TEXT("reading a nil function parameter"), TEXT("function void F(string _s)\n{\n\tTalk(_s);\n};\nF(nil);"), {}, nullptr, TEXT("It's a parameter of function 'F'") },
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
