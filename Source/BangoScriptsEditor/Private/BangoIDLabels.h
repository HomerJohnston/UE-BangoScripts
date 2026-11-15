#pragma once

#include "CoreMinimal.h"

/**
 * Draws each Bango ID component's name ("@john") over its actor in the level editor's viewports: screen-facing text on
 * the viewport's debug canvas (UDebugDrawService), fading out with distance from the camera. A name scripts can't use
 * (empty, invalid, or used by another actor in the level) is drawn in red with why. Turned on and off with the
 * viewport's Show > Bango IDs flag (on by default). Not drawn while playing (PIE) or in asset editors' viewports.
 */
namespace BangoIDLabels
{
	void Register();
	void Unregister();
}
