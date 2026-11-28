#pragma once

#include "CoreMinimal.h"
#include "BangoContainers.h"
#include "BangoTypes.h"

class UObject;
class FBangoInstance;

/**
 * Names one of a signature's type parameters (FBangoCommandSignature::TypeParam), where a type goes:
 *   const FBangoTypeVar T(TEXT("T"));
 *   Make(TEXT("Lerp")).TypeParam(T, { Float, Vector2, Vector3 }).Param(TEXT("A"), T).Param(TEXT("B"), T).Param(TEXT("Alpha"), Float).Returns(T)
 */
struct FBangoTypeVar
{
	FString Name;
	/** 0: the type parameter itself. 1: an array of it (Array()), and so on. */
	int32 ArrayDepth = 0;
	explicit FBangoTypeVar(const FString& InName) : Name(InName) {}
	/**
	 * An array of this: with T a type parameter, T.Array() is "an array of T", so Push is
	 *   Make(TEXT("Push")).AnyTypeParam(T, TEXT("element")).Target(T.Array()).MutableTarget().Param(TEXT("Value"), T)
	 * The array then says what T is, and the value must fit it.
	 */
	FBangoTypeVar Array() const
	{
		FBangoTypeVar Result = *this;
		++Result.ArrayDepth;
		return Result;
	}
};

/**
 * A type parameter of a command's signature: a type worked out at each call, from the arguments given for the
 * parameters that use it. They must all be the same type, one of Types (or any array, with bAnyArray); an int
 * counts as a float where the others are floats, or where only a float is allowed (as everywhere in Bango).
 * So `Max(1, 2)` is an int, `Max(1, 2.5)` a float, and `Lerp(_from, _to, 0.5)` whatever _from and _to are.
 */
struct BANGOSCRIPTS_API FBangoTypeParam
{
	FString Name;
	TArray<FBangoType> Types;
	/** Any array is allowed too, whatever its elements (Count's). */
	bool bAnyArray = false;
	/** Any type at all is allowed (FBangoCommandSignature::AnyTypeParam): an array command's element. */
	bool bAnyType = false;
	/** What signatures and the editor call it, like "number". Empty: its types, like "float|vector2|vector3" ("any" for any type). */
	FString DisplayName;

	bool Allows(const FBangoType& Type) const;
	/** What a parameter that's ArrayDepth levels of array of it is called: "element" (0), "array" (1, of any type) or "number[]". */
	FString GetDisplayName(int32 ArrayDepth) const;
	/** For messages, for a parameter that's ArrayDepth levels of array of it: "an array", "a number[]"; Describe() for 0. */
	FString Describe(int32 ArrayDepth) const;
	/** The type a value of Type is passed as: Type if it's allowed, float (at the same depth) for an int if that is. False if neither. */
	bool Resolve(const FBangoType& Type, FBangoType& OutType) const;
	/** "number", or "float|vector2|vector3". */
	FString GetDisplayName() const;
	/** For messages: "a number (an int or a float)", or "a float, a vector2 or a vector3". */
	FString Describe() const;
	/**
	 * Two arguments for the same type parameter: the type both are passed as (A, or B if A fits it: an int beside
	 * a float is a float). False if they can't be the same type.
	 */
	static bool Unify(const FBangoType& A, const FBangoType& B, FBangoType& OutType);
};

/** One parameter of a C++ command. */
struct FBangoCommandParam
{
	FString Name;
	FBangoType Type;
	/** Shown when hovering the parameter's value on the command's graph node, like "Seconds". Optional. */
	FString Description;
	/**
	 * The command gets this argument as it is, even nil or naming no actor, and decides what that means itself
	 * (IsNull does). A nil variable passed here doesn't stop the script, and an entity isn't checked against the
	 * actor registry first. See FBangoCommandSignature::MayBeNil.
	 */
	bool bMayBeNil = false;
	/**
	 * The signature's type parameter this parameter's type is (an index into FBangoCommandSignature::TypeParams), or
	 * INDEX_NONE. Type is then its first allowed type, only as a fallback. See FBangoCommandSignature::TypeParam.
	 */
	int32 TypeVar = INDEX_NONE;
	/** With TypeVar: 0 if the parameter is the type parameter itself, 1 for an array of it (FBangoTypeVar::Array), and so on. */
	int32 TypeVarDepth = 0;
};

