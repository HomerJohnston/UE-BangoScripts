#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BangoProgram.h"
#include "BangoScriptSubsystem.h"
class UBangoScriptAsset;

#include "BangoIDName.h"
#include "BangoInlineScript.h"
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
	 * ScriptPath is a Bango Script asset's path: full ("/Game/Quests/Intro") or relative to /Game ("Quests/Intro").
	 * The script runs straight away until it first pauses. Errors are shown on screen and return an invalid handle.
	 * Owner is the actor the script belongs to, which `self` means in it (Self, unless something else is connected).
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "Owner", DisplayName = "Spawn Bango Script (Dynamic Path)"))
	static FBangoScriptHandle SpawnBangoScript(UObject* WorldContextObject, const FString& ScriptPath, UObject* Owner);

	// ---- Bango ID names: converted to and from strings by themselves where one is wired to the other ----

	UFUNCTION(BlueprintPure, Category = "Bango", meta = (DisplayName = "To String (Bango ID Name)", CompactNodeTitle = "->", BlueprintAutocast))
	static FString Conv_BangoIDNameToString(const FBangoIDName& IDName) { return IDName.Name; }

	UFUNCTION(BlueprintPure, Category = "Bango", meta = (DisplayName = "To Bango ID Name (String)", CompactNodeTitle = "->", BlueprintAutocast))
	static FBangoIDName Conv_StringToBangoIDName(const FString& Name) { return FBangoIDName(Name); }

	// ---- Arguments for a script's parameters: Spawn Bango Inline Script's Args (the Spawn Bango Script node builds them itself) ----

	/** An empty list of values for a Bango script's parameters. Add to it with the Add Bango Arg nodes, in the order of the script's #set inputs line, then give it to Spawn Bango Inline Script. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Make Bango Args"))
	static FBangoBlueprintArgs MakeBangoArgs();

	/** Leaves out the next parameter (one with a default value in the script's #set inputs line), so it gets its default. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Skip Bango Arg"))
	static FBangoBlueprintArgs SkipBangoArg(const FBangoBlueprintArgs& Args);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Bool)"))
	static FBangoBlueprintArgs AddBangoArgBool(const FBangoBlueprintArgs& Args, bool Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Int)"))
	static FBangoBlueprintArgs AddBangoArgInt(const FBangoBlueprintArgs& Args, int32 Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Float)"))
	static FBangoBlueprintArgs AddBangoArgDouble(const FBangoBlueprintArgs& Args, double Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (String)"))
	static FBangoBlueprintArgs AddBangoArgString(const FBangoBlueprintArgs& Args, const FString& Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Text)"))
	static FBangoBlueprintArgs AddBangoArgText(const FBangoBlueprintArgs& Args, const FText& Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Actor (Entity))"))
	static FBangoBlueprintArgs AddBangoArgActor(const FBangoBlueprintArgs& Args, AActor* Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Vector2)"))
	static FBangoBlueprintArgs AddBangoArgVector2(const FBangoBlueprintArgs& Args, FVector2D Value);

	/** Adds the next parameter's value. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Vector3)"))
	static FBangoBlueprintArgs AddBangoArgVector3(const FBangoBlueprintArgs& Args, FVector Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Bool Array)"))
	static FBangoBlueprintArgs AddBangoArgBoolArray(const FBangoBlueprintArgs& Args, const TArray<bool>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Int Array)"))
	static FBangoBlueprintArgs AddBangoArgIntArray(const FBangoBlueprintArgs& Args, const TArray<int32>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Float Array)"))
	static FBangoBlueprintArgs AddBangoArgDoubleArray(const FBangoBlueprintArgs& Args, const TArray<double>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (String Array)"))
	static FBangoBlueprintArgs AddBangoArgStringArray(const FBangoBlueprintArgs& Args, const TArray<FString>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Text Array)"))
	static FBangoBlueprintArgs AddBangoArgTextArray(const FBangoBlueprintArgs& Args, const TArray<FText>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Actor (Entity) Array)"))
	static FBangoBlueprintArgs AddBangoArgActorArray(const FBangoBlueprintArgs& Args, const TArray<AActor*>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Vector2 Array)"))
	static FBangoBlueprintArgs AddBangoArgVector2Array(const FBangoBlueprintArgs& Args, const TArray<FVector2D>& Value);

	/** Adds the next parameter's value: an array. */
	UFUNCTION(BlueprintPure, Category = "Bango|Args", meta = (DisplayName = "Add Bango Arg (Vector3 Array)"))
	static FBangoBlueprintArgs AddBangoArgVector3Array(const FBangoBlueprintArgs& Args, const TArray<FVector>& Value);

	/** Spawns ScriptPath with collected arguments, belonging to Owner. */
	UFUNCTION(BlueprintCallable, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Owner"))
	static FBangoScriptHandle SpawnBangoScriptWithArgs(UObject* WorldContextObject, const FString& ScriptPath, const FBangoBlueprintArgs& Args, UObject* Owner);

	/**
	 * Starts an inline script (a Bango Inline Script variable or property, spec section 15). Args fill its #set inputs
	 * line, in order: make them with Make Bango Args and the Add Bango Arg nodes (leave it unconnected for a script with
	 * no parameters). They're checked when it starts: if they don't fit, it doesn't start, and the error says why. An
	 * empty script does nothing. Owner is the actor it belongs to, which `self` means in it; it also names it in errors
	 * and the debugger (Self, unless something else is connected).
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Args", DefaultToSelf = "Owner", DisplayName = "Spawn Bango Inline Script"))
	static FBangoScriptHandle SpawnBangoInlineScript(UObject* WorldContextObject, const FBangoInlineScript& Script, const FBangoBlueprintArgs& Args, UObject* Owner);

	/** Spawns a script asset with collected arguments, belonging to Owner (the Spawn Bango Script node). */
	UFUNCTION(BlueprintCallable, Category = "Bango|Internal", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Owner"))
	static FBangoScriptHandle SpawnBangoScriptAssetWithArgs(UObject* WorldContextObject, const UBangoScriptAsset* Script, const FBangoBlueprintArgs& Args, UObject* Owner);

	/**
	 * Stops a running script. With And Children, also stops the scripts it started (with Exec or spawn) that are still
	 * running, and the ones those started, and so on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bango", meta = (WorldContext = "WorldContextObject"))
	static void TerminateBangoScript(UObject* WorldContextObject, FBangoScriptHandle Handle, bool bAndChildren = false);

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
