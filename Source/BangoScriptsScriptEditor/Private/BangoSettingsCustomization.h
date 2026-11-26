#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Project Settings > Plugins > Bango: under the project's own Defines, the ones other plugins registered
 * (Bango::RegisterDefines), shown, not edited: each plugin's code gives them.
 */
class FBangoSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization
};
