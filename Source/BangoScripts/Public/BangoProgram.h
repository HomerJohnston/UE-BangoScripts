#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"

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

	// ---- Values ----
	Index,              // array, index -> element               (fails if nil or out of range)
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

	// ---- Calls ----
	CallFunction,       // A = function, B = argument count.  args... -> [result]
	CallCommand,        // A = name (command), B = argument count, C = 1 if a target comes first.  [target], args... -> [result]
	CallReflected,      // A = name (a Blueprint function), B = argument count, C = 1 if its result is used.  target, args... -> [result]
	Return,             // ends a void function
	ReturnValue,        // value -> (ends a function, the value goes to the caller)
	Halt,               // ends the script instance
	ErrorMissingReturn, // a non-void function reached its end without returning

	// ---- Built-in array commands ----
	ArrayCount,         // array -> int
	ArrayModify,        // A = place, B = EBangoArrayModify, C = 1 if the result is used.  indexes..., args... -> [result]
	ForEachNext,        // A = slot holding the array copy (A + 1 holds the int position), B = loop variable slot, C = target when done
	SpawnBlock,         // A = target: starts another instance at A with a copy of this frame's slots (a spawn block); this one carries on. B = 1: push the new instance's id

	// ---- waitUntil (Program.WaitSets) ----
	WaitCheck,          // A = wait set. Its condition follows: while it's worked out, a global that's unset makes it false, and it can't wait
	WaitResult,         // A = wait set, B = its WaitCheck.  bool -> : true carries on; false pauses until one of the set's globals changes (every frame, if it polls), then runs from B again

	Count               // not an op
};

enum class EBangoArrayModify : uint8
{
	Push,       // value
	InsertAt,   // value, index
	Pop,        //                 -> element
	RemoveAt,   // index           -> element
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
};

/** A variable reference, optionally indexed and ending in a vector part: `_x`, `gold`, `_grid[i][j]`, `_path[i].x`. The indexes are on the stack. */
struct FBangoPlace
{
	EBangoPlaceKind Kind = EBangoPlaceKind::Local;
	int32 Index = 0;
	int32 NumIndexes = 0;
	/** -1 for the whole value; 0, 1 or 2 for a vector's x, y or z. */
	int32 Part = -1;
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
};

/**
 * The inputs an object's C++ gives one of its inline scripts, in place of an #inputs line (spec section 15): the
 * script reads them without declaring them, and can't have an #inputs line. Its owner passes their values in this order.
 */
struct FBangoInputsSignature
{
	/** Whose they are, for messages: "BangoTrigger's 'Activation'" ("'Activation'" for a global one). */
	FString Label;
	/** In order. None has a default. */
	TArray<FBangoScriptParamInfo> Params;
	/** One per input, maybe empty: what it is, for the editor. */
	TArray<FString> Tooltips;

	/** "entity _trigger, int _count": as an #inputs line would list them. */
	BANGOSCRIPTS_API FString ToInputsText() const;
	/** "_trigger, _count" */
	BANGOSCRIPTS_API FString GetNames() const;
};

struct FBangoGlobalDecl
{
	FString Name;
	FBangoType Type;
	int32 Line = 0;
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
	/** Global names and command names. */
	TArray<FString> Names;
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
}
