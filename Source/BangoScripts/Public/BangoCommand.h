#pragma once

#include "CoreMinimal.h"
#include "BangoContainers.h"
#include "BangoTypes.h"

class UObject;

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
	 * A number parameter: takes an int or a float, and an int stays an int (Type is then float, only as a fallback).
	 * See FBangoCommandSignature::NumberParam and ReturnsNumber.
	 */
	bool bNumber = false;

	/** "number" for a number parameter, otherwise the type's name. */
	FString GetTypeName() const { return bNumber ? FString(TEXT("number")) : Type.ToString(); }
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

	TArray<FBangoCommandParam> Params;
	/** How many of Params must be given. The rest are optional. -1 means all are required. */
	int32 NumRequiredParams = -1;
	/** If true, any number of extra arguments of any type may follow Params: `Hint("{0} of {1}", _a, _b);` */
	bool bVariadic = false;
	/**
	 * How the graph editor labels the extra values: the prefix, a number counting from VariadicFirstIndex, then
	 * the suffix. The default gives "Value 1", "Value 2", ...; Log, Format and Hint use "{", 0 and "}" to match {0}, {1}, ...,
	 * and Hint and FormatText use "{", 0 and "}" to match {0}, {1}, ...
	 */
	FString VariadicLabelPrefix = TEXT("Value ");
	int32 VariadicFirstIndex = 1;
	FString VariadicLabelSuffix;
	/**
	 * The parameter whose text has placeholders for the extra values, or INDEX_NONE: {0}, {1}, ... for a string
	 * parameter, {0}, {1}, ... for a text one. When it's written out (a string or a LOCTEXT), the compiler checks
	 * every placeholder has a value, and the graph editor shows a row for every placeholder, even ones with no
	 * value yet.
	 */
	int32 PlaceholderParam = INDEX_NONE;

	bool bReturnsValue = false;
	FBangoType ReturnType;
	/**
	 * The command returns a number whose type follows its number parameters (like + does): an int if every number
	 * argument is an int, a float if any is a float. ReturnType is then float, only as a fallback. See ReturnsNumber.
	 */
	bool bReturnsNumber = false;
	/**
	 * Only for commands that return a value: true if a script may call it on a line of its own and throw the value
	 * away (like a latent command whose pausing is the point). Otherwise its result must be used, and
	 * `Format("x");` on its own is a compile error. See MustUseResult.
	 */
	bool bDiscardable = false;

	/** For the graph editor's node menu: a group like "Text" or "Actors", and a one-line description. Both optional. */
	FString CategoryName;
	FString CommandDescription;

	int32 GetNumRequiredParams() const { return NumRequiredParams < 0 ? Params.Num() : NumRequiredParams; }

	/** True if the command returns a value that must be used (it returns one and isn't Discardable). */
	bool MustUseResult() const { return bReturnsValue && !bDiscardable; }

	/** "entity Speak(string text, float pause = optional) -> void" style text for error messages. */
	FString ToString() const;

	// ---- Builders: FBangoCommandSignature::Make(TEXT("Speak")).Target(FBangoType::Entity()).Param(TEXT("text"), FBangoType::String()) ----

	static FBangoCommandSignature Make(const FString& InName);
	FBangoCommandSignature& Target(const FBangoType& Type);
	/** Adds a parameter. ParamDescription is the tooltip on its value in the graph editor (e.g. the unit: "Seconds"). */
	FBangoCommandSignature& Param(const FString& ParamName, const FBangoType& Type, const FString& ParamDescription = FString());
	/**
	 * Adds a number parameter: it takes an int or a float, and an int is passed on as an int (Param with a float type
	 * would turn it into a float). Used with ReturnsNumber for commands like Max that work on either.
	 */
	FBangoCommandSignature& NumberParam(const FString& ParamName, const FString& ParamDescription = FString());
	/** The parameter added last may be nil, or an entity naming no actor: the command gets it as it is (bMayBeNil). */
	FBangoCommandSignature& MayBeNil();
	/** Marks every parameter added after this call as optional. */
	FBangoCommandSignature& Optional();
	/** Any number of extra values may follow the parameters. LabelPrefix and FirstIndex only change how the graph editor labels them. */
	FBangoCommandSignature& Variadic(const FString& LabelPrefix = TEXT("Value "), int32 FirstIndex = 1, const FString& LabelSuffix = FString());
	/** The parameter added last is a text whose {0}, {1}, ... the extra values fill (like Hint's). Only changes the graph editor's rows. */
	FBangoCommandSignature& WithPlaceholders();
	/** The command returns a value. Scripts must use it (`_x = Name(...)`), unless Discardable() is also called. */
	FBangoCommandSignature& Returns(const FBangoType& Type);
	/**
	 * The command returns a number: an int if every number argument (NumberParam) is an int, otherwise a float, like
	 * `_a + _b`. The compiler works out which at each call. The command must return that type (an int result where a
	 * float is expected is turned into a float), and can't be latent.
	 */
	FBangoCommandSignature& ReturnsNumber();
	/** "number" for a ReturnsNumber command, otherwise the return type's name. */
	FString GetReturnTypeName() const { return bReturnsNumber ? FString(TEXT("number")) : ReturnType.ToString(); }
	/** For a ReturnsNumber command: true if the result should be an int, given these arguments (every number argument is an int). */
	bool NumberResultIsInt(TConstArrayView<FBangoValue> Args) const;
	/** A script may call this command on a line of its own and throw its value away. */
	FBangoCommandSignature& Discardable();
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
	/** The world the script runs in, if any. Commands use it to find actors, timers, etc. */
	UObject* WorldContext = nullptr;

	/** Start only: the target and arguments. Not available in Poll; copy what Poll needs into LatentState. */
	bool bHasTarget = false;
	FBangoValue Target;
	TArray<FBangoValue> Args;

	/** Poll only: seconds since the last poll (or since Start). */
	float DeltaSeconds = 0.0f;

	/**
	 * A latent command's own state, kept between Start and Poll calls and saved with the instance.
	 * For example, Sleep keeps the remaining seconds here. Only use FBangoValues, never pointers.
	 */
	TArray<FBangoValue> LatentState;

	/** Done with a return value: put it here. */
	FBangoValue Result;
	/** Failed: put the reason here. */
	FString Error;

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
	/** Runs every frame while Start (or the previous Poll) returned Pending. Required if Start can return Pending. */
	FBangoCommandHandler Poll;
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
	/** Fails for invalid names, duplicates, and the built-in array commands (Count, Push, InsertAt, Pop, RemoveAt). */
	bool Add(const FBangoCommandSignature& Signature, FString& OutError);
	bool Add(const FBangoCommand& Command, FString& OutError);

	virtual const FBangoCommandSignature* FindCommand(const FString& Name) const override;
	virtual FString FindNameIgnoringCase(const FString& Name) const override;
	virtual void GetCommandNames(TArray<FString>& OutNames) const override;
	const FBangoCommand* FindCommandImpl(const FString& Name) const;

	int32 Num() const { return Commands.Num(); }

private:
	TBangoNameMap<TSharedPtr<FBangoCommand>> Commands;
};

namespace Bango
{
	/** The array commands built into the compiler: Count, Push, InsertAt, Pop, RemoveAt. */
	BANGOSCRIPTS_API bool IsBuiltinCommandName(const FString& Name);
}
