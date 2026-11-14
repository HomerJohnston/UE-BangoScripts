#pragma once

#include "CoreMinimal.h"

/** One translatable text in a script, as Unreal's localization gathering sees it (spec section 15, Translation). */
struct FBangoLocalizedTextInfo
{
	FString Namespace;
	FString Key;
	/** The text in the script's own language. */
	FString Source;
	/** Notes for translators (NOTELOCTEXT), or empty. */
	FString Notes;
	int32 Line = 0;
	int32 Column = 0;
};

namespace Bango
{
	/**
	 * The script's translatable texts (LOCTEXT, NSLOCTEXT, NOTELOCTEXT, NSNOTELOCTEXT), in file order, with their
	 * namespaces worked out (the script's LOCTEXT_NAMESPACE, or their own). Each namespace and key is listed once.
	 * Texts in disabled code (comments) aren't included, and neither are INVTEXT texts. Only parses the script, so it
	 * doesn't need its commands to be registered.
	 *
	 * Returns false, with OutError, if the script doesn't parse (then nothing is listed). A LOCTEXT in a script with no
	 * LOCTEXT_NAMESPACE (a compile error) is skipped.
	 */
	BANGOSCRIPTS_API bool GetLocalizedTexts(const FString& Source, const FString& FileName, TArray<FBangoLocalizedTextInfo>& OutTexts, FString& OutError);
}