/**
 * What the compiler needs to know about a C++ command (spec section 7):
 *   [Target] Name(Params...) -> ReturnType
 *
 * The code that runs a command is attached in the interpreter step; the compiler only uses signatures.
 */
struct BANGOSCRIPTS_API FBangoCommandSignature
{
	FString Name;

	/** If true, the command must be called with a target of TargetType: `john Speak("Hi");` */
	bool bHasTarget = false;
	FBangoType TargetType;
	/**
	 * The type parameter the target's type is (an index into TypeParams), or INDEX_NONE, and how many levels of array
	 * of it (Push's target is an array of its element type: 1). TargetType is then only a fallback.
	 */
	int32 TargetTypeVar = INDEX_NONE;
	int32 TargetTypeVarDepth = 0;
	/**
	 * The command changes its target in place (Push, Pop): the call's target must be a variable, a declared global or
	 * an array element, the command changes FBangoCommandCall::Target, and that's stored back when it's done. Such a
	 * command can't wait (return Pending). See MutableTarget.
	 */
	bool bMutableTarget = false;

	TArray<FBangoCommandParam> Params;
	/** How many of Params must be given. The rest are optional. -1 means all are required. */
	int32 NumRequiredParams = -1;
	/** If true, any number of extra arguments of any type may follow Params: `WriteLog("{0} of {1}", _a, _b);` */
	bool bVariadic = false;
	/**
	 * How the graph editor labels the extra values: the prefix, a number counting from VariadicFirstIndex, then
	 * the suffix. The default gives "Value 1", "Value 2", ...; WriteLog, Format and FormatText use "{", 0 and "}"
	 * to match {0}, {1}, ...
	 */
	FString VariadicLabelPrefix = TEXT("Value ");
	int32 VariadicFirstIndex = 1;
	FString VariadicLabelSuffix;
	/**
	 * The parameter whose text has placeholders for the extra values, or INDEX_NONE: {0}, {1}, ... for a string
	 * parameter, {0}, {1}, ... for a text one. When it's written out (a string or a TEXT), the compiler checks
	 * every placeholder has a value, and the graph editor shows a row for every placeholder, even ones with no
	 * value yet.
	 */
	int32 PlaceholderParam = INDEX_NONE;

	/** The signature's type parameters (TypeParam), which its parameters and return type can be. */
	TArray<FBangoTypeParam> TypeParams;

	bool bReturnsValue = false;
	FBangoType ReturnType;
	/**
	 * The type parameter the return type is (an index into TypeParams), or INDEX_NONE: the command returns whatever
	 * type its arguments made it, like Max (an int for ints, a float for floats). ReturnType is then only a fallback.
	 */
	int32 ReturnTypeVar = INDEX_NONE;
	/** With ReturnTypeVar: 0 if the result is the type parameter itself, 1 for an array of it, and so on. */
	int32 ReturnTypeVarDepth = 0;
	/**
	 * The command's answer depends only on its arguments: not on the world, the time, chance, or anything else that
	 * changes by itself (Max, Count, Sqrt). A condition or waitUntil that calls only such commands isn't polled: it's
	 * checked again when a global or input it reads changes, like one that calls none. See Pure.
	 */
	bool bPure = false;
	/**
	 * The command takes a block of statements (`@john Speech(0, 1) { ... }`), and must be given one. It runs at the
	 * opening brace, as any command does, and its Close handler at the closing brace. See Block.
	 */
	bool bBlock = false;
	/**
	 * The command belongs to a block command's block: this is that command's name, and calling this one anywhere but
	 * directly in such a block is a compile error. Empty for a command that can be called anywhere. See Inside.
	 */
	FString InsideOf;
	/** With InsideOf: the command may also be called anywhere else, where it belongs to no block. See Inside. */
	bool bInsideOptional = false;
	/**
	 * With bBlock: the block is a list of entries (`entry if (condition): { ... }`, `entry: { ... }`) and nothing
	 * else, and the command chooses which of them run. See Entries.
	 */
	bool bEntries = false;
	/** With bEntries: the block may also be an ordinary block (one that doesn't start with `entry`). See Entries. */
	bool bEntriesOptional = false;
	/** With bEntries: the commands an entry holds calls of, and nothing else. Empty: anything. See EntriesHold. */
	TArray<FString> EntriesHoldCommands;
	/** With EntriesHoldCommands: an entry holds any number of their calls; without it, one, or none. */
	bool bEntriesHoldMany = false;
	/** With EntriesHoldCommands: the `default:` holds anything (it's a bypass, not one more entry). See DefaultHoldsAnything. */
	bool bDefaultHoldsAnything = false;
	/** What's wrong with how the signature was built (a type parameter that doesn't exist, ...): FBangoCommandTable::Add fails with it. */
	FString BuildError;
	/**
	 * Only for commands that return a value: true if a script may call it on a line of its own and throw the value
	 * away (like a latent command whose pausing is the point). Otherwise its result must be used, and
	 * `Format("x");` on its own is a compile error. See MustUseResult.
	 */
	bool bDiscardable = false;

