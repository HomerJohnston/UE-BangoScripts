#include "BangoGlobalStore.h"

#include "Serialization/Archive.h"

namespace BangoGlobalStorePrivate
{
	constexpr int32 SaveVersion = 2; // 2: ints are 32-bit
}

bool FBangoGlobalStore::Declare(const FString& Name, const FBangoType& Type, FString& OutError)
{
	if (!Bango::IsValidGlobalName(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid global name. Globals must be identifiers, must not start with '_', and must not be reserved words."), *Name);
		return false;
	}

	if (const FBangoValue* Existing = Globals.Find(Name))
	{
		if (Existing->GetType() != Type)
		{
			OutError = FString::Printf(TEXT("Global '%s' is declared as %s, but it already exists as %s."),
				*Name, *Type.ToString(), *Existing->GetType().ToString());
			return false;
		}
		return true;
	}

	Globals.Add(Name, FBangoValue::MakeNil(Type));
	return true;
}

bool FBangoGlobalStore::Set(const FString& Name, const FBangoValue& Value, FString& OutError)
{
	FBangoValue* Existing = Globals.Find(Name);
	if (!Existing)
	{
		OutError = FString::Printf(TEXT("Can't set global '%s': it has not been declared."), *Name);
		return false;
	}

	FBangoValue Converted;
	if (!Value.TryConvertTo(Existing->GetType(), Converted))
	{
		OutError = FString::Printf(TEXT("Can't set global '%s' (%s) to a %s."),
			*Name, *Existing->GetType().ToString(), *Value.GetType().ToString());
		return false;
	}

	*Existing = MoveTemp(Converted);
	return true;
}

const FBangoValue* FBangoGlobalStore::Find(const FString& Name) const
{
	return Globals.Find(Name);
}

FBangoValue* FBangoGlobalStore::FindMutable(const FString& Name)
{
	return Globals.Find(Name);
}

bool FBangoGlobalStore::IsDefined(const FString& Name) const
{
	const FBangoValue* Value = Globals.Find(Name);
	return Value && !Value->IsNil();
}

void FBangoGlobalStore::ResetAll()
{
	Globals.Reset();
}

void FBangoGlobalStore::Serialize(FArchive& Ar)
{
	int32 Version = BangoGlobalStorePrivate::SaveVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != BangoGlobalStorePrivate::SaveVersion)
	{
		Ar.SetError();
		return;
	}

	if (Ar.IsSaving())
	{
		int32 Count = Globals.Num();
		Ar << Count;
		for (TPair<FString, FBangoValue>& Pair : Globals)
		{
			Ar << Pair.Key;
			Ar << Pair.Value;
		}
	}
	else if (Ar.IsLoading())
	{
		int32 Count = 0;
		Ar << Count;
		if (Count < 0)
		{
			Ar.SetError();
			return;
		}

		Globals.Reset();
		for (int32 i = 0; i < Count && !Ar.IsError(); ++i)
		{
			FString Name;
			FBangoValue Value;
			Ar << Name;
			Ar << Value;
			Globals.Add(MoveTemp(Name), MoveTemp(Value));
		}
	}
}
