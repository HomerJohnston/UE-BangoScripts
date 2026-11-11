#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BangoProgram.h"
#include "BangoScriptSubsystem.h"
#include "BangoBlueprintLibrary.generated.h"

class AActor;

/**
 * Arguments being collected for a script spawn, built by the Spawn Bango Script node.
 * Each slot holds a value, or is marked "not passed" so the script's default is used.
 */
USTRUCT(BlueprintType)
struct BANGOSCRIPTS_API FBangoBlueprintArgs
{
	GENERATED_BODY()

	/** Number of slots (kept in step with Values; also gives Blueprint a visible property). */
	UPROPERTY()
	int32 NumArgs = 0;

	TArray<FBangoValue> Values;
	/** Parallel to Values: true where the argument was left out. */
	TArray<bool> NotPassed;
	/** The first problem found while collecting (e.g. an actor with no Bango ID). Reported when spawning. */
	FString Error;

	void Add(const FBangoValue& Value)
	{
		Values.Add(Value);
		NotPassed.Add(false);
		NumArgs = Values.Num();
	}

	void AddNotPassed()
	{
		Values.Add(FBangoValue());
		NotPassed.Add(true);
		NumArgs = Values.Num();
	}
};

namespace Bango
{
	/**
	 * Turns collected Blueprint arguments into the list for FBangoInstance::Start: each "not passed" slot
	 * gets its parameter's default, then trailing not-passed slots are dropped. Fails if a not-passed slot
	 * has no default, or Args holds a collection error.
	 */
	BANGOSCRIPTS_API bool ResolveBlueprintArgs(const FBangoBlueprintArgs& Args, const TArray<FBangoScriptParamInfo>& Params, TArray<FBangoValue>& OutValues, FString& OutError);
}

/** Blueprint nodes for running Bango scripts and using Bango global variables. */
UCLASS()
class BANGOSCRIPTS_API UBangoBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Scripts ----

	/**
	 * Starts a Bango script whose path is computed at runtime, with no arguments. For a fixed script, use the
	 * Spawn Bango Script node instead: it shows a pin for each of the script's parameters.
	 * ScriptPath is relative to Content/Bango, without the extension: "Quest/Intro".
	 * The script runs straight away until it first pauses. Errors are shown on screen and return an invalid handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango", meta = (WorldContext = "WorldContextObject", DisplayName = "Spawn Bango Script (Dynamic Path)"))
	static FBangoScriptHandle SpawnBangoScript(UObject* WorldContextObject, const FString& ScriptPath);

	// ---- Used by the Spawn Bango Script node (hidden from the Blueprint menu) ----

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs MakeBangoArgs();

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs SkipBangoArg(const FBangoBlueprintArgs& Args);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgBool(const FBangoBlueprintArgs& Args, bool Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgInt(const FBangoBlueprintArgs& Args, int32 Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgDouble(const FBangoBlueprintArgs& Args, double Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgString(const FBangoBlueprintArgs& Args, const FString& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgText(const FBangoBlueprintArgs& Args, const FText& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgActor(const FBangoBlueprintArgs& Args, AActor* Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgVector2(const FBangoBlueprintArgs& Args, FVector2D Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgVector3(const FBangoBlueprintArgs& Args, FVector Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgBoolArray(const FBangoBlueprintArgs& Args, const TArray<bool>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgIntArray(const FBangoBlueprintArgs& Args, const TArray<int32>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgDoubleArray(const FBangoBlueprintArgs& Args, const TArray<double>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgStringArray(const FBangoBlueprintArgs& Args, const TArray<FString>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgTextArray(const FBangoBlueprintArgs& Args, const TArray<FText>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgActorArray(const FBangoBlueprintArgs& Args, const TArray<AActor*>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgVector2Array(const FBangoBlueprintArgs& Args, const TArray<FVector2D>& Value);

	UFUNCTION(BlueprintPure, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoBlueprintArgs AddBangoArgVector3Array(const FBangoBlueprintArgs& Args, const TArray<FVector>& Value);

	/** Spawns ScriptPath with collected arguments. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static FBangoScriptHandle SpawnBangoScriptWithArgs(UObject* WorldContextObject, const FString& ScriptPath, const FBangoBlueprintArgs& Args);

	/** Stops a running script. */
	UFUNCTION(BlueprintCallable, Category = "Bango", meta = (WorldContext = "WorldContextObject"))
	static void TerminateBangoScript(UObject* WorldContextObject, FBangoScriptHandle Handle);

	/** True while the script is running or paused; false once it has ended for any reason. */
	UFUNCTION(BlueprintPure, Category = "Bango", meta = (WorldContext = "WorldContextObject"))
	static bool IsBangoScriptRunning(UObject* WorldContextObject, FBangoScriptHandle Handle);

	/** True if the handle came from a successful spawn (even if the script has since ended). */
	UFUNCTION(BlueprintPure, Category = "Bango")
	static bool IsValidBangoScriptHandle(FBangoScriptHandle Handle) { return Handle.IsValid(); }

	// ---- Global variables ----
	// Setting a global that doesn't exist yet declares it with that type. Setting one that exists with
	// another type is an error. Getting one that doesn't exist or is nil returns false in bIsDefined.

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalBool(UObject* WorldContextObject, const FString& Name, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalInt(UObject* WorldContextObject, const FString& Name, int32 Value);

	/** Sets a Bango float global (a double underneath). Named Double in C++ so existing Blueprints keep working. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Bango Global Float"))
	static bool SetBangoGlobalDouble(UObject* WorldContextObject, const FString& Name, double Value);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalString(UObject* WorldContextObject, const FString& Name, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalText(UObject* WorldContextObject, const FString& Name, const FText& Value);

	/** Sets a Bango vector2 global from a Vector 2D. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalVector2(UObject* WorldContextObject, const FString& Name, FVector2D Value);

	/** Sets a Bango vector3 global from a Vector. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool SetBangoGlobalVector3(UObject* WorldContextObject, const FString& Name, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static bool GetBangoGlobalBool(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static int32 GetBangoGlobalInt(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	/** Reads a Bango float global (a double underneath). Named Double in C++ so existing Blueprints keep working. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Bango Global Float"))
	static double GetBangoGlobalDouble(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static FString GetBangoGlobalString(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static FText GetBangoGlobalText(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	/** Reads a Bango vector2 global as a Vector 2D. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static FVector2D GetBangoGlobalVector2(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	/** Reads a Bango vector3 global as a Vector. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static FVector GetBangoGlobalVector3(UObject* WorldContextObject, const FString& Name, bool& bIsDefined);

	/** Makes a global nil (undefined). Does nothing if it doesn't exist. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Globals", meta = (WorldContext = "WorldContextObject"))
	static void ClearBangoGlobal(UObject* WorldContextObject, const FString& Name);

	// ---- Save and load ----

	/** Saves all running scripts and globals into Data (store it in your SaveGame). */
	UFUNCTION(BlueprintCallable, Category = "Bango|Save", meta = (WorldContext = "WorldContextObject"))
	static bool SaveBangoState(UObject* WorldContextObject, TArray<uint8>& Data);

	/** Replaces all running scripts and globals with ones saved by SaveBangoState. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Save", meta = (WorldContext = "WorldContextObject"))
	static bool LoadBangoState(UObject* WorldContextObject, const TArray<uint8>& Data);

	/** Stops all scripts and clears all globals, e.g. for a new game. Actors' Bango ID names stay registered. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Save", meta = (WorldContext = "WorldContextObject"))
	static void ResetBangoState(UObject* WorldContextObject);
};