	/** For the graph editor's node menu: a group like "Text" or "Actors", and a one-line description. Both optional. */
	FString CategoryName;
	FString CommandDescription;

	/**
	 * True for the commands that come with the Bango plugin (Bango::RegisterBuiltinCommands sets it). A game's own
	 * commands leave it false: there's nothing to set to register one.
	 */
	bool bBuiltIn = false;

	int32 GetNumRequiredParams() const { return NumRequiredParams < 0 ? Params.Num() : NumRequiredParams; }

	/** True if the command returns a value that must be used (it returns one and isn't Discardable). */
	bool MustUseResult() const { return bReturnsValue && !bDiscardable; }

	/** "entity Speak(string text, float pause = optional) -> void" style text for error messages. */
	FString ToString() const;

	// ---- Builders: FBangoCommandSignature::Make(TEXT("Speak")).Target(FBangoType::Entity()).Param(TEXT("text"), FBangoType::String()) ----

	static FBangoCommandSignature Make(const FString& InName);
	FBangoCommandSignature& Target(const FBangoType& Type);
	/** The command takes a target whose type is a type parameter, or an array of one (T.Array()). */
	FBangoCommandSignature& Target(const FBangoTypeVar& Var);
	/**
	 * The command changes its target in place (call after Target): `_list Push(4);`. The call's target must then be
	 * something that can be changed: a variable, a declared global or an array element (`_grid[1] Push(4);`). The
	 * command changes Call.Target, checking everything before it changes anything (a failed call should leave it as
	 * it was), and can't wait. Its target's type stays what it was.
	 */
	FBangoCommandSignature& MutableTarget();
	/** Adds a parameter. ParamDescription is the tooltip on its value in the graph editor (e.g. the unit: "Seconds"). */
	FBangoCommandSignature& Param(const FString& ParamName, const FBangoType& Type, const FString& ParamDescription = FString());
	/**
	 * Adds a type parameter: a type worked out at each call from the arguments of the parameters that use it
	 * (Param(Name, Var)), which must all be the same one of Types. An int counts as a float beside floats, or where
	 * only a float is allowed. The command gets those arguments as that type (FBangoCommandCall::TypeArgs says
	 * which), and a result of it (Returns(Var)) must be that type.
	 *   .TypeParam(T, { Int, Float }, TEXT("number"))    Max: an int for ints, otherwise a float
	 *   .TypeParam(T, { Float, Vector2, Vector3 })       Lerp: floats or vectors, both the same
	 * DisplayName is what signatures and the editor show for it; without one, its types ("float|vector2|vector3").
	 */
	FBangoCommandSignature& TypeParam(const FBangoTypeVar& Var, const TArray<FBangoType>& Types, const FString& DisplayName = FString());
	/** The type parameter added last also takes any array (Count's). */
	FBangoCommandSignature& OrAnyArray();
	/** Adds a type parameter that can be any type at all (an array command's element). DisplayName: what it's called ("element"); "any" without one. */
	FBangoCommandSignature& AnyTypeParam(const FBangoTypeVar& Var, const FString& DisplayName = FString());
	/** Adds a parameter whose type is a type parameter (TypeParam). */
	FBangoCommandSignature& Param(const FString& ParamName, const FBangoTypeVar& Var, const FString& ParamDescription = FString());
	/** The parameter added last may be nil, or an entity naming no actor: the command gets it as it is (bMayBeNil). */
	FBangoCommandSignature& MayBeNil();
	/** Marks every parameter added after this call as optional. */
	FBangoCommandSignature& Optional();
	/** Any number of extra values may follow the parameters. LabelPrefix and FirstIndex only change how the graph editor labels them. */
	FBangoCommandSignature& Variadic(const FString& LabelPrefix = TEXT("Value "), int32 FirstIndex = 1, const FString& LabelSuffix = FString());
	/** The parameter added last is a text whose {0}, {1}, ... the extra values fill (like WriteLog's). Only changes the graph editor's rows. */
	FBangoCommandSignature& WithPlaceholders();
	/** The command returns a value. Scripts must use it (`_x = Name(...)`), unless Discardable() is also called. */
	FBangoCommandSignature& Returns(const FBangoType& Type);
	/**
	 * The command returns a value of a type parameter's type: what the call's arguments made it (FBangoCommandCall::TypeArgs).
	 * The compiler works it out at each call. The command must return that type (an int where it's float is turned into one).
	 */
	FBangoCommandSignature& Returns(const FBangoTypeVar& Var);
	/** The command's answer depends only on its arguments (bPure), so a condition that calls it isn't polled for it. */
	FBangoCommandSignature& Pure();
	/** A parameter's type as signatures show it: its type parameter's name ("number") or its type's. */
	FString GetParamTypeName(int32 ParamIndex) const;
	/** The return type as signatures show it: its type parameter's name ("number") or its type's. */
	FString GetReturnTypeName() const { return TypeParams.IsValidIndex(ReturnTypeVar) ? TypeParams[ReturnTypeVar].GetDisplayName(ReturnTypeVarDepth) : ReturnType.ToString(); }
	/** The target's type as signatures show it: its type parameter's name ("array" for an array of any type) or its type's. Empty without a target. */
	FString GetTargetTypeName() const;
	/** The type of something that's Depth levels of array of a type parameter that is TypeArg in a call (Depth 0: TypeArg itself). */
	static FBangoType GetTypeOf(const FBangoType& TypeArg, int32 Depth) { return FBangoType(TypeArg.Base, (uint8)FMath::Min<int32>(TypeArg.ArrayDepth + Depth, FBangoType::MaxArrayDepth)); }
	/** The index in TypeParams of the type parameter with this name, or INDEX_NONE. */
	int32 FindTypeParam(const FString& VarName) const;
	/**
	 * Works out a call's type parameters from its target (null for none) and arguments as they are when it runs, and
	 * converts the arguments to them (an int to a float, where its type parameter came out as float). OutTypeArgs:
	 * one per type parameter (unset, a default FBangoType, for one nothing given uses). False, with why, if they
	 * don't fit.
	 *
	 * A type parameter is whatever its arrays say it is: the target, and anything that's an array of it (T.Array()).
	 * The rest must fit that (an int into a float is fine), and an array is never converted to fit a value. With no
	 * array, every argument for it must be the same type, an int counting as a float beside one.
	 */
	bool BindTypeParams(const FBangoValue* Target, TArray<FBangoValue>& Args, TArray<FBangoType>& OutTypeArgs, FString& OutError) const;
	/** A script may call this command on a line of its own and throw its value away. */
	FBangoCommandSignature& Discardable();
	/**
	 * The command takes a block: `Name(args) { statements }`. It's called at the opening brace (Start), where it can
	 * set the block's state (FBangoCommandCall::BlockState), and its Close handler at the closing brace, or when the
	 * script leaves the block early. It can't return a value or change its target.
	 */
	FBangoCommandSignature& Block();
	/**
	 * The command can only be called in the block of the block command named here, directly: at any depth of if,
	 * loop and so on, but not in another block command's block, a spawn block, or a function called from the block.
	 * It's handed that block's state (FBangoCommandCall::BlockState), and can end the block (EndBlock).
	 *
	 * With bOptional, it may also be called anywhere else (the top level, a function, another block command's block, a
	 * spawn block), where it belongs to no block: there BlockState is null (and OuterBlockState, for one that takes a
	 * block of its own), and ending the block is an error. Written directly in the block, it's as without bOptional.
	 * For a command that works on its own as well as in the block, like a line said outside any speech.
	 */
	FBangoCommandSignature& Inside(const FString& BlockCommand, bool bOptional = false);
	/**
	 * With Block: the block is a list of entries, `entry if (condition): { ... }` and `entry: { ... }`, and nothing
	 * else. Every entry's condition is worked out when the block opens, after the arguments, and the command chooses
	 * which entries run (FBangoCommandCall::RunEntry), and in what order. At the end of each entry, its EntryEnded
	 * handler is called. A condition must answer at once; one that reads a global that isn't set up yet doesn't pass.
	 *
	 * With bOptional, the block may also be an ordinary block: one whose first thing isn't `entry`.
	 */
	FBangoCommandSignature& Entries(bool bOptional = false);
	/**
	 * With Entries: each entry holds one call of the command named (with its own block, if it takes one), or nothing,
	 * and nothing else. For a command that runs every entry, passed or not, to gather them (a prompt's options), so
	 * that nothing else in an entry runs when its condition is false.
	 */
	FBangoCommandSignature& EntriesHold(const FString& CommandName);
	/**
	 * With Entries: each entry holds calls of the commands named and nothing else (comments, disabled statements and
	 * reroutes aside): with bMany any number of them, in any order and mix; without, one, or none. Each call's own
	 * block, if it takes one, is ordinary code.
	 */
	FBangoCommandSignature& EntriesHold(const TArray<FString>& CommandNames, bool bMany);
	/** With EntriesHold: the `default:` entry holds ordinary statements, as a bypass (a goto, ending a conversation). */
	FBangoCommandSignature& DefaultHoldsAnything();
	FBangoCommandSignature& InCategory(const FString& InCategory);
	FBangoCommandSignature& Describe(const FString& InDescription);
};

