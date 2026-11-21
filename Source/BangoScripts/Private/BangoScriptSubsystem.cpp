#include "BangoScriptSubsystem.h"

#include "BangoScriptAsset.h"

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoCompiler.h"
#include "BangoGlobalsSubsystem.h"
#include "BangoInputsSignature.h"
#include "BangoReflectedCall.h"
#include "BangoIDActorComponent.h"
#include "BangoLevelStartScript.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "BangoLog.h"
#include "BangoSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/SoftObjectPath.h"

namespace BangoScriptSubsystemPrivate
{
	constexpr int32 SubsystemSaveMagic = 0x4F474E42; // "BNGO"
	constexpr int32 SubsystemSaveVersion = 3; // raise it when the save format changes: data in another format is refused, not misread

	class FWorldEntityResolver : public IBangoEntityResolver
	{
	public:
		explicit FWorldEntityResolver(UObject* InWorldContext) : WorldContext(InWorldContext) {}

		virtual bool IsEntityRegistered(const FString& Name) const override
		{
			const UBangoActorRegistry* Registry = UBangoActorRegistry::Get(WorldContext.Get());
			return Registry && Registry->FindEntity(Name) != nullptr;
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
	Watches.Reset();
	for (TPair<int64, FBangoConditionHandle>& Pair : WatchConditions)
	{
		Bango::UnregisterCondition(Pair.Value);
	}
	WatchConditions.Reset();
	TerminateAll();
	EntityResolver.Reset();
	Super::Deinitialize();
}

void UBangoScriptSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RunLevelStartScript(InWorld);
}

void UBangoScriptSubsystem::RunLevelStartScript(UWorld& InWorld)
{
	const UBangoLevelStartScript* StartData = UBangoLevelStartScript::Find(&InWorld);
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	// Asked for once, whether or not this level has a start script, so it doesn't carry on to the next level.
	const bool bSkip = Globals && Globals->ConsumeSkipLevelStartScript();
	if (!StartData)
	{
		return;
	}
	const FString LevelName = InWorld.GetMapName();
	if (bSkip)
	{
		UE_LOG(LogBango, Log, TEXT("Not running %s's start script: SkipNextLevelStartScript was called (loading a saved game)."), *LevelName);
		return;
	}
	if (!StartData->Script)
	{
		Bango::ReportError(FString::Printf(TEXT("%s's World Settings have a Bango Level Start Script with no script chosen. Choose one in the Bango toolbar menu, or remove it there."), *LevelName));
		return;
	}

	// Actors register their Bango IDs in BeginPlay, which hasn't happened yet: register them now, so the script can use them.
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		TInlineComponentArray<UBangoIDActorComponent*> IDs(*It);
		for (UBangoIDActorComponent* ID : IDs)
		{
			ID->RegisterName();
		}
	}

	FBangoModule& Module = FBangoModule::Get();
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	if (!Module.GetScriptCache().GetProgram(*StartData->Script, Module.GetCommands(), Program, DisplayPath, Error))
	{
		Bango::ReportError(Error);
		return;
	}
	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	Script->Path = DisplayPath;
	FBangoScriptOwner Owner;
	Owner.Problem = TEXT("this is the level's start script, which has no owner, so there's no actor for self to be. Name the actor with its Bango ID instead, like @frontDoor.");
	Script->Instance.SetOwner(Owner);
	const FBangoScriptHandle Handle = StartScript(Program, Script, TArray<FBangoValue>(), nullptr);

