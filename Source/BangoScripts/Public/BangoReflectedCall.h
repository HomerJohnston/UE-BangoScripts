#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"

class UClass;
class UFunction;
class FProperty;

/**
 * `target call Name(args)` (spec section 4): calling a Blueprint function on an entity's actor, by reflection. A
 * function can be called if:
 *  - its name is exactly Name (case-sensitive), so it must already be a valid Bango name (letters, digits and '_');
 *  - it can be called from Blueprint (a Blueprint function, or a BlueprintCallable C++ one);
 *  - it's in the category "Bango" (or under it, like "Bango|Lights"). Categories only exist in editor builds, so a
 *    packaged game doesn't check this one: scripts calling anything else fail while they're made and tested;
 *  - its parameters and its result are types Bango has (BangoTypeOfProperty).
 */
namespace Bango
{
	/** A Bango-callable function, for the editor's lists. */
	struct FBangoCallableFunction
	{
		FString Name;
		TArray<FString> ParamNames;
		TArray<FBangoType> ParamTypes;
		/** False if it returns nothing (or more than one value, which a script can't use). */
		bool bHasResult = false;
		FBangoType ResultType;
		/** Its tooltip, in editor builds. */
		FString Description;
		/** "SetBrightness(float Brightness, bool Fade)" */
		FString Signature;
	};

	/** The Bango type of a parameter or result. False if Bango has no such type (an array, or another struct or object). */
	BANGOSCRIPTS_API bool BangoTypeOfProperty(const FProperty* Property, FBangoType& OutType);

	/** Class's functions that `call` can reach (with its parents'), by name. */
	BANGOSCRIPTS_API void GetBangoCallableFunctions(const UClass* Class, TArray<FBangoCallableFunction>& OutFunctions);

	/**
	 * Runs `Target call Name(Args)`: finds Target's actor and its function Name, converts Args to its parameters,
	 * calls it, and, with bWantResult, gives its result in OutResult. False, with OutError, if anything doesn't fit.
	 */
	BANGOSCRIPTS_API bool CallBangoCallable(const UObject* WorldContext, const FBangoValue& Target, const FString& Name, const TArray<FBangoValue>& Args,
		bool bWantResult, FBangoValue& OutResult, FString& OutError);

	/**
	 * The same on Object itself (CallBangoCallable's second half): TargetText names it in errors ("@light (BP_Light)").
	 * WorldContext resolves entity arguments and results.
	 */
	BANGOSCRIPTS_API bool CallBangoCallableOn(UObject* Object, const FString& TargetText, const UObject* WorldContext, const FString& Name,
		const TArray<FBangoValue>& Args, bool bWantResult, FBangoValue& OutResult, FString& OutError);
}
