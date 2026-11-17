#pragma once

#include "CoreMinimal.h"

/**
 * The level editor toolbar's Bango dropdown (spec section 15, Level start scripts): opens, creates, chooses and removes
 * the level's start script (UBangoLevelStartScript on the World Settings).
 */
class FBangoLevelStartScriptMenu
{
public:
	void Startup();
	void Shutdown();

private:
	void RegisterMenus();
};