/** What a command handler reports back to the interpreter. */
enum class EBangoCommandResult : uint8
{
	/** Finished. If the command returns a value, it is in Call.Result. */
	Done,
	/** Still running (latent). The script pauses, and Poll is called every frame until it finishes. */
	Pending,
	/** Something went wrong. Call.Error says what; the script stops with that error. */
	Failed,
};

/**
 * One call of a C++ command, passed to its Start and Poll handlers.
 *
 * Arguments have already been converted to the signature's types, and entity arguments have been
 * checked against the actor registry (except MayBeNil ones). A nil argument is possible when the script passed nil.
 */
struct FBangoCommandCall
{
	/** The instance making the call. */
	int64 InstanceId = 0;
	/** The instance itself, for FindOpenBlock. Set by the VM for every handler it calls; null in a call made any other way. */
	const FBangoInstance* Instance = nullptr;
	/** The world the script runs in, if any. Commands use it to find actors, timers, etc. */
	UObject* WorldContext = nullptr;

	/**
	 * Start only: the target and arguments. Not available in Poll; copy what Poll needs into LatentState.
	 * For a command that changes its target (MutableTarget), Target is the value to change: what it holds when the
	 * command is done is stored back where it came from.
	 */
	bool bHasTarget = false;
	FBangoValue Target;
	TArray<FBangoValue> Args;
	/**
	 * What each of the signature's type parameters (TypeParam) is in this call, in the order they were added: for
	 * Lerp, TypeArgs[0] is float, vector2 or vector3. The arguments of that type parameter are all that type. Kept
	 * for Poll too.
	 */
	TArray<FBangoType> TypeArgs;

