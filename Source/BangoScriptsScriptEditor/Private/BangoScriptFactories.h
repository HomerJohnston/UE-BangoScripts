#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "Factories/Factory.h"
#include "BangoScriptFactories.generated.h"

/**
 * Imports a .bango file as a linked Bango Script (Bango spec section 15): dragging one into the Content Browser, or the
 * engine's own auto import. The file is put next to the asset, with its name, if it isn't there already; from then on
 * the file is the script. (New files in Content are also imported by FBangoScriptSync.)
 */
UCLASS()
class UBangoScriptImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UBangoScriptImportFactory();

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

/** Add > Bango Script in the Content Browser: a new embedded script (its text is kept in the asset). */
UCLASS()
class UBangoScriptNewFactory : public UFactory
{
	GENERATED_BODY()

public:
	UBangoScriptNewFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};

/** How Bango Script assets look and behave in the Content Browser. */
UCLASS()
class UAssetDefinition_BangoScript : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	/** Opens each script in the Bango Script editor (FBangoScriptAssetEditor). */
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
