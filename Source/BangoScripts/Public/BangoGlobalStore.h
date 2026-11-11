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
 */
class BANGOSCRIPTS_API FBangoGlobalStore
{
public:
	bool Declare(const FString& Name, const FBangoType& Type, FString& OutError);
	bool Set(const FString& Name, const FBangoValue& Value, FString& OutError);
	const FBangoValue* Find(const FString& Name) const;

	/**
	 * For changing a global in place (array element writes, Push, ...). The caller must keep the value's type:
	 * use FBangoValue's own array operations, which check element types.
	 */
	FBangoValue* FindMutable(const FString& Name);

	/** False if the global was never created, or holds nil. Never reports an error. */
	bool IsDefined(const FString& Name) const;

	/** Removes all globals, e.g. when starting a new game. */
	void ResetAll();

	/** Saves or loads every global. Loading replaces the current globals. */
	void Serialize(FArchive& Ar);

	int32 Num() const { return Globals.Num(); }

private:
	TBangoNameMap<FBangoValue> Globals;
};
