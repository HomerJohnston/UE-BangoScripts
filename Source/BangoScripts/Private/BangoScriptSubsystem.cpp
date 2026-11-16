#include "BangoScriptSubsystem.h"

#include "BangoScriptAsset.h"

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoCompiler.h"
#include "BangoGlobalsSubsystem.h"
#include "BangoIDActorComponent.h"
#include "GameFramework/Actor.h"
#include "BangoLog.h"
#include "BangoSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace BangoScriptSubsystemPrivate
{
	constexpr int32 SaveMagic = 0x4F474E42; // "BNGO"
	constexpr int32 SaveVersion = 1; // raise it when the save format changes: data in another format is refused, not misread

	class FWorldEntityResolver : public IBangoEntityResolver
	{
	public:
		explicit FWorldEntityResolver(UObject* InWorldContext) : WorldContext(InWorldContext) {}

		virtual bool IsEntityRegistered(const FString& Name) const override
		{
			const UBangoActorRegistry* Registry = UBangoActorRegistry::Get(WorldContext.Get());
			return Registry && Registry->FindActor(Name) != nullptr;
		}

	private:
		TWeakObjectPtr<UObject> WorldContext;
	};
}

UBangoScriptSubsystem* UBangoScriptSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<UBangoScriptSubsystem>() : nullptr;
}

bool UBangoScriptSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UBangoScriptSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBangoScriptSubsystem, STATGROUP_Tickables);
}

void UBangoScriptSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntityResolver = MakeUnique<BangoScriptSubsystemPrivate::FWorldEntityResolver>(this);
}

void UBangoScriptSubsystem::Deinitialize()
{
	TerminateAll();
	EntityResolver.Reset();
	Super::Deinitialize();
}

bool UBangoScriptSubsystem::MakeContext(float DeltaSeconds, FBangoExecContext& OutContext)
{
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	if (!Globals)
	{
		return false;
	}
	const UBangoSettings* Settings = GetDefault<UBangoSettings>();
	OutContext.Globals = &Globals->GetStore();
	OutContext.Entities = EntityResolver.Get();
	OutContext.WorldContext = this;
	OutContext.DeltaSeconds = DeltaSeconds;
	OutContext.MaxInstructionsPerResume = Settings->MaxInstructionsPerFrame;
	OutContext.MaxCallDepth = Settings->MaxCallDepth;
	OutContext.SpawnBlock = [this](const FBangoInstance& Parent, int32 BlockIP, int64& OutId, FString& OutError)
	{
		return StartSpawnBlock(Parent, BlockIP, OutId, OutError);
	};
	return true;
}

bool UBangoScriptSubsystem::StartSpawnBlock(const FBangoInstance& Parent, int32 BlockIP, int64& OutId, FString& OutError)
{
	if (StartDepth >= MaxStartDepth)
	{
		OutError = FString::Printf(TEXT("%d scripts are already being started inside each other (each by the one before, before it paused). Is a spawn block spawning itself?"), MaxStartDepth);
		return false;
	}
	// A copy of the script it's in: the same path (so Exec finds files relative to it, and saving and hot reload find
	// its script), or the same text for a script spawned from text.
	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	if (const TSharedPtr<FRunningScript>* ParentScript = Scripts.Find(Parent.GetId()))
	{
		Script->Path = (*ParentScript)->Path;
		Script->bFromSource = (*ParentScript)->bFromSource;
		Script->Source = (*ParentScript)->Source;
	}
	else
	{
		Script->Path = Parent.GetProgram().IsValid() ? Parent.GetProgram()->FileName : FString();
		Script->bFromSource = true; // not one of this subsystem's scripts (a condition's run): nothing to reload it from
	}
	Script->ParentId = (int32)Parent.GetId();

#if WITH_EDITOR
	Script->Instance.SetRecordTrail(true); // for the editor's debugger
#endif
	const int32 Id = AllocateId();
	FBangoRuntimeError StartError;
	if (!Script->Instance.StartSpawned(Parent, BlockIP, Id, StartError))
	{
		OutError = StartError.Message;
		return false;
	}
	OutId = Id;
	Scripts.Add(Id, Script);
	++StartDepth;
	const bool bStillRunning = ResumeScript(*Script, 0.0f);
	--StartDepth;
	if (!bStillRunning)
	{
		RemoveScript(Id);
		PruneEndedParents();
	}
	return true;
}

