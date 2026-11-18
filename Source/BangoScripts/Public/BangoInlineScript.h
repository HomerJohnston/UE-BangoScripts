#pragma once

#include "CoreMinimal.h"
#include "BangoInlineScript.generated.h"

/**
 * A Bango script kept in a property of any class or Blueprint, instead of in a Bango Script asset (spec section 15):
 * usually a line or two, like `Sleep(4); player AddItem("Pistol");`. In the editor's details panel it's a box to
 * type the script in, with an Edit button that opens the full graph editor in a window of its own.
 *
 * The script may start with a `#inputs (...)` line; whoever spawns it passes the values, in order.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Bango Inline Script"))
struct BANGOSCRIPTS_API FBangoInlineScript
{
	GENERATED_BODY()

	/** The script's text, as in a .bango file. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bango")
	FString Source;

	/** True if there's no script (nothing but spaces). */
	bool IsEmpty() const { return Source.TrimStartAndEnd().IsEmpty(); }

	bool operator==(const FBangoInlineScript& Other) const { return Source.Equals(Other.Source, ESearchCase::CaseSensitive); }
	bool operator!=(const FBangoInlineScript& Other) const { return !(*this == Other); }
};
