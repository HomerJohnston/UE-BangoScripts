#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BangoScriptsBenchmarkBlueprint.generated.h"

/**
 * The Blueprint side of the Bango.Benchmark console command (BangoScriptsBenchmark.h): make a Blueprint class with this as
 * its parent, and override the functions of the cases to compare. Each gets how many passes to make and returns what
 * the work added up to, which the command checks against what C++ got. A function that isn't overridden is left out
 * of the results.
 *
 * What each case does is the Bango script the command's List argument prints (Bango.Benchmark List); the Blueprint
 * should do the same with nodes. The command looks for the class at the path in the console variable
 * Bango.Benchmark.Blueprint (/Game/BangoBenchmark/BP_BangoBenchmark unless it's set).
 *
 * In the editor, Blueprint stops a loop after the project's Maximum Loop Iteration Count (Project Settings, General
 * Settings, Blueprints; a million by default): raise it for these loops, or run fewer passes.
 */
UCLASS(Abstract, Blueprintable)
class BANGOSCRIPTSBENCHMARK_API UBangoBenchmarkBlueprint : public UObject
{
	GENERATED_BODY()

public:
	/** Count from 0 to Iterations, one at a time, in a variable. Return the count. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double EmptyLoop(int32 Iterations);

	/** Sum = (Sum + Index * 3) % 1000003, for Index from 0 to Iterations - 1, in an integer variable. Return Sum. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double IntMath(int32 Iterations);

	/** X = X * 0.5 + Index * 0.25, for Index from 0 to Iterations - 1, in a float variable. Return X. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double FloatMath(int32 Iterations);

	/** Set an integer variable of the Blueprint (not a local) to 0, then add 1 to it Iterations times. Return it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double GlobalReadWrite(int32 Iterations);

	/** Sum = AddOne(Sum), Iterations times, where AddOne is a function of the Blueprint that returns its input + 1. Return Sum. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double FunctionCall(int32 Iterations);

	/** Sum = Bench Add(Sum, 1), Iterations times: the node below, which is C++. Return Sum. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double NativeCall(int32 Iterations);

	/**
	 * Add Index to an integer array, for Index from 0 to Iterations - 1; then, for each element, Sum = (Sum + Element)
	 * % 1000003. Return Sum.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bango|Benchmark")
	double ArrayPushSum(int32 Iterations);

	/** A + B, in C++: what the NativeCall case calls, the same function the script's command and the C++ version call. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Benchmark")
	static int32 BenchAdd(int32 A, int32 B);

	/**
	 * Runs the case of that name. False if there's no such case, or this class doesn't override its function
	 * (bOutImplemented is false then).
	 */
	bool RunCase(const FString& Case, int32 Iterations, double& OutChecksum, bool& bOutImplemented);
};
