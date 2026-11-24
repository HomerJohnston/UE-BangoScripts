#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "BangoCommand.h"
#include "BangoScriptCache.h"

struct FBangoInlineScript;
struct FBangoInputsSignature;

/**
 * The Bango runtime module. Holds the command table every script is compiled and run against,
 * and the cache of compiled scripts.
 *
 * Game code adds its own commands, typically in its module's StartupModule:
 *
 *   FBangoCommand Speak;
 *   Speak.Signature = FBangoCommandSignature::Make(TEXT("Speak")).Target(FBangoType::Entity()).Param(TEXT("text"), FBangoType::String());
 *   Speak.Start = [](FBangoCommandCall& Call) { ...; return EBangoCommandResult::Done; };
 *   FBangoModule::Get().RegisterCommand(Speak);
 */
class UBangoScriptAsset;

class BANGOSCRIPTS_API FBangoModule : public IModuleInterface
{
public:
	static FBangoModule& Get();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Adds a command. Shows an error and returns false if the name is invalid or already taken. */
	bool RegisterCommand(const FBangoCommand& Command);

	const FBangoCommandTable& GetCommands() const { return Commands; }

	/** Reads a script's parameters (compiling it if needed) without running it. */
	bool GetScriptParams(const FString& ScriptPath, TArray<FBangoScriptParamInfo>& OutParams, FString& OutError);
	/** GetScriptParams, for a script asset (the Spawn Bango Script node's). */
	bool GetScriptParams(const UBangoScriptAsset& Script, TArray<FBangoScriptParamInfo>& OutParams, FString& OutError);
	FBangoScriptCache& GetScriptCache() { return ScriptCache; }

	/**
	 * An inline script's compiled program (FBangoInlineScript, spec section 15), with its inputs signature if it has
	 * one, compiled the first time this text is asked for under this Name (what errors call it) and kept: the same text
	 * isn't compiled again. Fails, with OutError, if it doesn't compile or its signature isn't registered.
	 *
	 * The script remembers the answer (FBangoInlineScriptBase::LastCompiled), so asking again for a script that hasn't
	 * changed only compares its text. OutSource, if given, gets the one shared copy of the text it was compiled from.
	 * NameOwner, if given, is the object Name was made from (UBangoScriptSubsystem::GetInlineScriptName), kept with
	 * what the script remembers so the name needn't be made again for the same owner.
	 */
	bool GetInlineProgram(const FBangoInlineScript& Script, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError,
		TSharedPtr<const FString>* OutSource = nullptr, const UObject* NameOwner = nullptr);

	/** GetInlineProgram for a script's text and its signature (null for none), as a saved script is restored. */
	bool GetInlineProgram(const FString& Source, const FBangoInputsSignature* Signature, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);

private:
	FBangoCommandTable Commands;
	FBangoScriptCache ScriptCache;
	/** GetInlineProgram's programs, by name and text. Emptied when it gets large (they're made again as needed). */
	TMap<FString, TSharedPtr<const FBangoProgram>> InlinePrograms;
	static constexpr int32 MaxInlinePrograms = 1024;
};