	/** Poll: seconds since the last poll (or since Start). Start: the length of the frame the script is running in. */
	float DeltaSeconds = 0.0f;

	/**
	 * A latent command's own state, kept between Start and Poll calls and saved with the instance.
	 * For example, Sleep keeps the remaining seconds here. Only use FBangoValues, never pointers.
	 */
	TArray<FBangoValue> LatentState;

	/**
	 * True if the script uses the command's result (`_x = _list Pop();`), false if it's called on a line of its own
	 * and the result is thrown away (`_list Pop();`, for a Discardable command).
	 */
	bool bResultUsed = false;

	/**
	 * For a block command (FBangoCommandSignature::Block) and the commands that belong to its block (Inside): the
	 * block's state, one value of any type, to read and change. It starts as nil when the block opens, is kept with
	 * the script's variables while the block is open (so it's saved with them: only Bango values), and is thrown
	 * away after the block closes. Null for any other command. Only valid during this call to Start, Poll or Close:
	 * don't keep the pointer.
	 */
	FBangoValue* BlockState = nullptr;
	/**
	 * For a command that belongs to a block and takes one of its own (Inside and Block, like a prompt's option):
	 * the state of the block it's written in, while BlockState is its own block's. Start and Poll only; null otherwise.
	 */
	FBangoValue* OuterBlockState = nullptr;
	/**
	 * For a block command, in Start, Poll and Close: its block's number, which is what the command of the block it's
	 * written in gives RunBlock to have this block run. -1 for any other command.
	 */
	int32 BlockNumber = -1;
	/** Close, and Poll after a Close that returned Pending: the call is the block's closing one. */
	bool bClosing = false;
	/** With bClosing: the script is leaving the block early (return, break, continue, goto), not at its closing brace. */
	bool bLeftEarly = false;
	/** Set by EndBlock. */
	bool bEndBlock = false;
	/**
	 * Ends the block this command is in, or opens: when the command is done, the script goes straight to the block's
	 * closing brace (where the block command's Close runs as usual). For a command that isn't in a block, the script
	 * stops with an error.
	 */
	void EndBlock() { bEndBlock = true; }
	/** Set by SkipBlock. */
	bool bSkipBlock = false;
	/**
	 * For a block command, from Start (or Poll while it waits on Start): its block isn't run now. The script goes on
	 * after the block, and its Close isn't called. The block can still be run later, by the command of the block it's
	 * written in (RunBlock).
	 */
	void SkipBlock() { bSkipBlock = true; }
	/** Set by RunBlock: the number of the block to run, or -1. */
	int32 RunBlockNumber = -1;
	/**
	 * For a block command, from Close (or Poll while it waits on Close), at the block's closing brace: one of the
	 * blocks written directly in its block, and skipped, runs now (Number is that block's BlockNumber). When that
	 * block ends, its command's Close is called as usual, and the script goes on after this command's block.
	 * Not when the script is leaving the block early (bLeftEarly).
	 */
	void RunBlock(int32 Number) { RunBlockNumber = Number; }
	/**
	 * Abandoned only. True: the script ended while this command's block was open. False: it ended while the script
	 * was waiting on this command (LatentState is what Poll would have had).
	 */
	bool bBlockAbandoned = false;

