#pragma once

#include "CoreMinimal.h"

class FBangoValue;

/**
 * Localized text (the `text` type, spec section 3): making FTexts from what a script says, and formatting them.
 * Everything that touches Unreal's localization goes through here.
 */
namespace Bango
{
	/**
	 * The text with this namespace and key, in the current language: the translation if there is one, otherwise
	 * Source. It changes language with the game. Notes are for translators.
	 */
	BANGOSCRIPTS_API FText MakeLocalizedText(const FString& Namespace, const FString& Key, const FString& Source, const FString& Notes);

	/** Text that is never translated (ToText, and strings or numbers shown inside a formatted text). */
	BANGOSCRIPTS_API FText MakeInvariantText(const FString& String);

	/**
	 * FText::Format: replaces {0}, {1}, ... in Pattern with Args (spec section 7, FormatText). Numbers are shown the way
	 * the current language writes them, texts in the current language, and everything else as ToDisplayString shows
	 * it. Fails if the pattern uses a placeholder with no value, or a named one like {Name} (see CountTextFormatValues).
	 */
	BANGOSCRIPTS_API bool FormatLocalizedText(const FText& Pattern, TConstArrayView<FBangoValue> Args, FText& OutText, FString& OutError);

	/** A new random key for a text, like the ones Unreal makes for text in assets: 32 hexadecimal characters. */
	BANGOSCRIPTS_API FString MakeTextKey();
}
