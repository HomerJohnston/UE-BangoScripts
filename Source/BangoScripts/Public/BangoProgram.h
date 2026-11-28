#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"

namespace Bango
{
	/** A CallCommand instruction's C: the call has a target, which comes before its arguments on the stack. */
	constexpr int32 CallHasTarget = 1;
	/** A CallCommand instruction's C: the command is pure (FBangoCommandSignature::bPure), so a condition isn't polled for it. */
	constexpr int32 CallIsPure = 2;
	/** A CallCommand instruction's C: the call's result is used (FBangoCommandCall::bResultUsed). */
	constexpr int32 CallResultUsed = 4;
	/** A CallCommandInPlace instruction's C: its argument count is the low bits, and CallIsPure and CallResultUsed are shifted up past it. */
	constexpr int32 InPlaceArgCountMask = 0xFFFF;
	constexpr int32 InPlaceFlagShift = 16;
	/**
	 * A CallCommand instruction's C, above its flags: 1 + the block command's block (FBangoFunctionCode::Blocks) the
	 * call opens or is written in, or 0 for a call that has nothing to do with one.
	 */
	constexpr int32 CallBlockShift = 8;
	/** A CallCommand instruction's C: the call opens the block above (it's the block command's own call), rather than being written in it. */
	constexpr int32 CallOpensBlock = 8;
	/** In a block's entry slot (FBangoBlockInfo::EntrySlot): its flags, and each entry's. */
	constexpr int64 EntryWatched = 1;
	constexpr int64 EntryRecheck = 2;
	constexpr int64 EntryPassed = 1;
	constexpr int64 EntryHasCondition = 2;
	/** The entry slot's elements before the entries' own: the entry running, and the flags. */
	constexpr int32 EntryHeader = 2;
	/** A WaitBegin instruction's B: which values it takes off the stack (pushed in this order). */
	constexpr int32 WaitHasHold = 1;
	constexpr int32 WaitHasTimeout = 2;
	constexpr int32 WaitHasIgnoreHold = 4;

	// What a FusedLoad or FusedConst instruction runs in one go (its C): itself and the instructions after it.
	// L is a LoadPlace of a whole local, C a PushConst, Op an arithmetic or comparison operator, S a StorePlace of a
	// whole variable, J a JumpIfFalse. In the last two, the operator's left side is already on the stack.
	constexpr int32 FuseLoadLoadOp = 1;        // L L Op      `_a + _b`
	constexpr int32 FuseLoadConstOp = 2;       // L C Op      `_a * 3`
	constexpr int32 FuseLoadLoadOpJump = 3;    // L L Op J    `while (_i < _n)`
	constexpr int32 FuseLoadConstOpJump = 4;   // L C Op J    `if (_count > 3)`
	constexpr int32 FuseLoadLoadOpStore = 5;   // L L Op S    `_sum = _sum + _i;`
	constexpr int32 FuseLoadConstOpStore = 6;  // L C Op S    `_i++;`
	constexpr int32 FuseLoadOp = 7;            // L Op        `... + _b`
	constexpr int32 FuseConstOp = 8;           // C Op        `... % 1000003`
	/** How many instructions a fused one of that kind stands for. */
	constexpr int32 GetFuseLength(int32 Kind)
	{
		return Kind == FuseLoadOp || Kind == FuseConstOp ? 2 : (Kind == FuseLoadLoadOp || Kind == FuseLoadConstOp ? 3 : 4);
	}
}

/**
 * Compiled Bango bytecode (produced by Bango::CompileScript, run by the interpreter).
 *
 * A stack machine. Every value on the stack and in a local slot is an FBangoValue, so a paused
 * script (stack, slots, instruction pointers) can be saved and loaded.
 *
 * Operands A, B and C are indexes into the program's pools or the function's tables,
 * or jump targets (instruction indexes within the same function).
 */
enum class EBangoOp : uint8
{
	Nop,

	// ---- Stack ----
	PushConst,          // A = constant                      -> value
	PushNil,            // A = type                          -> nil of that type
	Pop,                // value ->
	DupN,               // A = n: duplicates the top n values

