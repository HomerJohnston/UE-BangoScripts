#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BangoCallTestObject.generated.h"

/** For the automation tests of `call` (Bango.Game.Call): functions a script can and can't call. */
UCLASS(Transient, HideDropdown, NotBlueprintable)
class UBangoCallTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float LastBrightness = 0.0f;

	UPROPERTY()
	bool bLastFade = false;

	UFUNCTION(BlueprintCallable, Category = "Bango")
	void TurnOn(float Brightness, bool Fade) { LastBrightness = Brightness; bLastFade = Fade; }

	UFUNCTION(BlueprintCallable, Category = "Bango")
	int32 GetCount() const { return 5; }

	UFUNCTION(BlueprintCallable, Category = "Bango|Lights")
	FString Describe(FName Label, FVector Where, int32 Times) const
	{
		return FString::Printf(TEXT("%s %.0f,%.0f,%.0f x%d"), *Label.ToString(), Where.X, Where.Y, Where.Z, Times);
	}

	UFUNCTION(BlueprintCallable, Category = "Bango")
	void GetTwo(int32& A, int32& B) const { A = 1; B = 2; }

	UFUNCTION(BlueprintCallable, Category = "Bango")
	void TakeArray(const TArray<int32>& Values) {}

	UFUNCTION(BlueprintCallable, Category = "Other")
	void NotForBango() {}
};
