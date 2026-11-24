#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoScriptsBenchmark.h"

// The benchmark's cases all run, and its script, C++ (and Blueprint, where there is one) versions do the same work.
// A few passes only: this checks them, it doesn't time them (the console command Bango.Benchmark does).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoBenchmarkTest, "Bango.Benchmark.Cases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoBenchmarkTest::RunTest(const FString& Parameters)
{
	FBangoBenchmarkOptions Options;
	Options.Iterations = 2000;
	Options.Repeats = 2;
	TArray<FBangoBenchmarkResult> Results;
	Bango::RunBenchmarks(Options, Results);
	TArray<FBangoBenchmarkCaseInfo> Cases;
	Bango::GetBenchmarkCases(Cases);
	for (const FBangoBenchmarkCaseInfo& Case : Cases)
	{
		const FBangoBenchmarkResult* Script = Results.FindByPredicate([&Case](const FBangoBenchmarkResult& Result) { return Result.Case == Case.Name && Result.Runner == TEXT("Bango"); });
		const FBangoBenchmarkResult* Cpp = Results.FindByPredicate([&Case](const FBangoBenchmarkResult& Result) { return Result.Case == Case.Name && Result.Runner == TEXT("C++"); });
		if (Case.Source.IsEmpty())
		{
			// C++ only: what a value on its own costs.
			TestTrue(*FString::Printf(TEXT("%s: C++ ran, and there's no script row"), *Case.Name), Cpp && Cpp->bRan && !Script);
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s: the script ran (%s)"), *Case.Name, Script ? *Script->Error : TEXT("no row")), Script && Script->bRan);
		if (Case.bComparable)
		{
			TestTrue(*FString::Printf(TEXT("%s: C++ ran"), *Case.Name), Cpp && Cpp->bRan);
			TestTrue(*FString::Printf(TEXT("%s: both got the same answer"), *Case.Name), Script && Cpp && FMath::IsNearlyEqual(Script->Checksum, Cpp->Checksum, 1e-6 * FMath::Max(1.0, FMath::Abs(Cpp->Checksum))));
		}
	}
	TArray<FString> Lines;
	Bango::FormatBenchmarkTable(Results, Lines);
	TestEqual(TEXT("the table has a line per row, and its heading"), Lines.Num(), Results.Num() + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