	// ---- Variables (places: a local slot or a global, plus N indexes on the stack, and maybe a vector part) ----
	LoadPlace,          // A = place, B = 1: nil ok.  indexes... -> value  (fails if the value is nil or missing, unless B: then nil)
	StorePlace,         // A = place.  indexes..., value ->      (converts to the place's type; grows arrays)
	IsNilPlace,         // A = place.  indexes... -> bool        (never fails)
	IsNilValue,         // value -> bool                         (isNil of a call's result, or an element of one)

	// ---- Values ----
	Index,              // A = 1: nil ok.  array, index -> element  (fails if nil or out of range, unless A: then nil, as for isNil)
	Convert,            // A = type.   value -> value as that type (fails if it doesn't fit)
	MakeArray,          // A = count, B = element type.  elements... -> array
	MakeVector,         // A = 2 or 3.  numbers... -> vector2 / vector3
	GetPart,            // A = 0 (x), 1 (y) or 2 (z).  vector -> float

	// ---- Operators (types are checked at run time too, for values the compiler couldn't type) ----
	Not, Negate,
	Add, Subtract, Multiply, Divide, Modulo,
	Less, LessEqual, Greater, GreaterEqual,
	Equal, NotEqual,

	// ---- Flow ----
	Jump,               // A = target
	JumpIfFalse,        // A = target.  bool ->
	JumpIfFalseOrPop,   // A = target.  bool -> bool (if false, jumps and keeps it; otherwise pops)    for &&
	JumpIfTrueOrPop,    // A = target.  bool -> bool (if true, jumps and keeps it; otherwise pops)     for ||
	TryUnknown,         // A = where the right side of an && or || starts. Until EndTryUnknown, reading a global that isn't set (unknown) doesn't stop the script: it goes on at A, the left side noted as unknown
	EndTryUnknown,      // ends the TryUnknown before it: the left side has its value
	ResolveUnknown,     // A = the same as its TryUnknown's, B = 1 for ||, 0 for &&.  bool -> bool : if the left side was unknown, the right side must settle it (true for ||, false for &&); otherwise the left side's unset global stops the script after all

	// ---- Calls ----
	CallFunction,       // A = function, B = argument count.  args... -> [result]
	CallCommand,        // A = name (command), B = argument count, C = Bango::CallHasTarget and CallIsPure bits.  [target], args... -> [result]
	CallReflected,      // A = name (a Blueprint function), B = argument count, C = 1 if its result is used.  target, args... -> [result]
	Return,             // ends a void function
	ReturnValue,        // value -> (ends a function, the value goes to the caller)
	Halt,               // ends the script instance
	ErrorMissingReturn, // a non-void function reached its end without returning

	// A command that changes its target in place (FBangoCommandSignature::bMutableTarget), like Push: the target is the
	// place, handed to the command and stored back when it's done.
	CallCommandInPlace, // A = place (the target), B = name (command), C = argument count, and flags above it (Bango::InPlaceFlagShift).  indexes..., args... -> [result]

	ForEachNext,        // A = slot holding the array copy (A + 1 holds the int position), B = loop variable slot, C = target when done
	SpawnBlock,         // A = target: starts another instance at A with a copy of this frame's slots (a spawn block); this one carries on. B = 1: push the new instance's id
	CloseBlock,         // A = block (FBangoFunctionCode::Blocks): its command's close handler runs, then its state is emptied. B = 1: the script is leaving the block early (return, break, continue, goto)

	// ---- A block command's entries (FBangoBlockInfo::EntryCount) ----
	// Its entries' conditions are checked in one run of code, EntryCheckBegin to EntryCheckEnd: when the block opens
	// (after the command's arguments, before its call), and again from there when the command asks (RecheckEntries)
	// or watches them (WatchEntries). Then the entries' blocks follow the command's call, each ending in an EntryEnd.
	EntryCheckBegin,    // A = block. Starts checking its entries' conditions: an entry without one passes
	EntryCond,          // A = block, B = entry, C = where the next entry's check starts. Its condition follows: while it's worked out, a global that's unset makes it false (on at C), and it can't wait
	EntryResult,        // A = block, B = entry.  bool -> : whether it passed
	EntryCheckEnd,      // A = block. Done checking (a default passes if no other entry did): when the block opens, on to the command's call; for a re-check, back to the command, still waiting
	EntryEnd,           // A = block, B = entry: the entry's closing brace. The command's EntryEnded handler runs, and runs another entry, or the block closes

