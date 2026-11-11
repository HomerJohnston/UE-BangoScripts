#include "BangoScriptCache.h"

#include "BangoCompiler.h"
#include "BangoSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace Bango
{
	bool NormalizeScriptPath(const FString& ScriptPath, FString& OutPath, FString& OutError)
	{
		FString Path = ScriptPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);

		if (Path.IsEmpty())
		{
			OutError = TEXT("No script path was given.");
			return false;
		}
		if (Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive) || Path.Contains(TEXT(":"), ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(TEXT("Script path '%s' must be relative to the Bango script folder, like 'Quest/Intro'."), *ScriptPath);
			return false;
		}

		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("/"), true);
		for (const FString& Segment : Segments)
		{
			if (Segment.Equals(TEXT(".."), ESearchCase::CaseSensitive) || Segment.Equals(TEXT("."), ESearchCase::CaseSensitive))
			{
				OutError = FString::Printf(TEXT("Script path '%s' can't contain '.' or '..' folders."), *ScriptPath);
				return false;
			}
		}
		Path = FString::Join(Segments, TEXT("/"));

		if (!Path.EndsWith(TEXT(".bango"), ESearchCase::IgnoreCase))
		{
			Path += TEXT(".bango");
		}
		OutPath = Path;
		return true;
	}

	FString GetScriptFilePath(const FString& NormalizedPath)
	{
		const UBangoSettings* Settings = GetDefault<UBangoSettings>();
		return FPaths::Combine(FPaths::ProjectContentDir(), Settings->ScriptDirectory, NormalizedPath);
	}
}

bool FBangoScriptCache::GetProgram(const FString& ScriptPath, const IBangoCommandLookup& Commands, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutDisplayPath, FString& OutError)
{
	if (!Bango::NormalizeScriptPath(ScriptPath, OutDisplayPath, OutError))
	{
		return false;
	}

	const FString FilePath = Bango::GetScriptFilePath(OutDisplayPath);
	const FDateTime TimeStamp = IFileManager::Get().GetTimeStamp(*FilePath);
	if (TimeStamp == FDateTime::MinValue())
	{
		OutError = FString::Printf(TEXT("Script '%s' not found (looked for %s)."), *OutDisplayPath, *FPaths::ConvertRelativePathToFull(FilePath));
		return false;
	}

	if (const FEntry* Entry = Entries.Find(OutDisplayPath))
	{
		if (Entry->TimeStamp == TimeStamp)
		{
			OutProgram = Entry->Program;
			return true;
		}
	}

	FString Source;
	if (!FFileHelper::LoadFileToString(Source, *FilePath))
	{
		OutError = FString::Printf(TEXT("Couldn't read script '%s'."), *OutDisplayPath);
		return false;
	}

	TSharedPtr<FBangoProgram> Program = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	if (!Bango::CompileSource(Source, OutDisplayPath, Commands, *Program, CompileError))
	{
		OutError = CompileError.ToString();
		return false;
	}

	FEntry NewEntry;
	NewEntry.Program = Program;
	NewEntry.TimeStamp = TimeStamp;
	Entries.Add(OutDisplayPath, NewEntry);
	OutProgram = Program;
	return true;
}

void FBangoScriptCache::Clear()
{
	Entries.Reset();
}
