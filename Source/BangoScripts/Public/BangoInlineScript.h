#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "BangoProgram.h"
#include "BangoInlineScript.generated.h"

class UClass;

/**
 * What an inline script remembers of the last time it was compiled (FBangoModule::GetInlineProgram), so spawning it
 * again costs a comparison of its text instead of a lookup by all of it. Used only while everything it was compiled
 * from is still the same: the name, the text and the inputs it's given.
 */
struct FBangoInlineCompiled
{
	/** What errors call the script (its owner's name). */
	FString Name;
	/** The owner Name was made from, if it's known (none otherwise): the same owner again has the same Name. */
	FName OwnerName;
	/** The text it was compiled from: one copy, shared with the running scripts (they keep it for saving). */
	TSharedPtr<const FString> Source;
	/** The inputs it was compiled with, or null. */
	TSharedPtr<const FBangoInputsSignature> Signature;
	TSharedPtr<const FBangoProgram> Program;
};

/**
 * One of an inline script's instance inputs, as it's kept on the script (FBangoInlineScriptBase::SetInstanceInputs):
 * an FBangoInputDecl, as properties.
 */
USTRUCT()
struct BANGOSCRIPTS_API FBangoInstanceInput
{
	GENERATED_BODY()

	/** `entity _lamp`: a type and a name. */
	UPROPERTY(VisibleAnywhere, Category = "Bango")
	FString Decl;

	/** What it is, shown in the editor. */
	UPROPERTY(VisibleAnywhere, Category = "Bango")
	FString Tooltip;

	bool operator==(const FBangoInstanceInput& Other) const { return Decl.Equals(Other.Decl, ESearchCase::CaseSensitive) && Tooltip.Equals(Other.Tooltip, ESearchCase::CaseSensitive); }
	bool operator!=(const FBangoInstanceInput& Other) const { return !(*this == Other); }
};

/**
 * What inline scripts and inline conditions share: a Bango script kept in a property of any class or Blueprint,
 * instead of in a Bango Script asset (spec section 15). In the editor's details panel it's a box to type the script
 * in, with an Edit button that opens the full graph editor in a window of its own. Use FBangoInlineScript or
 * FBangoInlineCondition, not this.
 *
 * The script may start with a `#inputs (...)` line; whoever runs it passes the values, in order. Or its owner's C++
 * gives it an inputs signature (UseInputsSignature): then the signature's inputs are its parameters, and it can't have
 * an #inputs line. One object can add inputs of its own after those (SetInstanceInputs).
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
	bool IsEmpty() const
	{
		for (const TCHAR Char : Source)
		{
			if (!FChar::IsWhitespace(Char))
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Gives the script the inputs signature Name of Owner's class (or the global one, with a null Owner), registered
	 * with Bango::RegisterInputsSignature (see BangoInputsSignature.h). Call it in the owner's constructor: it isn't
	 * saved, so each object sets it again when it's made, and editing or pasting the script's text doesn't change it.
	 */
	void UseInputsSignature(const UObject* Owner, FName Name);

	/** True if UseInputsSignature gave it one. */
	bool HasInputsSignature() const { return !InputsSignatureName.IsNone(); }

	/**
	 * Gives this one script inputs of its owner's own, after its signature's (or by themselves, without one): constants
	 * that differ from one placed object to the next, like a light trigger's `vector3 _red`. Each is written like a
	 * signature's input, without a default (`entity _lamp`), with an optional description for the editor. The script
	 * reads them like the signature's, and whoever runs it passes their values after the signature's, in this order
	 * (Spawn, EvaluateCondition), or by name (watches).
	 *
	 * The owner calls it when it's set up (its constructor, PostLoad or OnRegister) and again whenever its list
	 * changes (PostEditChangeProperty, undo, and UActorComponent::PostApplyToComponent for a component). The same list
	 * again does nothing. An open editor on the script picks a change up by itself.
	 *
	 * The list is kept on the script as a property (InstanceInputs), so it goes wherever Unreal copies the script's
	 * owner by its properties, without the owner being told: a Blueprint's component is made again from the
	 * Blueprint each time its actor's construction script reruns, and then given back what was edited on it.
	 *
	 * False if they can't be used: one doesn't read or has a default, or has the name of another or of one of the
	 * signature's inputs. The script then doesn't compile, with that as its error (also shown in the editor), until
	 * they're set right. The error goes in OutError, or, without one, is reported (Bango::ReportError). A name that
	 * doesn't start with '_' is reported, but works.
	 */
	bool SetInstanceInputs(TArray<FBangoInputDecl> Inputs, FString* OutError = nullptr);
	TArray<FBangoInputDecl> GetInstanceInputs() const;

	/**
	 * The instance inputs, as SetInstanceInputs left them: set them with that, not here. A property so that it's
	 * copied with the script (see SetInstanceInputs), and VisibleAnywhere because Unreal only carries a component's
	 * editable properties over when it makes the component again; the details panel doesn't show it (the script's
	 * row is its own).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Bango")
	TArray<FBangoInstanceInput> InstanceInputs;

	/** True if its owner gives it inputs: a signature's, its own instance inputs, or both. */
	bool HasGivenInputs() const { return HasInputsSignature() || InstanceInputs.Num() > 0; }

	/**
	 * The inputs it's given: its inputs signature's, then its instance inputs. Null if it has neither, or, with
	 * OutError, if they can't be used (the signature isn't registered, or the instance inputs are wrong). Scripts with
	 * the same signature and the same instance inputs get the same object.
	 */
	TSharedPtr<const FBangoInputsSignature> FindInputsSignature(FString& OutError) const;

	/** For saving a running script: the signature's class (null for a global one, or one that's gone) and name. */
	const UClass* GetInputsSignatureClass() const;
	FName GetInputsSignatureName() const { return InputsSignatureName; }
	bool IsGlobalInputsSignature() const { return bGlobalInputsSignature; }

	/**
	 * FBangoModule::GetInlineProgram's: what this script was last compiled as, or null. Not a property, and not part
	 * of what makes two scripts equal. A copy of the script carries it along, which is safe: it's checked against the
	 * script's text and inputs each time it's used.
	 */
	mutable TSharedPtr<const FBangoInlineCompiled> LastCompiled;

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
