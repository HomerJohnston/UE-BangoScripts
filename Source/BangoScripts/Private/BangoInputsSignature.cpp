#include "BangoInputsSignature.h"

#include "BangoCompiler.h"
#include "BangoInlineScript.h"
#include "BangoLog.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "UObject/ObjectKey.h"

namespace BangoInputsSignaturePrivate
{
	using FSignatureKey = TTuple<FObjectKey, FName>;

	// Class default objects can be made on the loading thread (a Blueprint class loaded with a level), so it's locked.
	static FCriticalSection& GetLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	static TMap<FSignatureKey, TSharedPtr<const FBangoInputsSignature>>& GetSignatures()
	{
		static TMap<FSignatureKey, TSharedPtr<const FBangoInputsSignature>> Signatures;
		return Signatures;
	}

	static FString MakeLabel(const UClass* Class, FName Name)
	{
		if (!Class)
		{
			return FString::Printf(TEXT("the global '%s'"), *Name.ToString());
		}
		FString ClassName = Class->GetName();
		ClassName.RemoveFromEnd(TEXT("_C")); // a Blueprint's class
		return FString::Printf(TEXT("%s's '%s'"), *ClassName, *Name.ToString());
	}

	static bool SameInputs(const FBangoInputsSignature& A, const FBangoInputsSignature& B)
	{
		return A.ToInputsText() == B.ToInputsText() && A.Tooltips == B.Tooltips && A.Label == B.Label;
	}
}

namespace BangoInputsSignaturePrivate
{
	/** A signature with an object's instance inputs after it, or why they can't be used. */
	struct FWithInstanceInputs
	{
		TWeakPtr<const FBangoInputsSignature> Signature; // the registered one it was made from (not valid: none)
		bool bHadSignature = false;
		TSharedPtr<const FBangoInputsSignature> Inputs; // null: Error says why
		FString Error;
	};

	static TMap<FString, FWithInstanceInputs>& GetWithInstanceInputs()
	{
		static TMap<FString, FWithInstanceInputs> Made;
		return Made;
	}

	/**
	 * Signature's inputs (null for none) with InstanceInputs after them. Made once for each signature and list, and
	 * kept: every script with the same ones gets the same object, so two can be compared by pointer (the editor does,
	 * for several selected objects, and to notice a change), and the errors of a wrong list are reported once.
	 */
	static TSharedPtr<const FBangoInputsSignature> FindWithInstanceInputs(const TSharedPtr<const FBangoInputsSignature>& Signature, const TArray<FBangoInputDecl>& InstanceInputs, FString& OutError)
	{
		FString Key = Signature.IsValid() ? Signature->Label : FString();
		for (const FBangoInputDecl& Input : InstanceInputs)
		{
			Key += TEXT("\n");
			Key += Input.Decl;
			Key += TEXT("\t");
			Key += Input.Tooltip;
		}

		FScopeLock Lock(&GetLock());
		TMap<FString, FWithInstanceInputs>& Made = GetWithInstanceInputs();
		if (const FWithInstanceInputs* Found = Made.Find(Key))
		{
			// Still made from this signature (a class can register its signature again, with other inputs).
			if (Found->bHadSignature == Signature.IsValid() && (!Signature.IsValid() || Found->Signature.Pin() == Signature))
			{
				OutError = Found->Error;
				return Found->Inputs;
			}
		}
		if (Made.Num() >= 4096)
		{
			Made.Reset(); // far more lists than any game has: scripts keep the ones they were compiled with
		}

		FWithInstanceInputs Entry;
		Entry.Signature = Signature;
		Entry.bHadSignature = Signature.IsValid();
		TSharedPtr<FBangoInputsSignature> Inputs = MakeShared<FBangoInputsSignature>();
		TArray<FString> Warnings;
		if (Bango::AddInstanceInputs(Signature.Get(), InstanceInputs, *Inputs, Entry.Error, Warnings))
		{
			Entry.Inputs = Inputs;
			for (const FString& Warning : Warnings)
			{
				Bango::ReportError(FString::Printf(TEXT("An object's own script inputs (SetInstanceInputs): %s"), *Warning));
			}
		}
		Made.Add(Key, Entry);
		OutError = Entry.Error;
		return Entry.Inputs;
	}
}

