#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "BangoProgram.h"

/** Helpers for showing Bango script parameters as Blueprint pins. */
namespace BangoPinUtils
{
	/**
	 * The Blueprint pin type for a Bango type: bool, int (Integer), double, string, Actor (for entity), or an array of those.
	 * Fails for arrays of arrays, which Blueprint can't pass.
	 */
	bool MakePinType(const FBangoType& Type, FEdGraphPinType& OutPinType, FString& OutError);

	/** True if two pin types are the same (category, subcategory, object and container). */
	bool PinTypesMatch(const FEdGraphPinType& A, const FEdGraphPinType& B);

	/**
	 * The text a pin shows as its default for a parameter: "5", "2.5", "true", "hello".
	 * Empty for parameters without a default, nil defaults, arrays and entities (which have no pin default).
	 */
	FString DefaultValueText(const FBangoScriptParamInfo& Param);

	/**
	 * True if an unconnected pin holding PinDefaultValue counts as "not passed" for an optional parameter,
	 * so the script uses its own default: the pin still shows the script's default (compared by value),
	 * or is empty for a nil default. Arrays and entities count as not passed whenever unconnected.
	 */
	bool IsUnchangedDefault(const FBangoScriptParamInfo& Param, const FString& PinDefaultValue);

	/** The name of the UBangoBlueprintLibrary function that adds an argument of this type ("AddBangoArgInt", ...). */
	FName GetAddFunctionName(const FBangoType& Type);

	/** Pin label for a parameter: its name without the leading underscore. */
	FString PinLabel(const FString& ParamName);
}