	// ---- waitUntil (Program.WaitSets) ----
	WaitBegin,          // A = wait set (one with a heldFor or a timeout), B = which of the Bango::WaitHas... values are there.  [hold seconds] [timeout seconds] [ignoreHold] -> : notes them, before its first WaitCheck
	WaitCheck,          // A = wait set. Its condition follows: while it's worked out, a global that's unset makes it false, and it can't wait
	WaitTimeoutCheck,   // A = wait set, B = its WaitCheck. Its timeout's condition follows (`timeout (condition)`): reached from WaitResult, never by running into it. While it's worked out, a global that's unset makes it false
	WaitTimeoutResult,  // A = wait set, B = its WaitCheck.  bool -> : true goes on (into the timeout's block, which follows); false pauses, to run from B again
	WaitResult,         // A = wait set, B = its WaitCheck.  bool -> : true carries on; false pauses until one of the set's globals changes (every frame, if it polls), then runs from B again. With a timeout whose time is up, false goes on at the set's TimeoutIP instead. With a heldFor, true pauses too until it has been true that long (FBangoInstance::HoldAtWait)

	// ---- Fused (Bango::FuseInstructions) ----
	// The first instruction of a common run (`_i < _n` and its jump, `_i++`), which does the whole run in one go. The
	// run's other instructions are still there after it, unchanged, and it has its own operands unchanged, so
	// whenever the quick way doesn't apply (a nil variable, a reload waiting) it does what it was, a LoadPlace or a
	// PushConst, and the rest run as they would have. Nothing else about the program changes: not an instruction's
	// position, not a jump, not what's counted against the instruction limit. A listing shows what it was.
	FusedLoad,          // A = place, B = 0 (as LoadPlace), C = which run (Bango::FuseLoadLoadOp, ...)
	FusedConst,         // A = constant (as PushConst), C = Bango::FuseConstOp

	Count               // not an op
};

struct FBangoInstruction
{
	EBangoOp Op = EBangoOp::Nop;
	int32 A = 0;
	int32 B = 0;
	int32 C = 0;
};

/** Where an instruction came from, for errors and for highlighting the running graph node. */
struct FBangoDebugLoc
{
	int32 Line = 0;
	int32 Column = 0;
	int64 NodeID = -1;
};

enum class EBangoPlaceKind : uint8
{
	Local,   // Index = slot
	Global,  // Index = name
	Script,  // Index = slot of the main graph's frame: one of the script's inputs or script variables, used in a function
};

/** A variable reference, optionally indexed and ending in a vector part: `_x`, `gold`, `_grid[i][j]`, `_path[i].x`. The indexes are on the stack. */
struct FBangoPlace
{
	EBangoPlaceKind Kind = EBangoPlaceKind::Local;
	int32 Index = 0;
	int32 NumIndexes = 0;
	/**
	 * -1 for the whole value; 0, 1 or 2 for a vector's x, y or z; ElementPart if the last of the indexes picks a
	 * vector's part or a string's character (`_v[i]`, `_s[i]`) rather than an array's element.
	 */
	int32 Part = -1;

	static constexpr int32 ElementPart = -2;
};

/**
 * The code of one simple statement node (an assignment, ++/--, or a call on its own line), for reloading a running
 * script (FBangoInstance::RequestReload): when the node's code has run, the script can carry on at the same node's
 * End in a new version of the script.
 */
struct FBangoStatementRange
{
	int64 NodeID = -1;
	/** The node's first instruction, and the one after its last. */
	int32 Begin = 0;
	int32 End = 0;
};

