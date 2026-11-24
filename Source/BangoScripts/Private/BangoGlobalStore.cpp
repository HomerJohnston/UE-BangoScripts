#include "BangoGlobalStore.h"

#include "Serialization/Archive.h"

#include <atomic>

namespace BangoGlobalStorePrivate
{
	constexpr int32 SaveVersion = 2; // 2: ints are 32-bit
}

uint64 FBangoGlobalStore::FLayout::MakeNumber()
{
	static std::atomic<uint64> Last{0};
	return ++Last;
}

bool FBangoGlobalStore::Declare(const FString& Name, const FBangoType& Type, FString& OutError)
{
	if (!Bango::IsValidGlobalName(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid global name. Globals must be identifiers (letters, digits and '_', not starting with a digit), and must not be reserved words."), *Name);
		return false;
	}

	if (const FBangoValue* Existing = Find(Name))
	{
		if (Existing->GetType() != Type)
		{
			OutError = FString::Printf(TEXT("Global '%s' is declared as %s, but it already exists as %s."),
				*Name, *Type.ToString(), *Existing->GetType().ToString());
			return false;
		}
		return true;
	}

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Name = Name;
	Entry.Value = FBangoValue::MakeNil(Type);
	Entry.Stamp = ++ChangeStamp;
	Slots.Add(Name, Entries.Num() - 1);
	return true;
}

bool FBangoGlobalStore::Set(const FString& Name, const FBangoValue& Value, FString& OutError)
{
	const int32 Slot = FindSlot(Name);
	if (Slot == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Can't set global '%s': it has not been declared."), *Name);
		return false;
	}
	FBangoValue* Existing = &Entries[Slot].Value;

	FBangoValue Converted;
	if (!Value.TryConvertTo(Existing->GetType(), Converted))
	{
		OutError = FString::Printf(TEXT("Can't set global '%s' (%s) to a %s."),
			*Name, *Existing->GetType().ToString(), *Value.GetType().ToString());
		return false;
	}

	if (!(*Existing == Converted))
	{
		*Existing = MoveTemp(Converted);
		Entries[Slot].Stamp = ++ChangeStamp;
	}
	return true;
}

const FBangoValue* FBangoGlobalStore::Find(const FString& Name) const
{
	const int32 Slot = FindSlot(Name);
	return Slot != INDEX_NONE ? &Entries[Slot].Value : nullptr;
}

FBangoValue* FBangoGlobalStore::FindMutable(const FString& Name)
{
	const int32 Slot = FindSlot(Name);
	return Slot != INDEX_NONE ? &GetSlotMutable(Slot) : nullptr;
}

uint64 FBangoGlobalStore::GetChangeStamp(const FString& Name) const
{
	const int32 Slot = FindSlot(Name);
	return Slot != INDEX_NONE ? Entries[Slot].Stamp : AllChangedStamp;
}

bool FBangoGlobalStore::IsDefined(const FString& Name) const
{
	const FBangoValue* Value = Find(Name);
	return Value && !Value->IsNil();
}

void FBangoGlobalStore::ResetAll()
{
	Entries.Reset();
	Slots.Reset();
	NoteAllChanged();
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
		int32 Count = Entries.Num();
		Ar << Count;
		for (FEntry& Entry : Entries)
		{
			Ar << Entry.Name;
			Ar << Entry.Value;
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

		Entries.Reset();
		Slots.Reset();
		NoteAllChanged();
		for (int32 i = 0; i < Count && !Ar.IsError(); ++i)
		{
			FString Name;
			FBangoValue Value;
			Ar << Name;
			Ar << Value;
			if (const int32* Slot = Slots.Find(Name))
			{
				Entries[*Slot].Value = MoveTemp(Value); // a name twice: the later one, as before
				continue;
			}
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Value = MoveTemp(Value);
			Entry.Stamp = AllChangedStamp;
			Slots.Add(MoveTemp(Name), Entries.Num() - 1);
		}
	}
}
