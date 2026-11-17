#include "BangoLevelStartScript.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "BangoScriptAsset.h"

UBangoLevelStartScript* UBangoLevelStartScript::Find(const UWorld* World)
{
	AWorldSettings* Settings = World ? World->GetWorldSettings(/*bCheckStreamingPersistent*/ false, /*bChecked*/ false) : nullptr;
	return Settings ? Cast<UBangoLevelStartScript>(Settings->GetAssetUserDataOfClass(UBangoLevelStartScript::StaticClass())) : nullptr;
}
