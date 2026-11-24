#pragma once

#include "CoreMinimal.h"

/**
 * Benchmarks: the same work done by a Bango script, by C++ and (if the caller can run one) by a Blueprint, timed the
 * same way, to see what the script VM costs. The console command Bango.Benchmark runs them
 * (BangoScriptsBenchmarkCommand.cpp); this part needs no world or engine object, so it also runs in tests.
 *
 * All of it is in this module, BangoScriptsBenchmark, which nothing else needs: the plugin leaves it out of Shipping
 * builds (BangoScripts.uplugin). The one thing it needs from the VM is FBangoInstance::GetInstructionsRun.
 *
 * Each case is run Repeats times and the best and the median times are kept: the best is the least disturbed by
 * whatever else the machine was doing, so it's the one to compare. A Bango case is compiled once, outside the timing.
 * Only a build with optimizations (Development or Shipping, not DebugGame) says anything about real speed.
 */

/** One row: one case, run by one runner. */
struct FBangoBenchmarkResult
{
	/** The case's name, like "IntMath". */
	FString Case;
	/** "Bango", "C++" or "Blueprint". */
	FString Runner;
	/** False if it couldn't be run (then Error says why) or, for a Blueprint, isn't implemented (Error is empty). */
	bool bRan = false;
	FString Error;
	/** How many times the case's unit of work was done in one run (a loop's passes, scripts started, scripts checked). */
	int64 Iterations = 0;
	/** One run's time, in seconds: the fastest of the repeats, and their median. */
	double BestSeconds = 0.0;
	double MedianSeconds = 0.0;
	/** Bango only: the VM instructions one run took (0 where it isn't counted), and how long compiling the script took. */
	uint64 Instructions = 0;
	double CompileSeconds = 0.0;
	/** What the work added up to: the same for every runner of a case, or they didn't do the same work. */
	double Checksum = 0.0;

	double GetNanosecondsPerIteration() const { return Iterations > 0 ? BestSeconds * 1e9 / (double)Iterations : 0.0; }
	double GetNanosecondsPerInstruction() const { return Instructions > 0 ? BestSeconds * 1e9 / (double)Instructions : 0.0; }
};

struct FBangoBenchmarkOptions
{
	/** A loop case's passes. Cases that do more work per pass run a fraction of it (see each case). */
	int32 Iterations = 1000000;
	/** Runs per case and runner; the best and the median are reported. */
	int32 Repeats = 5;
	/** Only cases whose name contains this (case ignored); empty runs them all. */
	FString CaseFilter;
	/**
	 * Records the debugger's trail while the scripts run (FBangoInstance::SetRecordTrail), as the editor does for
	 * every script: slower, so it's off by default, to time the VM as a packaged game runs it.
	 */
	bool bRecordTrail = false;
	/**
	 * Runs a case in a Blueprint: its Iterations passes, giving the checksum. False if the Blueprint doesn't implement
	 * the case (bOutImplemented false) or couldn't run it. Unset: no Blueprint rows.
	 */
	TFunction<bool(const FString& Case, int32 Iterations, double& OutChecksum, bool& bOutImplemented)> RunBlueprint;
	/**
	 * Runs one of the cases that need a running game (SpawnInline and the like): Iterations spawns of Source through
	 * the game's script subsystem, giving the time they took in seconds (the first spawn, which compiles the script,
	 * isn't timed). False, with why, if it couldn't. Unset: those cases have no rows.
	 */
	TFunction<bool(const FString& Case, const FString& Source, int32 Iterations, double& OutSeconds, FString& OutError)> RunInGame;
};

/** A case, for listing them (and for writing its Blueprint version). */
struct FBangoBenchmarkCaseInfo
{
	FString Name;
	/** What it measures, in a sentence. */
	FString Description;
	/** The script the Bango runner runs. Empty for a case that's C++ only (what a value on its own costs). */
	FString Source;
	/** True if it has a script, a C++ version, and can have a Blueprint one. */
	bool bComparable = false;
	/** True if it only runs in a game (the options' RunInGame): the script subsystem's own way of starting a script. */
	bool bNeedsGame = false;
};

namespace Bango
{
	/** The cases, in the order they run. */
	BANGOSCRIPTSBENCHMARK_API void GetBenchmarkCases(TArray<FBangoBenchmarkCaseInfo>& OutCases);

	/** Runs the cases: for each, a Bango row (unless it's C++ only), then a C++ row and a Blueprint row where the case has them. */
	BANGOSCRIPTSBENCHMARK_API void RunBenchmarks(const FBangoBenchmarkOptions& Options, TArray<FBangoBenchmarkResult>& OutResults);

	/** The results as a table of lines, for a log: per case, each runner's time for one iteration and how many times C++'s it is. */
	BANGOSCRIPTSBENCHMARK_API void FormatBenchmarkTable(const TArray<FBangoBenchmarkResult>& Results, TArray<FString>& OutLines);

	/** The results as CSV text, with a header line. */
	BANGOSCRIPTSBENCHMARK_API FString FormatBenchmarkCsv(const TArray<FBangoBenchmarkResult>& Results);

	/**
	 * What the benchmark's native command does, for a Blueprint's version of the NativeCall case to call the same
	 * thing (UBangoBenchmarkBlueprint::BenchAdd): A + B, in a function the compiler can't fold into its caller.
	 */
	BANGOSCRIPTSBENCHMARK_API int32 BenchmarkAdd(int32 A, int32 B);
}
