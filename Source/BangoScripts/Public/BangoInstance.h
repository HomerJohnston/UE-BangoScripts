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
	/**
	 * Paused on a latent command (Resume polls the command and continues when it finishes), or at a waitUntil whose
	 * condition was false (Resume checks it again: GetWaitSet, IsWaitDue).
	 */
	Waiting,
	/** Reached the end, or `return;` at the top level. */
	Finished,
	/** Stopped by a runtime error (see GetError). */
	Failed,
	/** Stopped by Terminate. */
	Terminated,
};

/** What checking a condition gave (UBangoScriptSubsystem::EvaluateCondition). */
enum class EBangoConditionResult : uint8
{
	/** It returned true. */
	True,
	/** It returned false. */
	False,
	/**
	 * It couldn't answer yet: it read a global that doesn't exist or is nil (`gold > 3` before anything has set gold).
	 * Usually not a mistake, so a caller can treat it as false without reporting it. (isNil(gold) gives true instead.)
	 */
	Unset,
	/**
	 * It's broken: it didn't compile, its arguments don't fit, it stopped with any other error, it reached its end
	 * without returning true or false, or it called a command that waits (a condition must answer at once).
	 */
	Error,
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
	/**
	 * It stopped because it read a global that doesn't exist or is nil (gold + 1 with no gold yet): something not
	 * set up yet, not a mistake in the script. A condition reports this as Unset (EBangoConditionResult).
	 */
	bool bUnsetGlobal = false;

	/** "Quest/Intro.bango(12:5) [node 7] in Nag(): '_x' is nil." */
	FString ToString() const;
};

/**
 * Who a running script belongs to: the actor `self` means (FBangoInstance::SetOwner). Scripts it spawns, and its
 * spawn blocks, belong to the same actor.
 */
struct FBangoScriptOwner
{
	/** The owning actor's Bango ID name, which `self` gives. Empty when `self` can't be used, and Problem says why. */
	FString BangoName;
	/** Why `self` can't be used (no owner, an owner with no Bango ID, ...), or empty for the usual "no owner" message. */
	FString Problem;
};

/** What Resume needs from the outside world. */
class FBangoInstance;

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

	/**
	 * Starts a spawn block's copy of the script (spec section 8): a new instance, made with FBangoInstance::StartSpawned
	 * from Parent at the block's first instruction, that the host runs on its own. Parent carries on. OutId: the new
	 * instance's id. Unset where scripts can't be started (then a spawn stops the script with an error).
	 */
	TFunction<bool(const FBangoInstance& Parent, int32 BlockIP, int64& OutId, FString& OutError)> SpawnBlock;

	/**
	 * Runs `Target call Name(Args)` (spec section 4): the Blueprint function Name of Target's actor, at once. With
	 * bWantResult, OutResult is what it returned (an error if it returns nothing). False, with OutError, if it can't be
	 * called. Unset where actors can't be called (then a call stops the script with an error).
	 */
	TFunction<bool(const FBangoValue& Target, const FString& Name, const TArray<FBangoValue>& Args, bool bWantResult, FBangoValue& OutResult, FString& OutError)> CallReflected;
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
	 * inputs line, declares the script's globals, and sets up the top-level frame.
	 *
	 * Args are positional. Leaving off trailing parameters that have defaults uses the defaults; passing nil
	 * passes nil. Fails (and returns false) for too few or too many arguments, an argument of the wrong type,
	 * a command with no implementation, or a global that conflicts with an existing one of another type.
	 * The Commands table must outlive the instance.
	 */
	bool Start(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FBangoGlobalStore& Globals, int64 InId, FBangoRuntimeError& OutError,
		TConstArrayView<FBangoValue> Args = TConstArrayView<FBangoValue>());

	/**
	 * Prepares the instance to run a spawn block of Parent's script: the same program and commands, one frame that is a
	 * copy of Parent's current one (its variables as they are now), starting at BlockIP. Fails if Parent isn't running.
	 */
	bool StartSpawned(const FBangoInstance& Parent, int32 BlockIP, int64 InId, FBangoRuntimeError& OutError);

	/**
	 * The actor the script belongs to, which `self` means. Set it before Start (an input's default can be `self`); Start
	 * keeps it, StartSpawned copies the parent's, and it's saved with the instance. Without one, `self` stops the script
	 * with an error when it's used.
	 */
	void SetOwner(const FBangoScriptOwner& InOwner) { Owner = InOwner; }
	const FBangoScriptOwner& GetOwner() const { return Owner; }

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

	/**
	 * A condition's answer, from what its Resume gave: True or False, or Unset or Error with the message in OutError.
	 * Stops it if it's waiting (a condition must answer at once). The message isn't shown or logged here.
	 */
	EBangoConditionResult FinishCondition(EBangoInstanceStatus ResumeStatus, FString& OutError);

	/** For the debugger: the calls in progress, the main graph first and the innermost (where it is) last. */
	void GetCallStack(TArray<FBangoCallFrameInfo>& OutFrames) const;

	/** For the debugger: the command it's waiting on (like "Sleep"), "waitUntil", or empty if it isn't waiting. */
	FString GetWaitingCommandName() const;

	/** Paused at a waitUntil: what its condition depends on (FBangoProgram::WaitSets). Null otherwise. */
	const FBangoWaitSet* GetWaitSet() const;

	/**
	 * Paused at a waitUntil whose answer may have changed, so Resume should check it again: a global it reads has
	 * changed since it paused (or ever, if it was loaded since), or it calls a command (checked every frame). False if
	 * it isn't paused at a waitUntil.
	 */
	bool IsWaitDue(const FBangoGlobalStore& Globals) const;

	/** How many instructions it has run in all, over every Resume (for benchmarks). Not saved. */
	uint64 GetInstructionsRun() const { return InstructionsRun; }

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
	/**
	 * The same by line, for a script whose text has no NodeIDs (a compact script): the lines it ran (from 1, as its
	 * text has them), each time it moved on to another line. Recorded with the trail (SetRecordTrail).
	 */
	uint64 GetLineTrail(uint64 After, TArray<int32>& OutLines) const;
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

	/** Warnings since the last call (a global's default that was skipped, ...), for the caller to log. Clears them. */
	TArray<FString> TakeWarnings() { TArray<FString> Taken = MoveTemp(Warnings); Warnings.Reset(); return Taken; }

