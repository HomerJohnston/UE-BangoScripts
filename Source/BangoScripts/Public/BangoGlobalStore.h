#pragma once

#include "CoreMinimal.h"
#include "BangoContainers.h"
#include "BangoTypes.h"

/**
 * The Bango global variables (spec section 4), as a plain C++ object.
 * UBangoGlobalsSubsystem owns the game's store; tests and tools can create their own.
 *
 * - Declare() creates a global (holding nil) or confirms an existing one has the same type.
 *   A different type is a conflict: the caller fails, and the existing global is kept.
 * - Find() returns nullptr for a global that was never created. Scripts treat that the same as nil.
 * - Set() requires the global to exist, and the value to fit its type.
 * - Each change is stamped (GetChangeStamp), so a script waiting on globals (`waitUntil`) can tell whether any of
 *   them has changed since it last looked.
 */
class BANGOSCRIPTS_API FBangoGlobalStore
{
public:
	bool Declare(const FString& Name, const FBangoType& Type, FString& OutError);
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
	 * A script's default for Name (`global int stage = 1;`) was skipped while the global held nil: another
	 * declaration, without a value, created it first. True the first time for each global (so it's warned about
	 * once), until ResetAll or loading.
	 */
	bool NoteSkippedDefault(const FString& Name);

	/** Saves or loads every global. Loading replaces the current globals. */
	void Serialize(FArchive& Ar);

	int32 Num() const { return Globals.Num(); }

private:
	/** Stamps Name as changed. */
	void NoteChanged(const FString& Name) { ChangeStamps.FindOrAdd(Name) = ++ChangeStamp; }
	/** Stamps every global as changed (ResetAll, loading). */
	void NoteAllChanged() { AllChangedStamp = ++ChangeStamp; ChangeStamps.Reset(); }

	TBangoNameMap<FBangoValue> Globals;
	TBangoNameMap<bool> WarnedSkippedDefaults;
	uint64 ChangeStamp = 0;
	/** Each global's latest change since AllChangedStamp (ones changed only before it aren't kept). */
	TBangoNameMap<uint64> ChangeStamps;
	uint64 AllChangedStamp = 0;
};
