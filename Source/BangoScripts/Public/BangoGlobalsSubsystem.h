#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BangoGlobalStore.h"
#include "BangoGlobalsSubsystem.generated.h"

/**
 * Holds the game's Bango global variables (spec section 4) on the game instance,
 * so globals survive level changes. The logic lives in FBangoGlobalStore.
 */
UCLASS()
class BANGOSCRIPTS_API UBangoGlobalsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's game instance, or nullptr (e.g. in an editor world). */
	static UBangoGlobalsSubsystem* Get(const UObject* WorldContext);

	FBangoGlobalStore& GetStore() { return Store; }
	const FBangoGlobalStore& GetStore() const { return Store; }

	// Shortcuts to the store.
	bool Declare(const FString& Name, const FBangoType& Type, FString& OutError) { return Store.Declare(Name, Type, OutError); }
	bool Set(const FString& Name, const FBangoValue& Value, FString& OutError) { return Store.Set(Name, Value, OutError); }
	const FBangoValue* Find(const FString& Name) const { return Store.Find(Name); }
	bool IsDefined(const FString& Name) const { return Store.IsDefined(Name); }
	void ResetAll() { Store.ResetAll(); }
	void SerializeGlobals(FArchive& Ar) { Store.Serialize(Ar); }

private:
	FBangoGlobalStore Store;
};