/**
 * A block command's block (`@john Speech(0, 1) { ... }`), in the function it's written in. Its state is a hidden
 * local of that function, so it's saved, and copied for a spawn block, as any local is.
 */
struct FBangoBlockInfo
{
	/** The command, as a name (FBangoProgram::Names), like a CallCommand's A. */
	int32 Command = 0;
	/** The slot that holds the block's state while it's open. */
	int32 StateSlot = 0;
	/**
	 * The slot that says where the block is: nil or 0, not open (never reached, skipped, or closed); 1, open; 2,
	 * closed, with one of the blocks written in it (picked by its close handler) running.
	 */
	int32 PhaseSlot = 0;
	/** The first instruction of the block's statements, after the command's call: where a block picked to run starts. */
	int32 BodyAt = 0;
	/** The CloseBlock instruction at its closing brace: where a command that ends the block sends the script. The instruction after it is where one that skips the block does. */
	int32 CloseAt = 0;
	/** The block this one's command is written in and belongs to (FBangoCommandSignature::InsideOf), or -1. */
	int32 Parent = -1;

	// ---- An entry list (FBangoCommandSignature::Entries) ----
	/** How many entries the block has (not counting disabled ones), or -1 for an ordinary block. */
	int32 EntryCount = -1;
	/**
	 * The slot with what's known about its entries while it's open: an int array, [the entry running (-1 none),
	 * flags (Bango::EntryWatched, Bango::EntryRecheck), then each entry's Bango::EntryPassed and Bango::EntryHasCondition].
	 */
	int32 EntrySlot = -1;
	/** The EntryCheckBegin instruction, and the instruction after its EntryCheckEnd (what its conditions read is in between). */
	int32 CheckAt = -1;
	int32 CheckEnd = -1;
	/** Each entry's first instruction. */
	TArray<int32> EntryAt;
	/** Whether each entry has a condition (`entry if (...):`) or not (`entry:`, which always passes, and `default:`). */
	TArray<bool> EntryConditions;
	/** The `default:` entry (the last), which passes when no other entry does, or -1. Worked out at EntryCheckEnd. */
	int32 DefaultEntry = -1;
	/** The globals its conditions read (themselves, or in the script's functions they call): a change to one rechecks a watched block. */
	TArray<FString> EntryGlobals;
	/** Empty if only those globals can change its conditions' answers; else why a watched block is rechecked every frame. */
	FString EntryPollReason;
};

struct FBangoFunctionCode
{
	/** Empty for the script's top level. */
	FString Name;
	bool bReturnsVoid = true;
	FBangoType ReturnType;
	/** Parameters occupy the first slots. */
	int32 NumParams = 0;

	/** Every local slot's type. A new frame starts with every slot holding nil of its type. */
	TArray<FBangoType> SlotTypes;
	/** Names for errors and debugging. Hidden slots (forEach bookkeeping) have names in parentheses. */
	TArray<FString> SlotNames;
	/**
	 * For reloading: the node a slot belongs to, so two slots of the same name are told apart: the for or forEach loop
	 * (its variable and bookkeeping), or a local's declaration node (a declaration, or a spawn that declares its id).
	 * -1 for parameters, scriptVars, and locals without node metadata (matched by name); -2 for a loop without node
	 * metadata (such slots aren't carried over).
	 */
	TArray<int64> SlotNodeIDs;
	/** For reloading: each slot's declared starting value (nil if it has none), given to a local a new version adds. */
	TArray<FBangoValue> SlotStartValues;
	/** For reloading: the simple statement nodes that have node metadata, in code order. */
	TArray<FBangoStatementRange> Statements;

	TArray<FBangoPlace> Places;
	/** The block commands' blocks written in this function, in the order they're written. */
	TArray<FBangoBlockInfo> Blocks;
	TArray<FBangoInstruction> Code;
	/** One entry per instruction. */
	TArray<FBangoDebugLoc> Debug;
};

/** One parameter of a script, from its `inputs (...)` line. */
struct FBangoScriptParamInfo
{
	FString Name;
	FBangoType Type;
	/** Parameters without a default are required, and always come before those with one. */
	bool bHasDefault = false;
	/** The default value (already converted to Type; may be nil). Only meaningful if bHasDefault. */
	FBangoValue Default;
};

