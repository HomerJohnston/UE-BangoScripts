#include "BangoScriptCache.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "BangoCompiler.h"
#include "BangoScriptAsset.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Package.h"
#include <atomic>

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

uint64 FBangoScriptCache::NewGeneration()
{
	static std::atomic<uint64> Next{1};
	return Next.fetch_add(1, std::memory_order_relaxed);
}

bool FBangoScriptCache::GetProgram(const UBangoScriptAsset& Asset, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Bango_GetProgram); // compiles the script the first time it's asked for
	// The usual case, a script spawned again unchanged: what the asset remembers, without making its name or looking
	// it up by it.
	const UPackage* Package = Asset.GetOutermost();
	const FName PackageName = Package ? Package->GetFName() : FName();
	if (const FBangoScriptCompiled* Last = Asset.LastCompiled.Get())
	{
		if (Last->Generation == Generation && Last->Package == PackageName && Last->Program.IsValid()
			&& Last->Source.Equals(Asset.Source, ESearchCase::CaseSensitive))
		{
			OutProgram = Last->Program;
			OutDisplayPath = Last->DisplayPath;
			return true;
		}
	}
	const auto Remember = [this, &Asset, PackageName](const TSharedPtr<const FBangoProgram>& Program, const FString& DisplayPath)
	{
		const TSharedRef<FBangoScriptCompiled> Compiled = MakeShared<FBangoScriptCompiled>();
		Compiled->Package = PackageName;
		Compiled->DisplayPath = DisplayPath;
		Compiled->Source = Asset.Source;
		Compiled->Program = Program;
		Compiled->Generation = Generation;
		Asset.LastCompiled = Compiled;
	};

	const FString Name = Asset.GetScriptName();
	OutDisplayPath = Name + TEXT(".bango");
	if (const FEntry* Entry = Entries.Find(Name))
	{
		if (Entry->Source.Equals(Asset.Source, ESearchCase::CaseSensitive))
		{
			OutProgram = Entry->Program;
			Remember(OutProgram, OutDisplayPath);
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
	Remember(OutProgram, OutDisplayPath);
	return true;
}

void FBangoScriptCache::Clear()
{
	Entries.Reset();
	Generation = NewGeneration(); // what the assets remember is from before
}
