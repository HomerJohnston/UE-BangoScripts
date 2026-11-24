// The benchmark cases and their runners (BangoScriptsBenchmark.h). Nothing here needs a world or an engine object.

#include "BangoScriptsBenchmark.h"
#include "BangoCommand.h"
#include "BangoCompiler.h"
#include "BangoGlobalStore.h"
#include "BangoInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace BangoBenchmarkPrivate
{
	// ---- The C++ versions ----
	// Each does what its script does, in a function of its own that the compiler can't fold into its caller, with
	// the pass count read through a volatile so the loop can't be worked out when compiling. A loop with nothing in
	// it would be removed altogether, so the simplest ones write their counter to a volatile each pass: C++ doing
	// the least it can be made to do, which is the fairest thing to hold a script's empty loop against.

	static volatile int64 GSink = 0;

	FORCENOINLINE static int32 AddOne(int32 Value)
	{
		return Value + 1;
	}

	FORCENOINLINE static double CppEmptyLoop(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		int64 i = 0;
		while (i < N)
		{
			++i;
			GSink = i;
		}
		return (double)i;
	}

	FORCENOINLINE static double CppIntMath(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		int64 Sum = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Sum = (Sum + i * 3) % 1000003;
		}
		return (double)Sum;
	}

	FORCENOINLINE static double CppFloatMath(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		double X = 0.0;
		for (int64 i = 0; i < N; ++i)
		{
			X = X * 0.5 + (double)i * 0.25;
		}
		return X;
	}

	FORCENOINLINE static double CppGlobalReadWrite(int64 Passes)
	{
		// A script's global is looked up in the game's store; C++'s nearest thing is a variable.
		volatile int64 Count = Passes;
		const int64 N = Count;
		static volatile int64 Counter = 0;
		Counter = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Counter = Counter + 1;
		}
		return (double)Counter;
	}

	FORCENOINLINE static double CppFunctionCall(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		int32 Sum = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Sum = AddOne(Sum);
		}
		return (double)Sum;
	}

	FORCENOINLINE static double CppNativeCall(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		int32 Sum = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Sum = Bango::BenchmarkAdd(Sum, 1);
		}
		return (double)Sum;
	}

	FORCENOINLINE static double CppArrayPushSum(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		TArray<int32> List;
		for (int64 i = 0; i < N; ++i)
		{
			List.Add((int32)i);
		}
		int64 Sum = 0;
		for (const int32 Value : List)
		{
			Sum = (Sum + Value) % 1000003;
		}
		return (double)Sum;
	}

	// ---- A value on its own ----
	// What the VM does with a value on nearly every instruction (make one, copy it, move it, drop it), without the VM:
	// what FBangoValue itself costs. The slots are picked through a volatile so the copies can't be compiled away.

	FORCENOINLINE static double CppValueInt(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		volatile int32 First = 0;
		volatile int32 Second = 1;
		FBangoValue Slots[2];
		int64 Sum = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Slots[First] = FBangoValue::MakeInt((int32)(i & 0xFFFF));
			FBangoValue Copy = Slots[First];
			Slots[Second] = MoveTemp(Copy);
			Sum += Slots[Second].GetInt();
		}
		return (double)(Sum % 1000003);
	}

	FORCENOINLINE static double CppValueString(int64 Passes)
	{
		volatile int64 Count = Passes;
		const int64 N = Count;
		volatile int32 First = 0;
		volatile int32 Second = 1;
		FBangoValue Slots[2];
		const FBangoValue Text = FBangoValue::MakeString(TEXT("guard"));
		int64 Sum = 0;
		for (int64 i = 0; i < N; ++i)
		{
			Slots[First] = Text;
			FBangoValue Copy = Slots[First];
			Slots[Second] = MoveTemp(Copy);
			Sum += Slots[Second].GetString().Len();
		}
		return (double)(Sum % 1000003);
	}

	// ---- The cases ----

	enum class ECaseKind : uint8
	{
		/** C++ only (no script, no Blueprint): Cpp is all there is. */
		CppOnly,
		/** One script, run once to its end with its pass count as its input; its answer is the global benchResult. */
		Loop,
		/** Starting, running and finishing Iterations tiny scripts. */
		StartScripts,
		/** One frame's check of Iterations scripts paused at a waitUntil whose global hasn't changed. */
		WaitingIdle,
		/** Setting that global and resuming them all, to their end. */
		WaitingWake,
		/** Starting Iterations copies of a script, without running them. */
		StartOnly,
		/** Compiling a script's text Iterations times. */
		Compile,
		/** Spawning a script Iterations times through the game's script subsystem (the options' RunInGame): the whole way a game starts one. */
		InGame,
	};

	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Description;
		ECaseKind Kind;
		/** Its passes are the options' Iterations divided by this, and no more than MaxPasses (0: no limit). */
		int32 Divisor;
		int32 MaxPasses;
		const TCHAR* Source;
		double (*Cpp)(int64);
	};

	/** A script of an ordinary size and mix (three globals with defaults, some forty lines, five commands called from thirty places, a function), for the cases that start or compile one. */
	static const TCHAR* const GOrdinaryScript =
		TEXT("global int benchScore ?= 0;\nglobal bool benchFlag ?= false;\nglobal float benchTime ?= 1.5;\nint _x = 1;\nint[] _list = [];\n_x = BenchAdd(_x, 1);\n_x = BenchAdd(_x, 2);\n_x = BenchAdd(_x, 3);\n_list Push(_x);\n_x = BenchAdd(_x, 4);\n_x = BenchAdd(_x, 5);\n_x = BenchAdd(_x, 6);\nif (_x > 1000000)\n{\n\tbenchScore = benchScore + Helper(_x);\n}\n_x = BenchAdd(_x, 7);\n_x = BenchAdd(_x, 8);\n_x = BenchAdd(_x, 9);\n_list Push(_x);\n_x = BenchAdd(_x, 10);\n_x = BenchAdd(_x, 11);\n_x = BenchAdd(_x, 12);\n_x = BenchAdd(_x, 13);\n_x = BenchAdd(_x, 14);\nif (_x > 1000000)\n{\n\tbenchScore = benchScore + Helper(_x);\n}\n_x = BenchAdd(_x, 15);\n_list Push(_x);\n_x = BenchAdd(_x, 16);\n_x = BenchAdd(_x, 17);\n_x = BenchAdd(_x, 18);\n_x = BenchAdd(_x, 19);\n_x = BenchAdd(_x, 20);\n_x = BenchAdd(_x, 21);\n_list Push(_x);\n_x = BenchAdd(_x, 22);\nif (_x > 1000000)\n{\n\tbenchScore = benchScore + Helper(_x);\n}\n_x = BenchAdd(_x, 23);\n_x = BenchAdd(_x, 24);\nfor (int _i = 0; _i < Count(_list); _i++)\n{\n\tbenchScore = benchScore + _list[_i];\n}\nint _last = _list Pop();\nbenchFlag = _last > 0;\nbenchTime = benchTime + 0.5;\nbenchScore = benchScore + Count(_list);\n\nfunction int Helper(int _value)\n{\n\tint _twice = _value * 2;\n\treturn BenchAdd(_twice, 1);\n}\n");

	/** A script as long as an ordinary one that runs next to nothing (its block is skipped), for the cases that spawn one: what's timed is getting it started. It uses no global, as it runs in the game's own store. */
	static const TCHAR* const GLongQuickScript =
		TEXT("int _x = 1;\nif (_x > 1)\n{\n\tint[] _list = [];\n\t_x = (_x * 3 + 1) % 1000003;\n\t_x = (_x * 3 + 2) % 1000003;\n\t_x = (_x * 3 + 3) % 1000003;\n\t_x = (_x * 3 + 4) % 1000003;\n\t_x = (_x * 3 + 5) % 1000003;\n\t_list Push(_x);\n\t_x = (_x * 3 + 6) % 1000003;\n\t_x = (_x * 3 + 7) % 1000003;\n\t_x = (_x * 3 + 8) % 1000003;\n\t_x = (_x * 3 + 9) % 1000003;\n\t_x = (_x * 3 + 10) % 1000003;\n\t_x = (_x * 3 + 11) % 1000003;\n\t_x = (_x * 3 + 12) % 1000003;\n\t_x = (_x * 3 + 13) % 1000003;\n\t_x = (_x * 3 + 14) % 1000003;\n\t_x = (_x * 3 + 15) % 1000003;\n\t_list Push(_x);\n\t_x = (_x * 3 + 16) % 1000003;\n\t_x = (_x * 3 + 17) % 1000003;\n\t_x = (_x * 3 + 18) % 1000003;\n\t_x = (_x * 3 + 19) % 1000003;\n\t_x = (_x * 3 + 20) % 1000003;\n\t_x = (_x * 3 + 21) % 1000003;\n\t_x = (_x * 3 + 22) % 1000003;\n\t_x = (_x * 3 + 23) % 1000003;\n\t_x = (_x * 3 + 24) % 1000003;\n\t_x = (_x * 3 + 25) % 1000003;\n\t_list Push(_x);\n\t_x = (_x * 3 + 26) % 1000003;\n\t_x = (_x * 3 + 27) % 1000003;\n\t_x = (_x * 3 + 28) % 1000003;\n\t_x = (_x * 3 + 29) % 1000003;\n\t_x = (_x * 3 + 30) % 1000003;\n\t_x = _x + Count(_list);\n}\n_x = _x * 2;\n");

	static const FCase GCases[] =
	{
		{ TEXT("EmptyLoop"), TEXT("A loop that only counts: what one pass of any loop costs."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nint _i = 0;\nwhile (_i < _n)\n{\n\t_i++;\n}\nbenchResult = _i;\n"),
			&CppEmptyLoop },
		{ TEXT("IntMath"), TEXT("Whole-number arithmetic on locals: +, * and %."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nint _sum = 0;\nfor (int _i = 0; _i < _n; _i++)\n{\n\t_sum = (_sum + _i * 3) % 1000003;\n}\nbenchResult = _sum;\n"),
			&CppIntMath },
		{ TEXT("FloatMath"), TEXT("Float arithmetic on locals, with an int turned into a float each pass."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nfloat _x = 0.0;\nfor (int _i = 0; _i < _n; _i++)\n{\n\t_x = _x * 0.5 + _i * 0.25;\n}\nbenchResult = _x;\n"),
			&CppFloatMath },
		{ TEXT("GlobalReadWrite"), TEXT("Reading and writing a global each pass (C++ and Blueprint: a variable)."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nglobal int benchCounter;\nbenchCounter = 0;\nfor (int _i = 0; _i < _n; _i++)\n{\n\tbenchCounter = benchCounter + 1;\n}\nbenchResult = benchCounter;\n"),
			&CppGlobalReadWrite },
		{ TEXT("FunctionCall"), TEXT("Calling one of the script's own functions each pass."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nint _sum = 0;\nfor (int _i = 0; _i < _n; _i++)\n{\n\t_sum = AddOne(_sum);\n}\nbenchResult = _sum;\n\nfunction int AddOne(int _value)\n{\n\treturn _value + 1;\n}\n"),
			&CppFunctionCall },
		{ TEXT("NativeCall"), TEXT("Calling a C++ command each pass: the way from a script into native code and back."), ECaseKind::Loop, 1, 0,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nint _sum = 0;\nfor (int _i = 0; _i < _n; _i++)\n{\n\t_sum = BenchAdd(_sum, 1);\n}\nbenchResult = _sum;\n"),
			&CppNativeCall },
		{ TEXT("ArrayPushSum"), TEXT("Adding to an array one element at a time, then a forEach over it."), ECaseKind::Loop, 10, 1000000,
			TEXT("#inputs (int _n)\nglobal float benchResult;\nint[] _list = [];\nfor (int _i = 0; _i < _n; _i++)\n{\n\t_list Push(_i);\n}\nint _sum = 0;\nforEach (_value in _list)\n{\n\t_sum = (_sum + _value) % 1000003;\n}\nbenchResult = _sum;\n"),
			&CppArrayPushSum },
		{ TEXT("ValueInt"), TEXT("C++ only: making an int value, copying it and moving it, as the VM does on nearly every instruction. What one value costs."), ECaseKind::CppOnly, 1, 0,
			TEXT(""),
			&CppValueInt },
		{ TEXT("ValueString"), TEXT("C++ only: the same with a short string value, which has contents of its own to copy."), ECaseKind::CppOnly, 1, 0,
			TEXT(""),
			&CppValueString },
		{ TEXT("StartScripts"), TEXT("Starting a three-line script, running it and letting it finish, once per iteration."), ECaseKind::StartScripts, 100, 0,
			TEXT("int _x = 1;\n_x++;\n_x = _x * 2;\n"),
			nullptr },
		{ TEXT("StartOrdinary"), TEXT("Starting an ordinary script (three globals, forty lines, commands called from thirty places) without running it: what a script costs before its first line."), ECaseKind::StartOnly, 100, 0,
			GOrdinaryScript,
			nullptr },
		{ TEXT("CompileOrdinary"), TEXT("Compiling that script's text: reading, checking and compiling it, once per iteration."), ECaseKind::Compile, 1000, 0,
			GOrdinaryScript,
			nullptr },
		{ TEXT("SpawnInline"), TEXT("In a game only: spawning a three-line inline script through the script subsystem, as a trigger does, and letting it finish. StartScripts is the same script without the subsystem."), ECaseKind::InGame, 100, 0,
			TEXT("int _x = 1;\n_x++;\n_x = _x * 2;\n"),
			nullptr },
		{ TEXT("SpawnInlineLong"), TEXT("In a game only: the same with an inline script of some forty lines that runs four of them: what the script's length adds to a spawn."), ECaseKind::InGame, 100, 0,
			GLongQuickScript,
			nullptr },
		{ TEXT("SpawnAsset"), TEXT("In a game only: spawning that forty-line script as a Bango Script asset."), ECaseKind::InGame, 100, 0,
			GLongQuickScript,
			nullptr },
		{ TEXT("WaitingIdle"), TEXT("One frame's cost of a script paused at a waitUntil whose global hasn't changed, per script."), ECaseKind::WaitingIdle, 100, 0,
			TEXT("global bool benchGate ?= false;\nwaitUntil (benchGate);\n"),
			nullptr },
		{ TEXT("WaitingWake"), TEXT("The global changes: checking and carrying on every script paused at its waitUntil, per script."), ECaseKind::WaitingWake, 100, 0,
			TEXT("global bool benchGate ?= false;\nwaitUntil (benchGate);\n"),
			nullptr },
	};

	static int32 GetPasses(const FCase& Case, const FBangoBenchmarkOptions& Options)
	{
		int32 Passes = FMath::Max(1, Options.Iterations / FMath::Max(1, Case.Divisor));
		if (Case.MaxPasses > 0)
		{
			Passes = FMath::Min(Passes, Case.MaxPasses);
		}
		return Passes;
	}

	/** The benchmark's own commands: the ones every table has, and BenchAdd. */
	static bool MakeCommands(FBangoCommandTable& Commands, FString& OutError)
	{
		if (!Bango::RegisterCoreCommands(Commands, OutError))
		{
			return false;
		}
		FBangoCommand Command;
		Command.Signature = FBangoCommandSignature::Make(TEXT("BenchAdd")).Param(TEXT("A"), FBangoType::Int()).Param(TEXT("B"), FBangoType::Int()).Returns(FBangoType::Int());
		Command.Start = [](FBangoCommandCall& Call)
		{
			if (Call.Args.Num() < 2 || Call.Args[0].IsNil() || Call.Args[1].IsNil())
			{
				return Call.Fail(TEXT("BenchAdd needs two ints."));
			}
			Call.Result = FBangoValue::MakeInt(Bango::BenchmarkAdd(Call.Args[0].GetInt(), Call.Args[1].GetInt()));
			return EBangoCommandResult::Done;
		};
		return Commands.Add(Command, OutError);
	}

	/** Keeps the best and the median of the repeats' times. */
	static void SetTimes(FBangoBenchmarkResult& Result, TArray<double>& Seconds)
	{
		if (Seconds.Num() == 0)
		{
			return;
		}
		Seconds.Sort();
		Result.BestSeconds = Seconds[0];
		Result.MedianSeconds = Seconds[Seconds.Num() / 2];
	}

	static FBangoExecContext MakeContext(FBangoGlobalStore& Globals)
	{
		FBangoExecContext Context;
		Context.Globals = &Globals;
		Context.MaxInstructionsPerResume = MAX_int32; // these loops are long on purpose
		return Context;
	}

	static void RunBango(const FCase& Case, const FBangoBenchmarkOptions& Options, const FBangoCommandTable& Commands, FBangoBenchmarkResult& Result)
	{
		const int32 Passes = GetPasses(Case, Options);
		Result.Iterations = Passes;

		if (Case.Kind == ECaseKind::InGame)
		{
			// The game's subsystem compiles and runs it (with the game's commands, not the benchmark's): timed by whoever has a world.
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(FString(TEXT("Bango.Benchmark ")) + Case.Name + TEXT(" Bango")));
			TArray<double> GameSeconds;
			for (int32 Repeat = 0; Repeat < FMath::Max(1, Options.Repeats); ++Repeat)
			{
				double OneRun = 0.0;
				if (!Options.RunInGame || !Options.RunInGame(Case.Name, Case.Source, Passes, OneRun, Result.Error))
				{
					if (Result.Error.IsEmpty())
					{
						Result.Error = TEXT("It couldn't be run in the game.");
					}
					return;
				}
				GameSeconds.Add(OneRun);
			}
			Result.Checksum = (double)Passes;
			SetTimes(Result, GameSeconds);
			Result.bRan = true;
			return;
		}

		const TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
		FBangoCompileError CompileError;
		const double CompileStart = FPlatformTime::Seconds();
		if (!Bango::CompileSource(Case.Source, FString(Case.Name) + TEXT(".bango"), Commands, *Program, CompileError))
		{
			Result.Error = FString(TEXT("Its script doesn't compile: ")) + CompileError.ToString();
			return;
		}
		Result.CompileSeconds = FPlatformTime::Seconds() - CompileStart;

		// One trace scope per case and runner, named for them ("Bango.Benchmark IntMath Bango"): all its repeats.
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(FString(TEXT("Bango.Benchmark ")) + Case.Name + TEXT(" Bango")));
		const int32 Repeats = FMath::Max(1, Options.Repeats);
		TArray<double> Seconds;
		for (int32 Repeat = 0; Repeat < Repeats; ++Repeat)
		{
			FBangoGlobalStore Globals;
			const FBangoExecContext Context = MakeContext(Globals);
			FBangoRuntimeError RunError;
			if (Case.Kind == ECaseKind::Loop)
			{
				FBangoInstance Instance;
				Instance.SetRecordTrail(Options.bRecordTrail);
				TArray<FBangoValue> Args;
				Args.Add(FBangoValue::MakeInt(Passes));
				if (!Instance.Start(Program, Commands, Globals, 1, RunError, Args))
				{
					Result.Error = FString(TEXT("Its script didn't start: ")) + RunError.Message;
					return;
				}
				const double Start = FPlatformTime::Seconds();
				const EBangoInstanceStatus Status = Instance.Resume(Context);
				Seconds.Add(FPlatformTime::Seconds() - Start);
				if (Status != EBangoInstanceStatus::Finished)
				{
					Result.Error = FString(TEXT("Its script didn't finish: ")) + Instance.GetError().Message;
					return;
				}
				Result.Instructions = Instance.GetInstructionsRun();
				const FBangoValue* Answer = Globals.Find(TEXT("benchResult"));
				Result.Checksum = Answer && !Answer->IsNil() ? Answer->GetDouble() : 0.0;
			}
			else if (Case.Kind == ECaseKind::StartOnly)
			{
				const double Start = FPlatformTime::Seconds();
				for (int32 Index = 0; Index < Passes; ++Index)
				{
					FBangoInstance Instance;
					if (!Instance.Start(Program, Commands, Globals, Index + 1, RunError))
					{
						Result.Error = FString(TEXT("A script didn't start: ")) + RunError.Message;
						return;
					}
				}
				Seconds.Add(FPlatformTime::Seconds() - Start);
				Result.Checksum = (double)Passes;
			}
			else if (Case.Kind == ECaseKind::Compile)
			{
				const FString Source = Case.Source;
				const FString FileName = FString(Case.Name) + TEXT(".bango");
				const double Start = FPlatformTime::Seconds();
				for (int32 Index = 0; Index < Passes; ++Index)
				{
					FBangoProgram Compiled;
					if (!Bango::CompileSource(Source, FileName, Commands, Compiled, CompileError))
					{
						Result.Error = FString(TEXT("Its script doesn't compile: ")) + CompileError.ToString();
						return;
					}
				}
				Seconds.Add(FPlatformTime::Seconds() - Start);
				Result.Checksum = (double)Passes;
			}
			else if (Case.Kind == ECaseKind::StartScripts)
			{
				uint64 Instructions = 0;
				const double Start = FPlatformTime::Seconds();
				for (int32 Index = 0; Index < Passes; ++Index)
				{
					FBangoInstance Instance;
					Instance.SetRecordTrail(Options.bRecordTrail);
					if (!Instance.Start(Program, Commands, Globals, Index + 1, RunError) || Instance.Resume(Context) != EBangoInstanceStatus::Finished)
					{
						Result.Error = FString(TEXT("A script didn't run to its end: ")) + RunError.Message + Instance.GetError().Message;
						return;
					}
					Instructions += Instance.GetInstructionsRun();
				}
				Seconds.Add(FPlatformTime::Seconds() - Start);
				Result.Instructions = Instructions;
				Result.Checksum = (double)Passes;
			}
			else
			{
				// Scripts paused at a waitUntil, as the script subsystem keeps them: each frame it asks each one whether
				// its condition may have changed (IsWaitDue), and resumes the ones that say yes.
				TArray<TSharedPtr<FBangoInstance>> Waiting;
				Waiting.Reserve(Passes);
				for (int32 Index = 0; Index < Passes; ++Index)
				{
					TSharedPtr<FBangoInstance> Instance = MakeShared<FBangoInstance>();
					Instance->SetRecordTrail(Options.bRecordTrail);
					if (!Instance->Start(Program, Commands, Globals, Index + 1, RunError) || Instance->Resume(Context) != EBangoInstanceStatus::Waiting)
					{
						Result.Error = FString(TEXT("A script didn't pause at its waitUntil: ")) + RunError.Message + Instance->GetError().Message;
						return;
					}
					Waiting.Add(Instance);
				}
				if (Case.Kind == ECaseKind::WaitingIdle)
				{
					int32 Due = 0;
					const double Start = FPlatformTime::Seconds();
					for (const TSharedPtr<FBangoInstance>& Instance : Waiting)
					{
						Due += Instance->IsWaitDue(Globals) ? 1 : 0;
					}
					Seconds.Add(FPlatformTime::Seconds() - Start);
					if (Due != 0)
					{
						Result.Error = TEXT("Scripts asked to be checked though nothing had changed.");
						return;
					}
					Result.Checksum = (double)Passes;
				}
				else
				{
					FString SetError;
					if (!Globals.Set(TEXT("benchGate"), FBangoValue::MakeBool(true), SetError))
					{
						Result.Error = FString(TEXT("Couldn't set the global: ")) + SetError;
						return;
					}
					int32 Finished = 0;
					uint64 Instructions = 0;
					const double Start = FPlatformTime::Seconds();
					for (const TSharedPtr<FBangoInstance>& Instance : Waiting)
					{
						if (Instance->IsWaitDue(Globals))
						{
							const uint64 Before = Instance->GetInstructionsRun();
							Finished += Instance->Resume(Context) == EBangoInstanceStatus::Finished ? 1 : 0;
							Instructions += Instance->GetInstructionsRun() - Before;
						}
					}
					Seconds.Add(FPlatformTime::Seconds() - Start);
					if (Finished != Passes)
					{
						Result.Error = FString::Printf(TEXT("Only %d of %d scripts carried on when the global changed."), Finished, Passes);
						return;
					}
					Result.Instructions = Instructions;
					Result.Checksum = (double)Passes;
				}
			}
		}
		SetTimes(Result, Seconds);
		Result.bRan = true;
	}

	static void RunCpp(const FCase& Case, const FBangoBenchmarkOptions& Options, FBangoBenchmarkResult& Result)
	{
		const int32 Passes = GetPasses(Case, Options);
		Result.Iterations = Passes;
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(FString(TEXT("Bango.Benchmark ")) + Case.Name + TEXT(" C++")));
		TArray<double> Seconds;
		for (int32 Repeat = 0; Repeat < FMath::Max(1, Options.Repeats); ++Repeat)
		{
			const double Start = FPlatformTime::Seconds();
			Result.Checksum = Case.Cpp(Passes);
			Seconds.Add(FPlatformTime::Seconds() - Start);
		}
		SetTimes(Result, Seconds);
		Result.bRan = true;
	}

	static void RunBlueprint(const FCase& Case, const FBangoBenchmarkOptions& Options, FBangoBenchmarkResult& Result)
	{
		const int32 Passes = GetPasses(Case, Options);
		Result.Iterations = Passes;
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(FString(TEXT("Bango.Benchmark ")) + Case.Name + TEXT(" Blueprint")));
		TArray<double> Seconds;
		for (int32 Repeat = 0; Repeat < FMath::Max(1, Options.Repeats); ++Repeat)
		{
			bool bImplemented = false;
			const double Start = FPlatformTime::Seconds();
			const bool bOk = Options.RunBlueprint(Case.Name, Passes, Result.Checksum, bImplemented);
			Seconds.Add(FPlatformTime::Seconds() - Start);
			if (!bOk)
			{
				Result.Error = bImplemented ? FString(TEXT("The Blueprint couldn't run it.")) : FString();
				return;
			}
		}
		SetTimes(Result, Seconds);
		Result.bRan = true;
	}

	static bool SameChecksum(double A, double B)
	{
		const double Larger = FMath::Max(FMath::Max(A, -A), FMath::Max(B, -B));
		const double Difference = A > B ? A - B : B - A;
		return Difference <= 1e-6 * FMath::Max(1.0, Larger);
	}

	static FString Pad(const FString& Text, int32 Width, bool bRight)
	{
		FString Out = Text;
		while (Out.Len() < Width)
		{
			Out = bRight ? FString(TEXT(" ")) + Out : Out + TEXT(" ");
		}
		return Out;
	}
}