/**
 * One input of an inputs signature, as its C++ writes it (Bango::RegisterInputsSignature): a type and a name, as in an
 * #inputs line but without a default (`entity _activator`), and what it is, shown in the editor. Names should start
 * with '_'.
 */
struct FBangoInputDecl
{
	FString Decl;
	FString Tooltip;

	FBangoInputDecl(const TCHAR* InDecl, const TCHAR* InTooltip = TEXT("")) : Decl(InDecl), Tooltip(InTooltip) {}
	FBangoInputDecl(const FString& InDecl, const FString& InTooltip = FString()) : Decl(InDecl), Tooltip(InTooltip) {}

	bool operator==(const FBangoInputDecl& Other) const { return Decl.Equals(Other.Decl, ESearchCase::CaseSensitive) && Tooltip.Equals(Other.Tooltip, ESearchCase::CaseSensitive); }
	bool operator!=(const FBangoInputDecl& Other) const { return !(*this == Other); }
};

/**
 * The inputs an object's C++ gives one of its inline scripts, in place of an #inputs line (spec section 15): the
 * script reads them without declaring them, and can't have an #inputs line. Its owner passes their values in this order.
 *
 * They're a registered inputs signature's (Bango::RegisterInputsSignature), then, last, the object's own instance
 * inputs (FBangoInlineScriptBase::SetInstanceInputs), if it has any: either part may be missing.
 */
struct FBangoInputsSignature
{
	/**
	 * Whose they are, for messages: "BangoTrigger's 'Activation'" ("the global 'Activation'" for a global one), with
	 * " and this object" after it (or just "this object") when the last ones are the object's own.
	 */
	FString Label;
	/** In order. None has a default. */
	TArray<FBangoScriptParamInfo> Params;
	/** One per input, maybe empty: what it is, for the editor. */
	TArray<FString> Tooltips;
	/** How many of Params, at the end, are the object's own instance inputs. The ones before are the registered signature's. */
	int32 NumInstanceInputs = 0;
	/** The registered signature's label ("BangoTrigger's 'Activation'"), or empty if there's none (only instance inputs). */
	FString SignatureLabel;

	/** True if input Index is one of the object's own (an instance input). */
	bool IsInstanceInput(int32 Index) const { return Index >= Params.Num() - NumInstanceInputs && Index < Params.Num(); }
	/** Who gives input Index, for the editor: the signature's label, or "this object (its own inputs)". */
	BANGOSCRIPTS_API FString GetSourceLabel(int32 Index) const;

	/** "entity _trigger, int _count": as an #inputs line would list them. */
	BANGOSCRIPTS_API FString ToInputsText() const;
	/** "_trigger, _count" */
	BANGOSCRIPTS_API FString GetNames() const;
};

/**
 * A persistent local or script variable (`persistent int _i ?= 0;`): kept in the game instance's store of globals,
 * under a name of its own for each script and owner (FBangoInstance::GetPersistentKey), so the program reads and
 * writes it as a global, through the name Names[NameIndex] (Bango::PersistentLocalPrefix + its name).
 */
struct FBangoPersistentLocal
{
	FString Name;
	FBangoType Type;
	int32 NameIndex = 0;
	int32 Line = 0;
	bool bHasDefault = false;
	FBangoValue Default;
};

namespace Bango
{
	/** What a persistent local's name in FBangoProgram::Names begins with: not a name a script can write, so it's no global's. */
	inline const TCHAR* PersistentLocalPrefix = TEXT("%persistent ");
}

struct FBangoGlobalDecl
{
	FString Name;
	FBangoType Type;
	int32 Line = 0;
	/** `persistent global`: outlives the world (FBangoGlobalStore::ResetWorldGlobals keeps it). */
	bool bPersistent = false;
	/**
	 * `global int stage ?= 1;`: a default, given to the global when an instance starts, but only if the global doesn't
	 * exist yet or is nil (no script has given it a value, nothing has set it, and no save has brought one back). A
	 * fixed value, already converted to Type; it may be nil (which does nothing).
	 */
	bool bHasDefault = false;
	FBangoValue Default;
};

