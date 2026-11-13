#include "BangoScriptAsset.h"

#include "UObject/Package.h"

FString UBangoScriptAsset::GetScriptName() const
{
	const UPackage* Package = GetOutermost();
	return Package ? Package->GetName() : GetName();
}
