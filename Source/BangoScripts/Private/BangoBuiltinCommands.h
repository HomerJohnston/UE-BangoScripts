#pragma once

#include "CoreMinimal.h"
#include "BangoCommand.h"

namespace Bango
{
	/**
	 * Adds the commands every project gets:
	 *   Hint, Log, Format, Sleep, Random, RandomInt, Floor, Round, GameTime, ToEntity,
	 *   and for entities: SetPos, GetPos, SetHidden, DistanceTo.
	 */
	void RegisterBuiltinCommands(FBangoCommandTable& Commands);
}
