#pragma once

#include "CoreMinimal.h"
#include "BangoContainers.h"
#include "BangoTypes.h"

/**
 * The Bango global variables (spec section 4), as a plain C++ object.
 * UBangoGlobalsSubsystem owns the game's store; tests and tools can create their own.
 *
 * - Declare() creates a global (holding nil) or confirms an existing one has the same type and kind.
 *   A different type, or a world global declared as persistent (or the other way round), is a conflict: the caller
 *   fails, and the existing global is kept.
 * - A global is a **world global** (`global int stage;`), gone when the world is (ResetWorldGlobals, called when
 *   the script subsystem's world ends), or a **persistent global** (`persistent global int gold;`), kept until
 *   everything is reset or loaded. Both are saved and loaded together.
 * - Find() returns nullptr for a global that was never created. Scripts treat that the same as nil.
 * - Set() requires the global to exist, and the value to fit its type.
 * - Each change is stamped (GetChangeStamp), so a script waiting on globals (`waitUntil`) can tell whether any of
 *   them has changed since it last looked.
 * - Every global has a slot, a number that stays its own until every global is replaced (ResetAll, loading). The
 *   VM finds a global by name once and by slot after that (FindSlot, GetLayout).
 */
class BANGOSCRIPTS_API FBangoGlobalStore
{
public:
	bool Declare(const FString& Name, const FBangoType& Type, FString& OutError, bool bPersistent = false);
	bool Set(const FString& Name, const FBangoValue& Value, FString& OutError);
	const FBangoValue* Find(const FString& Name) const;

	/**
	 * For changing a global in place (array element writes, Push, ...). The caller must keep the value's type:
	 * use FBangoValue's own array operations, which check element types. Counts as a change to the global, even if
	 * the caller ends up not changing it.
	 */
	FBangoValue* FindMutable(const FString& Name);

	/**
	 * The stamp of the latest change to any global: it goes up by one with each change. A change is creating a
	 * global, setting it to a different value, finding it to change in place, and ResetAll or loading (which change
	 * every global). Stamps aren't saved.
	 */
	uint64 GetChangeStamp() const { return ChangeStamp; }

	/** The stamp of the latest change to Name (0 if it hasn't changed since the store was made). */
	uint64 GetChangeStamp(const FString& Name) const;

	/** False if the global was never created, or holds nil. Never reports an error. */
	bool IsDefined(const FString& Name) const;

	/** Removes all globals, e.g. when starting a new game. */
	void ResetAll();

	/**
	 * Removes the world globals and keeps the persistent ones: the world they belong to has ended (the player left
	 * the level). Every slot is renewed (GetLayout), as for ResetAll.
	 */
	void ResetWorldGlobals();

	/** True for a persistent global; false for a world global, or one that doesn't exist. */
	bool IsPersistent(const FString& Name) const
	{
		const int32* Slot = Slots.Find(Name);
		return Slot && Entries[*Slot].bPersistent;
	}
	bool IsSlotPersistent(int32 Slot) const { return Entries[Slot].bPersistent; }

	/** Saves or loads every global. Loading replaces the current globals. */
	void Serialize(FArchive& Ar);

	int32 Num() const { return Entries.Num(); }

	/**
	 * By slot, for a caller that reads or writes the same globals over and over (the VM): looking a global up by
	 * name hashes the name every time. A slot is only good for the layout it was found in: keep GetLayout() with it,
	 * and find it again once that has changed.
	 */
	int32 FindSlot(const FString& Name) const
	{
		const int32* Slot = Slots.Find(Name);
		return Slot ? *Slot : INDEX_NONE;
	}

	/**
	 * A number no other store has, and that this one changes whenever its slots stop meaning what they did (ResetAll,
	 * loading). A copy of a store gets its own. Never 0.
	 */
	uint64 GetLayout() const { return Layout.Number; }

	/** The global in Slot, which must be one FindSlot gave for this layout. */
	const FBangoValue& GetSlot(int32 Slot) const { return Entries[Slot].Value; }

	/** As FindMutable, by slot. */
	FBangoValue& GetSlotMutable(int32 Slot)
	{
		FEntry& Entry = Entries[Slot];
		Entry.Stamp = ++ChangeStamp;
		return Entry.Value;
	}

private:
	struct FEntry
	{
		FString Name;
		FBangoValue Value;
		/** Its latest change. */
		uint64 Stamp = 0;
		/** A persistent global (kept by ResetWorldGlobals). */
		bool bPersistent = false;
	};

	/** The layout's number: a new one for every store, and for every copy of one. */
	struct FLayout
	{
		uint64 Number;
		FLayout() : Number(MakeNumber()) {}
		FLayout(const FLayout&) : Number(MakeNumber()) {}
		FLayout& operator=(const FLayout&) { Number = MakeNumber(); return *this; }
		void Renew() { Number = MakeNumber(); }
		BANGOSCRIPTS_API static uint64 MakeNumber();
	};

	/** Every global is new or gone (ResetAll, loading): the slots mean something else now. */
	void NoteAllChanged() { AllChangedStamp = ++ChangeStamp; Layout.Renew(); }

	/** The globals, each at its slot. Only added to, until they're all replaced. */
	TArray<FEntry> Entries;
	/** Each global's slot. */
	TBangoNameMap<int32> Slots;
	uint64 ChangeStamp = 0;
	/** The stamp of the last time every global was replaced: what a global that doesn't exist was last changed at. */
	uint64 AllChangedStamp = 0;
	FLayout Layout;
};
