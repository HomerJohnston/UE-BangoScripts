#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoBlueprintLibrary.h"
#include "BangoBuiltinCommands.h"
#include "BangoCompiler.h"
#include "BangoInstance.h"
#include "BangoScriptCache.h"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

namespace BangoGameTestsPrivate
{
	/** Runs Source against Commands with no game world. Returns the final status; fills OutError on failure. */
	static EBangoInstanceStatus RunWithoutWorld(const TCHAR* Source, const FBangoCommandTable& Commands, FBangoGlobalStore& Globals, FString& OutError)
	{
		TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
		FBangoCompileError CompileError;
		if (!Bango::CompileSource(Source, TEXT("test.bango"), Commands, *Program, CompileError))
		{
			OutError = CompileError.ToString();
			return EBangoInstanceStatus::None;
		}
		FBangoInstance Instance;
		FBangoRuntimeError StartError;
		if (!Instance.Start(Program, Commands, Globals, 1, StartError))
		{
			OutError = StartError.ToString();
			return Instance.GetStatus();
		}
		FBangoExecContext Context;
		Context.Globals = &Globals;
		Context.DeltaSeconds = 0.25f;
		for (int32 i = 0; i < 100 && Instance.IsRunning(); ++i)
		{
			Instance.Resume(Context);
		}
		if (Instance.GetStatus() == EBangoInstanceStatus::Failed)
		{
			OutError = Instance.GetError().ToString();
		}
		return Instance.GetStatus();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoScriptPathTest, "Bango.Game.ScriptPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoScriptPathTest::RunTest(const FString& Parameters)
{
	FString Path;
	FString Error;
	TestTrue(TEXT("plain"), Bango::NormalizeScriptPath(TEXT("Quest/Intro"), Path, Error) && Path == TEXT("Quest/Intro.bango"));
	TestTrue(TEXT("with extension"), Bango::NormalizeScriptPath(TEXT("Quest/Intro.bango"), Path, Error) && Path == TEXT("Quest/Intro.bango"));
	TestTrue(TEXT("backslashes and spaces"), Bango::NormalizeScriptPath(TEXT("  Quest\\Sub\\Intro "), Path, Error) && Path == TEXT("Quest/Sub/Intro.bango"));
	TestTrue(TEXT("doubled slashes"), Bango::NormalizeScriptPath(TEXT("Quest//Intro"), Path, Error) && Path == TEXT("Quest/Intro.bango"));
	TestFalse(TEXT("empty"), Bango::NormalizeScriptPath(TEXT(""), Path, Error));
	TestFalse(TEXT("absolute"), Bango::NormalizeScriptPath(TEXT("/Quest/Intro"), Path, Error));
	TestFalse(TEXT("drive letter"), Bango::NormalizeScriptPath(TEXT("C:/Quest/Intro"), Path, Error));
	TestFalse(TEXT("parent folder"), Bango::NormalizeScriptPath(TEXT("Quest/../../Secrets"), Path, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBuiltinCommandsTest, "Bango.Game.BuiltinCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBuiltinCommandsTest::RunTest(const FString& Parameters)
{
	using namespace BangoGameTestsPrivate;
	FBangoCommandTable Commands;
	Bango::RegisterBuiltinCommands(Commands);
	for (const TCHAR* Name : { TEXT("Hint"), TEXT("Log"), TEXT("Format"), TEXT("FormatText"), TEXT("ToText"), TEXT("ToString"), TEXT("Sleep"), TEXT("Random"), TEXT("RandomInt"), TEXT("Floor"), TEXT("Round"),
		TEXT("GameTime"), TEXT("ToEntity"), TEXT("SetPos"), TEXT("GetPos"), TEXT("SetHidden"), TEXT("DistanceTo") })
	{
		TestNotNull(FString::Printf(TEXT("registered: %s"), Name), Commands.FindCommandImpl(Name));
	}

	FBangoGlobalStore Globals;
	FString Error;
	const TCHAR* Source =
		TEXT("#define LOCTEXT_NAMESPACE \"Bango.Tests\"\n")
		TEXT("global float r = Random(1.0, 2.0);\n")
		TEXT("global int ri = RandomInt(3, 3);\n")
		TEXT("global int fl = Floor(-1.5);\n")
		TEXT("global int ro = Round(2.5);\n")
		TEXT("global int rn = Round(-2.5);\n")
		TEXT("global string fm = Format(\"%0-%1-%2\", 1, \"a\", [1.5]);\n")
		TEXT("global string en = Format(\"%0 %1\", ToEntity(\"door17\"), [@john, ToEntity(\"_x\")]);\n")
		TEXT("global bool same = ToEntity(\"john\") == @john;\n")
		TEXT("Sleep(0.5);\n")
		TEXT("Sleep(0);\n")
		TEXT("Log(\"logged %0\", 1);\n")
		TEXT("Hint(LOCTEXT(\"NoWorld\", \"a hint with no world goes to the log, {0}\"), 1);\n")
		TEXT("global text tf = FormatText(LOCTEXT(\"Count\", \"{1}: {0}\"), 2, ToText(\"x\"));\n")
		TEXT("global string ts = ToString(NSLOCTEXT(\"Bango.Other\", \"Hello\", \"Hello\"));\n")
		TEXT("global bool teq = LOCTEXT(\"Hello\", \"Hello\") == ToText(\"Hello\");\n")
		TEXT("global bool done = true;\n")
		TEXT("#undef LOCTEXT_NAMESPACE");
	const EBangoInstanceStatus Status = RunWithoutWorld(Source, Commands, Globals, Error);
	if (!TestEqual(TEXT("built-ins script finishes"), (int32)Status, (int32)EBangoInstanceStatus::Finished))
	{
		AddError(Error);
		return false;
	}
	const double R = Globals.Find(TEXT("r"))->GetDouble();
	TestTrue(TEXT("Random in range"), R >= 1.0 && R <= 2.0);
	TestEqual(TEXT("RandomInt"), Globals.Find(TEXT("ri"))->GetInt(), 3);
	TestEqual(TEXT("Floor"), Globals.Find(TEXT("fl"))->GetInt(), -2);
	TestEqual(TEXT("Round"), Globals.Find(TEXT("ro"))->GetInt(), 3);
	TestEqual(TEXT("Round negative"), Globals.Find(TEXT("rn"))->GetInt(), -3);
	TestEqual(TEXT("Format"), Globals.Find(TEXT("fm"))->GetString(), FString(TEXT("1-a-[1.5]")));
	TestEqual(TEXT("ToEntity and entities in Format"), Globals.Find(TEXT("en"))->GetString(), FString(TEXT("door17 [john, _x]")));
	TestTrue(TEXT("ToEntity equals the @ literal"), Globals.Find(TEXT("same"))->GetBool());
	TestEqual(TEXT("FormatText, with values moved around"), Globals.Find(TEXT("tf"))->GetText().ToString(), FString(TEXT("x: 2")));
	TestEqual(TEXT("ToString of a text"), Globals.Find(TEXT("ts"))->GetString(), FString(TEXT("Hello")));
	TestTrue(TEXT("texts are equal if they show the same thing"), Globals.Find(TEXT("teq"))->GetBool());
	TestTrue(TEXT("ran to the end"), Globals.IsDefined(TEXT("done")));

	struct FErrorCase { const TCHAR* Source; const TCHAR* Contains; };
	const FErrorCase ErrorCases[] =
	{
		{ TEXT("int _x = RandomInt(5, 1);"), TEXT("Min (5) is greater than Max (1)") },
		{ TEXT("float _x = Random(2.0, 1.0);"), TEXT("Min (2.0) is greater than Max (1.0)") },
		{ TEXT("Sleep(-1.0);"), TEXT("negative time") },
		{ TEXT("float _t = GameTime();"), TEXT("needs a game world") },
		{ TEXT("int _x = Floor(1e300);"), TEXT("too large to convert") },
		{ TEXT("string _text = \"%2\";\nLog(_text, 1);"), TEXT("uses %2") },   // a literal text is checked when compiling instead
		{ TEXT("text _t = ToText(\"{2}\");\nHint(_t, 1);"), TEXT("uses {2}") },
		{ TEXT("text _t = ToText(\"{Name}\");\nHint(_t);"), TEXT("uses {Name}") },
		{ TEXT("text _s;\nHint(_s);"), TEXT("'_s' is nil") },
		{ TEXT("entity _e = ToEntity(\"two words\");"), TEXT("'two words' isn't a valid Bango ID name") },
		{ TEXT("entity _e = ToEntity(\"\");"), TEXT("isn't a valid Bango ID name") },
	};
	for (const FErrorCase& Case : ErrorCases)
	{
		FBangoGlobalStore CaseGlobals;
		FString CaseError;
		const EBangoInstanceStatus CaseStatus = RunWithoutWorld(Case.Source, Commands, CaseGlobals, CaseError);
		TestTrue(FString::Printf(TEXT("fails: %s"), Case.Source), CaseStatus == EBangoInstanceStatus::Failed && CaseError.Contains(Case.Contains, ESearchCase::CaseSensitive));
		if (!CaseError.Contains(Case.Contains, ESearchCase::CaseSensitive))
		{
			AddInfo(FString::Printf(TEXT("got: %s"), *CaseError));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoModuleTest, "Bango.Game.Module",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoModuleTest::RunTest(const FString& Parameters)
{
	FBangoModule& Module = FBangoModule::Get();
	TestNotNull(TEXT("module has built-in commands"), Module.GetCommands().FindCommandImpl(TEXT("Hint")));

	// The example script ships with the project in Content/Bango/Examples/Hello.bango.
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	const bool bCompiled = Module.GetScriptCache().GetProgram(TEXT("Examples/Hello"), Module.GetCommands(), Program, DisplayPath, Error);
	TestTrue(TEXT("example script loads and compiles"), bCompiled);
	if (!bCompiled)
	{
		AddError(Error);
	}
	TestEqual(TEXT("display path"), DisplayPath, FString(TEXT("Examples/Hello.bango")));

	TSharedPtr<const FBangoProgram> Again;
	Module.GetScriptCache().GetProgram(TEXT("Examples\\Hello.bango"), Module.GetCommands(), Again, DisplayPath, Error);
	TestTrue(TEXT("second load comes from the cache"), Again.IsValid() && Again == Program);

	// The example's contents may change, so only check that GetScriptParams matches what the compiler produced.
	TArray<FBangoScriptParamInfo> Params;
	TestTrue(TEXT("GetScriptParams succeeds"), Module.GetScriptParams(TEXT("Examples/Hello"), Params, Error));
	TestEqual(TEXT("GetScriptParams matches the compiled program"), Params.Num(), Program.IsValid() ? Program->Params.Num() : -1);

	FBangoValue Entity;
	TestTrue(TEXT("null actor becomes nil"), Bango::ActorToEntity(nullptr, Entity, Error) && Entity.IsNil() && Entity.GetType() == FBangoType::Entity());

	TestFalse(TEXT("missing script fails"), Module.GetScriptCache().GetProgram(TEXT("Examples/DoesNotExist"), Module.GetCommands(), Program, DisplayPath, Error));
	TestTrue(TEXT("missing script error"), Error.Contains(TEXT("not found"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBlueprintArgsTest, "Bango.Game.BlueprintArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBlueprintArgsTest::RunTest(const FString& Parameters)
{
	// params (string _name, int _times = 3, double _delay = 0.5, entity _who = nil)
	TArray<FBangoScriptParamInfo> Params;
	FString Error;
	TestTrue(TEXT("Greet example has parameters"), FBangoModule::Get().GetScriptParams(TEXT("Examples/Greet"), Params, Error) && Params.Num() == 4);
	if (Params.Num() != 4)
	{
		AddError(Error);
		return false;
	}

	// Name passed, Times not passed, Delay passed, Who not passed: Times gets its default, Who is dropped.
	FBangoBlueprintArgs Args = UBangoBlueprintLibrary::MakeBangoArgs();
	Args = UBangoBlueprintLibrary::AddBangoArgString(Args, TEXT("Ada"));
	Args = UBangoBlueprintLibrary::SkipBangoArg(Args);
	Args = UBangoBlueprintLibrary::AddBangoArgDouble(Args, 1.5);
	Args = UBangoBlueprintLibrary::SkipBangoArg(Args);
	TestEqual(TEXT("slots collected"), Args.NumArgs, 4);

	TArray<FBangoValue> Values;
	TestTrue(TEXT("resolves"), Bango::ResolveBlueprintArgs(Args, Params, Values, Error));
	TestEqual(TEXT("trailing not-passed slot dropped"), Values.Num(), 3);
	if (Values.Num() == 3)
	{
		TestTrue(TEXT("name"), Values[0] == FBangoValue::MakeString(TEXT("Ada")));
		TestTrue(TEXT("skipped middle slot uses the default"), Values[1] == FBangoValue::MakeInt(3));
		TestTrue(TEXT("delay"), Values[2] == FBangoValue::MakeDouble(1.5));
	}

	// Skipping a required parameter (only possible with an out-of-date node) is an error.
	FBangoBlueprintArgs Bad = UBangoBlueprintLibrary::SkipBangoArg(UBangoBlueprintLibrary::MakeBangoArgs());
	Bad = UBangoBlueprintLibrary::AddBangoArgInt(Bad, 1);
	TestFalse(TEXT("skipped required parameter fails"), Bango::ResolveBlueprintArgs(Bad, Params, Values, Error));

	// A null actor becomes nil.
	FBangoBlueprintArgs WithActor = UBangoBlueprintLibrary::AddBangoArgActor(UBangoBlueprintLibrary::MakeBangoArgs(), nullptr);
	TestTrue(TEXT("null actor is nil"), WithActor.Error.IsEmpty() && WithActor.Values.Num() == 1 && WithActor.Values[0].IsNil());

	// Arrays.
	FBangoBlueprintArgs WithArray = UBangoBlueprintLibrary::AddBangoArgIntArray(UBangoBlueprintLibrary::MakeBangoArgs(), { 1, 2 });
	TestTrue(TEXT("int array"), WithArray.Values.Num() == 1 && WithArray.Values[0].ToDebugString() == TEXT("[1, 2]"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
