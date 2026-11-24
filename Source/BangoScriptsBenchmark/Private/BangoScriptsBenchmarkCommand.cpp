// The Bango.Benchmark console command, and the Blueprint class it can run cases in (BangoScriptsBenchmark.h).

#include "BangoScriptsBenchmark.h"
#include "BangoScriptsBenchmarkBlueprint.h"
#include "BangoLog.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

int32 UBangoBenchmarkBlueprint::BenchAdd(int32 A, int32 B)
{
	return Bango::BenchmarkAdd(A, B);
}

bool UBangoBenchmarkBlueprint::RunCase(const FString& Case, int32 Iterations, double& OutChecksum, bool& bOutImplemented)
{
	bOutImplemented = false;
	struct FCaseFunction
	{
		const TCHAR* Name;
		double (UBangoBenchmarkBlueprint::*Function)(int32);
	};
	static const FCaseFunction Functions[] =
	{
		{ TEXT("EmptyLoop"), &UBangoBenchmarkBlueprint::EmptyLoop },
		{ TEXT("IntMath"), &UBangoBenchmarkBlueprint::IntMath },
		{ TEXT("FloatMath"), &UBangoBenchmarkBlueprint::FloatMath },
		{ TEXT("GlobalReadWrite"), &UBangoBenchmarkBlueprint::GlobalReadWrite },
		{ TEXT("FunctionCall"), &UBangoBenchmarkBlueprint::FunctionCall },
		{ TEXT("NativeCall"), &UBangoBenchmarkBlueprint::NativeCall },
		{ TEXT("ArrayPushSum"), &UBangoBenchmarkBlueprint::ArrayPushSum },
	};
	for (const FCaseFunction& Entry : Functions)
	{
		if (Case.Equals(Entry.Name, ESearchCase::CaseSensitive))
		{
			// Without an override the event does nothing and returns 0: that isn't a time worth showing.
			if (!GetClass()->IsFunctionImplementedInScript(FName(Entry.Name)))
			{
				return false;
			}
			bOutImplemented = true;
			OutChecksum = (this->*Entry.Function)(Iterations);
			return true;
		}
	}
	return false;
}

namespace BangoBenchmarkCommandPrivate
{
	static TAutoConsoleVariable<FString> CVarBlueprint(
		TEXT("Bango.Benchmark.Blueprint"),
		TEXT("/Game/BangoBenchmark/BP_BangoBenchmark"),
		TEXT("The Blueprint class Bango.Benchmark runs its Blueprint rows in: a child of Bango Benchmark Blueprint, by its asset path. Empty for none."));

