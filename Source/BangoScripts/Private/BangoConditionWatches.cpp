#include "BangoConditionWatches.h"
#include "BangoProgram.h"
#include "BangoGlobalStore.h"

FBangoWatchRegistry::FBangoWatchRegistry(const FBangoGlobalStore& InGlobals, FEvaluate InEvaluate, FReportError InReportError)
	: Globals(InGlobals)
	, Evaluate(MoveTemp(InEvaluate))
	, ReportError(MoveTemp(InReportError))
{
}

FBangoWatchGroupHandle FBangoWatchRegistry::CreateGroup(const TSharedPtr<const FBangoProgram>& Program, float PollInterval, const FString& Name,
	FBangoWatchChangesFunc OnChanges)
{
	FBangoWatchGroupHandle Handle;
	if (!Program.IsValid())
	{
		return Handle;
	}
	TUniquePtr<FGroup> Group = MakeUnique<FGroup>();
	Group->Id = NextId++;
	Group->Program = Program;
	Group->Name = Name;
	Group->OnChanges = MoveTemp(OnChanges);
	Group->PollInterval = FMath::Max(PollInterval, 0.0f);
	for (const FBangoScriptParamInfo& Param : Program->Params)
	{
		Group->GroupArgs.Add(Param.bHasDefault ? Param.Default : FBangoValue::MakeNil(Param.Type));
		Group->bReadsInput.Add(Program->ConditionDependencies.Inputs.Contains(Param.Name));
	}
	Handle.Id = Group->Id;
	Groups.Add(MoveTemp(Group));
	return Handle;
}

void FBangoWatchRegistry::DestroyGroup(FBangoWatchGroupHandle& Group)
{
	const int64 Id = Group.Id;
	Groups.RemoveAll([Id](const TUniquePtr<FGroup>& G) { return G->Id == Id; });
	Group = FBangoWatchGroupHandle();
}

FBangoWatchRegistry::FGroup* FBangoWatchRegistry::FindGroup(int64 Id)
{
	for (TUniquePtr<FGroup>& Group : Groups)
	{
		if (Group->Id == Id)
		{
			return Group.Get();
		}
	}
	return nullptr;
}

const FBangoWatchRegistry::FGroup* FBangoWatchRegistry::FindGroup(int64 Id) const
{
	return const_cast<FBangoWatchRegistry*>(this)->FindGroup(Id);
}

bool FBangoWatchRegistry::FindInputs(const FGroup& Group, TConstArrayView<FBangoWatchInput> Inputs, TArray<int32>& OutIndexes, FString& OutError)
{
	OutIndexes.Reset();
	const TArray<FBangoScriptParamInfo>& Params = Group.Program->Params;
	for (const FBangoWatchInput& Input : Inputs)
	{
		const int32 Index = Params.IndexOfByPredicate([&Input](const FBangoScriptParamInfo& Param) { return Param.Name == Input.Name; });
		if (Index == INDEX_NONE)
		{
			TArray<FString> Names;
			for (const FBangoScriptParamInfo& Param : Params)
			{
				Names.Add(Param.Name);
			}
			OutError = FString::Printf(TEXT("The condition has no input '%s'. Its inputs: %s."), *Input.Name,
				Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("none"));
			return false;
		}
		OutIndexes.Add(Index);
	}
	return true;
}

bool FBangoWatchRegistry::SetGroupInputs(FBangoWatchGroupHandle GroupHandle, TConstArrayView<FBangoWatchInput> Inputs, FString& OutError)
{
	FGroup* Group = FindGroup(GroupHandle.Id);
	if (!Group)
	{
		OutError = TEXT("The watch group doesn't exist (any more).");
		return false;
	}
	TArray<int32> Indexes;
	if (!FindInputs(*Group, Inputs, Indexes, OutError))
	{
		return false;
	}
	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		FBangoValue& Current = Group->GroupArgs[Indexes[i]];
		if (!(Current == Inputs[i].Value))
		{
			Current = Inputs[i].Value;
			Group->bInputsChanged |= Group->bReadsInput[Indexes[i]];
		}
	}
	return true;
}

bool FBangoWatchRegistry::StartWatch(FBangoWatchGroupHandle GroupHandle, TConstArrayView<FBangoWatchInput> Inputs, FBangoWatchHandle& OutWatch)
{
	OutWatch = FBangoWatchHandle();
	FGroup* Group = FindGroup(GroupHandle.Id);
	if (!Group)
	{
		return false;
	}
	TArray<int32> Indexes;
	FString InputError;
	if (!FindInputs(*Group, Inputs, Indexes, InputError))
	{
		ReportError(Group->Name, InputError);
		return false;
	}
	FWatch Watch;
	Watch.Id = NextId++;
	Watch.OwnArgs.SetNum(Group->GroupArgs.Num());
	Watch.bHasOwnArg.Init(false, Group->GroupArgs.Num());
	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		Watch.OwnArgs[Indexes[i]] = Inputs[i].Value;
		Watch.bHasOwnArg[Indexes[i]] = true;
	}
	Check(*Group, Watch, Globals.GetChangeStamp());
	const bool bSatisfied = Watch.bSatisfied;
	OutWatch.Group = Group->Id;
	OutWatch.Id = Watch.Id;
	Group->Watches.Add(MoveTemp(Watch));
	return bSatisfied;
}

