#include "BangoScriptSubsystem.h"

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoGlobalsSubsystem.h"
#include "BangoLog.h"
#include "BangoSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace BangoScriptSubsystemPrivate
{
	constexpr int32 SaveMagic = 0x4F474E42; // "BNGO"
	constexpr int32 SaveVersion = 2; // 2: ints are 32-bit

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
	return true;
}

void UBangoScriptSubsystem::ReportFailure(const FRunningScript& Script)
{
	const FString Message = Script.Instance.GetError().ToString();
	Bango::ReportError(Message);
	FBangoScriptHandle Handle;
	Handle.Id = Script.Instance.GetId();
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
	if (Script.Instance.GetStatus() == EBangoInstanceStatus::Failed)
	{
		ReportFailure(Script);
	}
	return Script.Instance.IsRunning();
}

FBangoScriptHandle UBangoScriptSubsystem::Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args)
{
	FBangoModule& Module = FBangoModule::Get();
	TSharedPtr<const FBangoProgram> Program;
	FString DisplayPath;
	FString Error;
	if (!Module.GetScriptCache().GetProgram(ScriptPath, Module.GetCommands(), Program, DisplayPath, Error))
	{
		Bango::ReportError(Error);
		return FBangoScriptHandle();
	}

	UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(this);
	if (!Globals)
	{
		Bango::ReportError(FString::Printf(TEXT("Can't spawn '%s': there is no game instance for global variables."), *DisplayPath));
		return FBangoScriptHandle();
	}

	TSharedPtr<FRunningScript> Script = MakeShared<FRunningScript>();
	Script->Path = DisplayPath;
	const int64 Id = NextId++;
	FBangoRuntimeError StartError;
	if (!Script->Instance.Start(Program, Module.GetCommands(), Globals->GetStore(), Id, StartError, Args))
	{
		Bango::ReportError(StartError.ToString());
		return FBangoScriptHandle();
	}

	Scripts.Add(Id, Script);
	if (!ResumeScript(*Script, 0.0f))
	{
		Scripts.Remove(Id);
	}

	FBangoScriptHandle Handle;
	Handle.Id = Id;
	return Handle;
}

void UBangoScriptSubsystem::Terminate(FBangoScriptHandle Handle)
{
	if (const TSharedPtr<FRunningScript>* Script = Scripts.Find(Handle.Id))
	{
		(*Script)->Instance.Terminate();
		// Removed at the end of the next tick, so a script can safely terminate itself from a command.
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
}

void UBangoScriptSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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

	for (const int64 Id : Ids)
	{
		const TSharedPtr<FRunningScript>* Script = Scripts.Find(Id);
		if (Script && !(*Script)->Instance.IsRunning())
		{
			Scripts.Remove(Id);
		}
	}
}

void UBangoScriptSubsystem::ShowHint(const FText& Text)
{
	UE_LOG(LogBango, Display, TEXT("Hint: %s"), *Text.ToString());
	if (OnHint.IsBound())
	{
		OnHint.Broadcast(Text);
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, GetDefault<UBangoSettings>()->HintDisplaySeconds, FColor::White, Text.ToString());
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
		Script.Instance.Serialize(Writer);
	}
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
		Script->Instance.Serialize(Reader);
		if (Reader.IsError())
		{
			break;
		}

		TSharedPtr<const FBangoProgram> Program;
		FString DisplayPath;
		FString Error;
		if (!Module.GetScriptCache().GetProgram(Script->Path, Module.GetCommands(), Program, DisplayPath, Error)
			|| !Script->Instance.Bind(Program, Module.GetCommands(), Error))
		{
			Bango::ReportError(FString::Printf(TEXT("Couldn't restore saved script '%s': %s"), *Script->Path, *Error));
			continue;
		}
		if (Script->Instance.IsRunning())
		{
			Scripts.Add(Script->Instance.GetId(), Script);
		}
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