private:
	/**
	 * Declares InProgram's globals (Start, and switching to a new version): a global that doesn't exist yet is
	 * created, and given its default if it has one. One that exists keeps its value; if it holds nil while this
	 * declaration has a default, that's warned about (once per global). False, with OutError and OutLine, on a type clash.
	 */
	bool DeclareGlobals(const FBangoProgram& InProgram, FBangoGlobalStore& Globals, FString& OutError, int32& OutLine);
	TArray<FString> Warnings;

	bool BindCommands(const FBangoCommandTable& Commands, FString& OutError);
	static bool BindCommandsFor(const FBangoProgram& InProgram, const FBangoCommandTable& Commands, TArray<const FBangoCommand*>& OutBound, FString& OutError);

	/** Switches to ReloadProgram if the script is at a point where it can (see RequestReload). True if it switched. */
	bool TryReload(FBangoGlobalStore& Globals);
	/** The frames as they'd be in ReloadProgram, or false with the reason they don't fit. */
	bool MakeReloadedFrames(TArray<FBangoFrame>& OutFrames, TArray<FBangoValue>& OutScriptSlots, FString& OutReason) const;
	bool MakeReloadedSlots(const FBangoFunctionCode& OldFn, const FBangoFunctionCode& NewFn, const TArray<FBangoValue>& OldSlots, TArray<FBangoValue>& OutSlots, FString& OutReason) const;
	/** Ends a pending reload with Result and Message. */
	void FinishReload(EBangoReloadResult Result, const FString& Message);
	bool CheckArguments(TConstArrayView<FBangoValue> Args, TArray<FBangoValue>& OutValues);
	bool Execute(const FBangoExecContext& Context);
	bool RuntimeFail(const FString& Message);
	/**
	 * RuntimeFail for reading a global that doesn't exist or is nil (FBangoRuntimeError::bUnsetGlobal). While a
	 * waitUntil's condition is worked out, it's no failure: the condition is false, and it pauses there (returning false).
	 */
	bool UnsetGlobalFail(const FString& Message, const FBangoGlobalStore& Globals);
	/**
	 * A waitUntil's condition was false: pauses at it (its WaitCheck is CheckIP, in the current frame) until Set's
	 * globals change. With a timeout whose time is up, goes on at its timeout block instead.
	 */
	void PauseAtWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals);
	/**
	 * A waitUntil's condition was true: carries on, unless it has a heldFor it hasn't been true for yet. Then it
	 * pauses there too, to be checked again as the seconds pass (or goes on at its timeout block, if the time is up
	 * and the timeout ignores holds).
	 */
	void HoldAtWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals);
	/** The pause itself, for both. */
	void EnterWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals);
	/** Forgets a waitUntil's timeout and hold: it's done with (or has neither). */
	void ClearWaitTimes();
	/** Why `self` can't be used here (the owner's Problem, or that there's no owner). */
	FString GetSelfProblem() const;
	/**
	 * Value with each `self` in it (Bango::GetSelfEntityName, alone or in an array) swapped for the owner's Bango ID.
	 * False if there's a `self` and no owner to swap it for.
	 */
	bool ResolveSelf(const FBangoValue& Value, FBangoValue& OutValue) const;

	// Stack helpers
	FBangoValue PopValue();
	bool PopIndexes(int32 Count, TArray<int64>& OutIndexes);

	// Places
	/** The variable's name, for messages and for finding a global. A reference into the program: nothing is copied. */
	const FString& DescribePlace(const FBangoPlace& Place) const;
	/** The main graph's slots: the script's inputs and script variables, which its functions see (EBangoPlaceKind::Script). */
	const TArray<FBangoValue>& GetScriptSlots() const;
	/** The slots a local or script place is in. */
	TArray<FBangoValue>& GetPlaceSlots(const FBangoPlace& Place);
	/**
	 * The store's slot of the global a place names, or INDEX_NONE if nobody has made that global. Found by name the
	 * first time and remembered (GlobalSlots), so reading or writing a global doesn't hash its name every time.
	 */
	FORCENOINLINE int32 FindGlobalSlot(const FBangoPlace& Place, const FBangoGlobalStore& Globals);
	/** The global a place names, or null if nobody has made it. */
	const FBangoValue* FindGlobal(const FBangoPlace& Place, const FBangoGlobalStore& Globals);
	/** As FindGlobal, to change it: it counts as changed, as with FBangoGlobalStore::FindMutable. */
	FBangoValue* FindGlobalToChange(const FBangoPlace& Place, FBangoGlobalStore& Globals);
	/** Entities is only used to add a hint when a missing global is really a Bango ID written without its `@`. */
	/** With bNilOk, a value that's nil or missing (a global nobody made, an element past the end) is loaded as nil instead of stopping the script. */
	bool LoadPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals, const IBangoEntityResolver* Entities, bool bNilOk = false);
	bool StorePlace(const FBangoPlace& Place, FBangoGlobalStore& Globals);
	bool IsNilPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals);
	/** A command that changes its target in place (bMutableTarget): the place's value is handed to it, and stored back when it's done. */
	bool CallCommandInPlace(const FBangoPlace& Place, int32 NameIndex, int32 ArgCount, bool bResultUsed, FBangoGlobalStore& Globals, const FBangoExecContext& Context);
	/** The value a place is, to change it: its local slot or global (which counts as changed), down its indexes. Null, after failing, if it isn't there. */
	FBangoValue* FindPlaceToChange(const FBangoPlace& Place, TConstArrayView<int64> Indexes, FBangoGlobalStore& Globals, const FString& What);
	/** Checks a command call's arguments as they are now (its type parameters, the entities it names), before it runs. */
	bool PrepareCommandCall(const FBangoCommandSignature& Signature, FBangoCommandCall& Call, const FBangoExecContext& Context);

	// Operators
	bool BinaryOp(EBangoOp Op);
	/** The operator on two values (not taken from the stack): its result is pushed. */
	bool BinaryOpValues(EBangoOp Op, const FBangoValue& Left, const FBangoValue& Right);

	// Calls
	bool CallCommand(int32 NameIndex, int32 ArgCount, int32 Flags, const FBangoExecContext& Context);
	/** `target call Name(args)`: hands it to Context.CallReflected. */
	bool CallReflected(int32 NameIndex, int32 ArgCount, bool bWantResult, const FBangoExecContext& Context);
	bool FinishCommand(EBangoCommandResult Result);
	/** Lends a command's call the instance's own (CallScratch) for as long as the call takes. */
	struct FCallScope;
	/** A command's call is done: fails if it failed, and puts its result, as the type it should be, on the stack. */
	bool FinishCall(EBangoCommandResult Result, const FBangoCommand& Command, FBangoCommandCall& Call);

	TSharedPtr<const FBangoProgram> Program;
	/** Program->Names index -> command, for names that are commands. Not saved; rebuilt by Bind. */
	TArray<const FBangoCommand*> BoundCommands;

	int64 Id = 0;
	EBangoInstanceStatus Status = EBangoInstanceStatus::None;
	/** Who `self` is (SetOwner). Saved. */
	FBangoScriptOwner Owner;
	FBangoRuntimeError Error;

	TArray<FBangoFrame> Frames;
	/**
	 * Only for a spawn block's copy that was made in a function, so whose first frame isn't the main graph's: its copy of
	 * the main graph's slots. Otherwise they're in Frames[0] (GetScriptSlots).
	 */
	TArray<FBangoValue> ScriptSlots;
	TArray<FBangoValue> Stack;

	/** The latent command being waited on. */
	int32 PendingCommandName = -1;
	FBangoCommandCall PendingCall;

	/** Paused at a waitUntil: its wait set, else -1. Saved. */
	int32 WaitSet = -1;
	/** The global store's change stamp when it paused there. Not saved: 0 after loading, so it's checked again. */
	uint64 WaitStamp = 0;
	/**
	 * A waitUntil with a timeout: the seconds it has left, from its WaitBegin until its condition is true or the time
	 * is up (0); below 0 otherwise. Saved. Resume counts it down by its context's DeltaSeconds while it's paused there.
	 */
	double WaitTimeLeft = -1.0;
	/** A waitUntil with a heldFor: the seconds its condition must stay true for, from its WaitBegin; below 0 otherwise. Saved. */
	double WaitHoldTime = -1.0;
	/**
	 * While that condition is true, the seconds it must still stay so (0: long enough); below 0 while it's false.
	 * Saved. Resume counts it down like WaitTimeLeft, and a false answer throws it away.
	 */
	double WaitHoldLeft = -1.0;
	/** `timeout (seconds, true)`: the time being up cuts a hold short. Saved. */
	bool bWaitIgnoreHold = false;
	/** PauseAtWait went on at the timeout's block instead of pausing (Resume then carries on, if Execute had stopped). */
	bool bWaitTimedOut = false;
	/** Whether what its condition reads may have changed since it paused (IsWaitDue without the timeout). */
	bool IsWaitConditionDue(const FBangoGlobalStore& Globals) const;
	/**
	 * While a waitUntil's condition is being worked out (WaitCheck to WaitResult): its wait set, and where to go back
	 * to if a global it reads is unset. Never saved, since a script can't pause then.
	 */
	struct FWaitGuard
	{
		int32 Set = -1;
		int32 NumFrames = 0;
		int32 StackSize = 0;
		int32 CheckIP = 0;
		/** It's the timeout's condition being worked out (`timeout (condition)`), not the waitUntil's own. */
		bool bTimeout = false;
	};
	FWaitGuard WaitGuard;

	/**
	 * While the left side of an && or || is worked out (between its TryUnknown and EndTryUnknown), where to go on if
	 * it reads a global that isn't set: the left side is then unknown, and the right side may still settle the
	 * answer. Innermost last. Saved.
	 */
	struct FUnknownGuard
	{
		int32 NumFrames = 0;
		int32 StackSize = 0;
		int32 HandlerIP = 0; // where the right side starts (in the frame it was made in)
		int32 NumPending = 0;
	};
	TArray<FUnknownGuard> UnknownGuards;
	/**
	 * The && and || whose left side was unknown and whose right side is being worked out, with the error the unset
	 * global would have been: their ResolveUnknown raises it if the right side doesn't settle the answer. Saved.
	 */
	struct FPendingUnknown
	{
		int32 NumFrames = 0;
		int32 HandlerIP = 0;
		FString Message;
		int32 Function = 0; // where it was read, for the error's place
		int32 IP = 0;
	};
	TArray<FPendingUnknown> PendingUnknowns;
	/** UnsetGlobalFail went on at an unknown guard's right side instead of stopping (Resume then carries on, as Execute had stopped). */
	bool bUnknownCaught = false;

	/** The instruction being run, for error locations. */
	int32 CurrentFunction = 0;
	int32 CurrentIP = 0;

	/** A condition's answer, once it has returned (GetResult). */
	FBangoValue ConditionResult;

	// GetInstructionsRun. Not saved.
	uint64 InstructionsRun = 0;

	// The debugger's trail (SetRecordTrail). Not saved.
	bool bRecordTrail = false;
	TArray<int64> Trail;
	uint64 TrailCount = 0;
	int64 LastTrailNode = -1;
	TArray<int32> LineTrail;
	uint64 LineTrailCount = 0;
	int32 LastTrailLine = 0;

	// Hot reload (RequestReload). Not saved: a reload that hasn't happened is forgotten by a save.
	TSharedPtr<const FBangoProgram> ReloadProgram;
	TArray<const FBangoCommand*> ReloadCommands;
	/** True once the script has reached a point where it tried to switch, and couldn't. */
	bool bReloadTried = false;
	/** Why the last try didn't fit. */
	FString ReloadFailure;
	EBangoReloadResult ReloadResult = EBangoReloadResult::None;
	FString ReloadMessage;

	/**
	 * The store's slot of each of the program's names that has been used as a global (INDEX_NONE: not looked up yet,
	 * or not there), good while the store's layout is GlobalSlotsLayout. Emptied whenever Program changes. Not saved.
	 * (Kept last: ahead of the members the VM's loop uses all the time, they moved those, and plain loops ran slower.)
	 */
	TArray<int32> GlobalSlots;
	uint64 GlobalSlotsLayout = 0;

	/**
	 * The call a command is given, used again for the next one so its argument list isn't allocated for every call.
	 * Empty between calls. Not saved: a command that waits gets PendingCall.
	 */
	FBangoCommandCall CallScratch;
	/** CallScratch is being used by a call that hasn't returned yet (a call made from inside it gets one of its own). */
	bool bCallScratchInUse = false;
	/** The memory of the slots of functions that have returned, for the next calls' slots. Always empty lists. Not saved. */
	TArray<TArray<FBangoValue>> SpareSlots;
};
