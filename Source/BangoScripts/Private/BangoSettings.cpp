#include "BangoSettings.h"
#include "BangoInlineScript.h"

UBangoSettings::UBangoSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Bango");
}

float FBangoInlineCondition::GetPollInterval() const
{
	return PollInterval >= 0.0f ? PollInterval : GetDefault<UBangoSettings>()->DefaultConditionPollInterval;
}
