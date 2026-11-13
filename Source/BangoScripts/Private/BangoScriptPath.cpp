#include "BangoScriptPath.h"

namespace Bango
{
	bool ResolveScriptPath(const FString& ScriptPath, const FString& RelativeToFolder, FString& OutPackagePath, FString& OutError)
	{
		FString Path = ScriptPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		if (Path.EndsWith(TEXT(".bango"), ESearchCase::IgnoreCase))
		{
			Path = Path.Left(Path.Len() - 6);
		}
		if (Path.IsEmpty())
		{
			OutError = TEXT("No script path was given.");
			return false;
		}
		if (Path.Contains(TEXT(":"), ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(TEXT("Script path '%s' can't have a ':'. Name a script by its asset path, like '/Game/Quests/Intro', or relative to this one, like 'Intro'."), *ScriptPath);
			return false;
		}

		const bool bFull = Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive);
		const FString Joined = bFull ? Path : RelativeToFolder + TEXT("/") + Path;
		TArray<FString> Segments;
		Joined.ParseIntoArray(Segments, TEXT("/"), true);

		TArray<FString> Resolved;
		for (const FString& Segment : Segments)
		{
			if (Segment.Equals(TEXT("."), ESearchCase::CaseSensitive))
			{
				continue;
			}
			if (Segment.Equals(TEXT(".."), ESearchCase::CaseSensitive))
			{
				if (Resolved.Num() <= 1)
				{
					OutError = FString::Printf(TEXT("Script path '%s' goes above its content root (like /Game)."), *ScriptPath);
					return false;
				}
				Resolved.Pop();
				continue;
			}
			Resolved.Add(Segment);
		}

		// An object path ("Intro.Intro", as Copy Reference gives) names the same asset.
		if (Resolved.Num() > 0)
		{
			FString& Last = Resolved.Last();
			int32 Dot = INDEX_NONE;
			if (Last.FindChar(TEXT('.'), Dot))
			{
				const FString Name = Last.Left(Dot);
				const FString Object = Last.Mid(Dot + 1);
				if (Name.IsEmpty() || !Object.Equals(Name, ESearchCase::CaseSensitive))
				{
					OutError = FString::Printf(TEXT("Script path '%s' isn't a script's name: asset names can't have a '.'."), *ScriptPath);
					return false;
				}
				Last = Name;
			}
		}
		if (Resolved.Num() < 2)
		{
			OutError = FString::Printf(TEXT("Script path '%s' needs a folder and a name, like '/Game/Quests/Intro'."), *ScriptPath);
			return false;
		}
		OutPackagePath = TEXT("/") + FString::Join(Resolved, TEXT("/"));
		return true;
	}

	FString GetScriptPathFolder(const FString& ScriptName)
	{
		FString Name = ScriptName;
		if (Name.EndsWith(TEXT(".bango"), ESearchCase::IgnoreCase))
		{
			Name = Name.Left(Name.Len() - 6);
		}
		int32 Slash = INDEX_NONE;
		if (!Name.FindLastChar(TEXT('/'), Slash) || Slash <= 0)
		{
			return DefaultScriptFolder();
		}
		return Name.Left(Slash);
	}
}
