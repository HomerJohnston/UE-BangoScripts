#include "Bango.h"

#include "BangoBuiltinCommands.h"
#include "BangoLog.h"

#define LOCTEXT_NAMESPACE "FBangoModule"

FBangoModule& FBangoModule::Get()
{
	return FModuleManager::LoadModuleChecked<FBangoModule>(TEXT("BangoScripts"));
}

void FBangoModule::StartupModule()
{
	Bango::RegisterBuiltinCommands(Commands);
}

void FBangoModule::ShutdownModule()
{
	ScriptCache.Clear();
}

bool FBangoModule::RegisterCommand(const FBangoCommand& Command)
{
	FString Error;
	if (!Commands.Add(Command, Error))
	{
		Bango::ReportError(FString::Printf(TEXT("Couldn't register command: %s"), *Error));
		return false;
	}
	// Convention: parameter names start with a capital letter (they're the labels on graph nodes).
	for (const FBangoCommandParam& Param : Command.Signature.Params)
	{
		if (!Param.Name.IsEmpty() && Param.Name[0] >= TEXT('a') && Param.Name[0] <= TEXT('z'))
		{
			UE_LOG(LogBango, Warning, TEXT("Command '%s': parameter '%s' should start with a capital letter, like the built-in commands' (it's the label on the command's graph node)."),
				*Command.Signature.Name, *Param.Name);
		}
	}
	// Scripts compiled before this command existed may now compile differently.
	ScriptCache.Clear();
	return true;
}

bool FBangoModule::GetScriptParams(const FString& ScriptPath, TArray<FBangoScriptParamInfo>& OutParams, FString& OutError)
{
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	if (!ScriptCache.GetProgram(ScriptPath, Commands, Program, DisplayPath, OutError))
	{
		return false;
	}
	OutParams = Program->Params;
	return true;
}

bool FBangoModule::GetScriptParams(const UBangoScriptAsset& Script, TArray<FBangoScriptParamInfo>& OutParams, FString& OutError)
{
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	if (!ScriptCache.GetProgram(Script, Commands, Program, DisplayPath, OutError))
	{
		return false;
	}
	OutParams = Program->Params;
	return true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBangoModule, BangoScripts)