/** A `target call Name(args)` as the compiler saw it (FBangoProgram::ReflectedCalls), for the editor's checks. */
struct FBangoReflectedCallSite
{
	/** The target's Bango ID name, if it's written as one (`@light`, or `self` / `player`'s names); else empty. */
	FString TargetEntity;
	FString Name;
	int32 NumArgs = 0;
	/** Its result is used (so the function must return one). */
	bool bResultUsed = false;
	/** Where its name is. */
	int32 Line = 0;
	int32 Column = 0;
};

/** What can change some code's answer (Bango::FindDependencies): for waitUntil, and for watched conditions. */
struct FBangoDependencies
{
	/** The globals it reads, itself or in the script's functions it calls (sorted). */
	TArray<FString> Globals;
	/** The main graph's inputs (#inputs, or an inputs signature) it reads, by name, in their order. */
	TArray<FString> Inputs;
	/**
	 * Empty if only those can change its answer; else why it must be checked again from time to time anyway
	 * ("it calls IsAlive"): it calls a command or a Blueprint function (`call`).
	 */
	FString PollReason;
};

/**
 * One `waitUntil (condition);` (WaitCheck and WaitResult): what can change its condition's answer while the script waits.
 * Locals can't change then (a spawn copies them), so only globals, and whatever commands look at.
 */
struct FBangoWaitSet
{
	/** The globals its condition reads, itself or in the script's functions it calls: a change to one rechecks it. */
	TArray<FString> Globals;
	/** Empty if only those globals can change its answer; else why it's rechecked every frame ("it calls IsAlive"). */
	FString PollReason;
	/** Where its condition's code is: Functions[Function].Code, from CondBegin to before CondEnd. */
	int32 Function = 0;
	int32 CondBegin = 0;
	int32 CondEnd = 0;
	/** Where the waitUntil is. */
	int32 Line = 0;
	int32 Column = 0;
	/** Its node in the graph, or -1. */
	int64 NodeID = -1;
	/** `timeout (seconds) { ... }`: where that block's code starts (in Function), or -1 without a timeout. */
	int32 TimeoutIP = -1;
	/**
	 * `timeout (condition) { ... }`: where the code that checks that condition starts (its WaitTimeoutCheck), or -1
	 * for a timeout in seconds (or none). It's checked whenever the waitUntil's own condition is false (and, with
	 * IgnoreHold, while that one is true and being held); true runs the timeout's block.
	 */
	int32 TimeoutCondIP = -1;
	/** That condition's code, from TimeoutCondBegin to before TimeoutCondEnd (what it reads is in Globals and PollReason too). */
	int32 TimeoutCondBegin = 0;
	int32 TimeoutCondEnd = 0;
	/** `heldFor (seconds)`: its condition must stay true that long. */
	bool bHold = false;
};


/** A Blueprint function `call` can reach (Bango::GetBangoCallableFunctions), for the editor. */
struct FBangoCallableFunction
{
	FString Name;
	TArray<FString> ParamNames;
	TArray<FBangoType> ParamTypes;
	/** False if it returns nothing (or more than one value, which a script can't use). */
	bool bHasResult = false;
	FBangoType ResultType;
	/** Its tooltip, in editor builds. */
	FString Description;
	/** "SetBrightness(float Brightness, bool Fade) -> string" */
	FString Signature;
};

namespace Bango
{
	/**
	 * Checks a `call` against its target's Bango functions (Functions, from GetBangoCallableFunctions), as the editor
	 * does when it knows the target's class: TargetLabel names it ("@light (BP_Light)"). False, with OutError, if
	 * there's no such function, the argument count is wrong, or its result is used but it returns none.
	 */
	BANGOSCRIPTS_API bool CheckReflectedCall(const FBangoReflectedCallSite& Site, const TArray<FBangoCallableFunction>& Functions, const FString& TargetLabel, FString& OutError);
}