void FBangoWatchRegistry::StopWatch(FBangoWatchHandle& Watch)
{
	if (FGroup* Group = FindGroup(Watch.Group))
	{
		const int64 Id = Watch.Id;
		Group->Watches.RemoveAll([Id](const FWatch& W) { return W.Id == Id; });
	}
	Watch = FBangoWatchHandle();
}

bool FBangoWatchRegistry::IsWatching(const FBangoWatchHandle& Watch, bool* bOutSatisfied) const
{
	const FGroup* Group = FindGroup(Watch.Group);
	const FWatch* Found = Group ? Group->Watches.FindByPredicate([&Watch](const FWatch& W) { return W.Id == Watch.Id; }) : nullptr;
	if (Found && bOutSatisfied)
	{
		*bOutSatisfied = Found->bSatisfied;
	}
	return Found != nullptr;
}

bool FBangoWatchRegistry::Check(FGroup& Group, FWatch& Watch, uint64 Stamp)
{
	TArray<FBangoValue> Args = Group.GroupArgs;
	for (int32 i = 0; i < Args.Num(); ++i)
	{
		if (Watch.bHasOwnArg[i])
		{
			Args[i] = Watch.OwnArgs[i];
		}
	}
	FString Error;
	const EBangoConditionResult Result = Evaluate(Group.Program, Args, Error);
	++NumChecks;
	Watch.CheckedStamp = Stamp;

	// An error is shown once, and again only after a true or false in between.
	if (Result == EBangoConditionResult::Error)
	{
		if (!Watch.bErrorShown)
		{
			ReportError(Group.Name, Error);
			Watch.bErrorShown = true;
		}
	}
	else if (Result == EBangoConditionResult::True || Result == EBangoConditionResult::False)
	{
		Watch.bErrorShown = false;
	}

	const bool bSatisfied = Result == EBangoConditionResult::True;
	const bool bChanged = bSatisfied != Watch.bSatisfied;
	Watch.bSatisfied = bSatisfied;
	return bChanged;
}

void FBangoWatchRegistry::Update(double Now)
{
	const uint64 Stamp = Globals.GetChangeStamp();

	// Check what's due. No callbacks yet, so nothing changes under these loops.
	struct FPending
	{
		int64 Group = 0;
		TArray<FBangoWatchChange> Changes;
	};
	TArray<FPending> Pending;
	for (TUniquePtr<FGroup>& GroupPtr : Groups)
	{
		FGroup& Group = *GroupPtr;
		const FBangoDependencies& Deps = Group.Program->ConditionDependencies;

		bool bPoll = false;
		if (!Deps.PollReason.IsEmpty())
		{
			if (Group.NextPoll < 0.0)
			{
				// Spread groups over the interval, so many made at once don't all poll on the same frame.
				const double Spread = FMath::Fmod((double)Group.Id * 0.6180339887498949, 1.0);
				Group.NextPoll = Now + Group.PollInterval * Spread;
			}
			if (Now >= Group.NextPoll)
			{
				bPoll = true;
				Group.NextPoll = FMath::Max(Group.NextPoll + Group.PollInterval, Now);
			}
		}

		uint64 GlobalsStamp = 0;
		for (const FString& Name : Deps.Globals)
		{
			GlobalsStamp = FMath::Max(GlobalsStamp, Globals.GetChangeStamp(Name));
		}

		const bool bAll = bPoll || Group.bInputsChanged;
		Group.bInputsChanged = false;
		FPending* Changes = nullptr;
		for (FWatch& Watch : Group.Watches)
		{
			if ((bAll || GlobalsStamp > Watch.CheckedStamp) && Check(Group, Watch, Stamp))
			{
				if (!Changes)
				{
					Changes = &Pending.AddDefaulted_GetRef();
					Changes->Group = Group.Id;
				}
				FBangoWatchChange& Change = Changes->Changes.AddDefaulted_GetRef();
				Change.Watch.Group = Group.Id;
				Change.Watch.Id = Watch.Id;
				Change.bSatisfied = Watch.bSatisfied;
			}
		}
	}

	// Then tell each group. A callback may stop watches, start them, or destroy groups: what's gone isn't reported.
	for (FPending& Entry : Pending)
	{
		const FGroup* Group = FindGroup(Entry.Group);
		if (!Group)
		{
			continue;
		}
		Entry.Changes.RemoveAll([this](const FBangoWatchChange& Change) { return !IsWatching(Change.Watch); });
		if (Entry.Changes.Num() > 0 && Group->OnChanges)
		{
			const FBangoWatchChangesFunc OnChanges = Group->OnChanges; // the group may be destroyed during the call
			OnChanges(Entry.Changes);
		}
	}
}
