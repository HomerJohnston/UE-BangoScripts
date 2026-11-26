#include "BangoSettings.h"
#include "BangoDefines.h"
#include "BangoInlineScript.h"
#include "BangoLog.h"

UBangoSettings::UBangoSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Bango");
}

#if WITH_EDITOR
void UBangoSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		Bango::ApplyDefineSettings(); // does nothing if the defines are the same
	}
}
#endif

void UBangoSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		Bango::ApplyDefineSettings();
	}
}

void Bango::ApplyDefineSettings()
{
	TArray<FBangoDefineText> Rows;
	for (const FBangoDefineSetting& Setting : GetDefault<UBangoSettings>()->Defines)
	{
		FBangoDefineText& Row = Rows.AddDefaulted_GetRef();
		Row.Name = Setting.Name;
		Row.TypeName = Setting.Type;
		Row.ValueText = Setting.Value;
		Row.Category = Setting.Category;
		Row.Description = Setting.Description;
	}
	TArray<FString> Errors;
	SetProjectDefines(Rows, &Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogBango, Warning, TEXT("Project Settings > Plugins > Bango > Defines: %s This define is left out."), *Error);
	}
}

float FBangoInlineCondition::GetPollInterval() const
{
	return PollInterval >= 0.0f ? PollInterval : GetDefault<UBangoSettings>()->DefaultConditionPollInterval;
}
