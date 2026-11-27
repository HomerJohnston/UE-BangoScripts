#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BangoConditions.h"
#include "BangoInstance.h"
#include "BangoInlineScript.h"
#include "BangoConditionWatches.h"
class UBangoScriptAsset;
class UBangoGlobalsSubsystem;
class UBangoActorRegistry;

#include "BangoScriptSubsystem.generated.h"

/**
 * Refers to a spawned script instance. A default (zero) handle refers to nothing. Ids are given out in order, from 1,
 * by each world's UBangoScriptSubsystem, so two running scripts never share one. It's an int32, like a Bango int, so a
 * script can keep the handle Exec gives it.
 */
USTRUCT(BlueprintType)
struct BANGOSCRIPTS_API FBangoScriptHandle
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
};

/** A script instance as the editor's debugger sees it (UBangoScriptSubsystem::GetDebugInfo). */
struct FBangoScriptDebugInfo
{
	int64 Id = 0;
	/** The script, like "Quest/Intro.bango". */
	FString Path;
	EBangoInstanceStatus Status = EBangoInstanceStatus::None;
	/** The calls in progress, the main graph first and the innermost (where it is) last. Empty once it has ended. */
	TArray<FBangoCallFrameInfo> CallStack;
	/** The command it's waiting on, like "Sleep", or empty. */
	FString WaitingOn;
	/** The blocks of commands with entries that are open, with which entries passed and which is running. */
	TArray<FBangoOpenEntries> OpenEntries;
	/**
	 * For a script spawned from text (an inline script, SpawnFromSource): FCrc::StrCrc32 of its text, so the graph
	 * editor can find the copies of the inline script it has open. 0 for a script asset.
	 */
	uint32 SourceHash = 0;
	/** Failed only: the error, with the node it happened at. */
	FBangoRuntimeError Error;
	/**
	 * Failed only: the last nodes it ran, up to the error (at most FBangoInstance::TrailSize, oldest first), and how
	 * many it ran in all (see FBangoInstance::GetTrail). A running script's trail comes from GetScriptTrail instead.
	 */
	TArray<int64> Trail;
	uint64 TrailCount = 0;
	/** Failed only: the same by line (FBangoInstance::GetLineTrail), for a script whose text has no NodeIDs. */
	TArray<int32> LineTrail;
	uint64 LineTrailCount = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBangoScriptFailedEvent, FBangoScriptHandle, Handle, const FString&, Error);

/**
 * Runs Bango scripts in one game world (spec section 10).
 *
 * Every frame, each running script resumes and runs until it pauses (on Sleep or another latent
 * command), finishes or fails. Then each script paused at a waitUntil checks its condition again, if a
 * global it reads changed (or it calls a command, so it's checked every frame). Scripts pause while the
 * game is paused. Errors are logged and shown on screen, and only stop the script that hit them.
 */
