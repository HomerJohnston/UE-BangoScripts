#include "BangoConditions.h"

#include "Bango.h"
#include "BangoCompiler.h"
#include "BangoInlineScript.h"

namespace BangoConditionsPrivate
{
	struct FRegistered
	{
		/** Name and source, to find a registration of the same condition. */
		FString Key;
		TSharedPtr<const FBangoProgram> Program;
		int32 Registrations = 0;
	};

	// Conditions are registered and checked on the game thread.
	static TMap<int64, FRegistered>& GetRegistered()
	{
		static TMap<int64, FRegistered> Registered;
		return Registered;
	}

	static TMap<FString, int64>& GetIdsByKey()
	{
		static TMap<FString, int64> IdsByKey;
		return IdsByKey;
	}

	static int64 NextId = 1;

	static FString MakeKey(const FString& Source, const FString& Name, const FBangoInputsSignature* Signature)
	{
		return Name + TEXT("\n") + (Signature ? Signature->Label + TEXT(": ") + Signature->ToInputsText() : FString()) + TEXT("\n") + Source;
	}
}

namespace Bango
{
	bool CompileConditionProgram(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError,
		const FBangoInputsSignature* Signature)
	{
		const FString ConditionName = Name.IsEmpty() ? FString(TEXT("Condition")) : Name;
		TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
		FBangoCompileError CompileError;
		if (!CompileCondition(Source, ConditionName, FBangoModule::Get().GetCommands(), *Program, CompileError, Signature))
		{
			OutError = CompileError.ToString();
			return false;
		}
		OutProgram = Program;
		return true;
	}

	FBangoConditionHandle RegisterCondition(const FBangoInlineCondition& Condition, const FString& Name, FString& OutError)
	{
		using namespace BangoConditionsPrivate;
		FBangoConditionHandle Handle;
		const FString ConditionName = Name.IsEmpty() ? FString(TEXT("Condition")) : Name;
		FString SignatureError;
		const TSharedPtr<const FBangoInputsSignature> Signature = Condition.FindInputsSignature(SignatureError);
		if (Condition.HasGivenInputs() && !Signature.IsValid())
		{
			OutError = FString::Printf(TEXT("%s: %s"), *ConditionName, *SignatureError);
			return Handle;
		}
		const FString& Source = Condition.Source;
		const FString Key = MakeKey(Source, ConditionName, Signature.Get());
		if (const int64* Existing = GetIdsByKey().Find(Key))
		{
			++GetRegistered().Find(*Existing)->Registrations;
			Handle.Id = *Existing;
			return Handle;
		}

		TSharedPtr<const FBangoProgram> Program;
		if (!CompileConditionProgram(Source, ConditionName, Program, OutError, Signature.Get()))
		{
			return Handle;
		}
		const int64 Id = NextId++;
		FRegistered Registered;
		Registered.Key = Key;
		Registered.Program = Program;
		Registered.Registrations = 1;
		GetRegistered().Add(Id, Registered);
		GetIdsByKey().Add(Key, Id);
		Handle.Id = Id;
		return Handle;
	}

	void UnregisterCondition(FBangoConditionHandle& Handle)
	{
		using namespace BangoConditionsPrivate;
		if (FRegistered* Registered = Handle.IsValid() ? GetRegistered().Find(Handle.Id) : nullptr)
		{
			if (--Registered->Registrations <= 0)
			{
				GetIdsByKey().Remove(Registered->Key);
				GetRegistered().Remove(Handle.Id);
			}
		}
		Handle = FBangoConditionHandle();
	}

	TSharedPtr<const FBangoProgram> FindCondition(FBangoConditionHandle Handle)
	{
		const BangoConditionsPrivate::FRegistered* Registered = BangoConditionsPrivate::GetRegistered().Find(Handle.Id);
		return Registered ? Registered->Program : TSharedPtr<const FBangoProgram>();
	}

	int32 GetNumRegisteredConditions()
	{
		return BangoConditionsPrivate::GetRegistered().Num();
	}
}
