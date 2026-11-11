#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "BangoCommand.h"
#include "BangoScriptCache.h"

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
	FBangoScriptCache& GetScriptCache() { return ScriptCache; }

private:
	FBangoCommandTable Commands;
	FBangoScriptCache ScriptCache;
};