namespace Bango
{
	int32 BenchmarkAdd(int32 A, int32 B)
	{
		// Through a volatile, so no caller can be compiled with the sum worked out in advance.
		volatile int32 Sum = A + B;
		return Sum;
	}

	void GetBenchmarkCases(TArray<FBangoBenchmarkCaseInfo>& OutCases)
	{
		for (const BangoBenchmarkPrivate::FCase& Case : BangoBenchmarkPrivate::GCases)
		{
			FBangoBenchmarkCaseInfo Info;
			Info.Name = Case.Name;
			Info.Description = Case.Description;
			Info.Source = Case.Source;
			Info.bComparable = Case.Cpp != nullptr && Case.Kind != BangoBenchmarkPrivate::ECaseKind::CppOnly;
			Info.bNeedsGame = Case.Kind == BangoBenchmarkPrivate::ECaseKind::InGame;
			OutCases.Add(Info);
		}
	}

	void RunBenchmarks(const FBangoBenchmarkOptions& Options, TArray<FBangoBenchmarkResult>& OutResults)
	{
		using namespace BangoBenchmarkPrivate;
		FBangoCommandTable Commands;
		FString CommandsError;
		const bool bCommands = MakeCommands(Commands, CommandsError);
		const FString Filter = Options.CaseFilter.ToLower();
		for (const FCase& Case : GCases)
		{
			if (!Filter.IsEmpty() && !FString(Case.Name).ToLower().Contains(Filter))
			{
				continue;
			}
			if (Case.Kind == ECaseKind::InGame && !Options.RunInGame)
			{
				continue; // no game to run it in: no row (as a case without a Blueprint version has no Blueprint row)
			}
			if (Case.Kind != ECaseKind::CppOnly)
			{
				FBangoBenchmarkResult Result;
				Result.Case = Case.Name;
				Result.Runner = TEXT("Bango");
				if (bCommands)
				{
					RunBango(Case, Options, Commands, Result);
				}
				else
				{
					Result.Error = FString(TEXT("Couldn't set up the benchmark's commands: ")) + CommandsError;
				}
				OutResults.Add(Result);
			}
			if (Case.Cpp)
			{
				FBangoBenchmarkResult Result;
				Result.Case = Case.Name;
				Result.Runner = TEXT("C++");
				RunCpp(Case, Options, Result);
				OutResults.Add(Result);
				if (Options.RunBlueprint && Case.Kind != ECaseKind::CppOnly)
				{
					FBangoBenchmarkResult BlueprintResult;
					BlueprintResult.Case = Case.Name;
					BlueprintResult.Runner = TEXT("Blueprint");
					RunBlueprint(Case, Options, BlueprintResult);
					OutResults.Add(BlueprintResult);
				}
			}
		}
	}