	/** The class at the console variable's path, or null (with why, unless there's just nothing there). */
	static UClass* LoadBlueprintClass(FString& OutNote)
	{
		FString Path = CVarBlueprint.GetValueOnGameThread().TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		// "/Game/Folder/BP_Name" is the asset; its class is "/Game/Folder/BP_Name.BP_Name_C".
		if (!Path.Contains(TEXT(".")))
		{
			Path = Path + TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		if (!Path.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			Path += TEXT("_C");
		}
		UClass* Class = StaticLoadClass(UBangoBenchmarkBlueprint::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Class)
		{
			OutNote = FString::Printf(TEXT("No Blueprint rows: there's no child of Bango Benchmark Blueprint at %s (the console variable Bango.Benchmark.Blueprint says where to look)."), *Path);
		}
		return Class;
	}

	static void Run(const TArray<FString>& Args)
	{
		// Bango.Benchmark List: the cases, each with the script it runs, for writing the Blueprint versions.
		if (Args.Num() > 0 && Args[0].Equals(TEXT("List"), ESearchCase::IgnoreCase))
		{
			TArray<FBangoBenchmarkCaseInfo> Cases;
			Bango::GetBenchmarkCases(Cases);
			for (const FBangoBenchmarkCaseInfo& Case : Cases)
			{
				UE_LOG(LogBango, Display, TEXT("%s: %s%s"), *Case.Name, *Case.Description, Case.bComparable ? TEXT("") : (Case.Source.IsEmpty() ? TEXT("") : TEXT(" (Bango only)")));
				TArray<FString> SourceLines;
				Case.Source.ParseIntoArrayLines(SourceLines, /*bCullEmpty*/ false);
				for (const FString& SourceLine : SourceLines)
				{
					UE_LOG(LogBango, Display, TEXT("    %s"), *SourceLine);
				}
			}
			return;
		}

		// Bango.Benchmark [Iterations] [CaseFilter] [Repeats=N] [Trail]
		FBangoBenchmarkOptions Options;
		for (const FString& Arg : Args)
		{
			if (Arg.IsNumeric())
			{
				Options.Iterations = FMath::Max(1, FCString::Atoi(*Arg));
			}
			else if (Arg.StartsWith(TEXT("Repeats="), ESearchCase::IgnoreCase))
			{
				Options.Repeats = FMath::Clamp(FCString::Atoi(*Arg.RightChop(8)), 1, 100);
			}
			else if (Arg.Equals(TEXT("Trail"), ESearchCase::IgnoreCase))
			{
				Options.bRecordTrail = true;
			}
			else
			{
				Options.CaseFilter = Arg;
			}
		}

		FString BlueprintNote;
		TStrongObjectPtr<UBangoBenchmarkBlueprint> Blueprint;
		if (UClass* Class = LoadBlueprintClass(BlueprintNote))
		{
			Blueprint.Reset(NewObject<UBangoBenchmarkBlueprint>(GetTransientPackage(), Class));
		}
		if (Blueprint.IsValid())
		{
			UBangoBenchmarkBlueprint* Object = Blueprint.Get();
			Options.RunBlueprint = [Object](const FString& Case, int32 Iterations, double& OutChecksum, bool& bOutImplemented)
			{
				return Object->RunCase(Case, Iterations, OutChecksum, bOutImplemented);
			};
		}

#if UE_BUILD_SHIPPING
		const TCHAR* BuildName = TEXT("Shipping");
#elif UE_BUILD_TEST
		const TCHAR* BuildName = TEXT("Test");
#elif UE_BUILD_DEVELOPMENT
		const TCHAR* BuildName = WITH_EDITOR ? TEXT("Development Editor") : TEXT("Development");
#else
		const TCHAR* BuildName = TEXT("Debug");
#endif
		UE_LOG(LogBango, Display, TEXT("Bango benchmark: %d iterations, best of %d runs%s. (%s build: only an optimized build, Development or Shipping, says anything about real speed, and the editor's is slower than a packaged game's.)"),
			Options.Iterations, Options.Repeats, Options.bRecordTrail ? TEXT(", recording the debugger's trail") : TEXT(""), BuildName);
		TArray<FBangoBenchmarkResult> Results;
		Bango::RunBenchmarks(Options, Results);

		TArray<FString> Lines;
		Bango::FormatBenchmarkTable(Results, Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogBango, Display, TEXT("%s"), *Line);
		}
		if (!BlueprintNote.IsEmpty())
		{
			UE_LOG(LogBango, Display, TEXT("%s"), *BlueprintNote);
		}

		const FString CsvPath = FPaths::ProjectSavedDir() / TEXT("BangoBuild") / TEXT("benchmark.csv");
		if (FFileHelper::SaveStringToFile(Bango::FormatBenchmarkCsv(Results), *CsvPath))
		{
			UE_LOG(LogBango, Display, TEXT("Written to %s"), *FPaths::ConvertRelativePathToFull(CsvPath));
		}
	}

	static FAutoConsoleCommand BenchmarkCommand(
		TEXT("Bango.Benchmark"),
		TEXT("Times Bango scripts against the same work in C++ (and in a Blueprint, see Bango.Benchmark.Blueprint). Bango.Benchmark [Iterations] [CaseFilter] [Repeats=N] [Trail]; Bango.Benchmark List prints the cases and their scripts."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