struct FBangoProgram
{
	FString FileName;
	/** A condition (Bango::CompileCondition): the main graph returns a bool, which FBangoInstance::GetResult gives. */
	bool bIsCondition = false;

	TArray<FBangoValue> Constants;
	/**
	 * Bango::GetDefinesFingerprint when it was compiled, if it uses a define (spec section 4, Defines); 0 if it uses
	 * none. A program whose fingerprint isn't the current one is compiled again before it's used (IsCompiledWithCurrentDefines).
	 */
	uint64 DefinesFingerprint = 0;
	/** False if the defines have changed since it was compiled, and it uses one: compile it again. */
	bool IsCompiledWithCurrentDefines() const;
	/** Global names and command names (and persistent locals', FBangoPersistentLocal). */
	TArray<FString> Names;
	/** The persistent locals and script variables the script declares, in all its functions. */
	TArray<FBangoPersistentLocal> PersistentLocals;
	/** For each of Names: its persistent local (an index into PersistentLocals), or -1. Empty if there are none. */
	TArray<int32> PersistentLocalOfName;
	TArray<FBangoType> Types;

	/** Functions[0] is the top level (the entry point). */
	TArray<FBangoFunctionCode> Functions;

	/** Every global the script declares. They are declared when an instance starts, before any code runs. */
	TArray<FBangoGlobalDecl> Globals;
	/** Every `target call Name(args)` in it, in order, for the editor's checks. */
	TArray<FBangoReflectedCallSite> ReflectedCalls;
	/** Every `waitUntil`, in the order compiled: what its WaitCheck and WaitResult refer to. */
	TArray<FBangoWaitSet> WaitSets;
	/** For a program compiled as a condition: what can change its answer (the whole program). Empty otherwise. */
	FBangoDependencies ConditionDependencies;

	/** The script's parameters, in order. They occupy the first slots of Functions[0]. Empty if there is no inputs line. */
	TArray<FBangoScriptParamInfo> Params;
	/** True if the script has an inputs line (even an empty one). */
	bool bHasParamsLine = false;
	/** Where the inputs line is, for errors about arguments. */
	FBangoDebugLoc ParamsLoc;

	/**
	 * Which command each of Names is (null for a name that isn't a command's), in the command table whose id is
	 * BoundTableId (0: not worked out yet). The first copy of the script to start works it out and the rest use it
	 * (FBangoInstance::BindCommandsFor), so starting one doesn't look every command up by name again. A table only
	 * ever gains commands, and keeps each for as long as it lasts, so this stays right for as long as that table does.
	 * Scripts start on the game thread, the only place this is touched. Not part of the program: compiling starts it empty.
	 */
	mutable uint64 BoundTableId = 0;
	mutable TArray<const struct FBangoCommand*> BoundCommands;

	/** How many arguments a spawn must pass at least. */
	int32 GetNumRequiredParams() const
	{
		int32 Count = 0;
		while (Count < Params.Num() && !Params[Count].bHasDefault)
		{
			++Count;
		}
		return Count;
	}
};

namespace Bango
{
	BANGOSCRIPTS_API const TCHAR* GetOpName(EBangoOp Op);

	/** Readable listing of a compiled program, for tests and debugging. */
	BANGOSCRIPTS_API FString Disassemble(const FBangoProgram& Program);

	/**
	 * What the code of Program.Functions[Function] from Begin to before End reads and calls, following calls into the
	 * script's functions (FBangoDependencies). Inputs are counted only in the main graph (Function 0).
	 */
	BANGOSCRIPTS_API FBangoDependencies FindDependencies(const FBangoProgram& Program, int32 Function, int32 Begin, int32 End);

	/**
	 * Makes the first instruction of each common run of instructions a fused one (EBangoOp::FusedLoad, FusedConst),
	 * which the VM runs in one go. The compiler does this last, to every program. Only runs that are one node's, on one
	 * line, are fused, so the debugger's trail is the same; and only whole locals and number, bool or string
	 * constants, so everything that looks for a global or a command in the code still finds it.
	 */
	BANGOSCRIPTS_API void FuseInstructions(FBangoProgram& Program);
}