	void FormatBenchmarkTable(const TArray<FBangoBenchmarkResult>& Results, TArray<FString>& OutLines)
	{
		using namespace BangoBenchmarkPrivate;
		OutLines.Add(Pad(TEXT("Case"), 17, false) + Pad(TEXT("Runner"), 10, false) + Pad(TEXT("Iterations"), 11, true) + Pad(TEXT("ns/iter"), 12, true)
			+ Pad(TEXT("x C++"), 9, true) + Pad(TEXT("ns/instr"), 10, true) + Pad(TEXT("best ms"), 11, true) + Pad(TEXT("median ms"), 11, true) + TEXT("  Note"));
		for (const FBangoBenchmarkResult& Result : Results)
		{
			// The case's C++ row, to compare with.
			const FBangoBenchmarkResult* Cpp = nullptr;
			for (const FBangoBenchmarkResult& Other : Results)
			{
				if (Other.Case == Result.Case && Other.Runner == TEXT("C++") && Other.bRan)
				{
					Cpp = &Other;
				}
			}
			FString Line = Pad(Result.Case, 17, false) + Pad(Result.Runner, 10, false);
			if (!Result.bRan)
			{
				OutLines.Add(Line + (Result.Error.IsEmpty() ? FString(TEXT("(not implemented)")) : FString(TEXT("FAILED: ")) + Result.Error));
				continue;
			}
			Line += Pad(FString::Printf(TEXT("%lld"), (long long)Result.Iterations), 11, true);
			Line += Pad(FString::Printf(TEXT("%.2f"), Result.GetNanosecondsPerIteration()), 12, true);
			const bool bRatio = Cpp && Cpp != &Result && Cpp->GetNanosecondsPerIteration() > 0.0;
			Line += Pad(bRatio ? FString::Printf(TEXT("%.1f"), Result.GetNanosecondsPerIteration() / Cpp->GetNanosecondsPerIteration()) : FString(TEXT("-")), 9, true);
			Line += Pad(Result.Instructions > 0 ? FString::Printf(TEXT("%.2f"), Result.GetNanosecondsPerInstruction()) : FString(TEXT("-")), 10, true);
			Line += Pad(FString::Printf(TEXT("%.3f"), Result.BestSeconds * 1000.0), 11, true);
			Line += Pad(FString::Printf(TEXT("%.3f"), Result.MedianSeconds * 1000.0), 11, true);
			if (Cpp && Cpp != &Result && !SameChecksum(Cpp->Checksum, Result.Checksum))
			{
				Line += FString::Printf(TEXT("  DIFFERENT ANSWER: %.6g, C++ got %.6g"), Result.Checksum, Cpp->Checksum);
			}
			OutLines.Add(Line);
		}
	}

	FString FormatBenchmarkCsv(const TArray<FBangoBenchmarkResult>& Results)
	{
		FString Csv = TEXT("Case,Runner,Ran,Iterations,BestSeconds,MedianSeconds,NanosecondsPerIteration,Instructions,NanosecondsPerInstruction,CompileSeconds,Checksum,Error\n");
		for (const FBangoBenchmarkResult& Result : Results)
		{
			FString Error = Result.Error;
			Error.ReplaceInline(TEXT("\""), TEXT("'"));
			Error.ReplaceInline(TEXT("\n"), TEXT(" "));
			Csv += FString::Printf(TEXT("%s,%s,%d,%lld,%.9f,%.9f,%.3f,%llu,%.3f,%.9f,%.9g,\"%s\"\n"), *Result.Case, *Result.Runner, Result.bRan ? 1 : 0,
				(long long)Result.Iterations, Result.BestSeconds, Result.MedianSeconds, Result.GetNanosecondsPerIteration(), (unsigned long long)Result.Instructions,
				Result.GetNanosecondsPerInstruction(), Result.CompileSeconds, Result.Checksum, *Error);
		}
		return Csv;
	}
}