UCLASS()
class BANGOSCRIPTS_API UBangoScriptSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's world, or nullptr (e.g. in an editor world). */
	static UBangoScriptSubsystem* Get(const UObject* WorldContext);

	/**
	 * Compiles (if needed) and starts the script at ScriptPath: a Bango Script asset's path, full ("/Game/Quests/Intro")
	 * or relative to /Game ("Quests/Intro"). Its asset is loaded if it isn't (a synchronous load).
	 * Args are matched to the script's inputs line in order (see FBangoInstance::Start); use Bango::ActorToEntity
	 * for entity arguments. The script runs straight away until it first pauses. Returns an invalid handle and
	 * shows an error if the script can't be found, doesn't compile, the arguments don't fit its parameters, or
	 * one of its globals conflicts with an existing global.
	 *
	 * Owner, if given, is the actor the script belongs to: what `self` means in it (see GetScriptOwner). It must be an
	 * actor with a Bango ID; anything else spawns as usual, and `self` stops the script with an error saying why.
	 */
	FBangoScriptHandle Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args = TArray<FBangoValue>(), const UObject* Owner = nullptr);

	/**
	 * Spawn, for callers that report errors themselves (the Exec command): an error starting the script is put in
	 * OutError instead of being shown. An error the script hits once it's running is reported as usual.
	 */
	FBangoScriptHandle Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args, FString& OutError);
	FBangoScriptHandle Spawn(const FString& ScriptPath, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError);

	/** Spawn, for a script asset already in hand (like a Spawn Bango Script node's). Null shows an error. */
	FBangoScriptHandle Spawn(const UBangoScriptAsset* Script, const TArray<FBangoValue>& Args = TArray<FBangoValue>(), const UObject* Owner = nullptr);

	/** Spawn of a script asset, for callers that report errors themselves (OutError, as above). */
	FBangoScriptHandle Spawn(const UBangoScriptAsset* Script, const TArray<FBangoValue>& Args, FString& OutError);
	FBangoScriptHandle Spawn(const UBangoScriptAsset* Script, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError);

	/**
	 * Spawn of a script path (reporting errors in OutError) as a child of the running script ParentId: the Exec
	 * command's. Terminate with bAndChildren stops a script's children, and theirs. The child belongs to the
	 * parent's owner, so `self` is the same actor in both.
	 */
	FBangoScriptHandle SpawnChild(int64 ParentId, const FString& ScriptPath, const TArray<FBangoValue>& Args, FString& OutError);

	/**
	 * Who `self` is in a script spawned with Owner: its Bango ID name, or (when it can't be used) why not: Owner is
	 * null, isn't an actor (a component should pass its owning actor), or has no Bango ID component or name.
	 */
	static FBangoScriptOwner GetScriptOwner(const UObject* Owner);

	/**
	 * Starts an inline script (an FBangoInlineScript property, spec section 15), as Spawn does a script asset: Args fill
	 * its #inputs line, in order, and it runs straight away until it first pauses. Its compiled program is kept
	 * (FBangoModule::GetInlineProgram), so the same text isn't compiled again. Owner, the object it belongs to (usually
	 * `this`), names it in errors (its class, as "BP_Door (inline script)", then which object and which of its scripts:
	 * Bango::DescribeInlineScriptOwner), and is what `self` means, as for Spawn.
	 * An empty script does nothing and gives an invalid handle, without an error. Its text is saved with it, so
	 * SaveState saves it.
	 */
	FBangoScriptHandle Spawn(const FBangoInlineScript& Script, const TArray<FBangoValue>& Args = TArray<FBangoValue>(), const UObject* Owner = nullptr);
	/** Spawn of an inline script, for callers that report errors themselves (OutError, as above). */
	FBangoScriptHandle Spawn(const FBangoInlineScript& Script, const TArray<FBangoValue>& Args, const UObject* Owner, FString& OutError);
	/**
	 * What errors and the debugger call an inline script of Owner's: its class, as "BP_Door (inline script)" (a
	 * Blueprint's class without its "_C"), or "Inline script" without an owner. By class, not by object, so every
	 * object of a class with the same script shares one compiled program; an error adds which object it was.
	 */
	static FString GetInlineScriptName(const UObject* Owner);
	/**
	 * Which script an inline script is, for its persistent variables (FBangoInstance::SetPersistentScope): its ID
	 * (FBangoInlineScriptBase::ScriptId), or without one, a hash of its text. GetTextScope gives the second.
	 */
	static FString GetInlineScriptScope(const FBangoInlineScriptBase& Script);
	static FString GetTextScope(const FString& Source);

	/**
	 * The name of the running script with this id ("/Game/Quests/Intro.bango"), for paths relative to it (Exec).
	 * Empty if there's none, or it was spawned from text (SpawnFromSource).
	 */
	FString GetScriptPath(int64 InstanceId) const;

	/**
	 * Compiles Source, the text of a script, and starts it, like Spawn does with a file. Name is what errors show in
	 * place of a file name (like "Dialogue.Intro"), and what the debugger lists it as. Everything else is as for Spawn:
	 * Args fill its inputs line, it runs straight away until it first pauses, and an invalid handle is returned (with
	 * an error shown) if it doesn't compile or can't start. It's compiled on every call. Its text is kept with it, so
	 * SaveState saves it and LoadState can restore it; it isn't hot reloaded, since it has no file.
	 */
	FBangoScriptHandle SpawnFromSource(const FString& Source, const FString& Name = TEXT("Script"), const TArray<FBangoValue>& Args = TArray<FBangoValue>(),
		const UObject* Owner = nullptr);

	/**
	 * Runs a condition registered with Bango::RegisterCondition and gives its answer. It runs straight away, to the end,
	 * in this world, with its globals and actors; Args fill its #inputs line, if it has one.
	 *
	 * Gives True or False if it returned that. Gives Unset, with the reason in OutError, if it read a global that doesn't
	 * exist or is nil: something not set up yet, which a caller usually treats as false without reporting it. Gives Error,
	 * with the error in OutError, if the handle isn't registered, its arguments don't fit, it stops with any other error,
	 * it reaches its end without returning, or it calls a command that waits (like Sleep): a condition must answer at once.
	 * Nothing is shown or logged here, so a caller checking often can say so once (Bango::ReportError shows one).
	 *
	 * Conditions aren't running scripts: they're not listed by the debugger, saved, or reloaded.
	 */
	EBangoConditionResult EvaluateCondition(FBangoConditionHandle Condition, FString& OutError,
		const TArray<FBangoValue>& Args = TArray<FBangoValue>());

	/**
	 * Compiles and runs a condition given as text (see Bango::CompileCondition), as the other EvaluateCondition does
	 * (Error if it doesn't compile). Name is what errors show in place of a file name. It's compiled on every call and
	 * nothing is kept, so for a condition checked often, register it once with Bango::RegisterCondition instead.
	 */
	EBangoConditionResult EvaluateCondition(const FString& Source, FString& OutError,
		const TArray<FBangoValue>& Args = TArray<FBangoValue>(), const FString& Name = TEXT("Condition"));

	// ---- Watching conditions (spec section 10, Watching conditions; BangoConditionWatches.h) ----

	/**
	 * Makes a group for watching Condition for many things (a trigger's, for each actor that could activate it): see
	 * StartWatch. Name is what its errors show (the owner's name). OnChanges gets, at the end of each frame where any of
	 * its watches' answers changed, the list of them: one call per frame. Fails (an invalid handle, and OutError) if
	 * the condition doesn't compile, or its inputs signature isn't registered. Destroy it when done: it isn't saved, and
	 * goes when the world does.
	 */
	FBangoWatchGroupHandle CreateWatchGroup(const FBangoInlineCondition& Condition, const FString& Name, FBangoWatchChangesFunc OnChanges, FString& OutError);

	/** Stops all its watches; nothing more is reported for it. Safe in its own OnChanges. */
	void DestroyWatchGroup(FBangoWatchGroupHandle& Group);

	/**
	 * Sets inputs shared by all the group's watches, by name (like a trigger's _potential and _actual). Watches whose
	 * condition reads one that's now different are checked at the end of the frame; setting the same values does
	 * nothing, so it's fine to set them every time. False (OutError) if a name isn't one of the condition's inputs.
	 */
	bool SetGroupInputs(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FString& OutError);

	/**
	 * Starts watching the group's condition with its own inputs (like a trigger's _activator), checks it straight away,
	 * and returns whether it's Satisfied (true; false, Unset and errors aren't). After that, a change is reported to the
	 * group's OnChanges: it's checked when a global or input it reads changes, or, if it calls a command (or uses
	 * `call`), also every Poll Interval (the condition's). Errors are shown once, and again only after a true or false.
	 */
	bool StartWatch(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FBangoWatchHandle& OutWatch);

	/** Stops it: it isn't checked or reported again, even this frame. Safe in OnChanges. */
	void StopWatch(FBangoWatchHandle& Watch);

	/**
	 * Stops a script. Does nothing if it has already ended. With bAndChildren, also stops every script it started
	 * (with Exec or spawn) that's still running, and every script those started, and so on, even if the ones in between
	 * (or this one) have already ended. A script may stop itself, or one that started it, from inside a command.
	 */
	void Terminate(FBangoScriptHandle Handle, bool bAndChildren = false);

	/** True while the script is running or paused. False once it has finished, failed or been terminated. */
	bool IsRunning(FBangoScriptHandle Handle) const;

	void TerminateAll();
	int32 GetNumRunning() const { return Scripts.Num(); }

	/**
	 * For the editor's debugger: every running script, in the order they were spawned, then (in editor builds) the
	 * ones that recently stopped with an error, so the error can be looked at. See ForgetEndedScripts.
	 */
	void GetDebugInfo(TArray<FBangoScriptDebugInfo>& OutScripts) const;

	/**
	 * For the debugger's trail: the nodes script Id ran after the first After of them (see FBangoInstance::GetTrail).
	 * Returns how many it has run in all, or 0 if it isn't running.
	 */
	uint64 GetScriptTrail(int64 Id, uint64 After, TArray<int64>& OutNodeIDs) const;
	/** The same by line (see FBangoInstance::GetLineTrail). */
	uint64 GetScriptLineTrail(int64 Id, uint64 After, TArray<int32>& OutLines) const;

	/** Forgets the scripts that stopped with an error (they're no longer listed by GetDebugInfo). */
	void ForgetEndedScripts() { EndedScripts.Reset(); }

	/**
	 * Saves every running script and every global variable. Restore with LoadState, typically into a
	 * USaveGame byte array. Loading state into scripts that changed since saving is not supported.
	 */
	bool SaveState(TArray<uint8>& OutData);

	/** Replaces all running scripts and globals with saved ones. Returns false (with an error shown) if the data is unreadable. */
	bool LoadState(const TArray<uint8>& Data);

	/** Stops all scripts and clears all globals (except those for actors in the level), e.g. when starting a new game. */
	void ResetState();

	/** Used by the WriteScreen command: writes the text to the output log and shows it on screen for Seconds. */
	void WriteScreen(const FString& Text, float Seconds);

	/** Fired when a script stops with an error (the error is also logged and shown on screen). */
	UPROPERTY(BlueprintAssignable, Category = "Bango")
	FBangoScriptFailedEvent OnScriptFailed;

	// UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	/** Runs the level's start script (UBangoLevelStartScript): this comes before any actor's BeginPlay. */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FRunningScript
	{
		/** The script's path, or for a script spawned from text (SpawnFromSource), its name. */
		FString Path;
		/**
		 * True for a script spawned from text (SpawnFromSource): Source is its text, and it has no file. The text is
		 * shared, not copied: an inline script's running copies and the script itself all use one copy.
		 */
		bool bFromSource = false;
		TSharedPtr<const FString> Source;
		const FString& GetSource() const
		{
			static const FString None;
			return Source.IsValid() ? *Source : None;
		}
		/**
		 * An inline script's inputs signature (FBangoInlineScriptBase::UseInputsSignature), saved with it so it compiles the
		 * same when it's restored: its class's path (empty for a global one) and its name (empty for none).
		 */
		FString SignatureClass;
		FString SignatureName;
		/**
		 * An inline script's own instance inputs (FBangoInlineScriptBase::SetInstanceInputs), as they were declared
		 * ("vector3 _red"), saved with it for the same reason. Their values are saved with the instance, as its parameters.
		 */
		TArray<FString> InstanceInputs;
		FBangoInstance Instance;
		/** The newest version it's been asked to switch to (hot reload), so each version is asked for once. */
		TSharedPtr<const FBangoProgram> ReloadTarget;
		/**
		 * True once it has started a script of its own (Exec, spawn, a spawn block). A script that neither has a parent
		 * nor ever started one links nothing to anything when it ends, so it isn't kept in EndedParents.
		 */
		bool bStartedChild = false;
		/** An inline script's owner as it was given to Spawn (not saved), for saying which object's script an error is in. */
		TWeakObjectPtr<const UObject> OwnerObject;
		/** The script that started it (with Exec or spawn), or 0. For Terminate's bAndChildren. */
		int32 ParentId = 0;
	};

	/**
	 * Scripts that have ended, each with the script that started it (or 0), kept while a running script descends from
	 * them, so Terminate with bAndChildren still finds a script's children's children after its children have ended.
	 */
	TMap<int32, int32> EndedParents;
	/** Spawn of a script path, belonging to Owner (FBangoInstance::SetOwner). */
	FBangoScriptHandle SpawnPath(const FString& ScriptPath, const TArray<FBangoValue>& Args, const FBangoScriptOwner& Owner, FString* OutError);
	/** The parent StartScript gives the script it starts (SpawnChild), or 0. */
	int32 PendingParentId = 0;
	/** The script that started this one, whether it's running or has ended (0 if none or unknown). */
	int32 GetParentId(int32 Id) const;
	/** True if Ancestor started Id, or started a script that started it, and so on. */
	bool IsDescendant(int32 Id, int32 Ancestor) const;
	/**
	 * Removes an ended script, keeping its parent in EndedParents (PruneEndedParents drops what's no longer needed).
	 * False if it had no parent and never started a script: then EndedParents is as it was, and needs no pruning.
	 */
	bool RemoveScript(int64 Id);
	void PruneEndedParents();

	bool MakeContext(float DeltaSeconds, FBangoExecContext& OutContext);
	/** Starts a compiled script (Spawn and SpawnFromSource). An error starting it goes in OutError if given, else it's shown. */
	FBangoScriptHandle StartScript(const TSharedPtr<const FBangoProgram>& Program, TSharedPtr<FRunningScript> Script, const TArray<FBangoValue>& Args,
		FString* OutError = nullptr);
	/**
	 * A spawn block (FBangoExecContext::SpawnBlock): starts a copy of Parent's script at the block, as a script of its
	 * own (the same path, for Exec and the debugger), and runs it until it first pauses, as StartScript does.
	 */
	bool StartSpawnBlock(const FBangoInstance& Parent, int32 BlockIP, int64& OutId, FString& OutError);
	/** The next id for a script (or a condition's run): in order, skipping any still in use if they ever wrap around. */
	int32 AllocateId();
	/** OnWorldBeginPlay: runs the level's start script, if it has one, failing it if it waits. */
	void RunLevelStartScript(UWorld& InWorld);
	/** Compiles a script's text (SpawnFromSource, and loading one that was saved). */
	static bool CompileFromSource(const FString& Source, const FString& Name, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);
	/** A restored inline script's program: compiled with its saved inputs signature, if it had one. */
	static bool CompileSavedSource(const FRunningScript& Script, TSharedPtr<const FBangoProgram>& OutProgram, FString& OutError);
	/** Resumes one script. Returns false once it has ended (for any reason). */
	bool ResumeScript(FRunningScript& Script, float DeltaSeconds);
	void ReportFailure(const FRunningScript& Script);
	/** For an error in an inline script: " (on 'Door_3', its script 'OnOpened')", or empty for any other script. */
	FString DescribeInlineOwner(const FRunningScript& Script) const;

	/** Hot reload, in the editor: asks the running copies of every script whose file changed to switch to its new version. */
	void ReloadChangedScripts();
	/** Reports what became of a script's reload, if anything did. */
	void ReportReload(FRunningScript& Script);
	/** Seconds until the next check for changed script files. */
	float ReloadCheckCountdown = 0.0f;
	/** The last compile error reported for each changed script, so each error is reported once. */
	TMap<FString, FString> ReloadErrors;

	/** Runs a compiled condition (EvaluateCondition). */
	EBangoConditionResult RunCondition(const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, FString& OutError);

	/** The watch groups, made when the first is (it needs the game instance's globals). Null if there are none. */
	FBangoWatchRegistry* GetWatches(FString& OutError);
	TUniquePtr<FBangoWatchRegistry> Watches;
	/** Each watch group's registered condition, released when it's destroyed. */
	TMap<int64, FBangoConditionHandle> WatchConditions;

	/** Editor builds: scripts that stopped with an error, newest last, for the debugger (at most MaxEndedScripts). */
	TArray<FBangoScriptDebugInfo> EndedScripts;
	static constexpr int32 MaxEndedScripts = 32;

	TMap<int64, TSharedPtr<FRunningScript>> Scripts;
	int32 NextId = 1;
	/** How many scripts are being started inside each other (a script that Execs a script that Execs ...), to stop runaway recursion. */
	int32 StartDepth = 0;
	static constexpr int32 MaxStartDepth = 32;
	/** Checks entity names against this world's UBangoActorRegistry. */
	TUniquePtr<IBangoEntityResolver> EntityResolver;
	/** The game instance's globals, found once (UBangoGlobalsSubsystem::Get looks them up through the world each time). */
	UBangoGlobalsSubsystem* GetGlobals();
	TWeakObjectPtr<UBangoGlobalsSubsystem> KnownGlobals;

	/**
	 * GetScriptOwner's answer for an actor, kept while it can't have changed: its registry has had no name added or
	 * removed (UBangoActorRegistry::GetVersion), and it's the player's pawn, or isn't, as it was. Only an answer
	 * without a problem is kept.
	 */
	struct FKnownOwner
	{
		FBangoScriptOwner Owner;
		TWeakObjectPtr<UBangoActorRegistry> Registry;
		uint64 Version = 0;
		bool bPlayerPawn = false;
	};
	TMap<TWeakObjectPtr<const UObject>, FKnownOwner> KnownOwners;
	static constexpr int32 MaxKnownOwners = 256;
	/** GetScriptOwner, from KnownOwners when it can be. */
	FBangoScriptOwner FindScriptOwner(const UObject* Owner);
	/**
	 * For a script with persistent variables: Owner's FBangoScriptOwner::Holder, the component holding the script
	 * (HolderHint, or Owner if it's a component) as an entity ID, or empty if it's held by the actor.
	 */
	FString FindScriptHolder(const UObject* Owner, const UActorComponent* HolderHint);
	/** FindScriptOwner, with the holder if Owner is a component (spawning a script by its path or asset). */
	FBangoScriptOwner FindSpawnOwner(const UObject* Owner);
#if !UE_BUILD_SHIPPING
	/** The inline scripts warned about having persistent variables and no ID, by their text's scope: each once. */
	TSet<FString> WarnedUnnamedScripts;
	/** The actors whose inline scripts' IDs have been checked for two the same (CheckInlineScriptIds): each once. */
	TSet<TWeakObjectPtr<const AActor>> CheckedIdOwners;
	/** For an inline script with persistent variables: its actor's scripts' IDs, checked once (Bango.CheckInlineScriptIds). */
	void CheckInlineScriptIds(const UObject* Owner, const UActorComponent* HolderHint);
#endif
};