	// ---- Entries (FBangoCommandSignature::Entries) ----
	// For a block command with entries, in Start, EntryEnded, Close and their Poll: what its entries are, and which
	// passed when their conditions were last worked out.

	/** The number of entries (not counting disabled ones), or -1 if the block isn't an entry list. Set by the VM. */
	int32 EntryCount = -1;
	/** Per entry: bit 0, its condition passed; bit 1, it has one (not `entry:`). Set by the VM. */
	TArray<uint8> EntryFlags;
	/** True if the block is an entry list (a command with optional entries may have an ordinary block). */
	bool HasEntries() const { return EntryCount >= 0; }
	/** How many entries there are, not counting disabled ones (0 if the block isn't an entry list). */
	int32 GetEntryCount() const { return FMath::Max(EntryCount, 0); }
	/** True if entry Index's condition passed when it was last worked out (always, for `entry:`; for `default:`, when no other entry did). */
	bool EntryPassed(int32 Index) const { return EntryFlags.IsValidIndex(Index) && (EntryFlags[Index] & 1) != 0; }
	/** False for `entry:`, which has no condition. */
	bool EntryHasCondition(int32 Index) const { return EntryFlags.IsValidIndex(Index) && (EntryFlags[Index] & 2) != 0; }
	/** The `default:` entry, the last if there's one: it passes (EntryPassed) when none of the others does. */
	bool EntryIsDefault(int32 Index) const { return EntryFlags.IsValidIndex(Index) && (EntryFlags[Index] & 4) != 0; }
	/** In Poll: a re-check (RecheckEntries) or the watch (WatchEntries) has changed an entry's answer since the last call. */
	bool bEntriesChanged = false;
	/** EntryEnded, and Poll after an EntryEnded that returned Pending: the entry that has just ended. -1 otherwise. */
	int32 EntryNumber = -1;
	/** EntryEnded, and Poll after one that returned Pending: the call is at the end of an entry (EntryNumber). */
	bool bEntryEnding = false;
	/** Set by RunEntry: the entry to run, or -1. */
	int32 RunEntryNumber = -1;
	/**
	 * From Start or EntryEnded (or Poll while waiting on either), when it returns Done: the script runs that entry's
	 * block, passed or not, and at its end EntryEnded is called. Without it, the script goes on to the block's close.
	 */
	void RunEntry(int32 Index) { RunEntryNumber = Index; }
	/** Set by RecheckEntries. */
	bool bRecheckEntries = false;
	/**
	 * From Start, EntryEnded or Close (or their Poll), which must then return Pending: every entry's condition is
	 * worked out again before the next Poll, which sees the new answers (and bEntriesChanged). From Start, EntryEnded
	 * or Close, that Poll comes straight away, in the same frame.
	 */
	void RecheckEntries() { bRecheckEntries = true; }
	/** Set by WatchEntries: 1 to watch, 0 to stop, -1 no change. */
	int32 WatchEntriesRequest = -1;
	/**
	 * Turns watching the entries on or off. While on, whenever the command waits (its Start, EntryEnded or Close, or
	 * their Poll, returned Pending), the entries' conditions are checked again by waitUntil's rules before each Poll:
	 * when a global they read changes, or every frame if they call a command that isn't pure. Poll then has
	 * bEntriesChanged. It stays on until it's turned off or the block closes.
	 */
	void WatchEntries(bool bWatch) { WatchEntriesRequest = bWatch ? 1 : 0; }
	/**
	 * For a command that belongs to a block (Inside), written in one of its entries: which entry (for one that also
	 * takes a block of its own, like a prompt's option, the entry of the block it's written in, with OuterBlockState).
	 * -1 if it isn't in an entry.
	 */
	int32 BlockEntryNumber = -1;

