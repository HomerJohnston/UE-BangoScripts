#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoBlueprintLibrary.h"
#include "BangoBuiltinCommands.h"
#include "BangoCompiler.h"
#include "BangoInstance.h"
#include "BangoLocalization.h"
#include "BangoScriptAsset.h"
#include "BangoScriptCache.h"
#include "UObject/Package.h"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

namespace BangoGameTestsPrivate
{
	/** A script asset for a test, in memory only: /Temp/BangoTests/<Name>, with Source as its text. */
	UBangoScriptAsset* MakeTestScript(const TCHAR* Name, const TCHAR* Source)
	{
		const FString PackageName = FString(TEXT("/Temp/BangoTests/")) + Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);
		UBangoScriptAsset* Asset = FindObject<UBangoScriptAsset>(Package, Name);
		if (!Asset)
		{
			Asset = NewObject<UBangoScriptAsset>(Package, Name, RF_Public | RF_Transient);
		}
		Asset->Source = Source;
		return Asset;
	}

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
	const auto Resolves = [](const TCHAR* Path, const TCHAR* Folder, const TCHAR* Expected)
	{
		FString Resolved;
		FString Error;
		return Bango::ResolveScriptPath(Path, Folder, Resolved, Error) && Resolved == Expected;
	};
	const auto Rejects = [](const TCHAR* Path, const TCHAR* Folder)
	{
		FString Resolved;
		FString Error;
		return !Bango::ResolveScriptPath(Path, Folder, Resolved, Error) && !Error.IsEmpty();
	};
	TestTrue(TEXT("relative to the script's folder"), Resolves(TEXT("Intro"), TEXT("/Game/Quests"), TEXT("/Game/Quests/Intro")));
	TestTrue(TEXT("up a folder"), Resolves(TEXT("../Shared/Greet"), TEXT("/Game/Quests"), TEXT("/Game/Shared/Greet")));
	TestTrue(TEXT("full path"), Resolves(TEXT("/Game/Shared/Greet"), TEXT("/Game/Quests"), TEXT("/Game/Shared/Greet")));
	TestTrue(TEXT("relative to /Game"), Resolves(TEXT("Quests/Intro"), TEXT("/Game"), TEXT("/Game/Quests/Intro")));
	TestTrue(TEXT("backslashes, spaces and .bango"), Resolves(TEXT("  Quests\\Sub\\Intro.bango "), TEXT("/Game"), TEXT("/Game/Quests/Sub/Intro")));
	TestTrue(TEXT("doubled slashes and ."), Resolves(TEXT("Quests//./Intro"), TEXT("/Game"), TEXT("/Game/Quests/Intro")));
	TestTrue(TEXT("object path"), Resolves(TEXT("/Game/Quests/Intro.Intro"), TEXT("/Game"), TEXT("/Game/Quests/Intro")));
	TestTrue(TEXT("a plugin's content"), Resolves(TEXT("/MyPlugin/Scripts/A"), TEXT("/Game"), TEXT("/MyPlugin/Scripts/A")));
	TestTrue(TEXT("empty"), Rejects(TEXT(""), TEXT("/Game")));
	TestTrue(TEXT("drive letter"), Rejects(TEXT("C:/Quest/Intro"), TEXT("/Game")));
	TestTrue(TEXT("above the content root"), Rejects(TEXT("../../Secrets"), TEXT("/Game/Quests")));
	TestTrue(TEXT("no name"), Rejects(TEXT("/Game"), TEXT("/Game")));
	TestTrue(TEXT("a dot in the name"), Rejects(TEXT("/Game/A.B"), TEXT("/Game")));
	TestEqual(TEXT("a script's folder"), Bango::GetScriptPathFolder(TEXT("/Game/Quests/Intro.bango")), FString(TEXT("/Game/Quests")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoLocalizedTextsTest, "Bango.Game.LocalizedTexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoLocalizedTextsTest::RunTest(const FString& Parameters)
{
	// What localization gathering reports for a script asset: each text once, with its namespace and notes; not
	// INVTEXT, and not texts in disabled nodes.
	const FString Source =
		TEXT("#define LOCTEXT_NAMESPACE \"Bango.Test\"\n")
		TEXT("Hint(LOCTEXT(\"Hello\", \"Hello there\"));\n")
		TEXT("Hint(NOTELOCTEXT(\"Bye\", \"Goodbye\", \"Said at the door\"));\n")
		TEXT("Hint(NSLOCTEXT(\"Other.NS\", \"Hey\", \"Hey!\"));\n")
		TEXT("Hint(LOCTEXT(\"Hello\", \"Hello there\"));\n")
		TEXT("Hint(INVTEXT(\"not translated\"));\n")
		TEXT("/*\n")
		TEXT("Hint(LOCTEXT(\"Hidden\", \"Disabled text\"));   //@ 9 (0,0)\n")
		TEXT("*/\n")
		TEXT("#undef LOCTEXT_NAMESPACE\n");
	TArray<FBangoLocalizedTextInfo> Texts;
	FString Error;
	TestTrue(TEXT("script reads"), Bango::GetLocalizedTexts(Source, TEXT("test.bango"), Texts, Error));
	TestEqual(TEXT("three texts"), Texts.Num(), 3);
	if (Texts.Num() == 3)
	{
		TestTrue(TEXT("LOCTEXT"), Texts[0].Namespace == TEXT("Bango.Test") && Texts[0].Key == TEXT("Hello") && Texts[0].Source == TEXT("Hello there") && Texts[0].Line == 2);
		TestTrue(TEXT("NOTELOCTEXT"), Texts[1].Key == TEXT("Bye") && Texts[1].Notes == TEXT("Said at the door"));
		TestTrue(TEXT("NSLOCTEXT"), Texts[2].Namespace == TEXT("Other.NS") && Texts[2].Key == TEXT("Hey"));
	}
	TestFalse(TEXT("a script that doesn't read"), Bango::GetLocalizedTexts(TEXT("Hint(LOCTEXT(\"A\""), TEXT("bad.bango"), Texts, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBuiltinCommandsTest, "Bango.Game.BuiltinCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBuiltinCommandsTest::RunTest(const FString& Parameters)
{
	using namespace BangoGameTestsPrivate;
	FBangoCommandTable Commands;
	Bango::RegisterBuiltinCommands(Commands);
	for (const TCHAR* Name : { TEXT("Hint"), TEXT("WriteLog"), TEXT("Format"), TEXT("FormatText"), TEXT("ToText"), TEXT("ToString"), TEXT("Sleep"), TEXT("Random"), TEXT("RandomInt"), TEXT("Floor"), TEXT("Round"),
		TEXT("Min"), TEXT("Max"), TEXT("Abs"), TEXT("Clamp"), TEXT("Sin"), TEXT("Cos"), TEXT("SinDegrees"), TEXT("CosDegrees"), TEXT("Time"), TEXT("ToEntity"), TEXT("SetPos"), TEXT("GetPos"), TEXT("SetHidden"), TEXT("DistanceTo"), TEXT("IsNull"),
		TEXT("Ceil"), TEXT("Sign"), TEXT("Sqrt"), TEXT("Pow"), TEXT("Log"), TEXT("Ln"), TEXT("Lerp"), TEXT("MapRange"), TEXT("InterpTo"), TEXT("DeltaTime"),
		TEXT("Tan"), TEXT("TanDegrees"), TEXT("Asin"), TEXT("AsinDegrees"), TEXT("Acos"), TEXT("AcosDegrees"), TEXT("Atan"), TEXT("AtanDegrees"), TEXT("Atan2"), TEXT("Atan2Degrees"),
		TEXT("Pi"), TEXT("RandomBool"), TEXT("Chance"), TEXT("IsNearlyEqual"), TEXT("Length"), TEXT("Distance"), TEXT("Normalize"), TEXT("Dot"), TEXT("Cross"), TEXT("LerpVector"), TEXT("Exec") })
	{
		const FBangoCommand* Registered = Commands.FindCommandImpl(Name);
		TestNotNull(FString::Printf(TEXT("registered: %s"), Name), Registered);
		TestTrue(FString::Printf(TEXT("built in: %s"), Name), Registered && Registered->Signature.bBuiltIn);
	}
	TestFalse(TEXT("a game's own command isn't built in, with nothing set"), FBangoCommandSignature::Make(TEXT("MyGameCommand")).bBuiltIn);

	FBangoGlobalStore Globals;
	FString Error;
	const TCHAR* Source =
		TEXT("#define LOCTEXT_NAMESPACE \"Bango.Tests\"\n")
		TEXT("global float r;\nglobal int ri;\nglobal int fl;\nglobal int ro;\nglobal int rn;\nglobal string fm;\nglobal string en;\nglobal bool same;\n")
		TEXT("global text tf;\nglobal string ts;\nglobal bool teq;\nglobal bool nobody;\nglobal bool nilnull;\nglobal bool done;\n")
		TEXT("global int ce;\nglobal int sgi;\nglobal float sqr;\nglobal float pw;\nglobal float lg;\nglobal float lg2;\nglobal float lne;\nglobal float lp;\nglobal float mr;\nglobal float a2;\nglobal bool ch;\nglobal bool ne;\nglobal float ln3;\nglobal vector3 nv;\nglobal vector3 cr;\n")
		TEXT("global int mxi;\nglobal float mxf;\nglobal int mni;\nglobal float mnf;\nglobal int absi;\nglobal float absf;\nglobal int cli;\nglobal float clf;\nglobal float sn;\nglobal float cs;\nglobal float snd;\nglobal float csd;\nentity _ne;\n")
		TEXT("r = Random(1.0, 2.0);\n")
		TEXT("ri = RandomInt(3, 3);\n")
		TEXT("fl = Floor(-1.5);\n")
		TEXT("ro = Round(2.5);\n")
		TEXT("rn = Round(-2.5);\n")
		TEXT("mxi = Max(3, 9);\n")
		TEXT("mxf = Max(3, 2.5);\n")
		TEXT("mni = Min(-3, 9);\n")
		TEXT("mnf = Min(1.5, 9);\n")
		TEXT("absi = Abs(-4);\n")
		TEXT("absf = Abs(-0.25);\n")
		TEXT("cli = Clamp(12, 0, 10);\n")
		TEXT("clf = Clamp(-1, 0.5, 10);\n")
		TEXT("sn = Sin(1.5707963267948966);\n")
		TEXT("cs = Cos(0);\n")
		TEXT("snd = SinDegrees(30);\n")
		TEXT("csd = CosDegrees(180.0);\n")
		TEXT("ce = Ceil(1.25);\nsgi = Sign(-7);\nsqr = Sqrt(16);\npw = Pow(2, 10);\nlg = Log(1000);\nlg2 = Log(8, 2);\nlne = Ln(1);\nlp = Lerp(0, 10, 0.25);\n")
		TEXT("mr = MapRange(5, 0, 10, 100, 200);\na2 = Atan2Degrees(1, 1);\nch = Chance(1);\nne = IsNearlyEqual(0.1 + 0.2, 0.3);\nln3 = Length([1, 2, 2]) + Distance([0, 0, 0], [0, 3, 4]) + Dot([1, 0, 0], [2, 0, 0]);\n")
		TEXT("nv = Normalize([0, 0, 5]);\ncr = Cross([1, 0, 0], [0, 1, 0]);\n")
		TEXT("fm = Format(\"{0}-{1}-{2}\", 1, \"a\", [1.5]);\n")
		TEXT("en = Format(\"{0} {1}\", ToEntity(\"door17\"), [@john, ToEntity(\"_x\")]);\n")
		TEXT("same = ToEntity(\"john\") == @john;\n")
		TEXT("Sleep(0.5);\n")
		TEXT("Sleep(0);\n")
		TEXT("WriteLog(\"logged {0}\", 1);\n")
		TEXT("Hint(LOCTEXT(\"NoWorld\", \"a hint with no world goes to the log, {0}\"), 1);\n")
		TEXT("tf = FormatText(LOCTEXT(\"Count\", \"{1}: {0}\"), 2, ToText(\"x\"));\n")
		TEXT("ts = ToString(NSLOCTEXT(\"Bango.Other\", \"Hello\", \"Hello\"));\n")
		TEXT("teq = LOCTEXT(\"Hello\", \"Hello\") == ToText(\"Hello\");\n")
		TEXT("nobody = IsNull(@nobody);\n")
		TEXT("nilnull = IsNull(_ne);\n")
		TEXT("done = true;\n")
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
	TestEqual(TEXT("Max of ints is an int"), Globals.Find(TEXT("mxi"))->GetInt(), 9);
	TestEqual(TEXT("Max with a float is a float"), Globals.Find(TEXT("mxf"))->GetDouble(), 3.0);
	TestEqual(TEXT("Min of ints"), Globals.Find(TEXT("mni"))->GetInt(), -3);
	TestEqual(TEXT("Min with a float"), Globals.Find(TEXT("mnf"))->GetDouble(), 1.5);
	TestEqual(TEXT("Abs of an int"), Globals.Find(TEXT("absi"))->GetInt(), 4);
	TestEqual(TEXT("Abs of a float"), Globals.Find(TEXT("absf"))->GetDouble(), 0.25);
	TestEqual(TEXT("Clamp of ints"), Globals.Find(TEXT("cli"))->GetInt(), 10);
	TestEqual(TEXT("Clamp with a float"), Globals.Find(TEXT("clf"))->GetDouble(), 0.5);
	TestTrue(TEXT("Sin of pi/2"), FMath::IsNearlyEqual(Globals.Find(TEXT("sn"))->GetDouble(), 1.0, 1e-9));
	TestTrue(TEXT("Cos of 0 (an int becomes a float)"), FMath::IsNearlyEqual(Globals.Find(TEXT("cs"))->GetDouble(), 1.0, 1e-9));
	TestTrue(TEXT("SinDegrees of 30"), FMath::IsNearlyEqual(Globals.Find(TEXT("snd"))->GetDouble(), 0.5, 1e-9));
	TestTrue(TEXT("CosDegrees of 180"), FMath::IsNearlyEqual(Globals.Find(TEXT("csd"))->GetDouble(), -1.0, 1e-9));
	TestEqual(TEXT("Ceil"), Globals.Find(TEXT("ce"))->GetInt(), 2);
	TestEqual(TEXT("Sign of an int is an int"), Globals.Find(TEXT("sgi"))->GetInt(), -1);
	TestEqual(TEXT("Sqrt"), Globals.Find(TEXT("sqr"))->GetDouble(), 4.0);
	TestEqual(TEXT("Pow"), Globals.Find(TEXT("pw"))->GetDouble(), 1024.0);
	TestTrue(TEXT("Log (base 10)"), FMath::IsNearlyEqual(Globals.Find(TEXT("lg"))->GetDouble(), 3.0, 1e-9));
	TestTrue(TEXT("Log with a base"), FMath::IsNearlyEqual(Globals.Find(TEXT("lg2"))->GetDouble(), 3.0, 1e-9));
	TestEqual(TEXT("Ln of 1"), Globals.Find(TEXT("lne"))->GetDouble(), 0.0);
	TestEqual(TEXT("Lerp"), Globals.Find(TEXT("lp"))->GetDouble(), 2.5);
	TestEqual(TEXT("MapRange"), Globals.Find(TEXT("mr"))->GetDouble(), 150.0);
	TestTrue(TEXT("Atan2Degrees"), FMath::IsNearlyEqual(Globals.Find(TEXT("a2"))->GetDouble(), 45.0, 1e-9));
	TestTrue(TEXT("Chance(1) is always true"), Globals.Find(TEXT("ch"))->GetBool());
	TestTrue(TEXT("IsNearlyEqual"), Globals.Find(TEXT("ne"))->GetBool());
	TestTrue(TEXT("Length, Distance and Dot"), FMath::IsNearlyEqual(Globals.Find(TEXT("ln3"))->GetDouble(), 10.0, 1e-9));
	TestEqual(TEXT("Normalize"), Globals.Find(TEXT("nv"))->GetVectorPart(2), 1.0);
	TestEqual(TEXT("Cross"), Globals.Find(TEXT("cr"))->GetVectorPart(2), 1.0);
	TestEqual(TEXT("Format"), Globals.Find(TEXT("fm"))->GetString(), FString(TEXT("1-a-[1.5]")));
	TestEqual(TEXT("ToEntity and entities in Format"), Globals.Find(TEXT("en"))->GetString(), FString(TEXT("door17 [john, _x]")));
	TestTrue(TEXT("ToEntity equals the @ literal"), Globals.Find(TEXT("same"))->GetBool());
	TestEqual(TEXT("FormatText, with values moved around"), Globals.Find(TEXT("tf"))->GetText().ToString(), FString(TEXT("x: 2")));
	TestEqual(TEXT("ToString of a text"), Globals.Find(TEXT("ts"))->GetString(), FString(TEXT("Hello")));
	TestTrue(TEXT("texts are equal if they show the same thing"), Globals.Find(TEXT("teq"))->GetBool());
	TestTrue(TEXT("IsNull: no actor has the ID (there's no world at all here)"), Globals.Find(TEXT("nobody"))->GetBool());
	TestTrue(TEXT("IsNull: a nil entity is null"), Globals.Find(TEXT("nilnull"))->GetBool());
	TestTrue(TEXT("ran to the end"), Globals.IsDefined(TEXT("done")));

	struct FErrorCase { const TCHAR* Source; const TCHAR* Contains; };
	const FErrorCase ErrorCases[] =
	{
		{ TEXT("int _x;\n_x = RandomInt(5, 1);"), TEXT("Min (5) is greater than Max (1)") },
		{ TEXT("float _x;\n_x = Random(2.0, 1.0);"), TEXT("Min (2.0) is greater than Max (1.0)") },
		{ TEXT("Sleep(-1.0);"), TEXT("negative time") },
		{ TEXT("float _x;\n_x = Sqrt(-1);"), TEXT("square root of a negative number") },
		{ TEXT("float _x;\n_x = Log(0);"), TEXT("greater than 0") },
		{ TEXT("float _x;\n_x = Pow(-8, 0.5);"), TEXT("isn't a whole number") },
		{ TEXT("float _x;\n_x = Asin(2);"), TEXT("from -1 to 1") },
		{ TEXT("bool _x;\n_x = Chance(1.5);"), TEXT("from 0 to 1") },
		{ TEXT("vector3 _x;\n_x = Normalize([0, 0, 0]);"), TEXT("no direction") },
		{ TEXT("float _x;\n_x = MapRange(1, 2, 2, 0, 1);"), TEXT("no range to map from") },
		{ TEXT("float _t;\n_t = Time();"), TEXT("needs a game world") },
		{ TEXT("Exec(\"Intro\");"), TEXT("needs a game world") },
		{ TEXT("int _x;\n_x = Floor(1e300);"), TEXT("too large to convert") },
		{ TEXT("int _x;\n_x = Abs(-2147483647 - 1);"), TEXT("has no positive int") },
		{ TEXT("int _x;\n_x = Clamp(5, 10, 1);"), TEXT("Min (10) is greater than Max (1)") },
		{ TEXT("string _text = \"{2}\";\nLog(_text, 1);"), TEXT("uses {2}") },   // a literal text is checked when compiling instead
		{ TEXT("text _t;\n_t = ToText(\"{2}\");\nHint(_t, 1);"), TEXT("uses {2}") },
		{ TEXT("text _t;\n_t = ToText(\"{Name}\");\nHint(_t);"), TEXT("uses {Name}") },
		{ TEXT("text _s;\nHint(_s);"), TEXT("'_s' is nil") },
		{ TEXT("entity _e;\n_e = ToEntity(\"two words\");"), TEXT("'two words' isn't a valid Bango ID name") },
		{ TEXT("entity _e;\n_e = ToEntity(\"\");"), TEXT("isn't a valid Bango ID name") },
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

	// A script asset made for the test (in memory, under /Temp), found by its path.
	UBangoScriptAsset* Asset = BangoGameTestsPrivate::MakeTestScript(TEXT("Hello"), TEXT("#define params (string _name = \"you\")\nHint(ToText(_name));"));
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	const bool bCompiled = Module.GetScriptCache().GetProgram(TEXT("/Temp/BangoTests/Hello"), Module.GetCommands(), Program, DisplayPath, Error);
	TestTrue(TEXT("script asset loads and compiles"), bCompiled);
	if (!bCompiled)
	{
		AddError(Error);
	}
	TestEqual(TEXT("display path"), DisplayPath, FString(TEXT("/Temp/BangoTests/Hello.bango")));

	TSharedPtr<const FBangoProgram> Again;
	Module.GetScriptCache().GetProgram(TEXT("\\Temp\\BangoTests\\Hello.bango"), Module.GetCommands(), Again, DisplayPath, Error);
	TestTrue(TEXT("second load comes from the cache"), Again.IsValid() && Again == Program);

	// Changing the asset's text (as the editor does) compiles it again.
	Asset->Source = TEXT("Hint(ToText(\"changed\"));");
	TSharedPtr<const FBangoProgram> Changed;
	TestTrue(TEXT("changed script compiles"), Module.GetScriptCache().GetProgram(*Asset, Module.GetCommands(), Changed, DisplayPath, Error));
	TestTrue(TEXT("changed text is compiled again"), Changed.IsValid() && Changed != Program);
	Asset->Source = TEXT("#define params (string _name = \"you\")\nHint(ToText(_name));");

	TArray<FBangoScriptParamInfo> Params;
	TestTrue(TEXT("GetScriptParams succeeds"), Module.GetScriptParams(TEXT("/Temp/BangoTests/Hello"), Params, Error));
	TestEqual(TEXT("GetScriptParams matches the script"), Params.Num(), 1);

	FBangoValue Entity;
	TestTrue(TEXT("null actor becomes nil"), Bango::ActorToEntity(nullptr, Entity, Error) && Entity.IsNil() && Entity.GetType() == FBangoType::Entity());

	TestFalse(TEXT("missing script fails"), Module.GetScriptCache().GetProgram(TEXT("/Temp/BangoTests/DoesNotExist"), Module.GetCommands(), Program, DisplayPath, Error));
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
	BangoGameTestsPrivate::MakeTestScript(TEXT("Greet"), TEXT("#define params (string _name, int _times = 3, float _delay = 0.5, entity _who = nil)\nSleep(_delay);"));
	TestTrue(TEXT("Greet has parameters"), FBangoModule::Get().GetScriptParams(TEXT("/Temp/BangoTests/Greet"), Params, Error) && Params.Num() == 4);
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
