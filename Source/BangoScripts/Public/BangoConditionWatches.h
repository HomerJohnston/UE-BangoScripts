#pragma once

#include "CoreMinimal.h"
#include "BangoTypes.h"
#include "BangoInstance.h"

struct FBangoProgram;
struct FBangoGlobalStore;

/**
 * Watching conditions (spec section 10, Watching conditions): a condition checked for many things (a trigger's, for
 * each actor that could activate it), checked again only when its answer might have changed, and reporting only
 * changes. UBangoScriptSubsystem::CreateWatchGroup and the rest are how it's used; FBangoWatchRegistry is the part
 * that doesn't need a world.
 */

/** A watch group: one condition, watched for many things (UBangoScriptSubsystem::CreateWatchGroup). */
struct FBangoWatchGroupHandle
{
	int64 Id = 0;
	bool IsValid() const { return Id != 0; }
	bool operator==(const FBangoWatchGroupHandle& Other) const { return Id == Other.Id; }
};

/** One watch: the group's condition, for one thing (UBangoScriptSubsystem::StartWatch). */
struct FBangoWatchHandle
{
	int64 Group = 0;
	int64 Id = 0;
	bool IsValid() const { return Id != 0; }
	bool operator==(const FBangoWatchHandle& Other) const { return Group == Other.Group && Id == Other.Id; }
};

/** An input given to a group or a watch, by name, as the condition's inputs (its #inputs or inputs signature) name it. */
struct FBangoWatchInput
{
	FString Name;
	FBangoValue Value;
};

/** One watch's answer changing, in a group's OnChanges. Satisfied: the condition is true; else false, Unset or an error. */
struct FBangoWatchChange
{
	FBangoWatchHandle Watch;
	bool bSatisfied = false;
};

/** Gets each end-of-frame pass's changes for one group, in the order its watches were started. */
using FBangoWatchChangesFunc = TFunction<void(TConstArrayView<FBangoWatchChange> Changes)>;

/**
 * The watch groups and watches of one world, without the world: the subsystem owns one, and gives it how to run a
 * condition (Evaluate) and how to show an error (ReportError).
 *
 * A watch is checked when it starts (StartWatch answers), and then by Update, at the end of each frame, if:
 * - a global its condition reads (or the script's functions it calls read) has changed since it was last checked
 *   (the global store's change stamps), or since it was started;
 * - one of its group's inputs that the condition reads has been set to a different value (SetGroupInputs);
 * - or the condition calls a command or uses `call`, and its group's Poll Interval has passed since the group's last
 *   poll. A group's watches are polled together; groups are spread over time.
 * Each is checked once per Update. Then each group with changes gets one OnChanges call. What OnChanges changes
 * (inputs, globals) is seen by the next Update. It may stop watches, start them, and destroy groups, its own included.
 */
class BANGOSCRIPTS_API FBangoWatchRegistry
{
public:
	/** Runs Program as a condition with Args (in its inputs' order). */
	using FEvaluate = TFunction<EBangoConditionResult(const TSharedPtr<const FBangoProgram>& Program, const TArray<FBangoValue>& Args, FString& OutError)>;
	/** Shows a condition's error, for the group Name. */
	using FReportError = TFunction<void(const FString& Name, const FString& Error)>;

	/** Globals: the store the conditions read (it must outlive the registry), for its change stamps. */
	FBangoWatchRegistry(const FBangoGlobalStore& InGlobals, FEvaluate InEvaluate, FReportError InReportError);
	FBangoWatchRegistry(const FBangoWatchRegistry&) = delete;
	FBangoWatchRegistry& operator=(const FBangoWatchRegistry&) = delete;

	/**
	 * A group for Program, a compiled condition (with its ConditionDependencies). PollInterval: seconds between polls,
	 * if it needs polling (0: every Update). Name is for errors.
	 */
	FBangoWatchGroupHandle CreateGroup(const TSharedPtr<const FBangoProgram>& Program, float PollInterval, const FString& Name, FBangoWatchChangesFunc OnChanges);

	/** Stops all its watches; nothing more is reported for it. Makes Group invalid. Fine for an invalid or gone one. */
	void DestroyGroup(FBangoWatchGroupHandle& Group);

	/**
	 * Sets inputs shared by all the group's watches (others keep their values). One the condition reads that's set to
	 * a different value makes every watch check again at the next Update. False, setting nothing, if a name isn't
	 * one of the condition's inputs (OutError says which).
	 */
	bool SetGroupInputs(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FString& OutError);

	/**
	 * Starts a watch with its own inputs (for the watch's lifetime; they win over the group's), checks it now, and
	 * returns whether it's Satisfied. Inputs given neither here nor to the group are nil. OutWatch is invalid, and it
	 * returns false, if the group is gone or an input name isn't the condition's (reported as an error).
	 */
	bool StartWatch(FBangoWatchGroupHandle Group, TConstArrayView<FBangoWatchInput> Inputs, FBangoWatchHandle& OutWatch);

	/** Stops it: it's not checked or reported again. Makes Watch invalid. Fine for an invalid or gone one. */
	void StopWatch(FBangoWatchHandle& Watch);

	/** Whether the watch exists, and if so whether its last answer was Satisfied. */
	bool IsWatching(const FBangoWatchHandle& Watch, bool* bOutSatisfied = nullptr) const;

	/** The end-of-frame pass. Now: game time, in seconds. */
	void Update(double Now);

	/** How many checks Update and StartWatch have run (for tests). */
	int64 GetNumChecks() const { return NumChecks; }
	int32 GetNumGroups() const { return Groups.Num(); }

private:
	struct FWatch
	{
		int64 Id = 0;
		TArray<FBangoValue> OwnArgs; // per input: its value, or a nil of no type if the watch didn't give it
		TArray<bool> bHasOwnArg;
		bool bSatisfied = false;
		bool bErrorShown = false;
		uint64 CheckedStamp = 0;
	};
	struct FGroup
	{
		int64 Id = 0;
		TSharedPtr<const FBangoProgram> Program;
		FString Name;
		FBangoWatchChangesFunc OnChanges;
		float PollInterval = 0.0f;
		double NextPoll = -1.0; // below 0: not yet placed in time (the first Update does)
		TArray<FBangoValue> GroupArgs; // per input
		TArray<bool> bReadsInput;      // per input: the condition reads it
		bool bInputsChanged = false;
		TArray<FWatch> Watches;
	};

	FGroup* FindGroup(int64 Id);
	const FGroup* FindGroup(int64 Id) const;
	/** Each named input's index in the condition's inputs; false (OutError) for a name it doesn't have. */
	static bool FindInputs(const FGroup& Group, TConstArrayView<FBangoWatchInput> Inputs, TArray<int32>& OutIndexes, FString& OutError);
	/** Checks Watch now; true if its answer changed. */
	bool Check(FGroup& Group, FWatch& Watch, uint64 Stamp);

	const FBangoGlobalStore& Globals;
	FEvaluate Evaluate;
	FReportError ReportError;
	TArray<TUniquePtr<FGroup>> Groups;
	int64 NextId = 1;
	int64 NumChecks = 0;
};