	/**
	 * Any handler (Start, Poll, Close, Abandoned), of any command: the state of the nearest open block of the block
	 * command named, in this running script, counting blocks opened by the callers of the function the command is in.
	 * Innermost first. Null if none is open (a spawn block's copy, or another script, has none of this script's open).
	 * Read only, and only during this call, like BlockState. A block that has picked one of its blocks to run (RunBlock)
	 * is closed, so it isn't found.
	 */
	BANGOSCRIPTS_API const FBangoValue* FindOpenBlock(const FString& CommandName) const;

	/** Done with a return value: put it here. */
	FBangoValue Result;
	/** Failed: put the reason here. */
	FString Error;

	/** Empties it, as a new one is, but keeps its lists' memory: the VM uses one over and over for the calls it makes. */
	void Reset()
	{
		InstanceId = 0;
		Instance = nullptr;
		WorldContext = nullptr;
		bHasTarget = false;
		Target = FBangoValue();
		Args.Reset();
		TypeArgs.Reset();
		DeltaSeconds = 0.0f;
		LatentState.Reset();
		bResultUsed = false;
		BlockState = nullptr;
		OuterBlockState = nullptr;
		BlockNumber = -1;
		bClosing = false;
		bLeftEarly = false;
		bEndBlock = false;
		bSkipBlock = false;
		RunBlockNumber = -1;
		bBlockAbandoned = false;
		EntryCount = -1;
		EntryFlags.Reset();
		bEntriesChanged = false;
		EntryNumber = -1;
		bEntryEnding = false;
		RunEntryNumber = -1;
		bRecheckEntries = false;
		WatchEntriesRequest = -1;
		BlockEntryNumber = -1;
		Result = FBangoValue();
		Error.Reset();
	}

	/** Fails the call with a message: `return Call.Fail(TEXT("..."));` */
	EBangoCommandResult Fail(const FString& Message)
	{
		Error = Message;
		return EBangoCommandResult::Failed;
	}
};

using FBangoCommandHandler = TFunction<EBangoCommandResult(FBangoCommandCall&)>;

