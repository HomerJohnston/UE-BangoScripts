#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "BangoInlineScript.generated.h"

struct FBangoInputsSignature;
class UClass;

/**
 * A Bango script kept in a property of any class or Blueprint, instead of in a Bango Script asset (spec section 15):
 * usually a line or two, like `Sleep(4); player AddItem("Pistol");`. In the editor's details panel it's a box to
 * type the script in, with an Edit button that opens the full graph editor in a window of its own.
 *
 * The script may start with a `#inputs (...)` line; whoever spawns it passes the values, in order. Or its owner's C++
 * gives it an inputs signature (UseInputsSignature): then the signature's inputs are its parameters, and it can't have
 * an #inputs line.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Bango Inline Script"))
struct BANGOSCRIPTS_API FBangoInlineScript
{
	GENERATED_BODY()

	/** The script's text, as in a .bango file. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bango")
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

	bool operator==(const FBangoInlineScript& Other) const { return Source.Equals(Other.Source, ESearchCase::CaseSensitive); }
	bool operator!=(const FBangoInlineScript& Other) const { return !(*this == Other); }

private:
	// Set by the owner's constructor, not saved (UseInputsSignature).
	TWeakObjectPtr<const UClass> InputsSignatureClass;
	FName InputsSignatureName;
	bool bGlobalInputsSignature = false;
};
