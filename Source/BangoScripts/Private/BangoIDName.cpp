#include "BangoIDName.h"

#include "UObject/PropertyTag.h"

bool FBangoIDName::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	if (Tag.Type == NAME_StrProperty)
	{
		Slot << Name;
		return true;
	}
	return false;
}