/** A C++ command: its signature and the code that runs it. */
struct FBangoCommand
{
	FBangoCommandSignature Signature;
	/** Runs when the script calls the command. Required for the command to run. */
	FBangoCommandHandler Start;
	/**
	 * Runs every frame while Start, Close or the previous Poll returned Pending. Required if Start or Close can
	 * return Pending. (FBangoCommandCall::bClosing says which it's waiting on.)
	 */
	FBangoCommandHandler Poll;
	/**
	 * For a block command (FBangoCommandSignature::Block): runs at the block's closing brace, and before the script
	 * leaves the block early (FBangoCommandCall::bLeftEarly). It has the block's state, but no target or arguments:
	 * what it needs of those, Start puts in the state. It may return Pending. Optional: without it nothing happens
	 * at the closing brace.
	 */
	FBangoCommandHandler Close;
	/**
	 * Runs when the script stops for good (it failed, or was terminated) while it was waiting on this command, or,
	 * for a block command, while its block was open (FBangoCommandCall::bBlockAbandoned says which; a block command
	 * can be told both ways, one after the other). It's the command's chance to stop what it started: a speech
	 * that's playing, a timer. It has LatentState and BlockState as Poll would have, and WorldContext if the script
	 * has run since it was loaded. What it returns is ignored, and it must not start or stop scripts. Optional.
	 * Not called when the script's world is torn down, or for a command that itself returned Failed.
	 */
	FBangoCommandHandler Abandoned;
	/**
	 * For a block command with entries (FBangoCommandSignature::Entries): runs at the end of each entry it ran, with
	 * FBangoCommandCall::EntryNumber, the block's state and its entries. It may RunEntry another (or the same one
	 * again), or return Done without one, and then the block's Close runs. It may return Pending, and Poll is called
	 * with bEntryEnding. Optional: without it, the block closes after the first entry it runs.
	 */
	FBangoCommandHandler EntryEnded;
};

/** Looks up command signatures by name (case-sensitive). */
class IBangoCommandLookup
{
public:
	virtual ~IBangoCommandLookup() = default;
	virtual const FBangoCommandSignature* FindCommand(const FString& Name) const = 0;

	/** For error messages: the name of a command that matches Name when case is ignored, or empty. */
	virtual FString FindNameIgnoringCase(const FString& Name) const { return FString(); }

	/** For the graph editor's node menu: the names of all the commands, in any order. */
	virtual void GetCommandNames(TArray<FString>& OutNames) const {}
};

/**
 * A set of commands. Commands added with only a signature can be compiled against but not run;
 * running one is an error when an instance starts.
 *
 * Commands are stored at stable addresses, so adding more commands never affects running instances.
 * Commands can't be removed.
 */
class BANGOSCRIPTS_API FBangoCommandTable : public IBangoCommandLookup
{
public:
	/** Fails for invalid names, duplicates, and a signature that wasn't built right (BuildError). */
	bool Add(const FBangoCommandSignature& Signature, FString& OutError);
	bool Add(const FBangoCommand& Command, FString& OutError);

	virtual const FBangoCommandSignature* FindCommand(const FString& Name) const override;
	virtual FString FindNameIgnoringCase(const FString& Name) const override;
	virtual void GetCommandNames(TArray<FString>& OutNames) const override;
	const FBangoCommand* FindCommandImpl(const FString& Name) const;

	int32 Num() const { return Commands.Num(); }

	/**
	 * A number no other table has (a copy of a table gets its own), never 0: what a compiled script keeps beside the
	 * commands it found here (FBangoProgram::BoundCommands), to know they're this table's.
	 */
	uint64 GetId() const { return Id.Number; }

private:
	struct FTableId
	{
		uint64 Number;
		FTableId() : Number(MakeNumber()) {}
		FTableId(const FTableId&) : Number(MakeNumber()) {}
		FTableId& operator=(const FTableId&) { Number = MakeNumber(); return *this; }
		BANGOSCRIPTS_API static uint64 MakeNumber();
	};

	TBangoNameMap<TSharedPtr<FBangoCommand>> Commands;
	FTableId Id;
};

namespace Bango
{
	/**
	 * Registers the commands that need nothing from the engine or a world, so every command table has them (the
	 * module's, and tests'): Count, Push, InsertAt, Pop and RemoveAt. Bango::RegisterBuiltinCommands calls it. False, with why, if one couldn't be added
	 * (the table has a command of that name already).
	 */
	BANGOSCRIPTS_API bool RegisterCoreCommands(FBangoCommandTable& Commands, FString& OutError);
}