FString UBangoScriptSubsystem::GetInlineScriptName(const UObject* Owner)
{
	return Owner ? FString::Printf(TEXT("%s (inline script)"), *Owner->GetName()) : FString(TEXT("Inline script"));
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FBangoInlineScript& Script, const TArray<FBangoValue>& Args, const UObject* Owner)
{
	FString Error;
	const FBangoScriptHandle Handle = Spawn(Script, Args, Owner, Error);
	if (!Handle.IsValid() && !Error.IsEmpty())
	{
		Bango::ReportError(Error);
	}
	return Handle;
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FBangoInlineScript& Script, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError)
{
	if (Script.IsEmpty())
	{
		return FBangoScriptHandle(); // nothing to run (an inline script left empty is normal)
	}
	const FString Name = GetInlineScriptName(Owner);
	TSharedPtr<const FBangoProgram> Program;
	if (!FBangoModule::Get().GetInlineProgram(Script.Source, Name, Program, OutError))
	{
		return FBangoScriptHandle();
	}
	TSharedPtr<FRunningScript> Running = MakeShared<FRunningScript>();
	Running->Path = Name;
	Running->bFromSource = true; // saved with its text; not hot reloaded (it has no file)
	Running->Source = Script.Source;
	Running->Instance.SetOwner(GetScriptOwner(Owner));
	return StartScript(Program, Running, Args, &OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::SpawnChild(int64 ParentId, const FString& ScriptPath, const TArray<FBangoValue>& Args, FString& OutError)
{
	// The child belongs to the same actor as its parent.
	const TSharedPtr<FRunningScript>* Parent = Scripts.Find(ParentId);
	const FBangoScriptOwner Owner = Parent && Parent->IsValid() ? (*Parent)->Instance.GetOwner() : FBangoScriptOwner();
	PendingParentId = (int32)ParentId;
	OutError.Reset();
	const FBangoScriptHandle Handle = SpawnPath(ScriptPath, Args, Owner, &OutError);
	PendingParentId = 0; // if it never got as far as StartScript
	return Handle;
}

FBangoScriptOwner UBangoScriptSubsystem::GetScriptOwner(const UObject* Owner)
{
	FBangoScriptOwner Result;
	if (!Owner)
	{
		return Result; // the instance's own "no owner" message
	}
	const AActor* Actor = Cast<AActor>(Owner);
	if (!Actor)
	{
		const UActorComponent* Component = Cast<UActorComponent>(Owner);
		Result.Problem = Component && Component->GetOwner()
			? FString::Printf(TEXT("this script's owner, '%s', is a component, and self is always an actor. Pass its actor, '%s', as the Owner instead."),
				*Owner->GetName(), *Component->GetOwner()->GetName())
			: FString::Printf(TEXT("this script's owner, '%s', isn't an actor, and self is always an actor. Pass the actor it belongs to as the Owner instead."), *Owner->GetName());
		return Result;
	}
	const UBangoIDActorComponent* ID = Actor->FindComponentByClass<UBangoIDActorComponent>();
	if (!ID)
	{
		Result.Problem = FString::Printf(TEXT("this script's owner, '%s', has no Bango ID component. self is the owner's Bango ID, so add one and give it a name."), *Actor->GetName());
	}
	else if (ID->BangoName.IsEmpty())
	{
		Result.Problem = FString::Printf(TEXT("this script's owner, '%s', has a Bango ID component with no name. self is the owner's Bango ID, so give it one."), *Actor->GetName());
	}
	else
	{
		Result.BangoName = ID->BangoName.ToString();
	}
	return Result;
}

int32 UBangoScriptSubsystem::GetParentId(int32 Id) const
{
	if (const TSharedPtr<FRunningScript>* Script = Scripts.Find(Id))
	{
		return (*Script)->ParentId;
	}
	const int32* Parent = EndedParents.Find(Id);
	return Parent ? *Parent : 0;
}

bool UBangoScriptSubsystem::IsDescendant(int32 Id, int32 Ancestor) const
{
	int32 Parent = GetParentId(Id);
	for (int32 Steps = 0; Parent != 0 && Steps < 100000; ++Steps) // the limit only guards against a loop in damaged save data
	{
		if (Parent == Ancestor)
		{
			return true;
		}
		Parent = GetParentId(Parent);
	}
	return false;
}

void UBangoScriptSubsystem::RemoveScript(int64 Id)
{
	if (const TSharedPtr<FRunningScript>* Script = Scripts.Find(Id))
	{
		EndedParents.Add((int32)Id, (*Script)->ParentId);
		Scripts.Remove(Id);
	}
}

void UBangoScriptSubsystem::PruneEndedParents()
{
	if (Scripts.Num() == 0)
	{
		EndedParents.Reset();
		return;
	}
	// Keep the ended scripts that running ones descend from: they link those to their ancestors.
	TSet<int32> Needed;
	for (const TPair<int64, TSharedPtr<FRunningScript>>& Pair : Scripts)
	{
		int32 Parent = Pair.Value->ParentId;
		for (int32 Steps = 0; Parent != 0 && Steps < 100000; ++Steps)
		{
			if (const TSharedPtr<FRunningScript>* Running = Scripts.Find(Parent))
			{
				Parent = (*Running)->ParentId;
			}
			else if (const int32* Up = EndedParents.Find(Parent))
			{
				bool bAlreadyIn = false;
				Needed.Add(Parent, &bAlreadyIn);
				if (bAlreadyIn)
				{
					break; // the rest of the way up is already kept
				}
				Parent = *Up;
			}
			else
			{
				break;
			}
		}
	}
	for (auto It = EndedParents.CreateIterator(); It; ++It)
	{
		if (!Needed.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

void UBangoScriptSubsystem::GetDebugInfo(TArray<FBangoScriptDebugInfo>& OutScripts) const
{
	TArray<int64> Ids;
	Scripts.GetKeys(Ids);
	Ids.Sort();
	for (const int64 Id : Ids)
	{
		const FRunningScript& Script = *Scripts.FindChecked(Id);
		if (!Script.Instance.IsRunning())
		{
			continue; // removed at the end of the tick; a failed one is in EndedScripts
		}
		FBangoScriptDebugInfo& Info = OutScripts.AddDefaulted_GetRef();
		Info.Id = Id;
		Info.Path = Script.Path;
		Info.Status = Script.Instance.GetStatus();
		Script.Instance.GetCallStack(Info.CallStack);
		Info.WaitingOn = Script.Instance.GetWaitingCommandName();
		Info.SourceHash = Script.bFromSource ? FCrc::StrCrc32(*Script.Source) : 0;
	}
	OutScripts.Append(EndedScripts);
}

uint64 UBangoScriptSubsystem::GetScriptTrail(int64 Id, uint64 After, TArray<int64>& OutNodeIDs) const
{
	const TSharedPtr<FRunningScript>* Script = Scripts.Find(Id);
	return Script ? (*Script)->Instance.GetTrail(After, OutNodeIDs) : 0;
}

void UBangoScriptSubsystem::ReportFailure(const FRunningScript& Script)
{
#if WITH_EDITOR
	// Kept for the debugger, so the failed node can be shown after the script is gone.
	FBangoScriptDebugInfo& Ended = EndedScripts.AddDefaulted_GetRef();
	Ended.Id = Script.Instance.GetId();
	Ended.Path = Script.Path;
	Ended.SourceHash = Script.bFromSource ? FCrc::StrCrc32(*Script.Source) : 0;
	Ended.Status = EBangoInstanceStatus::Failed;
	Ended.Error = Script.Instance.GetError();
	Ended.TrailCount = Script.Instance.GetTrail(0, Ended.Trail); // the way it came to the error
	if (EndedScripts.Num() > MaxEndedScripts)
	{
		EndedScripts.RemoveAt(0);
	}
#endif
	const FString Message = Script.Instance.GetError().ToString();
	Bango::ReportError(Message);
	FBangoScriptHandle Handle;
	Handle.Id = (int32)Script.Instance.GetId();
	OnScriptFailed.Broadcast(Handle, Message);
}

bool UBangoScriptSubsystem::ResumeScript(FRunningScript& Script, float DeltaSeconds)
{
	FBangoExecContext Context;
	if (!MakeContext(DeltaSeconds, Context))
	{
		Bango::ReportError(TEXT("Bango scripts can't run: there is no game instance for global variables."));
		Script.Instance.Terminate();
		return false;
	}
	Script.Instance.Resume(Context);
	ReportReload(Script);
	if (Script.Instance.GetStatus() == EBangoInstanceStatus::Failed)
	{
		ReportFailure(Script);
	}
	return Script.Instance.IsRunning();
}

void UBangoScriptSubsystem::ReportReload(FRunningScript& Script)
{
	FString Message;
	switch (Script.Instance.TakeReloadResult(Message))
	{
	case EBangoReloadResult::Reloaded:
		Bango::ReportNote(FString::Printf(TEXT("Reloaded %s (script %lld). %s"), *Script.Path, Script.Instance.GetId(), *Message), FColor::Green);
		break;
	case EBangoReloadResult::KeptOld:
		Bango::ReportNote(FString::Printf(TEXT("Couldn't reload %s (script %lld), so it carries on with the version it had: %s Scripts spawned from now on use the new version."),
			*Script.Path, Script.Instance.GetId(), *Message), FColor::Yellow, true);
		break;
	default:
		break;
	}
}

void UBangoScriptSubsystem::ReloadChangedScripts()
{
	FBangoModule& Module = FBangoModule::Get();
	// Each script's file is checked once, however many copies of it are running.
	TMap<FString, TSharedPtr<const FBangoProgram>> Latest;
	for (TPair<int64, TSharedPtr<FRunningScript>>& Pair : Scripts)
	{
		FRunningScript& Script = *Pair.Value;
		if (!Script.Instance.IsRunning() || Script.bFromSource)
		{
			continue; // a script spawned from text has no file to reload from
		}
		TSharedPtr<const FBangoProgram>* Found = Latest.Find(Script.Path);
		if (!Found)
		{
			TSharedPtr<const FBangoProgram> Program;
			FString DisplayPath;
			FString Error;
			if (Module.GetScriptCache().GetProgram(Script.Path, Module.GetCommands(), Program, DisplayPath, Error))
			{
				ReloadErrors.Remove(Script.Path);
			}
			else
			{
				// A script being edited often doesn't compile for a moment; say so once per error.
				FString& LastError = ReloadErrors.FindOrAdd(Script.Path);
				if (!LastError.Equals(Error, ESearchCase::CaseSensitive))
				{
					LastError = Error;
					Bango::ReportNote(FString::Printf(TEXT("%s was changed, but doesn't compile, so its running copies carry on with the version they have: %s"), *Script.Path, *Error), FColor::Yellow, true);
				}
				Program.Reset();
			}
			Found = &Latest.Add(Script.Path, Program);
		}

		const TSharedPtr<const FBangoProgram>& NewProgram = *Found;
		if (!NewProgram.IsValid() || NewProgram == Script.Instance.GetProgram() || NewProgram == Script.ReloadTarget)
		{
			continue;
		}
		Script.ReloadTarget = NewProgram;
		FString Error;
		if (!Script.Instance.RequestReload(NewProgram, Module.GetCommands(), Error))
		{
			Bango::ReportNote(FString::Printf(TEXT("Couldn't reload %s (script %lld), so it carries on with the version it had: %s"), *Script.Path, Script.Instance.GetId(), *Error), FColor::Yellow, true);
		}
	}
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args, FString& OutError)
{
	return Spawn(ScriptPath, Args, nullptr, OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError)
{
	OutError.Reset();
	return SpawnPath(ScriptPath, Args, GetScriptOwner(Owner), &OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args, const UObject* Owner)
{
	return SpawnPath(ScriptPath, Args, GetScriptOwner(Owner), nullptr);
}

FBangoScriptHandle UBangoScriptSubsystem::SpawnPath(const FString& ScriptPath, const TArray<FBangoValue>& Args, const FBangoScriptOwner& Owner, FString* OutError)
{
	FBangoModule& Module = FBangoModule::Get();
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	if (!Module.GetScriptCache().GetProgram(ScriptPath, Module.GetCommands(), Program, DisplayPath, Error))
	{
		if (OutError)
		{
			*OutError = Error;
		}
		else
		{
			Bango::ReportError(Error);
		}
		return FBangoScriptHandle();
	}
	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	Script->Path = DisplayPath;
	Script->Instance.SetOwner(Owner);
	return StartScript(Program, Script, Args, OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const UBangoScriptAsset* ScriptAsset, const TArray<FBangoValue>& Args, FString& OutError)
{
	return Spawn(ScriptAsset, Args, nullptr, OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const UBangoScriptAsset* ScriptAsset, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError)
{
	OutError.Reset();
	if (!ScriptAsset)
	{
		OutError = TEXT("No Bango Script was given to spawn.");
		return FBangoScriptHandle();
	}
	FBangoModule& Module = FBangoModule::Get();
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	if (!Module.GetScriptCache().GetProgram(*ScriptAsset, Module.GetCommands(), Program, DisplayPath, OutError))
	{
		return FBangoScriptHandle();
	}
	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	Script->Path = DisplayPath;
	Script->Instance.SetOwner(GetScriptOwner(Owner));
	return StartScript(Program, Script, Args, &OutError);
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const UBangoScriptAsset* ScriptAsset, const TArray<FBangoValue>& Args, const UObject* Owner)
{
	FString Error;
	const FBangoScriptHandle Handle = Spawn(ScriptAsset, Args, Owner, Error);
	if (!Handle.IsValid() && !Error.IsEmpty())
	{
		Bango::ReportError(Error);
	}
	return Handle;
}

FString UBangoScriptSubsystem::GetScriptPath(int64 InstanceId) const
{
	const TSharedPtr<FRunningScript>* Found = Scripts.Find(InstanceId);
	return Found && Found->IsValid() && !(*Found)->bFromSource ? (*Found)->Path : FString();
}

int32 UBangoScriptSubsystem::AllocateId()
{
	// Two billion ids won't run out, but if they ever did, start again from 1 and skip any still running.
	for (int32 Tries = 0; Tries < 1000; ++Tries)
	{
		if (NextId <= 0 || NextId == MAX_int32)
		{
			NextId = 1;
		}
		const int32 Id = NextId++;
		if (!Scripts.Contains(Id) && !EndedParents.Contains(Id))
		{
			return Id;
		}
	}
	return NextId++;
}

bool UBangoScriptSubsystem::CompileFromSource(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError)
{
	TSharedPtr<FBangoProgram> Compiled = MakeShared<FBangoProgram>();
	FBangoCompileError CompileError;
	if (!Bango::CompileSource(Source, Name, FBangoModule::Get().GetCommands(), *Compiled, CompileError))
	{
		OutError = CompileError.ToString();
		return false;
	}
	OutProgram = Compiled;
	return true;
}

FBangoScriptHandle UBangoScriptSubsystem::SpawnFromSource(const FString& Source, const FString& Name, const TArray<FBangoValue>& Args, const UObject* Owner)
{
	const FString ScriptName = Name.IsEmpty() ? FString(TEXT("Script")) : Name;
	TSharedPtr<const FBangoProgram> Program;
	FString Error;
	if (!CompileFromSource(Source, ScriptName, Program, Error))
	{
		Bango::ReportError(Error);
		return FBangoScriptHandle();
	}
	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	Script->Path = ScriptName;
	Script->bFromSource = true;
	Script->Source = Source;
	Script->Instance.SetOwner(GetScriptOwner(Owner));
	return StartScript(Program, Script, Args);
}

FBangoScriptHandle UBangoScriptSubsystem::StartScript(const TSharedPtr<const FBangoProgram>& Program, TSharedPtr<FRunningScript> Script, const TArray<FBangoValue>& Args,
	FString* OutError)
{
	const auto Report = [OutError](const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		else
		{
			Bango::ReportError(Message);
		}
	};
	FBangoModule& Module = FBangoModule::Get();
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	if (!Globals)
	{
		Report(FString::Printf(TEXT("Can't spawn '%s': there is no game instance for global variables."), *Script->Path));
		return FBangoScriptHandle();
	}
	if (StartDepth >= MaxStartDepth)
	{
		// A new script runs straight away, so a script that starts itself would start copies forever, all inside each other.
		Report(FString::Printf(TEXT("Can't spawn '%s': %d scripts are already being started inside each other (each spawned by the one before, before it paused). Is a script spawning itself?"),
			*Script->Path, MaxStartDepth));
		return FBangoScriptHandle();
	}

	Script->ParentId = PendingParentId; // SpawnChild's (taken now, so the scripts this one starts don't get it too)
	PendingParentId = 0;
#if WITH_EDITOR
	Script->Instance.SetRecordTrail(true); // for the editor's debugger
#endif
	const int32 Id = AllocateId();
	FBangoRuntimeError StartError;
	if (!Script->Instance.Start(Program, Module.GetCommands(), Globals->GetStore(), Id, StartError, Args))
	{
		Report(StartError.ToString());
		return FBangoScriptHandle();
	}

	Scripts.Add(Id, Script);
	++StartDepth;
	const bool bStillRunning = ResumeScript(*Script, 0.0f);
	--StartDepth;
	if (!bStillRunning)
	{
		RemoveScript(Id);
		PruneEndedParents();
	}

	FBangoScriptHandle Handle;
	Handle.Id = Id;
	return Handle;
}

bool UBangoScriptSubsystem::EvaluateBoolCondition(FBangoConditionHandle Condition, bool& bOutResult, FString& OutError, const TArray<FBangoValue>& Args)
{
	bOutResult = false;
	const TSharedPtr<const FBangoProgram> Program = Bango::FindCondition(Condition);
	if (!Program.IsValid())
	{
		OutError = FString::Printf(TEXT("Condition %lld isn't registered (or has been unregistered)."), Condition.Id);
		return false;
	}
	return RunCondition(Program, Args, bOutResult, OutError);
}

bool UBangoScriptSubsystem::EvaluateBoolCondition(const FString& Source, bool& bOutResult, FString& OutError,
	const TArray<FBangoValue>& Args, const FString& Name)
{
	bOutResult = false;
	TSharedPtr<const FBangoProgram> Program;
	if (!Bango::CompileConditionProgram(Source, Name, Program, OutError))
	{
		return false;
	}
	return RunCondition(Program, Args, bOutResult, OutError);
}

bool UBangoScriptSubsystem::RunCondition(const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, bool& bOutResult, FString& OutError)
{
	bOutResult = false;
	FBangoModule& Module = FBangoModule::Get();
	const FString& ConditionName = Program->FileName;
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	FBangoExecContext Context;
	if (!Globals || !MakeContext(0.0f, Context))
	{
		OutError = FString::Printf(TEXT("Can't check '%s': there is no game instance for global variables."), *ConditionName);
		return false;
	}

	FBangoInstance Instance;
	FBangoRuntimeError StartError;
	if (!Instance.Start(Program, Module.GetCommands(), Globals->GetStore(), AllocateId(), StartError, Args))
	{
		OutError = StartError.ToString();
		return false;
	}
	switch (Instance.Resume(Context))
	{
	case EBangoInstanceStatus::Finished:
		break;
	case EBangoInstanceStatus::Waiting:
	{
		const FString Command = Instance.GetWaitingCommandName();
		const FBangoDebugLoc Where = Instance.GetCurrentLocation();
		Instance.Terminate();
		OutError = FString::Printf(TEXT("%s(%d:%d): A condition must give its answer straight away, but it called '%s', which waits."),
			*ConditionName, Where.Line, Where.Column, *Command);
		return false;
	}
	default:
		OutError = Instance.GetError().ToString();
		return false;
	}
	const FBangoValue& Result = Instance.GetResult();
	if (Result.IsNil() || !(Result.GetType() == FBangoType::Bool()))
	{
		OutError = FString::Printf(TEXT("%s: the condition didn't return true or false."), *ConditionName);
		return false;
	}
	bOutResult = Result.GetBool();
	return true;
}

void UBangoScriptSubsystem::Terminate(FBangoScriptHandle Handle, bool bAndChildren)
{
	// Each is removed at the end of the next tick, so a script can safely terminate itself (or the script that started
	// it, which may be running a command that started this one) from inside a command.
	if (bAndChildren && Handle.Id != 0)
	{
		for (TPair<int64, TSharedPtr<FRunningScript>>& Pair : Scripts)
		{
			if (Pair.Value->Instance.IsRunning() && IsDescendant((int32)Pair.Key, Handle.Id))
			{
				Pair.Value->Instance.Terminate();
			}
		}
	}
	if (const TSharedPtr<FRunningScript>* Script = Scripts.Find(Handle.Id))
	{
		(*Script)->Instance.Terminate();
	}
}

bool UBangoScriptSubsystem::IsRunning(FBangoScriptHandle Handle) const
{
	const TSharedPtr<FRunningScript>* Script = Scripts.Find(Handle.Id);
	return Script && (*Script)->Instance.IsRunning();
}

void UBangoScriptSubsystem::TerminateAll()
{
	for (TPair<int64, TSharedPtr<FRunningScript>>& Pair : Scripts)
	{
		Pair.Value->Instance.Terminate();
	}
	Scripts.Reset();
	EndedParents.Reset();
}

void UBangoScriptSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if WITH_EDITOR
	// Hot reload: a few times a second, look for script files that were saved since their running copies started.
	ReloadCheckCountdown -= DeltaTime;
	if (ReloadCheckCountdown <= 0.0f && Scripts.Num() > 0 && GetDefault<UBangoSettings>()->bReloadRunningScripts)
	{
		ReloadCheckCountdown = 0.5f;
		ReloadChangedScripts();
	}
#endif

	// Scripts may spawn or terminate scripts while running, so work from a snapshot.
	TArray<int64> Ids;
	Scripts.GetKeys(Ids);
	Ids.Sort();

	for (const int64 Id : Ids)
	{
		// Hold a reference: the map may change while this script runs.
		TSharedPtr<FRunningScript> Script = Scripts.FindRef(Id);
		if (Script.IsValid() && Script->Instance.IsRunning())
		{
			ResumeScript(*Script, DeltaTime);
		}
	}

	bool bRemoved = false;
	for (const int64 Id : Ids)
	{
		const TSharedPtr<FRunningScript>* Script = Scripts.Find(Id);
		if (Script && !(*Script)->Instance.IsRunning())
		{
			RemoveScript(Id);
			bRemoved = true;
		}
	}
	if (bRemoved)
	{
		PruneEndedParents();
	}
}

void UBangoScriptSubsystem::WriteScreen(const FString& Text, float Seconds)
{
	UE_LOG(LogBango, Display, TEXT("WriteScreen: %s"), *Text);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, Seconds, FColor::White, Text);
	}
}

bool UBangoScriptSubsystem::SaveState(TArray<uint8>& OutData)
{
	using namespace BangoScriptSubsystemPrivate;
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	if (!Globals)
	{
		Bango::ReportError(TEXT("Can't save Bango state: there is no game instance."));
		return false;
	}

	OutData.Reset();
	FMemoryWriter Writer(OutData);
	int32 Magic = SaveMagic;
	int32 Version = SaveVersion;
	Writer << Magic;
	Writer << Version;
	Globals->GetStore().Serialize(Writer);
	Writer << NextId;

	TArray<int64> Ids;
	for (TPair<int64, TSharedPtr<FRunningScript>>& Pair : Scripts)
	{
		if (Pair.Value->Instance.IsRunning())
		{
			Ids.Add(Pair.Key);
		}
	}
	Ids.Sort();
	int32 Count = Ids.Num();
	Writer << Count;
	for (const int64 Id : Ids)
	{
		FRunningScript& Script = *Scripts.FindRef(Id);
		Writer << Script.Path;
		Writer << Script.ParentId;
		Writer << Script.bFromSource;
		if (Script.bFromSource)
		{
			Writer << Script.Source; // it has no file to compile it from again
		}
		Script.Instance.Serialize(Writer);
	}
	Writer << EndedParents; // links running scripts to the ended ones that started them
	return !Writer.IsError();
}

bool UBangoScriptSubsystem::LoadState(const TArray<uint8>& Data)
{
	using namespace BangoScriptSubsystemPrivate;
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	if (!Globals)
	{
		Bango::ReportError(TEXT("Can't load Bango state: there is no game instance."));
		return false;
	}

	FMemoryReader Reader(Data);
	int32 Magic = 0;
	int32 Version = 0;
	Reader << Magic;
	Reader << Version;
	if (Reader.IsError() || Magic != SaveMagic || Version != SaveVersion)
	{
		Bango::ReportError(TEXT("Can't load Bango state: the data isn't Bango save data, or is from an incompatible version."));
		return false;
	}

	TerminateAll();
	Globals->GetStore().Serialize(Reader);
	Reader << NextId;
	int32 Count = 0;
	Reader << Count;
	if (Reader.IsError() || Count < 0)
	{
		Bango::ReportError(TEXT("Can't load Bango state: the data is damaged."));
		return false;
	}

	FBangoModule& Module = FBangoModule::Get();
	for (int32 i = 0; i < Count && !Reader.IsError(); ++i)
	{
		TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
		Reader << Script->Path;
		Reader << Script->ParentId;
		Reader << Script->bFromSource;
		if (Script->bFromSource)
		{
			Reader << Script->Source;
		}
		Script->Instance.Serialize(Reader);
		if (Reader.IsError())
		{
			break;
		}

		TSharedPtr<const FBangoProgram> Program;
		FString DisplayPath;
		FString Error;
		const bool bCompiled = Script->bFromSource
			? CompileFromSource(Script->Source, Script->Path, Program, Error)
			: Module.GetScriptCache().GetProgram(Script->Path, Module.GetCommands(), Program, DisplayPath, Error);
		if (!bCompiled || !Script->Instance.Bind(Program, Module.GetCommands(), Error))
		{
			Bango::ReportError(FString::Printf(TEXT("Couldn't restore saved script '%s': %s"), *Script->Path, *Error));
			continue;
		}
		if (Script->Instance.IsRunning())
		{
			Scripts.Add(Script->Instance.GetId(), Script);
		}
	}

	if (!Reader.IsError())
	{
		Reader << EndedParents;
		PruneEndedParents();
	}
	if (Reader.IsError())
	{
		Bango::ReportError(TEXT("Bango save data is damaged; some scripts may not have been restored."));
		return false;
	}
	return true;
}

void UBangoScriptSubsystem::ResetState()
{
	TerminateAll();
	if (UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this))
	{
		Globals->ResetAll();
	}
}
