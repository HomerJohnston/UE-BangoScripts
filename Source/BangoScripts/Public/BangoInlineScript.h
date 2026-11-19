#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "BangoInlineScript.generated.h"

struct FBangoInputsSignature;
class UClass;

/**
 * What inline scripts and inline conditions share: a Bango script kept in a property of any class or Blueprint,
 * instead of in a Bango Script asset (spec section 15). In the editor's details panel it's a box to type the script
 * in, with an Edit button that opens the full graph editor in a window of its own. Use FBangoInlineScript or
 * FBangoInlineCondition, not this.
 *
 * The script may start with a `#inputs (...)` line; whoever runs it passes the values, in order. Or its owner's C++
 * gives it an inputs signature (UseInputsSignature): then the signature's inputs are its parameters, and it can't have
 * an #inputs line.
 */
USTRUCT()
struct BANGOSCRIPTS_API FBangoInlineScriptBase
{
	GENERATED_BODY()

	/**
	 * The script's text, as in a .bango file. Edited in the details panel; not a Blueprint pin (Blueprint only passes
	 * inline scripts and conditions along, to the Spawn Bango Inline Script node and the like).
	 */
	UPROPERTY(EditAnywhere, Category = "Bango")
	FString Source;

	/** True if there's no script (nothing but spaces). */
	bool IsEmpty() const { return Source.TrimStartAndEnd().IsEmpty(); }

	/**
	 * Gives the script the inputs signature Name of Owner's class (or the global one, with a null Owner), registered
	 * with Bango::RegisterInputsSignature (see BangoInputsSignature.h). Call it in the owner's constructor: it isn't
	 * saved, so each object sets it again when it's made, and editing or pasting the script's text doesn't change it.
	 */
	void UseInputsSignature(const UObject* Owner, FName Name);

	/** True if UseInputsSignature gave it one. */
	bool HasInputsSignature() const { return !InputsSignatureName.IsNone(); }

	/** Its inputs signature. Null if it has none, or, with OutError, if that signature isn't registered. */
	TSharedPtr<const FBangoInputsSignature> FindInputsSignature(FString& OutError) const;

	/** For saving a running script: the signature's class (null for a global one, or one that's gone) and name. */
	const UClass* GetInputsSignatureClass() const;
	FName GetInputsSignatureName() const { return InputsSignatureName; }
	bool IsGlobalInputsSignature() const { return bGlobalInputsSignature; }

private:
	// Set by the owner's constructor, not saved (UseInputsSignature).
	TWeakObjectPtr<const UClass> InputsSignatureClass;
	FName InputsSignatureName;
	bool bGlobalInputsSignature = false;
};

/**
 * A script to run (UBangoScriptSubsystem::Spawn, the Spawn Bango Inline Script node): usually a line or two, like
 * `Sleep(4); player AddItem("Pistol");`.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Bango Inline Script"))
struct BANGOSCRIPTS_API FBangoInlineScript : public FBangoInlineScriptBase
{
	GENERATED_BODY()

	bool operator==(const FBangoInlineScript& Other) const { return Source.Equals(Other.Source, ESearchCase::CaseSensitive); }
	bool operator!=(const FBangoInlineScript& Other) const { return !(*this == Other); }
};

/**
 * A condition: a script that answers true or false (spec section 10, Conditions), like `foundThings >= 10`, checked by
 * whatever owns it, or watched (UBangoScriptSubsystem::CreateWatchGroup). The editor checks it as a condition.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Bango Inline Condition"))
struct BANGOSCRIPTS_API FBangoInlineCondition : public FBangoInlineScriptBase
{
	GENERATED_BODY()

	/**
	 * When watched, and its answer can change without anything Bango sees changing (it calls a command, or uses
	 * `call`): how often it's checked, in seconds (0: every frame). Below 0: the project's Default Condition Poll
	 * Interval. Shown in the editor only for a condition that needs it.
	 */
	UPROPERTY(EditAnywhere, Category = "Bango")
	float PollInterval = -1.0f;

	/** PollInterval, or the project's default if it's below 0. */
	float GetPollInterval() const;

	bool operator==(const FBangoInlineCondition& Other) const
	{
		return Source.Equals(Other.Source, ESearchCase::CaseSensitive) && PollInterval == Other.PollInterval;
	}
	bool operator!=(const FBangoInlineCondition& Other) const { return !(*this == Other); }
};
