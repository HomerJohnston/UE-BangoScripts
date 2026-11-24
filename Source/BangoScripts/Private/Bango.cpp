#include "Bango.h"

#include "BangoBuiltinCommands.h"
#include "BangoCompiler.h"
#include "BangoInlineScript.h"
#include "BangoLog.h"
#include "UObject/Object.h"

#define LOCTEXT_NAMESPACE "FBangoModule"

namespace BangoModulePrivate
{
	// The loaded module, so Get doesn't ask the module manager each time (a lock and a lookup by name, on every spawn).
	static FBangoModule* Loaded = nullptr;
}

FBangoModule& FBangoModule::Get()
{
	if (BangoModulePrivate::Loaded)
	{
		return *BangoModulePrivate::Loaded;
	}
	return FModuleManager::LoadModuleChecked<FBangoModule>(TEXT("BangoScripts"));
}

void FBangoModule::StartupModule()
{
	BangoModulePrivate::Loaded = this;
	Bango::RegisterBuiltinCommands(Commands);
}

bool FBangoModule::GetInlineProgram(const FBangoInlineScript& Script, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError,
	TSharedPtr<const FString>* OutSource, const UObject* NameOwner)
{
	// The usual case, a script spawned again unchanged: what it remembers is still right if it was compiled from the
	// same text under the same name with the same inputs. Comparing the text is far cheaper than looking it up.
	const FBangoInlineCompiled* Last = Script.LastCompiled.Get(); // stays alive until LastCompiled is replaced, below
	const bool bSameText = Last && Last->Source.IsValid() && Last->Program.IsValid()
		&& Last->Source->Equals(Script.Source, ESearchCase::CaseSensitive) && Last->Name.Equals(Name, ESearchCase::CaseSensitive);
	if (bSameText && !Script.HasGivenInputs() && !Last->Signature.IsValid())
	{
		OutProgram = Last->Program;
		if (OutSource)
		{
			*OutSource = Last->Source;
		}
		return true;
	}

	FString SignatureError;
	const TSharedPtr<const FBangoInputsSignature> Signature = Script.FindInputsSignature(SignatureError);
	if (Script.HasGivenInputs() && !Signature.IsValid())
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Name, *SignatureError);
		return false;
	}
	if (bSameText && Last->Signature == Signature) // scripts given the same inputs get the same signature object
	{
		OutProgram = Last->Program;
		if (OutSource)
		{
			*OutSource = Last->Source;
		}
		return true;
	}
	if (!GetInlineProgram(Script.Source, Signature.Get(), Name, OutProgram, OutError))
	{
		return false;
	}
	const TSharedRef<FBangoInlineCompiled> Compiled = MakeShared<FBangoInlineCompiled>();
	Compiled->Name = Name;
	Compiled->OwnerName = NameOwner ? NameOwner->GetFName() : FName();
	Compiled->Source = MakeShared<FString>(Script.Source);
	Compiled->Signature = Signature;
	Compiled->Program = OutProgram;
	Script.LastCompiled = Compiled;
	if (OutSource)
	{
		*OutSource = Compiled->Source;
	}
	return true;
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
	BangoModulePrivate::Loaded = nullptr;
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
