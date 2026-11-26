#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoBlueprintLibrary.h"
#include "BangoBuiltinCommands.h"
#include "BangoCompiler.h"
#include "BangoCallTestObject.h"
#include "BangoConditions.h"
#include "BangoInlineScript.h"
#include "BangoInputsSignature.h"
#include "BangoReflectedCall.h"
#include "BangoInstance.h"
#include "BangoLocalization.h"
#include "BangoScriptAsset.h"
#include "BangoScriptCache.h"
#include "BangoScriptSubsystem.h"
#include "BangoGlobalsSubsystem.h"
#include "UObject/Package.h"
#include "BangoIDActorComponent.h"
#include "BangoEntityTestComponent.h"
#include "BangoCommand.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Internationalization/Text.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

namespace BangoGameTestsPrivate
{
	/** A script asset for a test, in memory only: /BangoScripts/BangoTests/<Name>, with Source as its text. */
	UBangoScriptAsset* MakeTestScript(const TCHAR* Name, const TCHAR* Source)
	{
		const FString PackageName = FString(TEXT("/BangoScripts/BangoTests/")) + Name;
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
		TEXT("#LOCTEXT_NAMESPACE \"Bango.Test\"\n")
		TEXT("WriteScreen(ToString(LOCTEXT(\"Hello\", \"Hello there\")));\n")
		TEXT("WriteScreen(ToString(NOTELOCTEXT(\"Bye\", \"Goodbye\", \"Said at the door\")));\n")
		TEXT("WriteScreen(ToString(NSLOCTEXT(\"Other.NS\", \"Hey\", \"Hey!\")));\n")
		TEXT("WriteScreen(ToString(LOCTEXT(\"Hello\", \"Hello there\")));\n")
		TEXT("WriteScreen(ToString(INVTEXT(\"not translated\")));\n")
		TEXT("/*\n")
		TEXT("WriteScreen(ToString(LOCTEXT(\"Hidden\", \"Disabled text\")));   //@ 9 (0,0)\n")
		TEXT("*/\n");
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
	TestFalse(TEXT("a script that doesn't read"), Bango::GetLocalizedTexts(TEXT("WriteScreen(ToString(LOCTEXT(\"A\""), TEXT("bad.bango"), Texts, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBuiltinCommandsTest, "Bango.Game.BuiltinCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBuiltinCommandsTest::RunTest(const FString& Parameters)
{
	using namespace BangoGameTestsPrivate;
	FBangoCommandTable Commands;
	Bango::RegisterBuiltinCommands(Commands);
	for (const TCHAR* Name : { TEXT("WriteScreen"), TEXT("WriteLog"), TEXT("Format"), TEXT("FormatText"), TEXT("ToText"), TEXT("ToString"), TEXT("Sleep"), TEXT("Random"), TEXT("RandomInt"), TEXT("Floor"), TEXT("Round"),
		TEXT("Min"), TEXT("Max"), TEXT("Abs"), TEXT("Clamp"), TEXT("Sin"), TEXT("Cos"), TEXT("SinDegrees"), TEXT("CosDegrees"), TEXT("Time"), TEXT("ToEntity"), TEXT("SetPos"), TEXT("GetPos"), TEXT("SetHidden"), TEXT("DistanceTo"), TEXT("IsNull"),
		TEXT("Ceil"), TEXT("Sign"), TEXT("Sqrt"), TEXT("Pow"), TEXT("Log"), TEXT("Ln"), TEXT("Lerp"), TEXT("MapRange"), TEXT("InterpTo"), TEXT("DeltaTime"),
		TEXT("Tan"), TEXT("TanDegrees"), TEXT("Asin"), TEXT("AsinDegrees"), TEXT("Acos"), TEXT("AcosDegrees"), TEXT("Atan"), TEXT("AtanDegrees"), TEXT("Atan2"), TEXT("Atan2Degrees"),
		TEXT("Pi"), TEXT("RandomBool"), TEXT("Chance"), TEXT("IsNearlyEqual"), TEXT("Length"), TEXT("Distance"), TEXT("Normalize"), TEXT("Dot"), TEXT("Cross"), TEXT("Count"), TEXT("Push"), TEXT("InsertAt"), TEXT("Pop"), TEXT("RemoveAt"), TEXT("Exec"),
		TEXT("Abort"), TEXT("IsRunning"), TEXT("WaitFor") })
	{
		const FBangoCommand* Registered = Commands.FindCommandImpl(Name);
		TestNotNull(FString::Printf(TEXT("registered: %s"), Name), Registered);
		TestTrue(FString::Printf(TEXT("built in: %s"), Name), Registered && Registered->Signature.bBuiltIn);
	}
	TestFalse(TEXT("a game's own command isn't built in, with nothing set"), FBangoCommandSignature::Make(TEXT("MyGameCommand")).bBuiltIn);

	FBangoGlobalStore Globals;
	FString Error;
	const TCHAR* Source =
		TEXT("#LOCTEXT_NAMESPACE \"Bango.Tests\"\n")
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
		// Type parameters: Lerp and the vector commands on vector2 and vector3, and Count on an array, a vector and a string.
		TEXT("global vector3 lpv = Lerp([0, 0, 0], [2, 4, 6], 0.5);\nglobal vector2 lp2 = Lerp([0, 0], [2, 4], 0.5);\n")
		TEXT("global float ln2 = Length([3, 4]) + Distance([0, 0], [3, 4]) + Dot([1, 2], [3, 4]);\nglobal vector2 nv2 = Normalize([0, 5]);\n")
		TEXT("global int cnt = Count(\"abc\") + Count([1, 2]) + Count(nv2) + Count(lpv);\n")
		TEXT("fm = Format(\"{0}-{1}-{2}\", 1, \"a\", [1.5]);\n")
		TEXT("en = Format(\"{0} {1}\", ToEntity(\"door17\"), [@john, ToEntity(\"_x\")]);\n")
		TEXT("same = ToEntity(\"john\") == @john;\n")
		TEXT("Sleep(0.5);\n")
		TEXT("Sleep(0);\n")
		TEXT("WriteLog(\"logged {0}\", 1);\n")
		TEXT("WriteScreen(\"a message with no world goes to the log\");\n")
		TEXT("WriteScreen(Format(\"for {0} seconds\", 2), 2);\n")
		TEXT("tf = FormatText(LOCTEXT(\"Count\", \"{1}: {0}\"), 2, ToText(\"x\"));\n")
		TEXT("ts = ToString(NSLOCTEXT(\"Bango.Other\", \"Hello\", \"Hello\"));\n")
		TEXT("teq = LOCTEXT(\"Hello\", \"Hello\") == ToText(\"Hello\");\n")
		TEXT("nobody = IsNull(@nobody);\n")
		TEXT("nilnull = IsNull(_ne);\n")
		TEXT("done = true;\n");
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
	TestTrue(TEXT("Lerp on vector3s"), Globals.Find(TEXT("lpv"))->GetType() == FBangoType::Vector3() && Globals.Find(TEXT("lpv"))->GetVectorPart(2) == 3.0);
	TestTrue(TEXT("Lerp on vector2s"), Globals.Find(TEXT("lp2"))->GetType() == FBangoType::Vector2() && Globals.Find(TEXT("lp2"))->GetVectorPart(1) == 2.0);
	TestTrue(TEXT("Length, Distance and Dot on vector2s"), FMath::IsNearlyEqual(Globals.Find(TEXT("ln2"))->GetDouble(), 21.0, 1e-9));
	TestTrue(TEXT("Normalize on a vector2"), Globals.Find(TEXT("nv2"))->GetType() == FBangoType::Vector2() && Globals.Find(TEXT("nv2"))->GetVectorPart(1) == 1.0);
	TestEqual(TEXT("Count of a string, an array, a vector2 and a vector3"), Globals.Find(TEXT("cnt"))->GetInt(), 10);
	TestTrue(TEXT("pure: Max, Count and Lerp are; Random and GetPos aren't"), Commands.FindCommand(TEXT("Max"))->bPure && Commands.FindCommand(TEXT("Count"))->bPure
		&& Commands.FindCommand(TEXT("Lerp"))->bPure && !Commands.FindCommand(TEXT("Random"))->bPure && !Commands.FindCommand(TEXT("GetPos"))->bPure);
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
		{ TEXT("vector2 _x;\n_x = Normalize([0, 0]);"), TEXT("no direction") },
		{ TEXT("vector3 _x;\n_x = Lerp([0, 0, 0], [1e308, 0, 0], 1e10);"), TEXT("Lerp's result isn't a vector that can be used") },
		{ TEXT("float _x;\n_x = MapRange(1, 2, 2, 0, 1);"), TEXT("no range to map from") },
		{ TEXT("float _t;\n_t = Time();"), TEXT("needs a game world") },
		{ TEXT("Exec(\"Intro\");"), TEXT("needs a game world") },
		{ TEXT("Abort();"), TEXT("needs a game world") },
		{ TEXT("Abort(nil, true);"), TEXT("needs a game world") },
		{ TEXT("bool _r;\n_r = IsRunning(1);"), TEXT("needs a game world") },
		{ TEXT("WaitFor(1);"), TEXT("needs a game world") },
		{ TEXT("spawn\n{\n}"), TEXT("only a script running in a game world can spawn") },
		{ TEXT("int _x;\n_x = Floor(1e300);"), TEXT("too large to convert") },
		{ TEXT("int _x;\n_x = Abs(-2147483647 - 1);"), TEXT("has no positive int") },
		{ TEXT("int _x;\n_x = Clamp(5, 10, 1);"), TEXT("Min (10) is greater than Max (1)") },
		{ TEXT("string _text = \"{2}\";\nWriteLog(_text, 1);"), TEXT("uses {2}") },   // a literal text is checked when compiling instead
		{ TEXT("text _t;\n_t = ToText(\"{2}\");\n_t = FormatText(_t, 1);"), TEXT("uses {2}") },
		{ TEXT("text _t;\n_t = ToText(\"{Name}\");\n_t = FormatText(_t);"), TEXT("uses {Name}") },
		{ TEXT("text _s;\ntext _t;\n_t = FormatText(_s);"), TEXT("'_s' is nil") },
		{ TEXT("string _n;\nWriteScreen(_n);"), TEXT("'_n' is nil") },
		{ TEXT("WriteScreen(\"x\", -1);"), TEXT("negative time") },
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
	TestNotNull(TEXT("module has built-in commands"), Module.GetCommands().FindCommandImpl(TEXT("WriteScreen")));

	// A script asset made for the test (in memory, under the plugin's content root, since /Temp isn't a valid script path root), found by its path.
	UBangoScriptAsset* Asset = BangoGameTestsPrivate::MakeTestScript(TEXT("Hello"), TEXT("#inputs (string _name = \"you\")\nWriteScreen(_name);"));
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	const bool bCompiled = Module.GetScriptCache().GetProgram(TEXT("/BangoScripts/BangoTests/Hello"), Module.GetCommands(), Program, DisplayPath, Error);
	TestTrue(TEXT("script asset loads and compiles"), bCompiled);
	if (!bCompiled)
	{
		AddError(Error);
	}
	TestEqual(TEXT("display path"), DisplayPath, FString(TEXT("/BangoScripts/BangoTests/Hello.bango")));

	TSharedPtr<const FBangoProgram> Again;
	Module.GetScriptCache().GetProgram(TEXT("\\BangoScripts\\BangoTests\\Hello.bango"), Module.GetCommands(), Again, DisplayPath, Error);
	TestTrue(TEXT("second load comes from the cache"), Again.IsValid() && Again == Program);

	// Changing the asset's text (as the editor does) compiles it again.
	Asset->Source = TEXT("WriteScreen(\"changed\");");
	TSharedPtr<const FBangoProgram> Changed;
	TestTrue(TEXT("changed script compiles"), Module.GetScriptCache().GetProgram(*Asset, Module.GetCommands(), Changed, DisplayPath, Error));
	TestTrue(TEXT("changed text is compiled again"), Changed.IsValid() && Changed != Program);
	{
		// The asset remembers what it was compiled as, for as long as its text is the same.
		TSharedPtr<const FBangoProgram> Remembered;
		FString RememberedPath;
		TestTrue(TEXT("remembered asset: the asset keeps what it was compiled as"), Asset->LastCompiled.IsValid() && Asset->LastCompiled->Program == Changed);
		TestTrue(TEXT("remembered asset: the same again, with its display path"),
			Module.GetScriptCache().GetProgram(*Asset, Module.GetCommands(), Remembered, RememberedPath, Error) && Remembered == Changed
			&& RememberedPath == TEXT("/BangoScripts/BangoTests/Hello.bango"));
		Asset->Source = TEXT("WriteScreen(");
		TestFalse(TEXT("remembered asset: text that no longer compiles fails"),
			Module.GetScriptCache().GetProgram(*Asset, Module.GetCommands(), Remembered, RememberedPath, Error));
		Asset->Source = TEXT("WriteScreen(\"changed\");");
		TestTrue(TEXT("remembered asset: the text it was compiled from, again"),
			Module.GetScriptCache().GetProgram(*Asset, Module.GetCommands(), Remembered, RememberedPath, Error) && Remembered == Changed);
	}
	Asset->Source = TEXT("#inputs (string _name = \"you\")\nWriteScreen(_name);");

	TArray<FBangoScriptParamInfo> Params;
	TestTrue(TEXT("GetScriptParams succeeds"), Module.GetScriptParams(TEXT("/BangoScripts/BangoTests/Hello"), Params, Error));
	TestEqual(TEXT("GetScriptParams matches the script"), Params.Num(), 1);

	FBangoValue Entity;
	TestTrue(TEXT("null actor becomes nil"), Bango::ActorToEntity(nullptr, Entity, Error) && Entity.IsNil() && Entity.GetType() == FBangoType::Entity());

	TestFalse(TEXT("missing script fails"), Module.GetScriptCache().GetProgram(TEXT("/BangoScripts/BangoTests/DoesNotExist"), Module.GetCommands(), Program, DisplayPath, Error));
	TestTrue(TEXT("missing script error"), Error.Contains(TEXT("not found"), ESearchCase::CaseSensitive));

	// Inline scripts: compiled once per text (and name), with their parameters; an error isn't kept.
	FBangoInlineScript Inline;
	Inline.Source = TEXT("#inputs (int _count, string _name = \"you\")\nSleep(1); WriteScreen(_name);");
	TSharedPtr<const FBangoProgram> InlineProgram;
	TSharedPtr<const FBangoProgram> InlineAgain;
	TestTrue(TEXT("inline script compiles"), Module.GetInlineProgram(Inline, TEXT("Test (inline script)"), InlineProgram, Error));
	TestTrue(TEXT("inline script: the same text comes from the cache"), Module.GetInlineProgram(Inline, TEXT("Test (inline script)"), InlineAgain, Error) && InlineAgain == InlineProgram);
	TestTrue(TEXT("inline script: its parameters"), InlineProgram.IsValid() && InlineProgram->Params.Num() == 2);
	{
		// The script remembers what it was compiled as, and stops using that as soon as anything it came from changes.
		FBangoInlineScript Remembering;
		Remembering.Source = TEXT("WriteScreen(\"one\");");
		TSharedPtr<const FBangoProgram> First;
		TSharedPtr<const FBangoProgram> Second;
		TSharedPtr<const FString> FirstText;
		TSharedPtr<const FString> SecondText;
		TestTrue(TEXT("remembered: compiles"), Module.GetInlineProgram(Remembering, TEXT("Remembering (inline script)"), First, Error, &FirstText));
		TestTrue(TEXT("remembered: the script keeps it"), Remembering.LastCompiled.IsValid() && Remembering.LastCompiled->Program == First);
		TestTrue(TEXT("remembered: the same again, with the same copy of its text"),
			Module.GetInlineProgram(Remembering, TEXT("Remembering (inline script)"), Second, Error, &SecondText) && Second == First
			&& FirstText.IsValid() && SecondText == FirstText && *FirstText == Remembering.Source);
		const FBangoInlineScript Copied = Remembering;
		TestTrue(TEXT("remembered: a copy of the script uses it too"),
			Module.GetInlineProgram(Copied, TEXT("Remembering (inline script)"), Second, Error) && Second == First);
		TestTrue(TEXT("remembered: another name is another program, named for it"),
			Module.GetInlineProgram(Remembering, TEXT("Other (inline script)"), Second, Error) && Second != First
			&& Second->FileName == TEXT("Other (inline script)"));
		Remembering.Source = TEXT("WriteScreen(\"two\");");
		TestTrue(TEXT("remembered: changed text is compiled again"),
			Module.GetInlineProgram(Remembering, TEXT("Other (inline script)"), First, Error, &FirstText) && First != Second
			&& FirstText.IsValid() && *FirstText == Remembering.Source);
		Remembering.Source = TEXT("WriteScreen(");
		TestFalse(TEXT("remembered: text that no longer compiles fails"), Module.GetInlineProgram(Remembering, TEXT("Other (inline script)"), First, Error));
		FBangoInlineScript Blank;
		Blank.Source = TEXT(" \t\n ");
		TestTrue(TEXT("remembered: only spaces is empty"), Blank.IsEmpty() && FBangoInlineScript().IsEmpty() && !Copied.IsEmpty());
	}
	FBangoInlineScript Broken;
	Broken.Source = TEXT("Sleep(");
	TestFalse(TEXT("inline script that doesn't compile"), Module.GetInlineProgram(Broken, TEXT("Test (inline script)"), InlineProgram, Error));
	TestTrue(TEXT("inline script error names it"), Error.Contains(TEXT("Test (inline script)"), ESearchCase::CaseSensitive));
	TestEqual(TEXT("inline script name"), UBangoScriptSubsystem::GetInlineScriptName(nullptr), FString(TEXT("Inline script")));

	// Inputs signatures: an inline script's inputs given by its owner's C++ (BangoInputsSignature.h).
	Bango::RegisterInputsSignature(nullptr, TEXT("BangoTests.Hit"), { { TEXT("entity _who"), TEXT("Who was hit.") }, { TEXT("int _damage") } });
	const TSharedPtr<const FBangoInputsSignature> Hit = Bango::FindInputsSignature(nullptr, TEXT("BangoTests.Hit"));
	TestTrue(TEXT("signature: a global one is registered"), Hit.IsValid() && Hit->Params.Num() == 2 && Hit->Tooltips[0] == TEXT("Who was hit.")
		&& Hit->Label == TEXT("the global 'BangoTests.Hit'"));
	FBangoInlineScript Signed;
	Signed.Source = TEXT("Sleep(_damage);\nWriteScreen(\"hit\");");
	Signed.UseInputsSignature(nullptr, TEXT("BangoTests.Hit"));
	TSharedPtr<const FBangoProgram> SignedProgram;
	TestTrue(TEXT("signature: the script compiles with its inputs"), Module.GetInlineProgram(Signed, TEXT("Test (inline script)"), SignedProgram, Error)
		&& SignedProgram.IsValid() && SignedProgram->Params.Num() == 2 && SignedProgram->Params[1].Name == TEXT("_damage"));
	const FBangoInlineScript SignedCopy = Signed;
	TSharedPtr<const FBangoProgram> CopyProgram;
	TestTrue(TEXT("signature: a copy keeps it"), SignedCopy.HasInputsSignature() && Module.GetInlineProgram(SignedCopy, TEXT("Test (inline script)"), CopyProgram, Error) && CopyProgram == SignedProgram);
	FBangoInlineScript Unsigned;
	Unsigned.Source = Signed.Source;
	TSharedPtr<const FBangoProgram> UnsignedProgram;
	TestTrue(TEXT("signature: the same text without it is another program"), Module.GetInlineProgram(Unsigned, TEXT("Test (inline script)"), UnsignedProgram, Error)
		&& UnsignedProgram != SignedProgram && UnsignedProgram->Params.Num() == 0);
	FBangoInlineScript WithInputs = Signed;
	WithInputs.Source = TEXT("#inputs (int _n = 1)\nSleep(_n);");
	TestFalse(TEXT("signature: no #inputs line"), Module.GetInlineProgram(WithInputs, TEXT("Test (inline script)"), InlineProgram, Error));
	TestTrue(TEXT("signature: no #inputs line: the error"), Error.Contains(TEXT("This script's inputs are given by the global 'BangoTests.Hit': _who, _damage"), ESearchCase::CaseSensitive));
	FBangoInlineScript Unregistered;
	Unregistered.Source = TEXT("Sleep(1);");
	Unregistered.UseInputsSignature(nullptr, TEXT("BangoTests.Nope"));
	TestFalse(TEXT("signature: one that isn't registered"), Module.GetInlineProgram(Unregistered, TEXT("Test (inline script)"), InlineProgram, Error));
	TestTrue(TEXT("signature: one that isn't registered: the error"), Error.Contains(TEXT("none is registered by that name"), ESearchCase::CaseSensitive));

	FBangoInlineCondition Condition;
	Condition.UseInputsSignature(nullptr, Signed.GetInputsSignatureName());
	Condition.Source = TEXT("_damage > 3 && isNil(_who)");
	FString ConditionError;
	FBangoConditionHandle ConditionHandle = Bango::RegisterCondition(Condition, TEXT("Test condition"), ConditionError);
	const TSharedPtr<const FBangoProgram> ConditionProgram = Bango::FindCondition(ConditionHandle);
	TestTrue(TEXT("signature: a condition with one"), ConditionProgram.IsValid() && ConditionProgram->Params.Num() == 2);
	if (!ConditionProgram.IsValid())
	{
		AddError(ConditionError);
	}
	Bango::UnregisterCondition(ConditionHandle);

	// Instance inputs: an object's own, after its signature's (FBangoInlineScriptBase::SetInstanceInputs).
	FString OwnError;
	FString FindError;
	FBangoInlineScript Lit = Signed;
	Lit.Source = TEXT("Sleep(_damage + _speed);\nWriteScreen(Format(\"{0}\", _red));");
	TestTrue(TEXT("instance inputs: set"), Lit.SetInstanceInputs({ { TEXT("vector3 _red"), TEXT("Its colour.") }, { TEXT("float _speed") } }, &OwnError)
		&& Lit.GetInstanceInputs().Num() == 2 && Lit.HasGivenInputs());
	TSharedPtr<const FBangoProgram> LitProgram;
	TestTrue(TEXT("instance inputs: the script compiles with both"), Module.GetInlineProgram(Lit, TEXT("Test (inline script)"), LitProgram, Error)
		&& LitProgram.IsValid() && LitProgram->Params.Num() == 4 && LitProgram->Params[1].Name == TEXT("_damage") && LitProgram->Params[2].Name == TEXT("_red")
		&& LitProgram->Params[3].Type == FBangoType::Double());
	const TSharedPtr<const FBangoInputsSignature> LitInputs = Lit.FindInputsSignature(FindError);
	TestTrue(TEXT("instance inputs: after the signature's"), LitInputs.IsValid() && LitInputs->NumInstanceInputs == 2 && LitInputs->Params.Num() == 4
		&& LitInputs->Label == TEXT("the global 'BangoTests.Hit' and this object") && LitInputs->Tooltips[2] == TEXT("Its colour.") && LitInputs->IsInstanceInput(3) && !LitInputs->IsInstanceInput(1));
	FBangoInlineScript Lit2 = Signed;
	Lit2.SetInstanceInputs({ { TEXT("vector3 _red"), TEXT("Its colour.") }, { TEXT("float _speed") } }, &OwnError);
	TestTrue(TEXT("instance inputs: the same list on another script is the same inputs"), Lit2.FindInputsSignature(FindError) == LitInputs);
	TestTrue(TEXT("instance inputs: setting the same list again changes nothing"), Lit.SetInstanceInputs({ { TEXT("vector3 _red"), TEXT("Its colour.") }, { TEXT("float _speed") } }, &OwnError)
		&& Lit.FindInputsSignature(FindError) == LitInputs);
	TestTrue(TEXT("instance inputs: a script without any still has just its signature"), Signed.FindInputsSignature(FindError) == Hit && Signed.GetInstanceInputs().Num() == 0);
	FBangoInlineScript OtherList = Lit;
	TSharedPtr<const FBangoProgram> OtherListProgram;
	TestTrue(TEXT("instance inputs: another list is another program"), OtherList.SetInstanceInputs({ { TEXT("vector3 _red") }, { TEXT("int _speed") } }, &OwnError)
		&& Module.GetInlineProgram(OtherList, TEXT("Test (inline script)"), OtherListProgram, Error) && OtherListProgram != LitProgram
		&& OtherListProgram->Params[3].Type == FBangoType::Int() && OtherList.FindInputsSignature(FindError) != LitInputs);
	FBangoInlineScript OnlyOwn;
	OnlyOwn.Source = TEXT("Sleep(_speed);");
	TSharedPtr<const FBangoProgram> OnlyOwnProgram;
	TestTrue(TEXT("instance inputs: without a signature"), OnlyOwn.SetInstanceInputs({ { TEXT("float _speed") } }, &OwnError) && OnlyOwn.HasGivenInputs() && !OnlyOwn.HasInputsSignature()
		&& Module.GetInlineProgram(OnlyOwn, TEXT("Test (inline script)"), OnlyOwnProgram, Error) && OnlyOwnProgram->Params.Num() == 1);
	FBangoInlineScript Clash = Signed;
	TestFalse(TEXT("instance inputs: a signature input's name"), Clash.SetInstanceInputs({ { TEXT("int _damage") } }, &OwnError));
	TestTrue(TEXT("instance inputs: a signature input's name: the error"), OwnError.Contains(TEXT("This object's own input '_damage' (SetInstanceInputs) has the name of an input of the global 'BangoTests.Hit'"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("instance inputs: wrong ones stop the script compiling"), Module.GetInlineProgram(Clash, TEXT("Test (inline script)"), InlineProgram, Error));
	TestTrue(TEXT("instance inputs: wrong ones stop the script compiling: the error"), Error.Contains(TEXT("has the name of an input of"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("instance inputs: a default"), Clash.SetInstanceInputs({ { TEXT("float _speed = 1") } }, &OwnError));
	TestTrue(TEXT("instance inputs: a default: the error"), OwnError.Contains(TEXT("can't have a default"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("instance inputs: taken away again"), Clash.SetInstanceInputs({}, &OwnError) && !Clash.GetInstanceInputs().Num()
		&& Module.GetInlineProgram(Clash, TEXT("Test (inline script)"), InlineProgram, Error) && InlineProgram == SignedProgram);
	AddExpectedError(TEXT("should begin with an underscore: 'speed'"), EAutomationExpectedErrorFlags::Contains, 1);
	FBangoInlineScript NoUnderscore = Signed;
	NoUnderscore.Source = TEXT("Sleep(speed);");
	TestTrue(TEXT("instance inputs: a name without '_' is reported, but works"), NoUnderscore.SetInstanceInputs({ { TEXT("float speed") } }, &OwnError)
		&& Module.GetInlineProgram(NoUnderscore, TEXT("Test (inline script)"), InlineProgram, Error) && InlineProgram->Params.Num() == 3);
	FBangoInlineCondition LitCondition;
	LitCondition.UseInputsSignature(nullptr, Signed.GetInputsSignatureName());
	LitCondition.SetInstanceInputs({ { TEXT("float _speed") } }, &OwnError);
	LitCondition.Source = TEXT("_damage > 3 && _speed > 1");
	FBangoConditionHandle LitConditionHandle = Bango::RegisterCondition(LitCondition, TEXT("Test condition"), ConditionError);
	const TSharedPtr<const FBangoProgram> LitConditionProgram = Bango::FindCondition(LitConditionHandle);
	TestTrue(TEXT("instance inputs: a condition with some"), LitConditionProgram.IsValid() && LitConditionProgram->Params.Num() == 3 && LitConditionProgram->Params[2].Name == TEXT("_speed"));
	Bango::UnregisterCondition(LitConditionHandle);

	// Per class, from a template only; a name without '_' is reported, but works; registering again replaces it.
	AddExpectedError(TEXT("isn't a template, so it was ignored"), EAutomationExpectedErrorFlags::Contains, 1);
	Bango::RegisterInputsSignature(Asset, TEXT("BangoTests.Asset"), { { TEXT("int _a") } });
	TestFalse(TEXT("signature: not from an instance"), Bango::FindInputsSignature(Asset->GetClass(), TEXT("BangoTests.Asset")).IsValid());
	AddExpectedError(TEXT("should begin with an underscore: 'count'"), EAutomationExpectedErrorFlags::Contains, 1);
	Bango::RegisterInputsSignature(GetDefault<UBangoScriptAsset>(), TEXT("BangoTests.Asset"), { { TEXT("int count") } });
	const TSharedPtr<const FBangoInputsSignature> ForClass = Bango::FindInputsSignature(UBangoScriptAsset::StaticClass(), TEXT("BangoTests.Asset"));
	TestTrue(TEXT("signature: for a class"), ForClass.IsValid() && ForClass->Label == TEXT("BangoScriptAsset's 'BangoTests.Asset'") && ForClass->Params[0].Name == TEXT("count"));
	TestFalse(TEXT("signature: not global"), Bango::FindInputsSignature(nullptr, TEXT("BangoTests.Asset")).IsValid());
	Bango::RegisterInputsSignature(GetDefault<UBangoScriptAsset>(), TEXT("BangoTests.Asset"), { { TEXT("int _a") }, { TEXT("int _b") } });
	const TSharedPtr<const FBangoInputsSignature> Replaced = Bango::FindInputsSignature(UBangoScriptAsset::StaticClass(), TEXT("BangoTests.Asset"));
	TestTrue(TEXT("signature: registering again replaces it"), Replaced.IsValid() && Replaced->Params.Num() == 2);
	FBangoInlineScript OfClass;
	OfClass.Source = TEXT("Sleep(_a + _b);");
	OfClass.UseInputsSignature(Asset, TEXT("BangoTests.Asset"));
	TestTrue(TEXT("signature: an instance uses its class's"), Module.GetInlineProgram(OfClass, TEXT("Test (inline script)"), InlineProgram, Error) && InlineProgram->Params.Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoCallTest, "Bango.Game.Call",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBangoCallTest::RunTest(const FString& Parameters)
{
	// `target call Name(args)`: Bango::CallBangoCallableOn, what CallBangoCallable does once it has found the actor.
	UBangoCallTestObject* Object = NewObject<UBangoCallTestObject>();
	const FString Target = TEXT("@lamp (BangoCallTestObject)");
	FBangoValue Result;
	FString Error;

	TestTrue(TEXT("call: arguments"), Bango::CallBangoCallableOn(Object, Target, nullptr, TEXT("TurnOn"),
		{ FBangoValue::MakeInt(2), FBangoValue::MakeBool(true) }, false, Result, Error));
	TestTrue(TEXT("call: arguments arrived"), FMath::IsNearlyEqual(Object->LastBrightness, 2.0f) && Object->bLastFade);
	TestTrue(TEXT("call: a result"), Bango::CallBangoCallableOn(Object, Target, nullptr, TEXT("GetCount"), {}, true, Result, Error)
		&& Result.GetType() == FBangoType::Int() && Result.GetInt() == 5);
	TestTrue(TEXT("call: a name, a vector, an int, and a string result; under 'Bango|'"), Bango::CallBangoCallableOn(Object, Target, nullptr, TEXT("Describe"),
		{ FBangoValue::MakeString(TEXT("hi")), FBangoValue::MakeVector3(1, 2, 3), FBangoValue::MakeInt(4) }, true, Result, Error)
		&& Result.GetType() == FBangoType::String() && Result.GetString() == TEXT("hi 1,2,3 x4"));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	struct FBadCall { const TCHAR* What; const TCHAR* Name; TArray<FBangoValue> Args; bool bWantResult; const TCHAR* ErrorContains; };
	const FBadCall BadCalls[] = {
		{ TEXT("not there"), TEXT("TurnOff"), {}, false, TEXT("@lamp (BangoCallTestObject) has no Blueprint function named 'TurnOff'") },
		{ TEXT("the wrong case"), TEXT("turnOn"), {}, false, TEXT("Did you mean 'TurnOn'? Names are case-sensitive.") },
		{ TEXT("not in the Bango category"), TEXT("NotForBango"), {}, false, TEXT("isn't in the Bango category") },
		{ TEXT("the wrong count"), TEXT("TurnOn"), { FBangoValue::MakeInt(1) }, false, TEXT("it takes 2 argument(s), but 1 were given: TurnOn(float Brightness, bool Fade)") },
		{ TEXT("the wrong type"), TEXT("TurnOn"), { FBangoValue::MakeString(TEXT("x")), FBangoValue::MakeBool(true) }, false, TEXT("'Brightness' takes: float. This is: string.") },
		{ TEXT("nil for a number"), TEXT("TurnOn"), { FBangoValue::MakeNil(FBangoType::Double()), FBangoValue::MakeBool(true) }, false, TEXT("'Brightness' (float) can't be nil.") },
		{ TEXT("no result"), TEXT("TurnOn"), { FBangoValue::MakeInt(1), FBangoValue::MakeBool(false) }, true, TEXT("it doesn't return a value") },
		{ TEXT("two results"), TEXT("GetTwo"), {}, true, TEXT("it returns 2 values") },
		{ TEXT("a type Bango doesn't have"), TEXT("TakeArray"), { FBangoValue::MakeInt(1) }, false, TEXT("which Bango has no type for") },
	};
	for (const FBadCall& Bad : BadCalls)
	{
		FString BadError;
		TestFalse(FString::Printf(TEXT("call: %s"), Bad.What), Bango::CallBangoCallableOn(Object, Target, nullptr, Bad.Name, Bad.Args, Bad.bWantResult, Result, BadError));
		TestTrue(FString::Printf(TEXT("call: %s: the error (%s)"), Bad.What, *BadError), BadError.Contains(Bad.ErrorContains, ESearchCase::CaseSensitive));
	}
	FString GetTwoError;
	TestTrue(TEXT("call: two results, unused"), Bango::CallBangoCallableOn(Object, Target, nullptr, TEXT("GetTwo"), {}, false, Result, GetTwoError));

	TArray<FBangoCallableFunction> Callable;
	Bango::GetBangoCallableFunctions(UBangoCallTestObject::StaticClass(), Callable);
	TArray<FString> Names;
	for (const FBangoCallableFunction& Function : Callable)
	{
		Names.Add(Function.Name);
	}
	Names.Sort();
	TestEqual(TEXT("call: the callable functions"), FString::Join(Names, TEXT(" ")), FString(TEXT("Describe GetCount GetTwo TakeArray TurnOn")));
	const FBangoCallableFunction* DescribeFunction = Callable.FindByPredicate([](const FBangoCallableFunction& F) { return F.Name == TEXT("Describe"); });
	TestTrue(TEXT("call: a callable function's signature"), DescribeFunction && DescribeFunction->bHasResult && DescribeFunction->ResultType == FBangoType::String()
		&& DescribeFunction->Signature == TEXT("Describe(string Label, vector3 Where, int Times) -> string"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBlueprintArgsTest, "Bango.Game.BlueprintArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBlueprintArgsTest::RunTest(const FString& Parameters)
{
	// inputs (string _name, int _times = 3, float _delay = 0.5, entity _who = nil)
	TArray<FBangoScriptParamInfo> Params;
	FString Error;
	BangoGameTestsPrivate::MakeTestScript(TEXT("Greet"), TEXT("#inputs (string _name, int _times = 3, float _delay = 0.5, entity _who = nil)\nSleep(_delay);"));
	TestTrue(TEXT("Greet has parameters"), FBangoModule::Get().GetScriptParams(TEXT("/BangoScripts/BangoTests/Greet"), Params, Error) && Params.Num() == 4);
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

// Entity IDs for actors without a Bango ID name (spec section 4, Bango IDs), in a game world that has begun play.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoEntityIDsTest, "Bango.Game.EntityIDs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoEntityIDsTest::RunTest(const FString& Parameters)
{
	// The kinds, which can't clash, and how they're shown.
	TestTrue(TEXT("kinds"), Bango::GetEntityIDKind(TEXT("john")) == Bango::EBangoEntityIDKind::Named
		&& Bango::GetEntityIDKind(Bango::GetPlayerEntityName()) == Bango::EBangoEntityIDKind::Keyword
		&& Bango::GetEntityIDKind(TEXT("%/Game/Maps/Farm.Farm:PersistentLevel.Door_3")) == Bango::EBangoEntityIDKind::Level
		&& Bango::GetEntityIDKind(TEXT("$0123456789ABCDEF0123456789abcdef")) == Bango::EBangoEntityIDKind::Automatic
		&& Bango::GetEntityIDKind(TEXT("$12")) == Bango::EBangoEntityIDKind::Invalid
		&& Bango::GetEntityIDKind(TEXT("#nope")) == Bango::EBangoEntityIDKind::Invalid
		&& Bango::GetEntityIDKind(TEXT("door_1|Trigger")) == Bango::EBangoEntityIDKind::Component
		&& Bango::GetEntityIDKind(TEXT("%/Game/Maps/Farm.Farm:PersistentLevel.Door_3|Trigger")) == Bango::EBangoEntityIDKind::Component
		&& Bango::GetEntityIDKind(TEXT("door_1|")) == Bango::EBangoEntityIDKind::Invalid
		&& Bango::GetEntityIDKind(TEXT("a|b|c")) == Bango::EBangoEntityIDKind::Invalid);
	TestEqual(TEXT("a Component ID never seen shows its actor's and its name"), Bango::GetEntityDisplayName(TEXT("%/Game/Maps/Farm.Farm:PersistentLevel.Door_3|Trigger")), FString(TEXT("Door_3 (Trigger)")));
	TestEqual(TEXT("a Level ID never seen shows its object name"), Bango::GetEntityDisplayName(TEXT("%/Game/Maps/Farm.Farm:PersistentLevel.Door_3")), FString(TEXT("Door_3")));
	TestEqual(TEXT("a named one shows as it is"), FBangoValue::MakeEntity(TEXT("john")).ToDisplayString(), FString(TEXT("john")));

	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("game world"), World))
	{
		GameInstance->RemoveFromRoot();
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	UBangoActorRegistry* Registry = UBangoActorRegistry::Get(World);
	TestNotNull(TEXT("registry"), Registry);

	if (Registry)
	{
		FString Error;
		FBangoValue Entity;

		// Spawned, with no Bango ID: an Automatic ID, kept on a Bango ID component added to it; the same one each time.
		AActor* Spawned = World->SpawnActor<AActor>();
		TestTrue(TEXT("a spawned actor becomes an entity"), Bango::ActorToEntity(Spawned, Entity, Error));
		const FString AutoID = Entity.IsNil() ? FString() : Entity.GetEntityName();
		const UBangoIDActorComponent* Added = Spawned->FindComponentByClass<UBangoIDActorComponent>();
		TestTrue(TEXT("an Automatic ID, on an added component"), Bango::GetEntityIDKind(AutoID) == Bango::EBangoEntityIDKind::Automatic
			&& Added && Added->AutomaticID == AutoID && Added->BangoName.IsEmpty());
		TestTrue(TEXT("it finds the actor"), Registry->FindActor(AutoID) == Spawned);
		TestEqual(TEXT("the same ID again"), Registry->GetOrMakeEntityID(Spawned, Error), AutoID);
		TestEqual(TEXT("shown as the actor's name"), Entity.ToDisplayString(), Spawned->GetActorNameOrLabel());
		TestTrue(TEXT("no duplicate component"), Spawned->GetComponents().Num() == 1);

		// Named: as before.
		AActor* Named = World->SpawnActor<AActor>();
		UBangoIDActorComponent* NameComponent = NewObject<UBangoIDActorComponent>(Named);
		NameComponent->BangoName = TEXT("entityTestJohn");
		NameComponent->RegisterComponent();
		TestTrue(TEXT("a named actor is its name"), Bango::ActorToEntity(Named, Entity, Error) && Entity.GetEntityName() == TEXT("entityTestJohn"));

		// Placed in a level (there when its level started): its path, found by the path too.
		AActor* Placed = World->SpawnActor<AActor>();
		Placed->bNetStartup = true;
		const FString LevelID = Registry->GetOrMakeEntityID(Placed, Error);
		TestEqual(TEXT("a placed actor's Level ID"), LevelID, Bango::MakeLevelEntityID(Placed->GetPathName()));
		AActor* PlacedLater = World->SpawnActor<AActor>();
		PlacedLater->bNetStartup = true;
		TestTrue(TEXT("a Level ID made elsewhere (a save) finds its actor by its path"),
			Registry->FindActor(Bango::MakeLevelEntityID(PlacedLater->GetPathName())) == PlacedLater);
		TestNull(TEXT("no component added to a placed actor"), Placed->FindComponentByClass<UBangoIDActorComponent>());

		// A Bango ID name given after it had an Automatic ID: an error, both find it, and the name is used from now on.
		AddExpectedError(TEXT("after scripts had already used it without one"), EAutomationExpectedErrorFlags::Contains, 1);
		UBangoIDActorComponent* LateName = NewObject<UBangoIDActorComponent>(Spawned);
		LateName->BangoName = TEXT("entityTestLate");
		LateName->RegisterComponent();
		TestEqual(TEXT("the name takes over"), Registry->GetOrMakeEntityID(Spawned, Error), FString(TEXT("entityTestLate")));
		TestTrue(TEXT("the old ID still finds it"), Registry->FindActor(AutoID) == Spawned);

		// Gone: not found, and errors say which actor it was.
		const FString SpawnedLabel = Spawned->GetActorNameOrLabel();
		Spawned->Destroy();
		TestNull(TEXT("a destroyed actor isn't found"), Registry->FindActor(AutoID));
		TestTrue(TEXT("the error names it"), Bango::DescribeMissingEntity(AutoID).Contains(SpawnedLabel));

		// self, for a spawned owner with no Bango ID.
		AActor* Owner = World->SpawnActor<AActor>();
		const FBangoScriptOwner ScriptOwner = UBangoScriptSubsystem::GetScriptOwner(Owner);
		TestTrue(TEXT("any actor can be self"), ScriptOwner.Problem.IsEmpty() && Registry->FindActor(ScriptOwner.BangoName) == Owner);
		// The registry's version: the same while nothing is added or removed (asking again adds nothing), and higher after.
		const uint64 VersionBefore = Registry->GetVersion();
		TestTrue(TEXT("asking again gives the same name and changes nothing"),
			UBangoScriptSubsystem::GetScriptOwner(Owner).BangoName == ScriptOwner.BangoName && Registry->GetVersion() == VersionBefore);
		AActor* AnotherOwner = World->SpawnActor<AActor>();
		TestTrue(TEXT("a new entity raises the version"),
			UBangoScriptSubsystem::GetScriptOwner(AnotherOwner).Problem.IsEmpty() && Registry->GetVersion() > VersionBefore);
		const uint64 VersionWithBoth = Registry->GetVersion();
		Registry->Unregister(ScriptOwner.BangoName, Owner);
		TestTrue(TEXT("removing a name raises the version"), Registry->GetVersion() > VersionWithBoth);
		AnotherOwner->Destroy();
		Owner->Destroy();

		// ---- Components ----
		// One made by its actor's class or level (here: marked as on a startup actor): its actor's ID and its name.
		AActor* Door = World->SpawnActor<AActor>();
		Door->bNetStartup = true;
		USceneComponent* Root = NewObject<USceneComponent>(Door, TEXT("Root"));
		Door->SetRootComponent(Root);
		Root->RegisterComponent();
		USceneComponent* Trigger = NewObject<USceneComponent>(Door, TEXT("Trigger"));
		Trigger->SetIsNetStartupComponent(true);
		Trigger->SetupAttachment(Root);
		Trigger->RegisterComponent();
		TestTrue(TEXT("a component becomes an entity"), Bango::ObjectToEntity(Trigger, Entity, Error));
		const FString TriggerID = Entity.IsNil() ? FString() : Entity.GetEntityName();
		TestEqual(TEXT("its actor's ID and its name"), TriggerID, Bango::MakeComponentEntityID(Registry->GetOrMakeEntityID(Door, Error), TEXT("Trigger")));
		TestTrue(TEXT("found by it"), Registry->FindEntity(TriggerID) == Trigger && Registry->FindActor(TriggerID) == nullptr);
		TestEqual(TEXT("shown as its actor's and its name"), Entity.ToDisplayString(), FString::Printf(TEXT("%s (Trigger)"), *Door->GetActorNameOrLabel()));
		TestEqual(TEXT("the same ID again"), Registry->GetOrMakeEntityID(Trigger, Error), TriggerID);

		// Commands act on the component itself: its own place, not its actor's.
		FBangoCommandTable Commands;
		Bango::RegisterBuiltinCommands(Commands);
		auto Run = [&Commands, World](const TCHAR* Name, const FBangoValue* Target, TArray<FBangoValue> Args, FBangoCommandCall& OutCall)
		{
			OutCall = FBangoCommandCall();
			OutCall.WorldContext = World;
			OutCall.bHasTarget = Target != nullptr;
			if (Target)
			{
				OutCall.Target = *Target;
			}
			OutCall.Args = MoveTemp(Args);
			const FBangoCommand* Command = Commands.FindCommandImpl(Name);
			return Command && Command->Start(OutCall) == EBangoCommandResult::Done;
		};
		FBangoCommandCall Call;
		const FBangoValue TriggerEntity = FBangoValue::MakeEntity(TriggerID);
		TestTrue(TEXT("SetPos on a scene component"), Run(TEXT("SetPos"), &TriggerEntity, { FBangoValue::MakeVector3(100.0, 200.0, 300.0) }, Call));
		TestTrue(TEXT("it moved, and its actor didn't"), Trigger->GetComponentLocation().Equals(FVector(100.0, 200.0, 300.0)) && Door->GetActorLocation().IsNearlyZero());
		TestTrue(TEXT("GetPos on it"), Run(TEXT("GetPos"), nullptr, { TriggerEntity }, Call) && Call.Result.ToDebugString() == FBangoValue::MakeVector3(100.0, 200.0, 300.0).ToDebugString());
		TestTrue(TEXT("SetHidden on it"), Run(TEXT("SetHidden"), &TriggerEntity, { FBangoValue::MakeBool(true) }, Call) && Trigger->bHiddenInGame && !Door->IsHidden());

		// One with no place (not a scene component): a runtime error naming it and the command.
		UActorComponent* Plain = NewObject<UBangoPlainTestComponent>(Door, TEXT("Counter"));
		Plain->SetIsNetStartupComponent(true);
		Plain->RegisterComponent();
		TestTrue(TEXT("a plain component becomes an entity"), Bango::ObjectToEntity(Plain, Entity, Error));
		TestFalse(TEXT("SetPos on a component with no place fails"), Run(TEXT("SetPos"), &Entity, { FBangoValue::MakeVector3(0.0, 0.0, 0.0) }, Call));
		TestTrue(TEXT("saying why"), Call.Error.Contains(TEXT("(Counter)")) && Call.Error.Contains(TEXT("SetPos can't be used on it")));

		// Added while playing: an Automatic ID; one that keeps its own keeps it there.
		UActorComponent* AddedLater = NewObject<UBangoPlainTestComponent>(Door);
		AddedLater->RegisterComponent();
		const FString AddedID = Registry->GetOrMakeEntityID(AddedLater, Error);
		TestTrue(TEXT("a component added while playing gets an Automatic ID"), Bango::GetEntityIDKind(AddedID) == Bango::EBangoEntityIDKind::Automatic && Registry->FindEntity(AddedID) == AddedLater);
		UBangoEntityTestComponent* Keeper = NewObject<UBangoEntityTestComponent>(Door);
		Keeper->RegisterComponent();
		const FString KeptID = Registry->GetOrMakeEntityID(Keeper, Error);
		TestTrue(TEXT("one that keeps its ID keeps it"), Bango::GetEntityIDKind(KeptID) == Bango::EBangoEntityIDKind::Automatic && Keeper->EntityID == KeptID);
		{
			// Inline scripts are named for their owner's class; an error says which object and which of its scripts.
			TestEqual(TEXT("an inline script is named for its owner's class"), UBangoScriptSubsystem::GetInlineScriptName(Door), FString(TEXT("Actor (inline script)")));
			Keeper->OnOpened.Source = TEXT("WriteScreen(\"opened\");");
			Keeper->OnClosed.Source = TEXT("WriteScreen(\"closed\");");
			Keeper->Steps.AddDefaulted(2);
			Keeper->Steps[1].Source = TEXT("WriteScreen(\"step\");");
			const FString ByPlace = Bango::DescribeInlineScriptOwner(Door, &Keeper->OnOpened, nullptr);
			TestTrue(TEXT("which script: found by where it is, on the actor's component"),
				ByPlace.StartsWith(TEXT("on '")) && ByPlace.Contains(TEXT(".OnOpened'")) && !ByPlace.Contains(TEXT("OnClosed")));
			const FString ByText = Bango::DescribeInlineScriptOwner(Door, nullptr, &Keeper->OnClosed.Source);
			TestTrue(TEXT("which script: found by its text"), ByText.Contains(TEXT(".OnClosed'")) && !ByText.Contains(TEXT("OnOpened")));
			TestTrue(TEXT("which script: in an array"), Bango::DescribeInlineScriptOwner(Door, &Keeper->Steps[1], nullptr).Contains(TEXT(".Steps[1]'")));
			TestTrue(TEXT("which script: the component as the owner"), Bango::DescribeInlineScriptOwner(Keeper, &Keeper->OnOpened, nullptr).Contains(TEXT("its script 'OnOpened'")));
			const FBangoInlineScript Copied = Keeper->OnClosed;
			TestTrue(TEXT("which script: a copy is found by its text"), Bango::DescribeInlineScriptOwner(Door, &Copied, nullptr).Contains(TEXT(".OnClosed'")));
			Keeper->OnClosed.Source = Keeper->OnOpened.Source;
			const FString Both = Bango::DescribeInlineScriptOwner(Door, nullptr, &Keeper->OnOpened.Source);
			TestTrue(TEXT("which script: two of the same text are both named"), Both.Contains(TEXT(".OnOpened'")) && Both.Contains(TEXT(" or ")) && Both.Contains(TEXT(".OnClosed'")));
			const FString Elsewhere = TEXT("WriteScreen(\"nowhere\");");
			const FString OnlyObject = Bango::DescribeInlineScriptOwner(Door, nullptr, &Elsewhere);
			TestTrue(TEXT("which script: one that isn't the owner's only names the owner"), OnlyObject.StartsWith(TEXT("on '")) && !OnlyObject.Contains(TEXT("its script")));
			TestTrue(TEXT("which script: nothing without an owner"), Bango::DescribeInlineScriptOwner(nullptr, &Keeper->OnOpened, nullptr).IsEmpty());
		}
		{
			// Inline script IDs (persistent variables are kept under them): two the same in one actor, and one missing.
			Keeper->OnOpened.Source = TEXT("WriteScreen(\"opened\");");
			Keeper->OnClosed.Source = TEXT("WriteScreen(\"closed\");");
			Keeper->OnOpened.ScriptId = FGuid::NewGuid();
			Keeper->OnClosed.ScriptId = FGuid::NewGuid();
			Keeper->Steps[0].Source.Reset();
			Keeper->Steps[0].ScriptId = FGuid();
			Keeper->Steps[1].ScriptId = FGuid::NewGuid();
			const TArray<UObject*> Unit = Bango::GetInlineScriptIdUnit(Door);
			TestTrue(TEXT("ids: the unit is the actor and its components"), Unit.Num() > 1 && Unit[0] == Door && Unit.Contains(Keeper));
			TestEqual(TEXT("ids: all different, none missing: no problems"), Bango::FindInlineScriptIdProblems(Unit, true).Num(), 0);
			TestTrue(TEXT("ids: a script is found by where it is"), Bango::FindInlineScriptAt(Keeper, TEXT("Steps[1]")) == &Keeper->Steps[1]);
			TestNull(TEXT("ids: nothing where there's none"), Bango::FindInlineScriptAt(Keeper, TEXT("Steps[7]")));

			Keeper->Steps[1].ScriptId = Keeper->OnOpened.ScriptId; // copied from it
			TArray<Bango::FBangoInlineScriptIdProblem> Problems = Bango::FindInlineScriptIdProblems(Unit, true);
			TestTrue(TEXT("ids: a copied ID is found, with the one it was copied from"), Problems.Num() == 1 && !Problems[0].bMissing
				&& Problems[0].Object == Keeper && Problems[0].Path == TEXT("Steps[1]") && Problems[0].OtherObject == Keeper && Problems[0].OtherPath == TEXT("OnOpened"));

			Keeper->Steps[1].ScriptId = FGuid(); // text and no ID
			Problems = Bango::FindInlineScriptIdProblems(Unit, true);
			TestTrue(TEXT("ids: a missing ID is found (an empty script without one isn't)"), Problems.Num() == 1 && Problems[0].bMissing && Problems[0].Path == TEXT("Steps[1]"));
			TestEqual(TEXT("ids: and not looked for without bMissing"), Bango::FindInlineScriptIdProblems(Unit, false).Num(), 0);
		}
		// As if loaded from a save: a new component holding that ID, registered as it's initialized, is found by it.
		Keeper->DestroyComponent();
		TestNull(TEXT("a removed component isn't found"), Registry->FindEntity(KeptID));
		UBangoEntityTestComponent* Loaded = NewObject<UBangoEntityTestComponent>(Door);
		Loaded->EntityID = KeptID;
		Loaded->RegisterComponent();
		Bango::RegisterEntity(Loaded);
		TestTrue(TEXT("a loaded one is found by the ID it kept"), Registry->FindEntity(KeptID) == Loaded);

		// call on a component, and a component a function returns.
		FBangoValue CallResult;
		FString CallError;
		TestTrue(TEXT("call on a component"), Bango::CallBangoCallable(World, FBangoValue::MakeEntity(KeptID), TEXT("Poke"), { FBangoValue::MakeInt(3) }, false, CallResult, CallError));
		TestEqual(TEXT("it ran on it"), Loaded->Pokes, 3);

		// Persistent variables: kept for each inline script (its ID) and for the component holding it.
		{
			UBangoScriptSubsystem* Scripts = World->GetSubsystem<UBangoScriptSubsystem>();
			UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(World);
			if (TestNotNull(TEXT("persistent: script subsystem"), Scripts) && TestNotNull(TEXT("persistent: globals"), Globals))
			{
				FBangoInlineScript Counting;
				Counting.Source = TEXT("persistent int _n ?= 0;\n_n++;");
				Counting.ScriptId = FGuid::NewGuid();
				FBangoInlineScript OtherCounting = Counting;
				OtherCounting.ScriptId = FGuid::NewGuid();
				const FString DoorID = Registry->GetOrMakeEntityID(Door, Error);
				auto KeptFor = [&](const FBangoInlineScript& Script, const FString& Holder) -> int64
				{
					TSharedPtr<const FBangoProgram> CountProgram;
					FString CountError;
					if (!FBangoModule::Get().GetInlineProgram(Script, UBangoScriptSubsystem::GetInlineScriptName(Door), CountProgram, CountError) || CountProgram->PersistentLocals.Num() != 1)
					{
						return -2;
					}
					FBangoInstance KeyInstance;
					FBangoScriptOwner KeyOwner;
					KeyOwner.BangoName = DoorID;
					KeyOwner.Holder = Holder;
					KeyInstance.SetOwner(KeyOwner);
					KeyInstance.SetPersistentScope(UBangoScriptSubsystem::GetInlineScriptScope(Script));
					const FBangoValue* Kept = Globals->GetStore().Find(KeyInstance.GetPersistentKey(*CountProgram, CountProgram->PersistentLocals[0]));
					return Kept && !Kept->IsNil() ? Kept->GetInt() : -1;
				};
				Scripts->Spawn(Counting, TArray<FBangoValue>(), Door);
				Scripts->Spawn(Counting, TArray<FBangoValue>(), Door);
				Scripts->Spawn(OtherCounting, TArray<FBangoValue>(), Door);
				TestEqual(TEXT("persistent: kept for the script on its actor"), KeptFor(Counting, FString()), (int64)2);
				TestEqual(TEXT("persistent: another inline script on the same actor has its own"), KeptFor(OtherCounting, FString()), (int64)1);
				Scripts->Spawn(Counting, TArray<FBangoValue>(), Trigger);
				TestEqual(TEXT("persistent: a component as the owner has its own"), KeptFor(Counting, TriggerID), (int64)1);
				FBangoInlineScript Held = Counting;
				Held.SetHolder(Trigger);
				Scripts->Spawn(Held, TArray<FBangoValue>(), Door);
				TestEqual(TEXT("persistent: the holder hint keeps it for the component, with the actor as the owner"), KeptFor(Counting, TriggerID), (int64)2);
				TestEqual(TEXT("persistent: and not for the actor"), KeptFor(Counting, FString()), (int64)2);
				// No ID: kept for its text, which an edit changes.
				FBangoInlineScript Unnamed;
				Unnamed.Source = Counting.Source;
				TestTrue(TEXT("persistent: no ID, kept for its text"), UBangoScriptSubsystem::GetInlineScriptScope(Unnamed).StartsWith(TEXT("text "))
					&& UBangoScriptSubsystem::GetInlineScriptScope(Unnamed) == UBangoScriptSubsystem::GetTextScope(Counting.Source)
					&& UBangoScriptSubsystem::GetInlineScriptScope(Unnamed) != UBangoScriptSubsystem::GetTextScope(Counting.Source + TEXT(" ")));
				Scripts->Spawn(Unnamed, TArray<FBangoValue>(), Door);
				TestEqual(TEXT("persistent: an unnamed script runs, kept for its text"), KeptFor(Unnamed, FString()), (int64)1);
				TestTrue(TEXT("persistent: an ID is its scope"), UBangoScriptSubsystem::GetInlineScriptScope(Counting).Contains(Counting.ScriptId.ToString(EGuidFormats::Digits)));

				// Blueprint's Set Bango Global: Persistent declares a persistent global; one that exists keeps its kind.
				TestTrue(TEXT("persistent: Blueprint sets a persistent global"), UBangoBlueprintLibrary::SetBangoGlobalInt(World, TEXT("bpGold"), 3, true)
					&& UBangoBlueprintLibrary::IsBangoGlobalPersistent(World, TEXT("bpGold")));
				TestTrue(TEXT("persistent: setting it again without Persistent keeps its kind"), UBangoBlueprintLibrary::SetBangoGlobalInt(World, TEXT("bpGold"), 4)
					&& UBangoBlueprintLibrary::IsBangoGlobalPersistent(World, TEXT("bpGold")));
				TestTrue(TEXT("persistent: without it, a world global"), UBangoBlueprintLibrary::SetBangoGlobalInt(World, TEXT("bpStage"), 1)
					&& !UBangoBlueprintLibrary::IsBangoGlobalPersistent(World, TEXT("bpStage")) && !UBangoBlueprintLibrary::IsBangoGlobalPersistent(World, TEXT("bpNothing")));
			}
		}

		// Gone: not found, and the error says which.
		Trigger->DestroyComponent();
		TestNull(TEXT("a removed component isn't found"), Registry->FindEntity(TriggerID));
		TestTrue(TEXT("the error names it"), Bango::DescribeMissingEntity(TriggerID).Contains(TEXT("(Trigger)")) && Bango::DescribeMissingEntity(TriggerID).Contains(TEXT("isn't there any more")));
	}

	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	return true;
}

// A text value keeps its key and namespace (which BangoYap takes a line's ID from) through the VM, copies, saving and
// loading; INVTEXT and ToText give none.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoTextKeyTest, "Bango.Game.TextKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBangoTextKeyTest::RunTest(const FString& Parameters)
{
	FBangoCommandTable Commands;
	Bango::RegisterBuiltinCommands(Commands);
	TArray<FText> Kept;
	FBangoCommand Keep;
	Keep.Signature = FBangoCommandSignature::Make(TEXT("Keep")).Param(TEXT("Line"), FBangoType::Text());
	Keep.Start = [&Kept](FBangoCommandCall& Call)
	{
		Kept.Add(Call.Args[0].GetText());
		return EBangoCommandResult::Done;
	};
	FString AddError;
	TestTrue(TEXT("text keys: Keep is added"), Commands.Add(Keep, AddError));

	const TCHAR* Source =
		TEXT("#LOCTEXT_NAMESPACE \"Yap.Test\"\n")
		TEXT("global text heard;\n")
		TEXT("Keep(LOCTEXT(\"UXA010\", \"Hello\"));\n")
		TEXT("text _t = LOCTEXT(\"UXA011\", \"Hi\");\n")
		TEXT("text[] _list = [_t];\n")
		TEXT("Keep(_list[0]);\n")
		TEXT("Keep(NSLOCTEXT(\"Other\", \"K2\", \"There\"));\n")
		TEXT("Keep(INVTEXT(\"plain\"));\n")
		TEXT("Keep(ToText(\"made\"));\n")
		TEXT("heard = _t;\n")
		TEXT("Sleep(1);\n")
		TEXT("Keep(_t);\n");
	TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	if (!TestTrue(FString::Printf(TEXT("text keys: compiles: %s"), *CompileError.ToString()), Bango::CompileSource(Source, TEXT("textkeys.bango"), Commands, *Program, CompileError)))
	{
		return false;
	}
	FBangoGlobalStore Globals;
	FBangoInstance Instance;
	FBangoRuntimeError StartError;
	if (!TestTrue(TEXT("text keys: starts"), Instance.Start(Program, Commands, Globals, 1, StartError)))
	{
		return false;
	}
	FBangoExecContext Context;
	Context.Globals = &Globals;
	Context.DeltaSeconds = 0.25f;
	Instance.Resume(Context);
	TestEqual(TEXT("text keys: five texts before the sleep"), Kept.Num(), 5);

	auto KeyOf = [](const FText& Text) { const TOptional<FString> Key = FTextInspector::GetKey(Text); return Key.IsSet() ? Key.GetValue() : FString(TEXT("<none>")); };
	auto NamespaceOf = [](const FText& Text) { const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text); return Namespace.IsSet() ? Namespace.GetValue() : FString(TEXT("<none>")); };
	if (Kept.Num() == 5)
	{
		TestEqual(TEXT("text keys: LOCTEXT's key"), KeyOf(Kept[0]), FString(TEXT("UXA010")));
		TestEqual(TEXT("text keys: LOCTEXT's namespace is the script's"), NamespaceOf(Kept[0]), FString(TEXT("Yap.Test")));
		TestEqual(TEXT("text keys: through a local and an array"), KeyOf(Kept[1]), FString(TEXT("UXA011")));
		TestEqual(TEXT("text keys: through a local and an array, its namespace"), NamespaceOf(Kept[1]), FString(TEXT("Yap.Test")));
		TestEqual(TEXT("text keys: NSLOCTEXT's key"), KeyOf(Kept[2]), FString(TEXT("K2")));
		TestEqual(TEXT("text keys: NSLOCTEXT's namespace"), NamespaceOf(Kept[2]), FString(TEXT("Other")));
		TestTrue(FString::Printf(TEXT("text keys: INVTEXT has no key (%s)"), *KeyOf(Kept[3])), KeyOf(Kept[3]) == TEXT("<none>") || KeyOf(Kept[3]).IsEmpty());
		TestTrue(FString::Printf(TEXT("text keys: ToText has no key (%s)"), *KeyOf(Kept[4])), KeyOf(Kept[4]) == TEXT("<none>") || KeyOf(Kept[4]).IsEmpty());
		TestEqual(TEXT("text keys: and its string"), Kept[4].ToString(), FString(TEXT("made")));
	}

	// Saved and loaded: a local the script is waiting with, and a global.
	TArray<uint8> InstanceBytes;
	TArray<uint8> GlobalBytes;
	{
		FMemoryWriter InstanceWriter(InstanceBytes, true);
		Instance.Serialize(InstanceWriter);
		FMemoryWriter GlobalWriter(GlobalBytes, true);
		Globals.Serialize(GlobalWriter);
	}
	FBangoGlobalStore LoadedGlobals;
	FMemoryReader GlobalReader(GlobalBytes, true);
	LoadedGlobals.Serialize(GlobalReader);
	FBangoInstance Loaded;
	FMemoryReader InstanceReader(InstanceBytes, true);
	Loaded.Serialize(InstanceReader);
	FString BindError;
	TestTrue(FString::Printf(TEXT("text keys: the loaded script binds: %s"), *BindError), Loaded.Bind(Program, Commands, BindError));
	const FBangoValue* Heard = LoadedGlobals.Find(TEXT("heard"));
	TestTrue(TEXT("text keys: a loaded global keeps its key"), Heard && !Heard->IsNil() && KeyOf(Heard->GetText()) == TEXT("UXA011") && NamespaceOf(Heard->GetText()) == TEXT("Yap.Test"));
	Context.Globals = &LoadedGlobals;
	Context.DeltaSeconds = 2.0f;
	for (int32 Step = 0; Step < 4 && Loaded.IsRunning(); ++Step)
	{
		Loaded.Resume(Context);
	}
	TestEqual(TEXT("text keys: the loaded script keeps its text"), Kept.Num(), 6);
	if (Kept.Num() == 6)
	{
		TestEqual(TEXT("text keys: a loaded local keeps its key"), KeyOf(Kept[5]), FString(TEXT("UXA011")));
		TestEqual(TEXT("text keys: a loaded local keeps its namespace"), NamespaceOf(Kept[5]), FString(TEXT("Yap.Test")));
		TestEqual(TEXT("text keys: and its source"), Kept[5].ToString(), FString(TEXT("Hi")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
