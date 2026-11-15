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
	 * An inline script's compiled program (FBangoInlineScript, spec section 15), compiled the first time this text is
	 * asked for under this Name (what errors call it) and kept: the same text isn't compiled again.
	 */
	bool GetInlineProgram(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);

private:
	FBangoCommandTable Commands;
	FBangoScriptCache ScriptCache;
	/** GetInlineProgram's programs, by name and text. Emptied when it gets large (they're made again as needed). */
	TMap<FString, TSharedPtr<const FBangoProgram>> InlinePrograms;
	static constexpr int32 MaxInlinePrograms = 1024;
};
