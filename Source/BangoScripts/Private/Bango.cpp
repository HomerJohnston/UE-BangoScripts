#include "Bango.h"

#include "BangoBuiltinCommands.h"
#include "BangoCompiler.h"
#include "BangoInlineScript.h"
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

bool FBangoModule::GetInlineProgram(const FBangoInlineScript& Script, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError)
{
	FString SignatureError;
	const TSharedPtr<const FBangoInputsSignature> Signature = Script.FindInputsSignature(SignatureError);
	if (Script.HasInputsSignature() && !Signature.IsValid())
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Name, *SignatureError);
		return false;
	}
	return GetInlineProgram(Script.Source, Signature.Get(), Name, OutProgram, OutError);
}

bool FBangoModule::GetInlineProgram(const FString& Source, const FBangoInputsSignature* Signature, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError)
{
	// The signature is part of what it was compiled as: the same text with other inputs is another program.
	const FString Key = Name + TEXT("\n") + (Signature ? Signature->Label + TEXT(": ") + Signature->ToInputsText() : FString()) + TEXT("\n") + Source;
	if (const TSharedPtr<const FBangoProgram>* Found = InlinePrograms.Find(Key))
	{
		OutProgram = *Found;
		return true;
	}
	TSharedPtr<FBangoProgram> Compiled = MakeShared<FBangoProgram>();
	FBangoCompileError Error;
	if (!Bango::CompileSource(Source, Name, Commands, *Compiled, Error, Signature))
	{
		OutError = Error.ToString(); // not kept: the text is probably being fixed
		return false;
	}
	if (InlinePrograms.Num() >= MaxInlinePrograms)
	{
		// Running scripts keep their own reference, so emptying this doesn't affect them.
		UE_LOG(LogBango, Warning, TEXT("More than %d different inline scripts were compiled, so the compiled inline scripts were cleared. They're compiled again as they're spawned. If this repeats, raise FBangoModule::MaxInlinePrograms."), MaxInlinePrograms);
		InlinePrograms.Reset();
	}
	InlinePrograms.Add(Key, Compiled);
	OutProgram = Compiled;
	return true;
}

void FBangoModule::ShutdownModule()
{
	InlinePrograms.Reset();
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
