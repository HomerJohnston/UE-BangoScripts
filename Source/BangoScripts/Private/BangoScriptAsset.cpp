#include "BangoScriptAsset.h"

#include "UObject/Package.h"

#if WITH_EDITORONLY_DATA
#include "BangoLocalization.h"
#include "BangoLog.h"
#include "BangoText.h"
#include "Serialization/PropertyLocalizationDataGathering.h"
#endif

FString UBangoScriptAsset::GetScriptName() const
{
	const UPackage* Package = GetOutermost();
	return Package ? Package->GetName() : GetName();
}

#if WITH_EDITORONLY_DATA
namespace BangoScriptAssetPrivate
{
	/**
	 * Localization gathering for Bango Script assets (spec section 15, Translation): the script's text isn't in text
	 * properties, so its LOCTEXTs are reported here, with their namespaces, keys and translator notes. This runs when
	 * the asset is saved (the result is kept in the package for "Gather from Packages"), and while gathering.
	 */
	void GatherBangoScriptForLocalization(const UObject* Object, FPropertyLocalizationDataGatherer& Gatherer, const EPropertyLocalizationGathererTextFlags GatherTextFlags)
	{
		Gatherer.GatherLocalizationDataFromObject(Object, GatherTextFlags); // its ordinary properties, as for any object

		const UBangoScriptAsset* Script = CastChecked<UBangoScriptAsset>(Object);
		const FString FileName = Script->GetScriptName() + TEXT(".bango");
		TArray<FBangoLocalizedTextInfo> Texts;
		FString Error;
		if (!Bango::GetLocalizedTexts(Script->Source, FileName, Texts, Error))
		{
			UE_LOG(LogBango, Warning, TEXT("%s doesn't read, so its texts weren't gathered for translation: %s"), *FileName, *Error);
			return;
		}
		for (const FBangoLocalizedTextInfo& Text : Texts)
		{
			Gatherer.GatherTextInstance(Bango::MakeLocalizedText(Text.Namespace, Text.Key, Text.Source, Text.Notes),
				FString::Printf(TEXT("%s(%d:%d)"), *FileName, Text.Line, Text.Column), /*bIsEditorOnly*/ false);
		}
	}

	static FAutoRegisterLocalizationDataGatheringCallback AutomaticRegistrationOfLocalizationGatherer(
		UBangoScriptAsset::StaticClass(), &GatherBangoScriptForLocalization);
}
#endif
