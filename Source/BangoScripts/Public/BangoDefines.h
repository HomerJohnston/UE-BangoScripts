#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"

/**
 * Defines (spec section 4, Defines): project-wide named values, like `SELECT_RANDOM = 3`, that every script can use by
 * name and none can change. A use compiles to the value itself, so it works wherever a fixed value does.
 *
 * The project's are in Project Settings > Plugins > Bango > Defines (UBangoSettings::Defines), and are given to the
 * table here when the module starts and whenever they're changed there (Bango::ApplyDefineSettings). Other plugins
 * add their own with Bango::RegisterDefines. Use these from the game thread.
 */
struct FBangoDefineEntry
{
	/** SHOUTING_SNAKE_CASE (Bango::IsDefineName). */
	FString Name;
	FBangoValue Value;
	/** The value as written in the settings (`3`, `[1, 2]`, `@narrator`), for showing. */
	FString ValueText;
	FString TypeName;
	/** For autocomplete and the editors' lists; either may be empty. */
	FString Category;
	FString Description;
	/** Who gave it: empty for the project's (Project Settings), else the plugin that registered it (Bango::RegisterDefines). */
	FString Source;
};

namespace Bango
{
	/**
	 * True if Name can be a define's name: SHOUTING_SNAKE_CASE, capital letters, digits and `_`, starting with a letter
	 * (`SELECT_RANDOM`, `MAX_PARTY_2`). If not, why, in OutWhy.
	 */
	BANGOSCRIPTS_API bool IsDefineName(const FString& Name, FString* OutWhy = nullptr);

	/** The define named Name, or null. Good until the defines next change. */
	BANGOSCRIPTS_API const FBangoDefineEntry* FindDefine(const FString& Name);
	/** Every define, in the order they were given. */
	BANGOSCRIPTS_API const TArray<FBangoDefineEntry>& GetDefines();
	/**
	 * Changes whenever the defines do. A program that uses a define keeps the one it was compiled with
	 * (FBangoProgram::DefinesFingerprint), and is compiled again when it differs.
	 */
	BANGOSCRIPTS_API uint64 GetDefinesFingerprint();

	/** One define as written (a row of Project Settings): checked and read by SetProjectDefines. */
	struct FBangoDefineText
	{
		FString Name;
		FString TypeName;
		FString ValueText;
		FString Category;
		FString Description;
	};
	/**
	 * Makes Defines the project's defines, in place of any it had. A row that can't be one (a name that isn't
	 * SHOUTING_SNAKE_CASE or is taken, a value that doesn't fit its type or isn't a fixed value, a translated text) is
	 * left out, with a line in OutErrors (each starts with the row's name). Changes the fingerprint if anything changed.
	 */
	BANGOSCRIPTS_API void SetProjectDefines(const TArray<FBangoDefineText>& Defines, TArray<FString>* OutErrors = nullptr);
	/** Why row Row of the project's defines was left out, or empty if it's a define (or there's no such row). */
	BANGOSCRIPTS_API FString GetDefineRowError(int32 Row);

	/**
	 * A plugin's own defines (spec section 4, Defines from plugins): call it from the module's StartupModule, and
	 * UnregisterDefines from its ShutdownModule. Owner names the plugin ("BangoYap"): it's the defines' category when
	 * a row has none, and what errors and the editors say they're from. Registering again for the same owner replaces
	 * its defines.
	 *
	 *     Bango::RegisterDefines(TEXT("BangoYap"), {
	 *         { TEXT("RUN_ALL"), TEXT("int"), TEXT("1"), TEXT("Speech"), TEXT("Speech run mode: play every line.") },
	 *         { TEXT("SELECT_RANDOM"), TEXT("int"), TEXT("3"), TEXT("Speech"), TEXT("Speech run mode: play one line, picked at random.") },
	 *     });
	 *
	 * Each row is checked as a project row is. A plugin's defines come before the project's, in the order the plugins
	 * registered: a name defined twice keeps the first, and the other is left out with an error, reported to the log
	 * (SetDefineErrorReporter) and in OutErrors, as is a project row that now clashes with one of these.
	 */
	BANGOSCRIPTS_API void RegisterDefines(const FString& Owner, const TArray<FBangoDefineText>& Defines, TArray<FString>* OutErrors = nullptr);
	/** Takes away the defines Owner registered. Does nothing if it has none. */
	BANGOSCRIPTS_API void UnregisterDefines(const FString& Owner);
	/** Why row Row of Owner's registered defines was left out, or empty. */
	BANGOSCRIPTS_API FString GetRegisteredDefineError(const FString& Owner, int32 Row);
	/** Where a define comes from, for messages: "in Project Settings > Plugins > Bango > Defines" or "registered by BangoYap". */
	BANGOSCRIPTS_API FString DescribeDefineSource(const FBangoDefineEntry& Define);
	/** Where RegisterDefines reports its errors (the BangoScripts module sends them to the log). */
	BANGOSCRIPTS_API void SetDefineErrorReporter(TFunction<void(const FString&)> Reporter);
	/**
	 * Checks one define as SetProjectDefines would: false, with why, if it can't be one. OutValue is its value. Taken
	 * lists names already used (the rows before it); may be null.
	 */
	BANGOSCRIPTS_API bool CheckDefine(const FBangoDefineText& Define, const TSet<FString>* Taken, FBangoValue& OutValue, FString& OutError);
}
