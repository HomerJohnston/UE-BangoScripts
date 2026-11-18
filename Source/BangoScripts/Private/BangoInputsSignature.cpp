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

const UClass* FBangoInlineScript::GetInputsSignatureClass() const
{
	return InputsSignatureClass.Get();
}

void FBangoInlineScript::UseInputsSignature(const UObject* Owner, FName Name)
{
	InputsSignatureClass = Owner ? Owner->GetClass() : nullptr;
	bGlobalInputsSignature = Owner == nullptr;
	InputsSignatureName = Name;
}

TSharedPtr<const FBangoInputsSignature> FBangoInlineScript::FindInputsSignature(FString& OutError) const
{
	if (!HasInputsSignature())
	{
		return nullptr;
	}
	const UClass* Class = InputsSignatureClass.Get();
	if (!bGlobalInputsSignature && !Class)
	{
		OutError = FString::Printf(TEXT("This script uses the inputs signature '%s' of a class that no longer exists."), *InputsSignatureName.ToString());
		return nullptr;
	}
	TSharedPtr<const FBangoInputsSignature> Signature = Bango::FindInputsSignature(Class, InputsSignatureName);
	if (!Signature.IsValid())
	{
		OutError = FString::Printf(TEXT("This script uses the inputs signature %s, but none is registered by that name (Bango::RegisterInputsSignature). Is the name spelled the same in both places?"),
			*BangoInputsSignaturePrivate::MakeLabel(Class, InputsSignatureName));
	}
	return Signature;
}
