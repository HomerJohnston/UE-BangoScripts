#include "BangoLocalization.h"

#include "BangoAST.h"
#include "BangoParser.h"

namespace Bango
{
	bool GetLocalizedTexts(const FString& Source, const FString& FileName, TArray<FBangoLocalizedTextInfo>& OutTexts, FString& OutError)
	{
		OutTexts.Reset();
		FBangoScript Script;
		FBangoCompileError Error;
		if (!ParseScript(Source, FileName, Script, Error))
		{
			OutError = Error.ToString();
			return false;
		}

		TSet<FString> Seen;
		for (const FBangoLiteralExpr* Lit : Script.TextLiterals)
		{
			if (!Lit || Lit->bTextInvariant)
			{
				continue;
			}
			FString Namespace;
			if (Lit->bTextHasNamespace)
			{
				Namespace = Lit->TextNamespace;
			}
			else if (Script.bHasLocNamespace)
			{
				Namespace = Script.LocNamespace;
			}
			else
			{
				continue; // the compiler reports it
			}
			const FString Id = Namespace + TEXT("\n") + Lit->TextKey;
			if (Seen.Contains(Id))
			{
				continue; // the same text used again (a different one with the same key is a compile error)
			}
			Seen.Add(Id);

			FBangoLocalizedTextInfo& Info = OutTexts.AddDefaulted_GetRef();
			Info.Namespace = Namespace;
			Info.Key = Lit->TextKey;
			Info.Source = Lit->StringValue;
			Info.Notes = Lit->TextNotes;
			Info.Line = Lit->Loc.Line;
			Info.Column = Lit->Loc.Column;
		}
		return true;
	}
}
