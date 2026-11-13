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
	Return,             // ends a void function
	ReturnValue,        // value -> (ends a function, the value goes to the caller)
	Halt,               // ends the script instance
	ErrorMissingReturn, // a non-void function reached its end without returning

	// ---- Built-in array commands ----
	ArrayCount,         // array -> int
	ArrayModify,        // A = place, B = EBangoArrayModify, C = 1 if the result is used.  indexes..., args... -> [result]
	ForEachNext,        // A = slot holding the array copy (A + 1 holds the int position), B = loop variable slot, C = target when done

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
	 * For reloading: the NodeID of the for or forEach loop a slot belongs to (its variable and bookkeeping), so two
	 * loops' variables of the same name are told apart. -1 for parameters and the graph's locals; -2 for a loop
	 * without node metadata (such slots aren't carried over).
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

/** One parameter of a script, from its `params (...)` line. */
struct FBangoScriptParamInfo
{
	FString Name;
	FBangoType Type;
	/** Parameters without a default are required, and always come before those with one. */
	bool bHasDefault = false;
	/** The default value (already converted to Type; may be nil). Only meaningful if bHasDefault. */
	FBangoValue Default;
};

struct FBangoGlobalDecl
{
	FString Name;
	FBangoType Type;
	int32 Line = 0;
};

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

	/** The script's parameters, in order. They occupy the first slots of Functions[0]. Empty if there is no params line. */
	TArray<FBangoScriptParamInfo> Params;
	/** True if the script has a params line (even an empty one). */
	bool bHasParamsLine = false;
	/** Where the params line is, for errors about arguments. */
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
}
