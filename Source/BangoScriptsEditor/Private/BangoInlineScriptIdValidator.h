#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "BangoInlineScriptIdValidator.generated.h"

/**
 * Checks inline scripts' IDs (FBangoInlineScriptBase::ScriptId), which their persistent variables are kept under
 * (spec section 4, Persistent variables): two scripts of one owner with the same ID would share theirs, and a script
 * with text and no ID is kept for its text. Both are errors, each with a Fix that gives the script a new ID.
 *
 * Runs wherever Unreal's data validation does: on save, on Validate Assets, and in a cook run with
 * `-RunAssetValidation -ValidationErrorsAreFatal`, which then fails. One owner is a Blueprint (its default object
 * and its component templates), an actor in a level (and its components), or any other asset.
 */
UCLASS()
class UBangoInlineScriptIdValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;

private:
	/** Reports Unit's problems; Owner names it in the messages, and Blueprint is what a fix marks as changed, if any. */
	void ValidateUnit(const FAssetData& AssetData, const TArray<UObject*>& Unit, const FString& OwnerName, class UBlueprint* Blueprint);
};
