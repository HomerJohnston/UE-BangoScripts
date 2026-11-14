#include "BangoScriptCache.h"

#include "BangoCompiler.h"
#include "BangoScriptAsset.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

bool FBangoScriptCache::GetProgram(const FString& ScriptPath, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError)
{
	FString PackagePath;
	if (!Bango::ResolveScriptPath(ScriptPath, Bango::DefaultScriptFolder(), PackagePath, OutError))
	{
		return false;
	}
	OutDisplayPath = PackagePath + TEXT(".bango");
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(PackagePath, /*bIncludeReadOnlyRoots*/ false, &Reason))
	{
		OutError = FString::Printf(TEXT("Script path '%s' isn't an asset path: %s"), *ScriptPath, *Reason.ToString());
		return false;
	}

	// Loaded already, or on disk (checked first, so a missing script doesn't log the engine's load warning).
	const FSoftObjectPath ObjectPath(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath));
	UObject* Object = ObjectPath.ResolveObject();
	if (!Object && FPackageName::DoesPackageExist(PackagePath))
	{
		Object = ObjectPath.TryLoad();
	}
	if (!Object)
	{
		OutError = FString::Printf(TEXT("Script '%s' not found: there's no Bango Script asset at %s."), *ScriptPath, *PackagePath);
		return false;
	}
	const UBangoScriptAsset* Asset = Cast<UBangoScriptAsset>(Object);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("'%s' isn't a Bango Script (it's a %s)."), *PackagePath, *Object->GetClass()->GetName());
		return false;
	}
	return GetProgram(*Asset, Commands, OutProgram, OutDisplayPath, OutError);
}

bool FBangoScriptCache::GetProgram(const UBangoScriptAsset& Asset, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError)
{
	const FString Name = Asset.GetScriptName();
	OutDisplayPath = Name + TEXT(".bango");
	if (const FEntry* Entry = Entries.Find(Name))
	{
		if (Entry->Source.Equals(Asset.Source, ESearchCase::CaseSensitive))
		{
			OutProgram = Entry->Program;
			return true;
		}
	}

	TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	if (!Bango::CompileSource(Asset.Source, OutDisplayPath, Commands, *Program, CompileError))
	{
		OutError = CompileError.ToString();
		return false;
	}

	FEntry& NewEntry = Entries.FindOrAdd(Name);
	NewEntry.Program = Program;
	NewEntry.Source = Asset.Source;
	OutProgram = Program;
	return true;
}

void FBangoScriptCache::Clear()
{
	Entries.Reset();
}
