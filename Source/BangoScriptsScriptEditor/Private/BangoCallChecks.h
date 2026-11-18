// Bango script editor: checking `target call Name(args)` while a script is edited (spec section 7, Calling Blueprint
// functions). A call's target is only known here when it's written as `self` (an inline script's actor) or as an
// `@name` some actor in the open level has: then its Blueprint functions are known, so the call can be checked as it's
// typed, autocomplete can offer them, and its graph node can show their parameters. Others are checked when they run.

#pragma once

#include "CoreMinimal.h"
#include "BangoProgram.h"

class AActor;
struct FBangoCompileError;
struct FBangoNodeLayoutContext;

namespace BangoCallChecks
{
	/**
	 * The actor a call's target (FBangoReflectedCallSite::TargetEntity) is while editing: Self for `self`, the level's
	 * actor with that Bango ID for `@name`. Null if it isn't known (player, a variable, a name no actor has).
	 */
	const AActor* FindTarget(const FString& TargetEntity, const AActor* Self);

	/** The target's Bango functions, and how to name it ("@light (BP_Light)"). False if the target isn't known. */
	bool GetTargetFunctions(const FString& TargetEntity, const AActor* Self, TArray<FBangoCallableFunction>& OutFunctions, FString& OutLabel);

	/**
	 * Checks Program's calls whose targets are known (Bango::CheckReflectedCall). False, with OutError at the first
	 * that doesn't fit (its line and column, FileName as given).
	 */
	bool CheckCalls(const FBangoProgram& Program, const AActor* Self, const FString& FileName, FBangoCompileError& OutError);

	/** Lets a layout context's call nodes show their targets' functions (FBangoNodeLayoutContext::FindCallTarget). */
	void SetUpLayoutContext(FBangoNodeLayoutContext& Context, TFunction<const AActor*()> GetSelf);
}