	// It runs before anything else, so it must have finished: one that's waiting would let the level start under it.
	if (Script->Instance.IsRunning() && Script->Instance.GetStatus() == EBangoInstanceStatus::Waiting)
	{
		const FString Command = Script->Instance.GetWaitingCommandName();
		const FBangoDebugLoc Where = Script->Instance.GetCurrentLocation();
		Terminate(Handle);
		Bango::ReportError(FString::Printf(TEXT("%s(%d:%d): The level's start script runs before the level starts, so it must finish straight away, but it called '%s', which waits. Put what waits in a spawn block: spawn { ... }"),
			*DisplayPath, Where.Line, Where.Column, *Command));
	}
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
	OutContext.CallReflected = [this](const FBangoValue& Target, const FString& Name, const TArray<FBangoValue>& Args, bool bWantResult, FBangoValue& OutResult, FString& OutError)
	{
		return Bango::CallBangoCallable(this, Target, Name, Args, bWantResult, OutResult, OutError);
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
		Script->SignatureClass = (*ParentScript)->SignatureClass;
		Script->SignatureName = (*ParentScript)->SignatureName;
		Script->InstanceInputs = (*ParentScript)->InstanceInputs;
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
	if (!FBangoModule::Get().GetInlineProgram(Script, Name, Program, OutError))
	{
		return FBangoScriptHandle();
	}
	TSharedPtr<FRunningScript> Running = MakeShared<FRunningScript>();
	Running->Path = Name;
	Running->bFromSource = true; // saved with its text; not hot reloaded (it has no file)
	Running->Source = Script.Source;
	if (Script.HasInputsSignature())
	{
		const UClass* SignatureClass = Script.GetInputsSignatureClass();
		Running->SignatureClass = Script.IsGlobalInputsSignature() || !SignatureClass ? FString() : SignatureClass->GetPathName();
		Running->SignatureName = Script.GetInputsSignatureName().ToString();
	}
	for (const FBangoInputDecl& Input : Script.GetInstanceInputs())
	{
		Running->InstanceInputs.Add(Input.Decl);
	}
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
	// Any actor can be self: its Bango ID name, or else the ID it's given as an entity (spec section 4, Bango IDs).
	UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Actor);
	FString Error;
	const FString ID = Registry ? Registry->GetOrMakeEntityID(Actor, Error) : FString();
	if (!ID.IsEmpty()) // the player pawn without a Bango ID name: self is `player`
	{
		Result.BangoName = ID;
	}
	else if (const UBangoIDActorComponent* Component = Actor->FindComponentByClass<UBangoIDActorComponent>(); Component && !Component->BangoName.IsEmpty())
	{
		Result.BangoName = Component->BangoName.ToString(); // not in a game world (yet): its name, as written
	}
	else
	{
		Result.Problem = !Error.IsEmpty()
			? FString::Printf(TEXT("this script's owner can't be self: %s"), *Error)
			: FString::Printf(TEXT("this script's owner, '%s', isn't in a game world, and has no Bango ID name, so it can't be self."), *Actor->GetActorNameOrLabel());
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
	for (const FString& Warning : Script.Instance.TakeWarnings())
	{
		Bango::ReportNote(Warning, FColor::Yellow, /*bWarning*/ true); // from switching to a reloaded version
	}
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

bool UBangoScriptSubsystem::CompileSavedSource(const FRunningScript& Script, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError)
{
	if (Script.SignatureName.IsEmpty() && Script.InstanceInputs.Num() == 0)
	{
		return CompileFromSource(Script.Source, Script.Path, OutProgram, OutError);
	}
	TSharedPtr<const FBangoInputsSignature> Signature;
	if (!Script.SignatureName.IsEmpty())
	{
		const UClass* Class = nullptr;
		if (!Script.SignatureClass.IsEmpty())
		{
			Class = FSoftClassPath(Script.SignatureClass).ResolveClass();
			if (!Class)
			{
				OutError = FString::Printf(TEXT("its inputs signature's class, %s, isn't loaded."), *Script.SignatureClass);
				return false;
			}
		}
		Signature = Bango::FindInputsSignature(Class, FName(*Script.SignatureName));
		if (!Signature.IsValid())
		{
			OutError = FString::Printf(TEXT("its inputs signature '%s' isn't registered any more."), *Script.SignatureName);
			return false;
		}
	}
	if (Script.InstanceInputs.Num() == 0)
	{
		return FBangoModule::Get().GetInlineProgram(Script.Source, Signature.Get(), Script.Path, OutProgram, OutError);
	}
	// With its object's own inputs after the signature's, as it was compiled when it was spawned.
	TArray<FBangoInputDecl> InstanceInputs;
	for (const FString& Decl : Script.InstanceInputs)
	{
		InstanceInputs.Add(FBangoInputDecl(Decl));
	}
	FBangoInputsSignature Inputs;
	TArray<FString> Warnings;
	if (!Bango::AddInstanceInputs(Signature.Get(), InstanceInputs, Inputs, OutError, Warnings))
	{
		return false;
	}
	return FBangoModule::Get().GetInlineProgram(Script.Source, &Inputs, Script.Path, OutProgram, OutError);
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
	const bool bStarted = Script->Instance.Start(Program, Module.GetCommands(), Globals->GetStore(), Id, StartError, Args);
	for (const FString& Warning : Script->Instance.TakeWarnings())
	{
		Bango::ReportNote(Warning, FColor::Yellow, /*bWarning*/ true); // a global's default that wasn't used
	}
	if (!bStarted)
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

EBangoConditionResult UBangoScriptSubsystem::EvaluateCondition(FBangoConditionHandle Condition, FString& OutError, const TArray<FBangoValue>& Args)
{
	const TSharedPtr<const FBangoProgram> Program = Bango::FindCondition(Condition);
	if (!Program.IsValid())
	{
		OutError = FString::Printf(TEXT("Condition %lld isn't registered (or has been unregistered)."), Condition.Id);
		return EBangoConditionResult::Error;
	}
	return RunCondition(Program, Args, OutError);
}

EBangoConditionResult UBangoScriptSubsystem::EvaluateCondition(const FString& Source, FString& OutError,
	const TArray<FBangoValue>& Args, const FString& Name)
{
	TSharedPtr<const FBangoProgram> Program;
	if (!Bango::CompileConditionProgram(Source, Name, Program, OutError))
	{
		return EBangoConditionResult::Error;
	}
	return RunCondition(Program, Args, OutError);
}

FBangoWatchRegistry* UBangoScriptSubsystem::GetWatches(FString& OutError)
{
	if (!Watches.IsValid())
	{
		UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
		if (!Globals)
		{
			OutError = TEXT("Conditions can't be watched: there is no game instance for global variables.");
			return nullptr;
		}
		Watches = MakeUnique<FBangoWatchRegistry>(Globals->GetStore(),
			[this](const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, FString& OutCheckError)
			{
				return RunCondition(Program, Args, OutCheckError);
			},
			[](const FString& Name, const FString& Error)
			{
				Bango::ReportError(Error.StartsWith(Name, ESearchCase::CaseSensitive) ? Error : FString::Printf(TEXT("%s: %s"), *Name, *Error));
			});
	}
	return Watches.Get();
}

FBangoWatchGroupHandle UBangoScriptSubsystem::CreateWatchGroup(const FBangoInlineCondition& Condition, const FString& Name, FBangoWatchChangesFunc OnChanges, FString& OutError)
{
	FBangoWatchRegistry* Registry = GetWatches(OutError);
	if (!Registry)
	{
		return FBangoWatchGroupHandle();
	}
	FBangoConditionHandle Registered = Bango::RegisterCondition(Condition, Name, OutError);
	const TSharedPtr<const FBangoProgram> Program = Bango::FindCondition(Registered);
	if (!Program.IsValid())
	{
		Bango::UnregisterCondition(Registered);
		return FBangoWatchGroupHandle();
	}
	const FBangoWatchGroupHandle Group = Registry->CreateGroup(Program, Condition.GetPollInterval(), Name, MoveTemp(OnChanges));
	WatchConditions.Add(Group.Id, Registered);
	return Group;
}

void UBangoScriptSubsystem::DestroyWatchGroup(FBangoWatchGroupHandle& Group)
{
	if (FBangoConditionHandle* Registered = WatchConditions.Find(Group.Id))
	{
		Bango::UnregisterCondition(*Registered);
		WatchConditions.Remove(Group.Id);
	}
	if (Watches.IsValid())
	{
		Watches->DestroyGroup(Group);
	}
	Group = FBangoWatchGroupHandle();
}

bool UBangoScriptSubsystem::SetGroupInputs(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FString& OutError)
{
	if (!Watches.IsValid())
	{
		OutError = TEXT("The watch group doesn't exist (any more).");
		return false;
	}
	return Watches->SetGroupInputs(Group, Inputs, OutError);
}

bool UBangoScriptSubsystem::StartWatch(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FBangoWatchHandle& OutWatch)
{
	OutWatch = FBangoWatchHandle();
	return Watches.IsValid() && Watches->StartWatch(Group, Inputs, OutWatch);
}

void UBangoScriptSubsystem::StopWatch(FBangoWatchHandle& Watch)
{
	if (Watches.IsValid())
	{
		Watches->StopWatch(Watch);
	}
	Watch = FBangoWatchHandle();
}

EBangoConditionResult UBangoScriptSubsystem::RunCondition(const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, FString& OutError)
{
	FBangoModule& Module = FBangoModule::Get();
	const FString& ConditionName = Program->FileName;
	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	FBangoExecContext Context;
	if (!Globals || !MakeContext(0.0f, Context))
	{
		OutError = FString::Printf(TEXT("Can't check '%s': there is no game instance for global variables."), *ConditionName);
		return EBangoConditionResult::Error;
	}

	FBangoInstance Instance;
	FBangoRuntimeError StartError;
	if (!Instance.Start(Program, Module.GetCommands(), Globals->GetStore(), AllocateId(), StartError, Args))
	{
		OutError = StartError.ToString();
		return EBangoConditionResult::Error;
	}
	for (const FString& Warning : Instance.TakeWarnings())
	{
		Bango::ReportNote(Warning, FColor::Yellow, true);
	}
	return Instance.FinishCondition(Instance.Resume(Context), OutError);
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
		if (Script.IsValid() && Script->Instance.IsRunning() && !Script->Instance.GetWaitSet())
		{
			ResumeScript(*Script, DeltaTime);
		}
	}

	// Then the scripts paused at a waitUntil whose answer may have changed, this frame or since they last checked: once
	// each, after the others have run, so globals set one after another are checked together. What these scripts
	// change is seen by the ones after them here, and the others next frame.
	if (UBangoGlobalsSubsystem* GlobalsSubsystem = UBangoGlobalsSubsystem::Get(this))
	{
		for (const int64 Id : Ids)
		{
			TSharedPtr<FRunningScript> Script = Scripts.FindRef(Id);
			if (Script.IsValid() && Script->Instance.IsWaitDue(GlobalsSubsystem->GetStore()))
			{
				ResumeScript(*Script, DeltaTime);
			}
		}
	}

	// Then watched conditions whose answer may have changed (CreateWatchGroup).
	if (Watches.IsValid())
	{
		Watches->Update(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
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
	int32 Magic = SubsystemSaveMagic;
	int32 Version = SubsystemSaveVersion;
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
			Writer << Script.SignatureClass;
			Writer << Script.SignatureName;
			Writer << Script.InstanceInputs;
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
	if (Reader.IsError() || Magic != SubsystemSaveMagic || Version != SubsystemSaveVersion)
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
			Reader << Script->SignatureClass;
			Reader << Script->SignatureName;
			Reader << Script->InstanceInputs;
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
			? CompileSavedSource(*Script, Program, Error)
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