namespace Bango
{
	void RegisterInputsSignature(const UObject* Template, FName Name, const TArray<FBangoInputDecl>& Inputs)
	{
		using namespace BangoInputsSignaturePrivate;
		if (Template && !Template->IsTemplate())
		{
			ReportError(FString::Printf(TEXT("Bango::RegisterInputsSignature('%s') was called for %s, which isn't a template, so it was ignored. Call it in the constructor, inside if (IsTemplate()), so it's registered once per class and not for every instance."),
				*Name.ToString(), *Template->GetName()));
			return;
		}
		if (Name.IsNone())
		{
			ReportError(TEXT("Bango::RegisterInputsSignature needs a name for the signature."));
			return;
		}

		const UClass* Class = Template ? Template->GetClass() : nullptr;
		TSharedPtr<FBangoInputsSignature> Signature = MakeShared<FBangoInputsSignature>();
		Signature->Label = MakeLabel(Class, Name);
		FString Error;
		TArray<FString> Warnings;
		if (!ParseInputsSignature(Inputs, *Signature, Error, Warnings))
		{
			ReportError(FString::Printf(TEXT("Couldn't register the inputs signature %s: %s"), *Signature->Label, *Error));
			return;
		}

		FScopeLock Lock(&GetLock());
		TSharedPtr<const FBangoInputsSignature>& Slot = GetSignatures().FindOrAdd(FSignatureKey(FObjectKey(Class), Name));
		if (Slot.IsValid() && SameInputs(*Slot, *Signature))
		{
			return; // the same again (another template of the class): keep the one scripts were compiled with
		}
		Slot = Signature;
		for (const FString& Warning : Warnings)
		{
			ReportError(FString::Printf(TEXT("Inputs signature %s: %s"), *Signature->Label, *Warning));
		}
	}

	TSharedPtr<const FBangoInputsSignature> FindInputsSignature(const UClass* Class, FName Name)
	{
		using namespace BangoInputsSignaturePrivate;
		FScopeLock Lock(&GetLock());
		const TSharedPtr<const FBangoInputsSignature>* Found = GetSignatures().Find(FSignatureKey(FObjectKey(Class), Name));
		return Found ? *Found : nullptr;
	}
}

const UClass* FBangoInlineScriptBase::GetInputsSignatureClass() const
{
	return InputsSignatureClass.Get();
}

void FBangoInlineScriptBase::UseInputsSignature(const UObject* Owner, FName Name)
{
	InputsSignatureClass = Owner ? Owner->GetClass() : nullptr;
	bGlobalInputsSignature = Owner == nullptr;
	InputsSignatureName = Name;
}

TArray<FBangoInputDecl> FBangoInlineScriptBase::GetInstanceInputs() const
{
	TArray<FBangoInputDecl> Inputs;
	Inputs.Reserve(InstanceInputs.Num());
	for (const FBangoInstanceInput& Input : InstanceInputs)
	{
		Inputs.Emplace(Input.Decl, Input.Tooltip);
	}
	return Inputs;
}

bool FBangoInlineScriptBase::SetInstanceInputs(TArray<FBangoInputDecl> Inputs, FString* OutError)
{
	TArray<FBangoInstanceInput> Kept;
	Kept.Reserve(Inputs.Num());
	for (const FBangoInputDecl& Input : Inputs)
	{
		FBangoInstanceInput& Added = Kept.AddDefaulted_GetRef();
		Added.Decl = Input.Decl;
		Added.Tooltip = Input.Tooltip;
	}
	if (Kept != InstanceInputs)
	{
		InstanceInputs = MoveTemp(Kept);
	}
	if (InstanceInputs.Num() == 0)
	{
		return true;
	}
	// Checked now, so whoever sets them hears about it (the script itself says the same when it's compiled).
	FString Error;
	if (FindInputsSignature(Error).IsValid() || Error.IsEmpty())
	{
		return true;
	}
	if (OutError)
	{
		*OutError = Error;
	}
	else
	{
		Bango::ReportError(Error);
	}
	return false;
}

TSharedPtr<const FBangoInputsSignature> FBangoInlineScriptBase::FindInputsSignature(FString& OutError) const
{
	TSharedPtr<const FBangoInputsSignature> Signature;
	if (HasInputsSignature())
	{
		const UClass* Class = InputsSignatureClass.Get();
		if (!bGlobalInputsSignature && !Class)
		{
			OutError = FString::Printf(TEXT("This script uses the inputs signature '%s' of a class that no longer exists."), *InputsSignatureName.ToString());
			return nullptr;
		}
		Signature = Bango::FindInputsSignature(Class, InputsSignatureName);
		if (!Signature.IsValid())
		{
			OutError = FString::Printf(TEXT("This script uses the inputs signature %s, but none is registered by that name (Bango::RegisterInputsSignature). Is the name spelled the same in both places?"),
				*BangoInputsSignaturePrivate::MakeLabel(Class, InputsSignatureName));
			return nullptr;
		}
	}
	if (InstanceInputs.Num() == 0)
	{
		return Signature;
	}
	return BangoInputsSignaturePrivate::FindWithInstanceInputs(Signature, GetInstanceInputs(), OutError);
}
