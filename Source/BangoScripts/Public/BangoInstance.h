#pragma once

#include "CoreMinimal.h"
#include "BangoCommand.h"
#include "BangoGlobalStore.h"
#include "BangoProgram.h"

class UObject;

/** Tells the interpreter which entity names have a registered actor (see UBangoActorRegistry). */
class IBangoEntityResolver
{
public:
	virtual ~IBangoEntityResolver() = default;
	virtual bool IsEntityRegistered(const FString& Name) const = 0;
};

enum class EBangoInstanceStatus : uint8
{
	/** Created but not started. */
	None,
	/** Can run: Resume will run it until it pauses, ends or fails. */
	Ready,
	/** Paused on a latent command. Resume polls the command and continues when it finishes. */
	Waiting,
	/** Reached the end, or `return;` at the top level. */
	Finished,
	/** Stopped by a runtime error (see GetError). */
	Failed,
	/** Stopped by Terminate. */
	Terminated,
};

/** A runtime error, pointing at the line and graph node that caused it. */
struct BANGOSCRIPTS_API FBangoRuntimeError
{
	FString FileName;
	int32 Line = 0;
	int32 Column = 0;
	int64 NodeID = -1;
	/** Empty for the top level. */
	FString FunctionName;
	FString Message;

	/** "Quest/Intro.bango(12:5) [node 7] in Nag(): '_x' is nil." */
	FString ToString() const;
};

/** What Resume needs from the outside world. */
struct FBangoExecContext
{
	FBangoGlobalStore* Globals = nullptr;
	/** Optional. If set, entity targets and arguments are checked before a command runs. */
	const IBangoEntityResolver* Entities = nullptr;
	/** Passed to commands. */
	UObject* WorldContext = nullptr;
	/** Seconds since the last Resume, passed to latent commands' Poll. */
	float DeltaSeconds = 0.0f;

	/** Running more instructions than this in one Resume without pausing is treated as an infinite loop. */
	int32 MaxInstructionsPerResume = 1000000;
	/** Deepest allowed function call nesting. */
	int32 MaxCallDepth = 200;
};

/** One function call in progress, for the debugger (FBangoInstance::GetCallStack). */
struct FBangoCallFrameInfo
{
	/** Empty for the script's main graph. */
	FString FunctionName;
	/** The node it's at: for the innermost call, the statement it's running or paused on; for the others, the call it's in. */
	FBangoDebugLoc Loc;
};

/** What became of a reload (FBangoInstance::RequestReload). */
enum class EBangoReloadResult : uint8
{
	/** No reload was asked for, or its result has been taken. */
	None,
	/** Asked for, and waiting for the script to reach a point where it can switch. */
	Pending,
	/** The script switched to the new version and carries on in it. */
	Reloaded,
	/** The script couldn't switch, so it carries on with the version it was running (the message says why). */
	KeptOld,
};

/** One function call in progress. */
struct FBangoFrame
{
	int32 Function = 0;
	/** The next instruction to run. */
	int32 IP = 0;
	/** Where this frame's part of the value stack starts. */
	int32 StackBase = 0;
	TArray<FBangoValue> Slots;
};

/**
 * One running copy of a compiled script (spec section 10).
 *
 * Everything an instance holds is plain data (FBangoValues and indexes), so a paused instance can be
 * saved with Serialize and resumed after loading.
 */
class BANGOSCRIPTS_API FBangoInstance
{
public:
	/**
	 * Prepares the instance to run Program: finds every command it calls, checks Args against the script's
	 * params line, declares the script's globals, and sets up the top-level frame.
	 *
	 * Args are positional. Leaving off trailing parameters that have defaults uses the defaults; passing nil
	 * passes nil. Fails (and returns false) for too few or too many arguments, an argument of the wrong type,
	 * a command with no implementation, or a global that conflicts with an existing one of another type.
	 * The Commands table must outlive the instance.
	 */
	bool Start(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FBangoGlobalStore& Globals, int64 InId, FBangoRuntimeError& OutError,
		TConstArrayView<FBangoValue> Args = TConstArrayView<FBangoValue>());

	/**
	 * Runs until the script pauses on a latent command, finishes or fails. Returns the new status.
	 * Does nothing unless the status is Ready or Waiting.
	 */
	EBangoInstanceStatus Resume(const FBangoExecContext& Context);

	/** Stops the instance. It can't be resumed afterwards. */
	void Terminate();

	EBangoInstanceStatus GetStatus() const { return Status; }
	bool IsRunning() const { return Status == EBangoInstanceStatus::Ready || Status == EBangoInstanceStatus::Waiting; }
	const FBangoRuntimeError& GetError() const { return Error; }
	int64 GetId() const { return Id; }
	const TSharedPtr<const FBangoProgram>& GetProgram() const { return Program; }

	/** Where the instance is: the statement it's running or paused on. For highlighting the graph node. */
	FBangoDebugLoc GetCurrentLocation() const;

	/** Once a condition (Bango::CompileCondition) has finished: the bool it returned. Nil otherwise. */
	const FBangoValue& GetResult() const { return ConditionResult; }

	/** For the debugger: the calls in progress, the main graph first and the innermost (where it is) last. */
	void GetCallStack(TArray<FBangoCallFrameInfo>& OutFrames) const;

	/** For the debugger: the command it's waiting on (like "Sleep"), or empty if it isn't waiting. */
	FString GetWaitingCommandName() const;

