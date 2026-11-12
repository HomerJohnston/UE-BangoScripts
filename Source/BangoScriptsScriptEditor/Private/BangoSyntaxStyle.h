// Bango text editor: one text style per token kind.
// See "Bango Text Editor Spec", section 3.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "BangoLineTokenizer.h"

class UObject;

class FBangoSyntaxStyle
{
public:
	/**
	 * @param FontObject  A composite font (UFont) with "Regular", "Bold", "Italic" and "BoldItalic"
	 *                    typefaces, all monospace and the same width. If null, the engine's Mono
	 *                    font is used and bold and italic are ignored, since it has no such faces
	 *                    and a proportional bold would break the //@ metadata column.
	 */
	explicit FBangoSyntaxStyle(float FontSize = 10.0f, const UObject* FontObject = nullptr);

	const FTextBlockStyle& GetStyle(EBangoSyntaxKind Kind, bool bDisconnected) const;
	const FTextBlockStyle& GetDefaultStyle() const { return GetStyle(EBangoSyntaxKind::Default, false); }

	FLinearColor GetBackgroundColor() const { return BackgroundColor; }

private:
	static constexpr int32 NumKinds = static_cast<int32>(EBangoSyntaxKind::Count);

	// [Kind][0] connected, [Kind][1] disconnected.
	FTextBlockStyle Styles[NumKinds][2];
	FLinearColor BackgroundColor;
};
