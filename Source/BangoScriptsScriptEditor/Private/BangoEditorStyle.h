#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;
struct FSlateBrush;

/**
 * Slate style for the Bango graph editor. Icons live in Plugins/BangoScripts/Resources/Icons as SVG files;
 * replace them there to restyle the editor (keep the file names).
 */
class FBangoEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();

	/**
	 * The icon for a field's type ("int", "string[]", "any", ...) and the colour to tint it with.
	 * Arrays use the array icon tinted with their element type's colour. Returns null for an empty type.
	 */
	static const FSlateBrush* GetTypeIcon(const FString& TypeHint, FLinearColor& OutTint);

	/**
	 * The icon for a field that names a variable, `(x)`, tinted with its type's colour if the type is known
	 * (VariableType may be empty). Never null once the style is initialized.
	 */
	static const FSlateBrush* GetVariableIcon(const FString& VariableType, FLinearColor& OutTint);

	/** The icon and tint for a layout field: the variable icon for variable fields, else its type's icon (or null). */
	static const FSlateBrush* GetFieldIcon(const struct FBangoNodeField& Field, FLinearColor& OutTint);

	/** The colour of wires and of pin names: a mid grey, 75% as bright as white on screen. */
	static FLinearColor GetWireColor();
	/** A pin circle with no wire: dark grey, so the pins still to be wired stand out from the ones that are. */
	static FLinearColor GetDisconnectedPinColor();

	/** An exec pin's circle: filled when connected, a ring when not. Never null once the style is initialized. */
	static const FSlateBrush* GetPinBrush(bool bConnected);

	/** A plain white rectangle, to be tinted (like the script tabs' backgrounds). Never null. */
	static const FSlateBrush* GetWhiteBrush();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
