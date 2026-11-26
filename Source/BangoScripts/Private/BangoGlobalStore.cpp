#include "BangoGlobalStore.h"

#include "Serialization/Archive.h"

#include <atomic>

namespace BangoGlobalStorePrivate
{
	constexpr int32 SaveVersion = 3; // 2: ints are 32-bit. 3: each global says whether it's persistent
}

uint64 FBangoGlobalStore::FLayout::MakeNumber()
{
	static std::atomic<uint64> Last{0};
	return ++Last;
}

bool FBangoGlobalStore::Declare(const FString& Name, const FBangoType& Type, FString& OutError, bool bPersistent)
{
	// One that's already there first: every script that declares a global does this each time it starts, and its
	// name was checked when it was made.
	if (const int32* ExistingSlot = Slots.Find(Name))
	{
		const FEntry& Existing = Entries[*ExistingSlot];
		if (Existing.Value.GetType() != Type)
		{
			OutError = FString::Printf(TEXT("Global '%s' is declared as %s, but it already exists as %s."),
				*Name, *Type.ToString(), *Existing.Value.GetType().ToString());
			return false;
		}
		if (Existing.bPersistent != bPersistent)
		{
			OutError = bPersistent
				? FString::Printf(TEXT("Global '%s' is declared as 'persistent global' here, but it already exists as a world global (declared 'global', without 'persistent'). A name is one or the other in every script."), *Name)
				: FString::Printf(TEXT("Global '%s' is declared as a world global ('global') here, but it already exists as a 'persistent global'. A name is one or the other in every script."), *Name);
			return false;
		}
		return true;
	}

	if (!Bango::IsValidGlobalName(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid global name. Globals must be identifiers (letters, digits and '_', not starting with a digit), and must not be reserved words."), *Name);
		return false;
	}

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Name = Name;
	Entry.Value = FBangoValue::MakeNil(Type);
	Entry.Stamp = ++ChangeStamp;
	Entry.bPersistent = bPersistent;
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

void FBangoGlobalStore::DeclarePersistentLocal(const FString& Key, const FBangoType& Type)
{
	if (const int32* ExistingSlot = Slots.Find(Key))
	{
		FEntry& Existing = Entries[*ExistingSlot];
		if (Existing.Value.GetType() != Type)
		{
			// The script now says it's another type: what was kept is thrown away.
			Existing.Value = FBangoValue::MakeNil(Type);
			Existing.Stamp = ++ChangeStamp;
		}
		Existing.bPersistent = true;
		return;
	}
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Name = Key;
	Entry.Value = FBangoValue::MakeNil(Type);
	Entry.Stamp = ++ChangeStamp;
	Entry.bPersistent = true;
	Slots.Add(Key, Entries.Num() - 1);
}

void FBangoGlobalStore::ResetAll()
{
	Entries.Reset();
	Slots.Reset();
	NoteAllChanged();
}

void FBangoGlobalStore::ResetWorldGlobals()
{
	TArray<FEntry> Kept;
	for (FEntry& Entry : Entries)
	{
		if (Entry.bPersistent)
		{
			Kept.Add(MoveTemp(Entry));
		}
	}
	if (Kept.Num() == Entries.Num())
	{
		return; // no world globals: nothing changes
	}
	Entries = MoveTemp(Kept);
	Slots.Reset();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		Slots.Add(Entries[Index].Name, Index);
	}
	NoteAllChanged(); // the slots mean something else now, and the world globals are gone
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
			Ar << Entry.bPersistent;
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
			bool bPersistent = false;
			Ar << Name;
			Ar << Value;
			Ar << bPersistent;
			if (const int32* Slot = Slots.Find(Name))
			{
				Entries[*Slot].Value = MoveTemp(Value); // a name twice: the later one, as before
				Entries[*Slot].bPersistent = bPersistent;
				continue;
			}
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Value = MoveTemp(Value);
			Entry.bPersistent = bPersistent;
			Entry.Stamp = AllChangedStamp;
			Slots.Add(MoveTemp(Name), Entries.Num() - 1);
		}
	}
}
