#pragma once

#include "CoreMinimal.h"

struct FBangoProgram;

/**
 * A condition registered with Bango::RegisterCondition: a script, or one expression, that answers true or false,
 * compiled once and kept until it's unregistered. Check it with UBangoScriptSubsystem::EvaluateBoolCondition.
 */
struct BANGOSCRIPTS_API FBangoConditionHandle
{
	int64 Id = 0;

	bool IsValid() const { return Id != 0; }
	bool operator==(const FBangoConditionHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FBangoConditionHandle& Other) const { return Id != Other.Id; }
	friend uint32 GetTypeHash(const FBangoConditionHandle& Handle) { return (uint32)(Handle.Id ^ (Handle.Id >> 32)); }
};

namespace Bango
{
	/**
	 * Compiles a condition (see Bango::CompileCondition for what Source can be) and keeps it until UnregisterCondition,
	 * so checking it costs only running it. Name is what its errors show in place of a file name. There's no limit on how
	 * many are registered. Registering the same Name and Source again gives the same handle, and the condition is kept
	 * until each registration has been unregistered.
	 *
	 * Returns an invalid handle, with the compile error in OutError, if it doesn't compile. Commands it calls must be
	 * registered first (built-in commands always are).
	 */
	BANGOSCRIPTS_API FBangoConditionHandle RegisterCondition(const FString& Source, const FString& Name, FString& OutError);

	/** Releases one registration of a condition, and makes Handle invalid. Does nothing for an invalid or unknown handle. */
	BANGOSCRIPTS_API void UnregisterCondition(FBangoConditionHandle& Handle);

	/** The compiled condition, or null if Handle isn't registered. */
	BANGOSCRIPTS_API TSharedPtr<const FBangoProgram> FindCondition(FBangoConditionHandle Handle);

	/** How many different conditions are registered (for checking that a plugin unregisters its own). */
	BANGOSCRIPTS_API int32 GetNumRegisteredConditions();

	/** Compiles a condition without registering it, with the module's commands. Used by RegisterCondition. */
	BANGOSCRIPTS_API bool CompileConditionProgram(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);
}
