#pragma once

#include "CoreMinimal.h"
#include "BangoProgram.h"
#include "BangoTypes.h"
#include "BangoConstant.generated.h"

class AActor;
struct FBangoInlineScriptBase;

/**
 * A named value an object gives its inline scripts (spec section 15, Constants): each script on that object, and each
 * condition, reads it as an input, after the ones its inputs signature gives it. For values that differ from one
 * placed object to the next and that several of its scripts share: a lever's `_door`, a light switch's `_lamps`.
 *
 * An owner keeps an array of them as a property (`UPROPERTY(EditAnywhere) TArray<FBangoConstant> Constants;`), gives
 * its scripts their declarations as instance inputs (Bango::SetConstantInputs) whenever the list may have changed,
 * and passes their values (Bango::GetConstantValues) after each script's other arguments when it spawns it.
 *
 * Kept as a script variable is: a name, a type and a value written as Bango text, edited on one row in the details
 * panel with the script editor's own type picker and value box (BangoScriptsScriptEditor). An `entity` and an
 * `entity[]` are the exceptions: they're actors picked in the level, so actors without a Bango ID can be used too.
 */
USTRUCT()
struct BANGOSCRIPTS_API FBangoConstant
{
	GENERATED_BODY()

	/** The name scripts read it by. A leading '_' is usual, like other inputs (`_door`). */
	UPROPERTY(EditAnywhere, Category = "Constant")
	FString Name = TEXT("_value");

	/** Its type, as scripts write it: "int", "vector3", "entity[]" (Bango::GetValueTypeNames). */
	UPROPERTY(EditAnywhere, Category = "Constant")
	FString Type = TEXT("int");

	/**
	 * Its value as Bango text, as a script variable's starting value is written (`[1, 0, 0]`, `"hello"`); empty is
	 * nil. Not used for an `entity` or an `entity[]`.
	 */
	UPROPERTY(EditAnywhere, Category = "Constant")
	FString Value = TEXT("0");

	/** An `entity`'s value: an actor in the same level. None is nil. */
	UPROPERTY(EditAnywhere, Category = "Constant")
	TObjectPtr<AActor> Actor;

	/** An `entity[]`'s value: actors in the same level, in order. A None among them is a nil element. */
	UPROPERTY(EditAnywhere, Category = "Constant")
	TArray<TObjectPtr<AActor>> Actors;

	/** True for the type whose value is Actor. */
	bool IsActorType() const { return Type == TEXT("entity"); }

	/** True for the type whose value is Actors. */
	bool IsActorArrayType() const { return Type == TEXT("entity[]"); }

	/** How it's declared as an input: "vector3 _red". */
	FString GetDeclaration() const { return FString::Printf(TEXT("%s %s"), *Type, *Name); }

	/**
	 * Its value, for the scripts (Bango::ParseFixedValue, or the actors' entities). Work it out in a game world, when
	 * the owner begins play or later: an actor's entity is only known there. False, with why in OutError, if the type
	 * or the value can't be used: OutValue is then nil.
	 */
	bool GetValue(FBangoValue& OutValue, FString& OutError) const;
};

namespace Bango
{
	/** The constants as instance inputs, in order (FBangoInlineScriptBase::SetInstanceInputs). */
	BANGOSCRIPTS_API TArray<FBangoInputDecl> GetConstantInputs(const TArray<FBangoConstant>& Constants);

	/**
	 * Gives each of the scripts (inline scripts and conditions; nulls are skipped) the constants as its instance
	 * inputs. Call it wherever the owner is set up and whenever its constants may have changed: PostInitProperties,
	 * PostLoad, PostEditChangeProperty, PostEditUndo, and for a component PostApplyToComponent (spec section 15,
	 * Instance inputs). Setting the same list again does nothing. False, with the first error in OutError, if a
	 * constant doesn't read as an input (a bad name or type, or a name used twice): the scripts then show it as their
	 * own error.
	 */
	BANGOSCRIPTS_API bool SetConstantInputs(const TArray<FBangoConstant>& Constants, TArrayView<FBangoInlineScriptBase* const> Scripts, FString* OutError = nullptr);

	/**
	 * The constants' values, in order, to pass after a script's other arguments when it's spawned. Always one value
	 * per constant: one that can't be used is nil, and OutErrors (if given) gets a line for it, "_door: why". False
	 * if any couldn't. Call it in a game world (see FBangoConstant::GetValue).
	 */
	BANGOSCRIPTS_API bool GetConstantValues(const TArray<FBangoConstant>& Constants, TArray<FBangoValue>& OutValues, TArray<FString>* OutErrors = nullptr);
}