	/**
	 * For the debugger's trail: records the node IDs it runs, in order (each time it moves on to another node), keeping
	 * the last TrailSize. Off by default; the script subsystem turns it on in the editor.
	 */
	void SetRecordTrail(bool bRecord) { bRecordTrail = bRecord; }
	/**
	 * Adds the nodes it ran after the first After of them (oldest first; only the last TrailSize are kept), and returns
	 * how many it has run in all, to pass as After next time.
	 */
	uint64 GetTrail(uint64 After, TArray<int64>& OutNodeIDs) const;
	static constexpr int32 TrailSize = 256;

	/**
	 * Saves or loads the instance's state (not the program). After loading, call Bind with the same
	 * program before resuming. Loading a state saved from a different version of the script is not supported.
	 */
	void Serialize(FArchive& Ar);

	/** After loading: attaches the program and commands, and checks the loaded state fits the program. */
	bool Bind(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FString& OutError);

	/**
	 * Hot reload: asks a running instance to switch to NewProgram, a new version of its script, without starting over.
	 *
	 * The switch happens the next time the script finishes a simple statement node (an assignment, ++/--, or a call on
	 * its own line) while running, usually as soon as the latent command it's waiting on (a Sleep) finishes. It carries on
	 * after the same node (by NodeID) in the new version. Each function call in progress is matched the same way: the
	 * function by name, the call by the node it's in (a call on its own line, or the value of an assignment). Locals,
	 * parameters and loop variables are kept by name (a loop's by its loop node); a local the new version adds gets its
	 * declared starting value. The new version's globals are declared when it switches.
	 *
	 * If the switch doesn't fit at that point (the node is gone, a local changed type, a function call is in the middle
	 * of an expression, ...), the script keeps trying at the following statements until it next pauses; then it gives
	 * up and carries on with the old version (GetReloadResult says why). Returns false, and changes nothing, if NewProgram
	 * can't run at all (a command it calls isn't registered) or the instance isn't running.
	 */
	bool RequestReload(const TSharedPtr<const FBangoProgram>& NewProgram, const FBangoCommandTable& Commands, FString& OutError);

	/** True while a reload has been asked for and hasn't happened or been given up on. */
	bool IsReloadPending() const { return ReloadProgram.IsValid(); }

	/** The outcome of the last reload, once known (Reloaded or KeptOld), and a message about it. Clears it, so each is reported once. */
	EBangoReloadResult TakeReloadResult(FString& OutMessage);

private:
	bool BindCommands(const FBangoCommandTable& Commands, FString& OutError);
	static bool BindCommandsFor(const FBangoProgram& InProgram, const FBangoCommandTable& Commands, TArray<const FBangoCommand*>& OutBound, FString& OutError);

	/** Switches to ReloadProgram if the script is at a point where it can (see RequestReload). True if it switched. */
	bool TryReload(FBangoGlobalStore& Globals);
	/** The frames as they'd be in ReloadProgram, or false with the reason they don't fit. */
	bool MakeReloadedFrames(TArray<FBangoFrame>& OutFrames, FString& OutReason) const;
	/** Ends a pending reload with Result and Message. */
	void FinishReload(EBangoReloadResult Result, const FString& Message);
	bool CheckArguments(TConstArrayView<FBangoValue> Args, TArray<FBangoValue>& OutValues);
	bool Execute(const FBangoExecContext& Context);
	bool RuntimeFail(const FString& Message);

	// Stack helpers
	FBangoValue PopValue();
	bool PopIndexes(int32 Count, TArray<int64>& OutIndexes);

	// Places
	FString DescribePlace(const FBangoPlace& Place) const;
	/** Entities is only used to add a hint when a missing global is really a Bango ID written without its `@`. */
	/** With bNilOk, a value that's nil or missing (a global nobody made, an element past the end) is loaded as nil instead of stopping the script. */
	bool LoadPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals, const IBangoEntityResolver* Entities, bool bNilOk = false);
	bool StorePlace(const FBangoPlace& Place, FBangoGlobalStore& Globals);
	bool IsNilPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals);
	bool ArrayModify(const FBangoPlace& Place, int32 Kind, bool bResultUsed, FBangoGlobalStore& Globals);

	// Operators
	bool BinaryOp(EBangoOp Op);

	// Calls
	bool CallCommand(int32 NameIndex, int32 ArgCount, bool bHasTarget, const FBangoExecContext& Context);
	bool FinishCommand(EBangoCommandResult Result);

	TSharedPtr<const FBangoProgram> Program;
	/** Program->Names index -> command, for names that are commands. Not saved; rebuilt by Bind. */
	TArray<const FBangoCommand*> BoundCommands;

	int64 Id = 0;
	EBangoInstanceStatus Status = EBangoInstanceStatus::None;
	FBangoRuntimeError Error;

	TArray<FBangoFrame> Frames;
	TArray<FBangoValue> Stack;

	/** The latent command being waited on. */
	int32 PendingCommandName = -1;
	FBangoCommandCall PendingCall;

	/** The instruction being run, for error locations. */
	int32 CurrentFunction = 0;
	int32 CurrentIP = 0;

	/** A condition's answer, once it has returned (GetResult). */
	FBangoValue ConditionResult;

	// The debugger's trail (SetRecordTrail). Not saved.
	bool bRecordTrail = false;
	TArray<int64> Trail;
	uint64 TrailCount = 0;
	int64 LastTrailNode = -1;

	// Hot reload (RequestReload). Not saved: a reload that hasn't happened is forgotten by a save.
	TSharedPtr<const FBangoProgram> ReloadProgram;
	TArray<const FBangoCommand*> ReloadCommands;
	/** True once the script has reached a point where it tried to switch, and couldn't. */
	bool bReloadTried = false;
	/** Why the last try didn't fit. */
	FString ReloadFailure;
	EBangoReloadResult ReloadResult = EBangoReloadResult::None;
	FString ReloadMessage;
};
