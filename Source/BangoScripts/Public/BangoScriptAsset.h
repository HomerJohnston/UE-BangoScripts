#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BangoScriptAsset.generated.h"

/**
 * A Bango script (Bango spec section 15): an asset anywhere in Content, named by its asset path ("/Game/Quests/Intro").
 *
 * Embedded: Source is the script. Linked (bLinked): the script is the .bango file next to this asset, with the same
 * name (Intro.uasset and Intro.bango), and the file always wins: Source is a copy of it, kept for packaged games and
 * the runtime, refreshed by the editor whenever the file changes.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Bango Script"))
class BANGOSCRIPTS_API UBangoScriptAsset : public UObject
{
	GENERATED_BODY()

public:
	/** The script's text (for a linked script, a copy of its .bango file). Compiled when the script is first spawned. */
	UPROPERTY()
	FString Source;

	/** The script's name: its asset path, like "/Game/Quests/Intro". */
	FString GetScriptName() const;

#if WITH_EDITORONLY_DATA
	/** The script is the .bango file next to this asset (same folder, same name), and Source is a copy of it. */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Bango")
	bool bLinked = false;

	/** A hash of the linked file's text as last read or written, so a real change to the file can be told from the editor's own save. */
	UPROPERTY(AssetRegistrySearchable)
	FString SourceFileHash;
#endif
};
