#include "BangoInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Serialization/Archive.h"

namespace BangoInstancePrivate
{
	constexpr int32 InstanceSaveVersion = 8; // 2: waitUntil. 3: a pending command's type arguments. 4: and whether its result is used. 5: a waitUntil's timeout. 6: and its hold. 7: unknown sides of && and ||. 8: the script's variables, for a spawn block's copy made in a function. Raise it when the save format changes: data in another format is refused, not misread

	static FString TypeText(const FBangoType& Type)
	{
		const bool bVowel = Type.Base == EBangoBaseType::Int || Type.Base == EBangoBaseType::Entity;
		return FString::Printf(TEXT("%s %s"), bVowel ? TEXT("an") : TEXT("a"), *Type.ToString());
	}

	static FString ValueTypeText(const FBangoValue& Value)
	{
		return Value.IsNil() ? FString(TEXT("nil")) : TypeText(Value.GetType());
	}

	static bool IsScalar(const FBangoValue& Value, EBangoBaseType Base)
	{
		return !Value.IsNil() && !Value.GetType().IsArray() && Value.GetType().Base == Base;
	}

	static bool IsNumber(const FBangoValue& Value)
	{
		return IsScalar(Value, EBangoBaseType::Int) || IsScalar(Value, EBangoBaseType::Double);
	}

	static const TCHAR* PartName(int32 Part)
	{
		return Part == 0 ? TEXT("x") : Part == 1 ? TEXT("y") : TEXT("z");
	}

	/** A vector2 or vector3 from Size parts. */
	static FBangoValue MakeVector(int32 Size, const double* Parts)
	{
		return Size == 2 ? FBangoValue::MakeVector2(Parts[0], Parts[1]) : FBangoValue::MakeVector3(Parts[0], Parts[1], Parts[2]);
	}

	/** Int results are computed in 64 bits and must fit back into an int (32 bits); overflow is an error, never wraps. */
	static bool FitsInt(int64 Value)
	{
		return Value >= MIN_int32 && Value <= MAX_int32;
	}

	static const TCHAR* OpSymbol(EBangoOp Op)
	{
		switch (Op)
		{
		case EBangoOp::Add:          return TEXT("+");
		case EBangoOp::Subtract:     return TEXT("-");
		case EBangoOp::Multiply:     return TEXT("*");
		case EBangoOp::Divide:       return TEXT("/");
		case EBangoOp::Modulo:       return TEXT("%");
		case EBangoOp::Less:         return TEXT("<");
		case EBangoOp::LessEqual:    return TEXT("<=");
		case EBangoOp::Greater:      return TEXT(">");
		case EBangoOp::GreaterEqual: return TEXT(">=");
		case EBangoOp::Equal:        return TEXT("==");
		case EBangoOp::NotEqual:     return TEXT("!=");
		default:                     return TEXT("?");
		}
	}

	static void SerializeError(FArchive& Ar, FBangoRuntimeError& Error)
	{
		Ar << Error.FileName;
		Ar << Error.Line;
		Ar << Error.Column;
		Ar << Error.NodeID;
		Ar << Error.FunctionName;
		Ar << Error.Message;
	}
}

using namespace BangoInstancePrivate;

// ---------------------------------------------------------------------------
// Errors and setup
// ---------------------------------------------------------------------------

FString FBangoRuntimeError::ToString() const
{
	FString Result = FString::Printf(TEXT("%s(%d:%d)"), FileName.IsEmpty() ? TEXT("<script>") : *FileName, Line, Column);
	if (NodeID >= 0)
	{
		Result += FString::Printf(TEXT(" [node %lld]"), NodeID);
	}
	if (!FunctionName.IsEmpty())
	{
		Result += FString::Printf(TEXT(" in %s()"), *FunctionName);
	}
	Result += TEXT(": ");
	Result += Message;
	return Result;
}

bool FBangoInstance::BindCommands(const FBangoCommandTable& Commands, FString& OutError)
{
	return BindCommandsFor(*Program, Commands, BoundCommands, OutError);
}

bool FBangoInstance::BindCommandsFor(const FBangoProgram& InProgram, const FBangoCommandTable& Commands, TArray<const FBangoCommand*>& OutBound, FString& OutError)
{
	OutBound.Init(nullptr, InProgram.Names.Num());
	for (const FBangoFunctionCode& Function : InProgram.Functions)
	{
		for (const FBangoInstruction& Instruction : Function.Code)
		{
			if (Instruction.Op != EBangoOp::CallCommand && Instruction.Op != EBangoOp::CallCommandInPlace)
			{
				continue;
			}
			const bool bInPlace = Instruction.Op == EBangoOp::CallCommandInPlace;
			const int32 NameIndex = bInPlace ? Instruction.B : Instruction.A;
			if (!InProgram.Names.IsValidIndex(NameIndex))
			{
				OutError = TEXT("The compiled script is damaged (bad command name).");
				return false;
			}
			const FString& Name = InProgram.Names[NameIndex];
			const FBangoCommand* Command = Commands.FindCommandImpl(Name);
			if (!Command)
			{
				OutError = FString::Printf(TEXT("This script calls the command '%s', which isn't registered."), *Name);
				return false;
			}
			if (!Command->Start)
			{
				OutError = FString::Printf(TEXT("This script calls the command '%s', which has no implementation (only a signature was registered)."), *Name);
				return false;
			}
			if (Command->Signature.bMutableTarget != bInPlace)
			{
				// Compiled against a signature that has changed since (whether it changes its target in place).
				OutError = FString::Printf(TEXT("The command '%s' isn't what it was when this script was compiled (whether it changes its target). Compile the script again."), *Name);
				return false;
			}
			OutBound[NameIndex] = Command;
		}
	}
	return true;
}

bool FBangoInstance::Start(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FBangoGlobalStore& Globals, int64 InId, FBangoRuntimeError& OutError,
	TConstArrayView<FBangoValue> Args)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Bango_InstanceStart);
	Program = InProgram;
	Id = InId;
	Error = FBangoRuntimeError();
	Frames.Reset();
	ScriptSlots.Reset();
	Stack.Reset();
	PendingCommandName = -1;
	PendingCall = FBangoCommandCall();
	Status = EBangoInstanceStatus::Failed;

	if (!Program.IsValid() || Program->Functions.Num() == 0)
	{
		Error.Message = TEXT("There is no compiled script to run.");
		OutError = Error;
		return false;
	}
	Error.FileName = Program->FileName;

	FString Message;
	if (!BindCommands(Commands, Message))
	{
		Error.Message = Message;
		OutError = Error;
		return false;
	}

	// Arguments are checked before anything else changes (globals aren't declared for a spawn that fails).
	TArray<FBangoValue> ParamValues;
	if (!CheckArguments(Args, ParamValues))
	{
		OutError = Error;
		return false;
	}

	int32 GlobalLine = 0;
	if (!DeclareGlobals(*Program, Globals, Message, GlobalLine))
	{
		Error.Line = GlobalLine;
		Error.Column = 1;
		Error.Message = Message;
		OutError = Error;
		return false;
	}

	FBangoFrame Main;
	Main.Function = 0;
	for (const FBangoType& SlotType : Program->Functions[0].SlotTypes)
	{
		Main.Slots.Add(FBangoValue::MakeNil(SlotType));
	}
	for (int32 i = 0; i < ParamValues.Num() && i < Main.Slots.Num(); ++i)
	{
		Main.Slots[i] = MoveTemp(ParamValues[i]);
	}
	Frames.Add(MoveTemp(Main));
	Status = EBangoInstanceStatus::Ready;
	return true;
}

bool FBangoInstance::StartSpawned(const FBangoInstance& Parent, int32 BlockIP, int64 InId, FBangoRuntimeError& OutError)
{
	Program = Parent.Program;
	BoundCommands = Parent.BoundCommands;
	Id = InId;
	Error = FBangoRuntimeError();
	Frames.Reset();
	ScriptSlots.Reset();
	Stack.Reset();
	PendingCommandName = -1;
	PendingCall = FBangoCommandCall();
	Status = EBangoInstanceStatus::Failed;
	if (!Program.IsValid() || Parent.Frames.Num() == 0)
	{
		Error.Message = TEXT("Internal error: a spawn block's script isn't running.");
		OutError = Error;
		return false;
	}
	Error.FileName = Program->FileName;
	Owner = Parent.Owner; // a spawn block's copy belongs to the same actor
	const FBangoFrame& From = Parent.Frames.Last();
	if (!Program->Functions.IsValidIndex(From.Function) || !Program->Functions[From.Function].Code.IsValidIndex(BlockIP))
	{
		Error.Message = TEXT("Internal error: bad spawn block.");
		OutError = Error;
		return false;
	}
	// Only the frame it's in: what the block does doesn't go back to a caller. Its variables are copies.
	FBangoFrame Frame;
	Frame.Function = From.Function;
	Frame.IP = BlockIP;
	Frame.StackBase = 0;
	Frame.Slots = From.Slots;
	if (From.Function != 0)
	{
		// A block spawned in a function: the copy has no main graph frame, so it keeps its copies of the script's
		// inputs and script variables (which the function sees) here.
		ScriptSlots = Parent.GetScriptSlots();
	}
	Frames.Add(MoveTemp(Frame));
	Status = EBangoInstanceStatus::Ready;
	return true;
}

bool FBangoInstance::CheckArguments(TConstArrayView<FBangoValue> Args, TArray<FBangoValue>& OutValues)
{
	const TArray<FBangoScriptParamInfo>& Params = Program->Params;
	auto ArgFail = [this](const FString& Message)
	{
		Error.Line = Program->bHasParamsLine ? Program->ParamsLoc.Line : 1;
		Error.Column = Program->bHasParamsLine ? Program->ParamsLoc.Column : 1;
		Error.NodeID = Program->bHasParamsLine ? Program->ParamsLoc.NodeID : 0; // the entry node
		Error.Message = Message;
		return false;
	};

	// Values past the script's last input are left unused: a script declares the inputs it wants, in order, and may
	// stop before the end of what its caller passes (or declare none).
	const int32 Required = Program->GetNumRequiredParams();
	if (Args.Num() < Required)
	{
		const FBangoScriptParamInfo& Missing = Params[Args.Num()];
		return ArgFail(FString::Printf(TEXT("Missing argument %d ('%s', %s). This script needs at least %d argument(s), but %d were passed."),
			Args.Num() + 1, *Missing.Name, *TypeText(Missing.Type), Required, Args.Num()));
	}

	OutValues.Reset();
	for (int32 i = 0; i < Params.Num(); ++i)
	{
		const FBangoScriptParamInfo& Param = Params[i];
		if (i >= Args.Num())
		{
			FBangoValue Default;
			if (!ResolveSelf(Param.Default, Default))
			{
				return ArgFail(FString::Printf(TEXT("The default for '%s' is self, but %s"), *Param.Name, *GetSelfProblem()));
			}
			OutValues.Add(MoveTemp(Default));
			continue;
		}
		const FBangoValue& Arg = Args[i];
		if (Arg.IsNil())
		{
			OutValues.Add(FBangoValue::MakeNil(Param.Type));
			continue;
		}
		FBangoValue Converted;
		if (!Arg.TryConvertTo(Param.Type, Converted))
		{
			return ArgFail(FString::Printf(TEXT("Argument %d ('%s') must be %s, but it is %s."), i + 1, *Param.Name, *TypeText(Param.Type), *ValueTypeText(Arg)));
		}
		OutValues.Add(MoveTemp(Converted));
	}
	return true;
}

void FBangoInstance::Terminate()
{
	if (IsRunning())
	{
		Status = EBangoInstanceStatus::Terminated;
		PendingCommandName = -1;
		PendingCall = FBangoCommandCall();
	}
}

FString FBangoInstance::GetSelfProblem() const
{
	return Owner.Problem.IsEmpty()
		? FString(TEXT("this script has no owner (it was started without one), so there's no actor for self to be. Pass the actor it belongs to as the Owner when you spawn it."))
		: Owner.Problem;
}

bool FBangoInstance::DeclareGlobals(const FBangoProgram& InProgram, FBangoGlobalStore& Globals, FString& OutError, int32& OutLine)
{
	for (const FBangoGlobalDecl& Global : InProgram.Globals)
	{
		if (!Globals.Declare(Global.Name, Global.Type, OutError))
		{
			OutLine = Global.Line;
			return false;
		}
		// `?= value`: given if it didn't exist (Declare just made it nil) or held nil.
		if (Global.bHasDefault && !Global.Default.IsNil() && Globals.Find(Global.Name)->IsNil())
		{
			FBangoValue Default;
			if (!ResolveSelf(Global.Default, Default) || !Globals.Set(Global.Name, Default, OutError))
			{
				OutLine = Global.Line;
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Global '%s''s default can't be given: %s"), *Global.Name, *GetSelfProblem());
				}
				return false;
			}
		}
	}
	return true;
}

bool FBangoInstance::ResolveSelf(const FBangoValue& Value, FBangoValue& OutValue) const
{
	if (Value.IsNil() || Value.GetType().Base != EBangoBaseType::Entity)
	{
		OutValue = Value;
		return true;
	}
	if (Value.GetType().IsArray())
	{
		TArray<FBangoValue> Elements;
		Elements.Reserve(Value.GetArray().Num());
		for (const FBangoValue& Element : Value.GetArray())
		{
			if (!ResolveSelf(Element, Elements.AddDefaulted_GetRef()))
			{
				return false;
			}
		}
		OutValue = FBangoValue::MakeArray(Value.GetType().GetElementType(), Elements);
		return true;
	}
	if (!Bango::IsSelfEntityName(Value.GetEntityName()))
	{
		OutValue = Value;
		return true;
	}
	if (Owner.BangoName.IsEmpty())
	{
		return false;
	}
	OutValue = FBangoValue::MakeEntity(Owner.BangoName);
	return true;
}

EBangoConditionResult FBangoInstance::FinishCondition(EBangoInstanceStatus ResumeStatus, FString& OutError)
{
	const FString ConditionName = Program.IsValid() ? Program->FileName : FString();
	switch (ResumeStatus)
	{
	case EBangoInstanceStatus::Finished:
		break;
	case EBangoInstanceStatus::Waiting:
	{
		const FString Command = GetWaitingCommandName();
		const FBangoDebugLoc Where = GetCurrentLocation();
		Terminate();
		OutError = FString::Printf(TEXT("%s(%d:%d): A condition must give its answer straight away, but it called '%s', which waits."),
			*ConditionName, Where.Line, Where.Column, *Command);
		return EBangoConditionResult::Error;
	}
	case EBangoInstanceStatus::Failed:
		OutError = Error.ToString();
		return Error.bUnsetGlobal ? EBangoConditionResult::Unset : EBangoConditionResult::Error;
	default:
		OutError = FString::Printf(TEXT("%s: the condition didn't finish."), *ConditionName);
		return EBangoConditionResult::Error;
	}
	if (ConditionResult.IsNil() || !(ConditionResult.GetType() == FBangoType::Bool()))
	{
		OutError = FString::Printf(TEXT("%s: the condition didn't return true or false."), *ConditionName);
		return EBangoConditionResult::Error;
	}
	return ConditionResult.GetBool() ? EBangoConditionResult::True : EBangoConditionResult::False;
}

bool FBangoInstance::UnsetGlobalFail(const FString& Message, const FBangoGlobalStore& Globals)
{
	// On the left side of an && or ||: unknown, not an error yet. On at its right side, which may settle the answer
	// (its ResolveUnknown raises this again if it doesn't). The innermost one first: a waitUntil in a function the
	// left side calls is further in than the guard, and takes it.
	if (Status == EBangoInstanceStatus::Ready && UnknownGuards.Num() > 0
		&& (WaitGuard.Set < 0 || UnknownGuards.Last().NumFrames >= WaitGuard.NumFrames)
		&& Frames.Num() >= UnknownGuards.Last().NumFrames && Stack.Num() >= UnknownGuards.Last().StackSize)
	{
		const FUnknownGuard Guard = UnknownGuards.Pop();
		Frames.SetNum(Guard.NumFrames);
		Stack.SetNum(Guard.StackSize);
		PendingUnknowns.SetNum(FMath::Min(Guard.NumPending, PendingUnknowns.Num()));
		FPendingUnknown& Pending = PendingUnknowns.AddDefaulted_GetRef();
		Pending.NumFrames = Guard.NumFrames;
		Pending.HandlerIP = Guard.HandlerIP;
		Pending.Message = Message;
		Pending.Function = CurrentFunction;
		Pending.IP = CurrentIP;
		Frames.Last().IP = Guard.HandlerIP;
		bUnknownCaught = true;
		return false;
	}
	if (WaitGuard.Set >= 0 && Status == EBangoInstanceStatus::Ready && Frames.Num() >= WaitGuard.NumFrames && Stack.Num() >= WaitGuard.StackSize)
	{
		// In a waitUntil's condition: not set up yet, so not true yet. Back out of what it was working out, and wait.
		Frames.SetNum(WaitGuard.NumFrames);
		Stack.SetNum(WaitGuard.StackSize);
		// With the && and || it was in the middle of (those further out, around a call to its function, stay).
		while (UnknownGuards.Num() > 0 && UnknownGuards.Last().NumFrames >= WaitGuard.NumFrames)
		{
			UnknownGuards.Pop();
		}
		while (PendingUnknowns.Num() > 0 && PendingUnknowns.Last().NumFrames >= WaitGuard.NumFrames)
		{
			PendingUnknowns.Pop();
		}
		if (WaitGuard.bTimeout)
		{
			// In its timeout's condition: not true yet either, so it waits (a hold under way carries on).
			const FWaitGuard Guard = WaitGuard;
			WaitGuard = FWaitGuard();
			EnterWait(Guard.Set, Guard.CheckIP, Globals);
			return false;
		}
		PauseAtWait(WaitGuard.Set, WaitGuard.CheckIP, Globals);
		return false;
	}
	const bool bFirst = Status != EBangoInstanceStatus::Failed;
	RuntimeFail(Message);
	if (bFirst)
	{
		Error.bUnsetGlobal = true;
	}
	return false;
}

bool FBangoInstance::RuntimeFail(const FString& Message)
{
	if (Status == EBangoInstanceStatus::Failed)
	{
		return false;
	}
	Status = EBangoInstanceStatus::Failed;
	Error.FileName = Program.IsValid() ? Program->FileName : FString();
	Error.Message = Message;
	Error.bUnsetGlobal = false;
	Error.Line = 0;
	Error.Column = 0;
	Error.NodeID = -1;
	Error.FunctionName.Reset();
	if (Program.IsValid() && Program->Functions.IsValidIndex(CurrentFunction))
	{
		const FBangoFunctionCode& Function = Program->Functions[CurrentFunction];
		Error.FunctionName = Function.Name;
		if (Function.Debug.IsValidIndex(CurrentIP))
		{
			const FBangoDebugLoc& Debug = Function.Debug[CurrentIP];
			Error.Line = Debug.Line;
			Error.Column = Debug.Column;
			Error.NodeID = Debug.NodeID;
		}
	}
	PendingCommandName = -1;
	return false;
}

FBangoDebugLoc FBangoInstance::GetCurrentLocation() const
{
	if (!Program.IsValid() || Frames.Num() == 0)
	{
		return FBangoDebugLoc();
	}
	const FBangoFrame& Frame = Frames.Last();
	if (!Program->Functions.IsValidIndex(Frame.Function))
	{
		return FBangoDebugLoc();
	}
	const FBangoFunctionCode& Function = Program->Functions[Frame.Function];
	// Waiting on a command, it's past the call; at a waitUntil, it's at its WaitCheck.
	const int32 IP = Status == EBangoInstanceStatus::Waiting && WaitSet < 0 ? Frame.IP - 1 : Frame.IP;
	return Function.Debug.IsValidIndex(IP) ? Function.Debug[IP] : FBangoDebugLoc();
}

void FBangoInstance::GetCallStack(TArray<FBangoCallFrameInfo>& OutFrames) const
{
	OutFrames.Reset();
	if (!Program.IsValid())
	{
		return;
	}
	for (int32 k = 0; k < Frames.Num(); ++k)
	{
		const FBangoFrame& Frame = Frames[k];
		if (!Program->Functions.IsValidIndex(Frame.Function))
		{
			continue;
		}
		const FBangoFunctionCode& Function = Program->Functions[Frame.Function];
		FBangoCallFrameInfo Info;
		Info.FunctionName = Function.Name;
		// A frame further out has run its call, so its IP is past it; the innermost one is past its latent command while
		// waiting on one (but at its WaitCheck at a waitUntil).
		const bool bInnermost = k == Frames.Num() - 1;
		const int32 IP = !bInnermost || (Status == EBangoInstanceStatus::Waiting && WaitSet < 0) ? Frame.IP - 1 : Frame.IP;
		if (Function.Debug.IsValidIndex(IP))
		{
			Info.Loc = Function.Debug[IP];
		}
		OutFrames.Add(Info);
	}
}

uint64 FBangoInstance::GetLineTrail(uint64 After, TArray<int32>& OutLines) const
{
	const uint64 First = FMath::Max(After, LineTrailCount > (uint64)TrailSize ? LineTrailCount - TrailSize : (uint64)0);
	for (uint64 n = First; n < LineTrailCount; ++n)
	{
		OutLines.Add(LineTrail[(int32)(n % TrailSize)]);
	}
	return LineTrailCount;
}

uint64 FBangoInstance::GetTrail(uint64 After, TArray<int64>& OutNodeIDs) const
{
	// The buffer holds the last TrailSize nodes: entry n (counting from 0) is at n % TrailSize.
	const uint64 First = FMath::Max(After, TrailCount > (uint64)TrailSize ? TrailCount - TrailSize : (uint64)0);
	for (uint64 n = First; n < TrailCount; ++n)
	{
		OutNodeIDs.Add(Trail[(int32)(n % TrailSize)]);
	}
	return TrailCount;
}

FString FBangoInstance::GetWaitingCommandName() const
{
	if (GetWaitSet())
	{
		return TEXT("waitUntil");
	}
	if (Status != EBangoInstanceStatus::Waiting || !Program.IsValid() || !Program->Names.IsValidIndex(PendingCommandName))
	{
		return FString();
	}
	return Program->Names[PendingCommandName];
}

const FBangoWaitSet* FBangoInstance::GetWaitSet() const
{
	if (Status != EBangoInstanceStatus::Waiting || WaitSet < 0 || !Program.IsValid() || !Program->WaitSets.IsValidIndex(WaitSet))
	{
		return nullptr;
	}
	return &Program->WaitSets[WaitSet];
}

bool FBangoInstance::IsWaitDue(const FBangoGlobalStore& Globals) const
{
	// One with a timeout, or holding, is resumed every frame, to count its time down (Resume only checks the condition if it's due).
	return GetWaitSet() && (WaitTimeLeft >= 0.0 || WaitHoldLeft >= 0.0 || IsWaitConditionDue(Globals));
}

bool FBangoInstance::IsWaitConditionDue(const FBangoGlobalStore& Globals) const
{
	const FBangoWaitSet* Set = GetWaitSet();
	if (!Set)
	{
		return false;
	}
	if (!Set->PollReason.IsEmpty() || WaitStamp == 0 || IsReloadPending())
	{
		return true; // polled; loaded since it paused; or a new version to switch to, which it can do here
	}
	for (const FString& Name : Set->Globals)
	{
		if (Globals.GetChangeStamp(Name) > WaitStamp)
		{
			return true;
		}
	}
	return false;
}

void FBangoInstance::PauseAtWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals)
{
	WaitGuard = FWaitGuard();
	WaitHoldLeft = -1.0; // false: a hold starts over the next time it's true
	const int32 TimeoutIP = Program->WaitSets.IsValidIndex(Set) ? Program->WaitSets[Set].TimeoutIP : -1;
	const int32 TimeoutCondIP = Program->WaitSets.IsValidIndex(Set) ? Program->WaitSets[Set].TimeoutCondIP : -1;
	if (TimeoutCondIP >= 0)
	{
		// Still false, and its timeout is a condition: on to that, which pauses if it's false too. (The same flag as
		// below: Execute may have been stopped by an unset global, and goes on from there.)
		WaitTimeLeft = -1.0;
		Frames.Last().IP = TimeoutCondIP;
		bWaitTimedOut = true;
		return;
	}
	if (TimeoutIP < 0)
	{
		WaitTimeLeft = -1.0; // no timeout (or a reloaded version took it away)
	}
	else if (WaitTimeLeft >= 0.0 && WaitTimeLeft <= 0.0)
	{
		// Still false, and its time is up: on to its timeout block instead of waiting.
		ClearWaitTimes();
		Frames.Last().IP = TimeoutIP;
		bWaitTimedOut = true;
		return;
	}
	EnterWait(Set, CheckIP, Globals);
}

void FBangoInstance::HoldAtWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals)
{
	const FBangoWaitSet* Info = Program->WaitSets.IsValidIndex(Set) ? &Program->WaitSets[Set] : nullptr;
	if (!Info || !Info->bHold || WaitHoldTime < 0.0)
	{
		ClearWaitTimes(); // no heldFor (or a reloaded version took it away): true is enough
		return;
	}
	if (WaitHoldLeft < 0.0)
	{
		WaitHoldLeft = WaitHoldTime; // it has just turned true
	}
	if (WaitHoldLeft <= 0.0)
	{
		ClearWaitTimes(); // true for long enough
		return;
	}
	if (Info->TimeoutIP < 0)
	{
		WaitTimeLeft = -1.0;
	}
	else if (Info->TimeoutCondIP >= 0)
	{
		WaitTimeLeft = -1.0;
		if (bWaitIgnoreHold)
		{
			// Its timeout is a condition that doesn't let a hold finish: it's checked during the hold too. True
			// runs its block; false pauses, with the hold carrying on (Execute carries on there).
			Frames.Last().IP = Info->TimeoutCondIP;
			return;
		}
	}
	else if (bWaitIgnoreHold && WaitTimeLeft >= 0.0 && WaitTimeLeft <= 0.0)
	{
		// The time is up, and the timeout doesn't let a hold finish: on to its block (Execute carries on there).
		ClearWaitTimes();
		Frames.Last().IP = Info->TimeoutIP;
		return;
	}
	EnterWait(Set, CheckIP, Globals);
}

void FBangoInstance::ClearWaitTimes()
{
	WaitTimeLeft = -1.0;
	WaitHoldTime = -1.0;
	WaitHoldLeft = -1.0;
	bWaitIgnoreHold = false;
}

void FBangoInstance::EnterWait(int32 Set, int32 CheckIP, const FBangoGlobalStore& Globals)
{
	Frames.Last().IP = CheckIP; // when it carries on, it works the condition out again
	Status = EBangoInstanceStatus::Waiting;
	WaitSet = Set;
	WaitStamp = FMath::Max<uint64>(Globals.GetChangeStamp(), 1);
	if (ReloadProgram.IsValid() && bReloadTried)
	{
		// It ran from one pause to the next without reaching a point where the new version fits.
		FinishReload(EBangoReloadResult::KeptOld, ReloadFailure);
	}
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

EBangoInstanceStatus FBangoInstance::Resume(const FBangoExecContext& Context)
{
	if (!IsRunning())
	{
		return Status;
	}
	// One scope for the whole run to its next pause, never one per instruction.
	TRACE_CPUPROFILER_EVENT_SCOPE(Bango_InstanceResume);
	if (!Context.Globals)
	{
		RuntimeFail(TEXT("No global variable store was given to run the script with."));
		return Status;
	}

	if (Status == EBangoInstanceStatus::Waiting && WaitSet >= 0)
	{
		if (WaitTimeLeft >= 0.0 || WaitHoldLeft >= 0.0)
		{
			// One with a timeout, or whose condition is true and being held: its times run while it's paused there,
			// and nothing else happens until one is up or its condition may have changed.
			const double Delta = (double)FMath::Max(Context.DeltaSeconds, 0.0f);
			const bool bHolding = WaitHoldLeft >= 0.0;
			if (WaitTimeLeft >= 0.0)
			{
				WaitTimeLeft = FMath::Max(0.0, WaitTimeLeft - Delta);
			}
			if (bHolding)
			{
				WaitHoldLeft = FMath::Max(0.0, WaitHoldLeft - Delta);
			}
			// The time being up doesn't matter during a hold, which is let finish, unless the timeout ignores holds.
			const bool bTimeUp = WaitTimeLeft >= 0.0 && WaitTimeLeft <= 0.0 && (!bHolding || bWaitIgnoreHold);
			const bool bHeld = bHolding && WaitHoldLeft <= 0.0;
			if (!bTimeUp && !bHeld && !IsWaitConditionDue(*Context.Globals))
			{
				return Status;
			}
		}
		// At a waitUntil: check its condition again (from its WaitCheck). If it's still false, it pauses there again
		// (or, with its timeout's time up, goes on at its timeout block). If it's true but must be held longer, too.
		WaitSet = -1;
		Status = EBangoInstanceStatus::Ready;
	}
	else if (Status == EBangoInstanceStatus::Waiting)
	{
		CurrentFunction = Frames.Last().Function;
		CurrentIP = Frames.Last().IP - 1;

		const FBangoCommand* Command = BoundCommands.IsValidIndex(PendingCommandName) ? BoundCommands[PendingCommandName] : nullptr;
		if (!Command || !Command->Poll)
		{
			RuntimeFail(TEXT("The script is waiting on a command that can't be polled."));
			return Status;
		}
		PendingCall.InstanceId = Id;
		PendingCall.WorldContext = Context.WorldContext;
		PendingCall.DeltaSeconds = Context.DeltaSeconds;
		const EBangoCommandResult Result = Command->Poll(PendingCall);
		if (Status != EBangoInstanceStatus::Waiting || Result == EBangoCommandResult::Pending)
		{
			return Status;
		}
		if (!FinishCommand(Result))
		{
			return Status;
		}
	}

	do
	{
		// (A waitUntil that timed out while a global it read was unset stops Execute, ready to go on at its timeout block.
		// So does an unset global on the left side of an && or ||, ready to go on at the right side.)
		bWaitTimedOut = false;
		bUnknownCaught = false;
		Execute(Context);
	}
	while ((bWaitTimedOut || bUnknownCaught) && Status == EBangoInstanceStatus::Ready);
	return Status;
}

FBangoValue FBangoInstance::PopValue()
{
	const int32 Base = Frames.Num() > 0 ? Frames.Last().StackBase : 0;
	if (Stack.Num() <= Base)
	{
		RuntimeFail(TEXT("Internal error: the value stack is empty."));
		return FBangoValue();
	}
	// Never shrinking: the stack empties at the end of every statement, and giving its memory back each time (which
	// is what a TArray does when it empties) meant allocating it again for the next one.
	return Stack.Pop(EAllowShrinking::No);
}

bool FBangoInstance::PopIndexes(int32 Count, TArray<int64>& OutIndexes)
{
	OutIndexes.SetNum(Count);
	for (int32 i = Count - 1; i >= 0; --i)
	{
		const FBangoValue Index = PopValue();
		if (Status == EBangoInstanceStatus::Failed)
		{
			return false;
		}
		if (Index.IsNil())
		{
			return RuntimeFail(TEXT("An array index is nil."));
		}
		if (!IsScalar(Index, EBangoBaseType::Int))
		{
			return RuntimeFail(FString::Printf(TEXT("An array index must be an int, but it is %s."), *ValueTypeText(Index)));
		}
		OutIndexes[i] = Index.GetInt();
	}
	return true;
}

const TArray<FBangoValue>& FBangoInstance::GetScriptSlots() const
{
	return Frames.Num() > 0 && Frames[0].Function == 0 ? Frames[0].Slots : ScriptSlots;
}

TArray<FBangoValue>& FBangoInstance::GetPlaceSlots(const FBangoPlace& Place)
{
	if (Place.Kind == EBangoPlaceKind::Script)
	{
		return Frames.Num() > 0 && Frames[0].Function == 0 ? Frames[0].Slots : ScriptSlots;
	}
	return Frames.Last().Slots;
}

const FString& FBangoInstance::DescribePlace(const FBangoPlace& Place) const
{
	// By reference: this is asked on every read and write of a variable, and a copy of the name each time (an
	// allocation) was most of what a simple instruction cost.
	static const FString UnknownScriptVariable(TEXT("<script variable>"));
	static const FString UnknownLocal(TEXT("<local>"));
	static const FString UnknownGlobal(TEXT("<global>"));
	if (Place.Kind == EBangoPlaceKind::Script)
	{
		const FBangoFunctionCode& Main = Program->Functions[0];
		return Main.SlotNames.IsValidIndex(Place.Index) ? Main.SlotNames[Place.Index] : UnknownScriptVariable;
	}
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		const FBangoFunctionCode& Function = Program->Functions[Frames.Last().Function];
		return Function.SlotNames.IsValidIndex(Place.Index) ? Function.SlotNames[Place.Index] : UnknownLocal;
	}
	return Program->Names.IsValidIndex(Place.Index) ? Program->Names[Place.Index] : UnknownGlobal;
}

namespace BangoInstancePrivate
{
	/** "vector3", "string": what an element index (`_v[i]`, `_s[i]`) is into. */
	static bool IsElementIndexable(const FBangoValue& Value)
	{
		const FBangoType Type = Value.GetType();
		return !Value.IsNil() && !Type.IsArray() && (Type.IsVector() || Type.Base == EBangoBaseType::String);
	}

	/** Part or character Index of Value (a vector or a string, not nil), or false with OutError naming Path. */
	static bool ReadElement(const FBangoValue& Value, int64 Index, const FString& Path, FBangoValue& OutValue, FString& OutError)
	{
		const FBangoType Type = Value.GetType();
		const int64 Count = Type.IsVector() ? Type.GetVectorSize() : Value.GetString().Len();
		if (Index < 0)
		{
			OutError = FString::Printf(TEXT("Index %lld for '%s' is negative."), Index, *Path);
			return false;
		}
		if (Index >= Count)
		{
			OutError = Type.IsVector()
				? FString::Printf(TEXT("'%s[%lld]' is past the end of '%s': a %s has %lld parts, [0] to [%lld]."), *Path, Index, *Path, Type.GetVectorSize() == 2 ? TEXT("vector2") : TEXT("vector3"), Count, Count - 1)
				: FString::Printf(TEXT("'%s[%lld]' is past the end of '%s', which has %lld character(s). Check with Count first."), *Path, Index, *Path, Count);
			return false;
		}
		OutValue = Type.IsVector() ? FBangoValue::MakeDouble(Value.GetVectorPart((int32)Index)) : FBangoValue::MakeString(Value.GetString().Mid((int32)Index, 1));
		return true;
	}

	/** Sets part or character Index of Target (a vector or a string, not nil) to Value, or false with OutError. */
	static bool WriteElement(FBangoValue& Target, int64 Index, const FBangoValue& Value, const FString& Path, FString& OutError)
	{
		FBangoValue Current;
		if (!ReadElement(Target, Index, Path, Current, OutError))
		{
			return false;
		}
		if (Target.GetType().IsVector())
		{
			if (!IsNumber(Value))
			{
				OutError = FString::Printf(TEXT("A vector's part must be a number, but this is %s."), *ValueTypeText(Value));
				return false;
			}
			Target.SetVectorPart((int32)Index, Value.GetNumberAsDouble());
			return true;
		}
		if (Value.IsNil() || Value.GetType() != FBangoType::String() || Value.GetString().Len() != 1)
		{
			OutError = Value.IsNil() || Value.GetType() != FBangoType::String()
				? FString::Printf(TEXT("A string's character must be a string of one character, but this is %s."), *ValueTypeText(Value))
				: FString::Printf(TEXT("A string's character must be a string of one character, but \"%s\" has %d."), *Value.GetString(), Value.GetString().Len());
			return false;
		}
		const FString& Chars = Target.GetString();
		Target = FBangoValue::MakeString(Chars.Left((int32)Index) + Value.GetString() + Chars.Mid((int32)Index + 1));
		return true;
	}
}

bool FBangoInstance::LoadPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals, const IBangoEntityResolver* Entities, bool bNilOk)
{
	TArray<int64> Indexes;
	if (!PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	if (bNilOk && Place.Part == -1)
	{
		// Like isNil: nil anywhere along the way (or a global nobody made, or past the end) gives nil.
		const FBangoValue* Root = nullptr;
		if (Place.Kind != EBangoPlaceKind::Global)
		{
			TArray<FBangoValue>& Slots = GetPlaceSlots(Place);
			if (!Slots.IsValidIndex(Place.Index))
			{
				return RuntimeFail(TEXT("Internal error: bad local slot."));
			}
			Root = &Slots[Place.Index];
		}
		else
		{
			Root = Globals.Find(DescribePlace(Place));
		}
		const FBangoValue* Found = Root;
		if (Root && Indexes.Num() > 0)
		{
			Found = !Root->IsNil() && Root->GetType().IsArray() ? Root->GetElementAtPath(Indexes) : nullptr;
		}
		Stack.Add(Found ? *Found : FBangoValue::MakeNil(FBangoType::Entity()));
		return true;
	}

	const FString& RootName = DescribePlace(Place);
	const FBangoValue* Current = nullptr;
	if (Place.Kind != EBangoPlaceKind::Global)
	{
		TArray<FBangoValue>& Slots = GetPlaceSlots(Place);
		if (!Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Current = &Slots[Place.Index];
	}
	else
	{
		Current = Globals.Find(RootName);
		if (!Current)
		{
			// Still just not set up yet (a condition is false, a waitUntil waits): the hint is for when it stops a script.
			if (Entities && Entities->IsEntityRegistered(RootName))
			{
				return UnsetGlobalFail(FString::Printf(TEXT("Global '%s' doesn't exist. There is an actor with the Bango ID '%s': write @%s to use it."), *RootName, *RootName, *RootName), Globals);
			}
			return UnsetGlobalFail(FString::Printf(TEXT("Global '%s' doesn't exist. No script or Blueprint has declared it."), *RootName), Globals);
		}
	}

	// The usual read, a whole variable that has a value: nothing more to work out, and no message to build.
	if (Indexes.Num() == 0 && Place.Part == -1 && !Current->IsNil())
	{
		Stack.Add(*Current);
		return true;
	}

	FString Path = RootName;
	// The last index picks a vector's part or a string's character: always for an ElementPart place, and for one whose
	// type wasn't known (a global declared elsewhere) when it reaches a vector or a string.
	for (int32 i = 0; i < Indexes.Num(); ++i)
	{
		const int64 Index = Indexes[i];
		const bool bLast = i == Indexes.Num() - 1;
		if (bLast && (Place.Part == FBangoPlace::ElementPart || BangoInstancePrivate::IsElementIndexable(*Current)) && !Current->IsNil())
		{
			if (!BangoInstancePrivate::IsElementIndexable(*Current))
			{
				return RuntimeFail(FString::Printf(TEXT("'%s' is %s, so it can't be indexed: only arrays, vectors and strings can be."), *Path, *ValueTypeText(*Current)));
			}
			FBangoValue Element;
			FString ElementError;
			if (!BangoInstancePrivate::ReadElement(*Current, Index, Path, Element, ElementError))
			{
				return RuntimeFail(ElementError);
			}
			Stack.Add(MoveTemp(Element));
			return true;
		}
		if (Current->IsNil())
		{
			if (Place.Kind == EBangoPlaceKind::Global && Path == RootName)
			{
				return UnsetGlobalFail(FString::Printf(TEXT("Global '%s' is nil, so it can't be indexed."), *Path), Globals);
			}
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil, so it can't be indexed."), *Path));
		}
		if (!Current->GetType().IsArray())
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is %s, so it can't be indexed: only arrays, vectors and strings can be."), *Path, *ValueTypeText(*Current)));
		}
		const FBangoValue* Next = Current->GetElement(Index);
		if (!Next)
		{
			if (Index < 0)
			{
				return RuntimeFail(FString::Printf(TEXT("Index %lld for '%s' is negative."), Index, *Path));
			}
			return RuntimeFail(FString::Printf(TEXT("'%s[%lld]' is past the end of '%s', which has %lld element(s). Check with isNil first."),
				*Path, Index, *Path, Current->ArrayCount()));
		}
		Path += FString::Printf(TEXT("[%lld]"), Index);
		Current = Next;
	}

	if (Current->IsNil())
	{
		const FBangoFunctionCode& Function = Program->Functions[Frames.Last().Function];
		if (Place.Kind == EBangoPlaceKind::Local && Indexes.Num() == 0 && Place.Index < Function.NumParams)
		{
			const FString ParamOf = Function.Name.IsEmpty() ? FString(TEXT("this script")) : FString::Printf(TEXT("function '%s'"), *Function.Name);
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil. It's a parameter of %s; check what the caller passed."), *Path, *ParamOf));
		}
		if (Place.Kind == EBangoPlaceKind::Script && Indexes.Num() == 0 && Place.Index < Program->Functions[0].NumParams)
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil. It's a parameter of this script; check what the caller passed."), *Path));
		}
		if (Place.Kind == EBangoPlaceKind::Global && Indexes.Num() == 0)
		{
			return UnsetGlobalFail(FString::Printf(TEXT("Global '%s' is nil. Give it a value before reading it, or check it with isNil(%s)."), *Path, *Path), Globals);
		}
		return RuntimeFail(FString::Printf(TEXT("'%s' is nil. Give it a value before reading it, or check it with isNil(%s)."), *Path, *Path));
	}
	if (Place.Part >= 0)
	{
		if (Current->GetType().GetVectorSize() <= Place.Part)
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is %s, which has no '%s' part."), *Path, *ValueTypeText(*Current), PartName(Place.Part)));
		}
		Stack.Add(FBangoValue::MakeDouble(Current->GetVectorPart(Place.Part)));
		return true;
	}
	Stack.Add(*Current);
	return true;
}

bool FBangoInstance::StorePlace(const FBangoPlace& Place, FBangoGlobalStore& Globals)
{
	FBangoValue Value = PopValue();
	TArray<int64> Indexes;
	if (Status == EBangoInstanceStatus::Failed || !PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	const FString& RootName = DescribePlace(Place);
	FBangoValue* Root = nullptr;
	if (Place.Kind != EBangoPlaceKind::Global)
	{
		TArray<FBangoValue>& Slots = GetPlaceSlots(Place);
		if (!Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Root = &Slots[Place.Index];
	}
	else
	{
		Root = Globals.FindMutable(RootName);
		if (!Root)
		{
			return RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist."), *RootName));
		}
	}

	// The usual write, a whole variable given a value of its own type: it's moved in.
	if (Indexes.Num() == 0 && Place.Part == -1 && !Value.IsNil() && Value.GetType() == Root->GetType())
	{
		*Root = MoveTemp(Value);
		return true;
	}

	FString Message;
	if (Place.Part == FBangoPlace::ElementPart)
	{
		// `_v[i] = 1;`, `_s[i] = "a";`: the arrays on the way aren't grown, as reading them must work first.
		FString Path = RootName;
		const FBangoValue* Found = Root;
		for (int32 i = 0; i + 1 < Indexes.Num() && Found; ++i)
		{
			if (Found->IsNil() || !Found->GetType().IsArray())
			{
				return RuntimeFail(FString::Printf(TEXT("'%s' is nil, so it can't be indexed."), *Path));
			}
			const FBangoValue* Next = Found->GetElement(Indexes[i]);
			if (!Next)
			{
				return RuntimeFail(Indexes[i] < 0
					? FString::Printf(TEXT("Index %lld for '%s' is negative."), Indexes[i], *Path)
					: FString::Printf(TEXT("'%s[%lld]' is past the end of '%s', which has %lld element(s)."), *Path, Indexes[i], *Path, Found->ArrayCount()));
			}
			Path += FString::Printf(TEXT("[%lld]"), Indexes[i]);
			Found = Next;
		}
		if (!Found || Found->IsNil())
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil, so one of its parts or characters can't be set on its own. Give it a whole value first."), *Path));
		}
		FBangoValue* Target = const_cast<FBangoValue*>(Found); // Root is ours to change
		if (!BangoInstancePrivate::WriteElement(*Target, Indexes.Last(), Value, Path, Message))
		{
			return RuntimeFail(Message);
		}
		return true;
	}
	if (Place.Part >= 0)
	{
		const TCHAR* Part = PartName(Place.Part);
		FString Path = RootName;
		for (const int64 Index : Indexes)
		{
			Path += FString::Printf(TEXT("[%lld]"), Index);
		}
		FBangoValue* Target = Root;
		if (Indexes.Num() > 0)
		{
			Target = Root->FindElementAtPath(Indexes, Message);
			if (!Target)
			{
				return RuntimeFail(FString::Printf(TEXT("Writing to '%s': %s"), *RootName, *Message));
			}
		}
		if (Target->IsNil())
		{
			const bool bVector2 = Target->GetType().Base == EBangoBaseType::Vector2 && !Target->GetType().IsArray();
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil, so its %s can't be set on its own. Give it a whole vector first, like %s = %s;"),
				*Path, Part, *Path, bVector2 ? TEXT("[0, 0]") : TEXT("[0, 0, 0]")));
		}
		if (Target->GetType().GetVectorSize() <= Place.Part)
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is %s, which has no '%s' part."), *Path, *ValueTypeText(*Target), Part));
		}
		if (!IsNumber(Value))
		{
			return RuntimeFail(FString::Printf(TEXT("A vector's %s must be a number, but this is %s."), Part, *ValueTypeText(Value)));
		}
		Target->SetVectorPart(Place.Part, Value.GetNumberAsDouble());
		return true;
	}
	if (Indexes.Num() == 0)
	{
		FBangoValue Converted;
		if (!Value.TryConvertTo(Root->GetType(), Converted))
		{
			return RuntimeFail(FString::Printf(TEXT("Can't store %s in '%s', which holds %s."), *ValueTypeText(Value), *RootName, *TypeText(Root->GetType())));
		}
		*Root = MoveTemp(Converted);
		return true;
	}
	if (!Root->SetElementAtPath(Indexes, Value, Message))
	{
		return RuntimeFail(FString::Printf(TEXT("Writing to '%s': %s"), *RootName, *Message));
	}
	return true;
}

bool FBangoInstance::IsNilPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals)
{
	TArray<int64> Indexes;
	if (!PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	const FBangoValue* Root = nullptr;
	if (Place.Kind != EBangoPlaceKind::Global)
	{
		TArray<FBangoValue>& Slots = GetPlaceSlots(Place);
		if (!Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Root = &Slots[Place.Index];
	}
	else
	{
		Root = Globals.Find(DescribePlace(Place));
	}

	bool bDefined = false;
	if (Root)
	{
		if (Indexes.Num() == 0)
		{
			bDefined = !Root->IsNil();
		}
		else if (!Root->IsNil() && Root->GetType().IsArray())
		{
			const FBangoValue* Element = Root->GetElementAtPath(Indexes);
			bDefined = Element && !Element->IsNil();
		}
	}
	Stack.Add(FBangoValue::MakeBool(!bDefined));
	return true;
}

bool FBangoInstance::BinaryOp(EBangoOp Op)
{
	const FBangoValue Right = PopValue();
	const FBangoValue Left = PopValue();
	if (Status == EBangoInstanceStatus::Failed)
	{
		return false;
	}
	const TCHAR* Symbol = OpSymbol(Op);

	if (Left.IsNil() || Right.IsNil())
	{
		return RuntimeFail(FString::Printf(TEXT("nil can't be used with '%s'."), Symbol));
	}
	if (Left.GetType().IsArray() || Right.GetType().IsArray())
	{
		return RuntimeFail(FString::Printf(TEXT("Arrays can't be used with '%s'."), Symbol));
	}

	// Equality works on any matching types.
	if (Op == EBangoOp::Equal || Op == EBangoOp::NotEqual)
	{
		bool bEqual = false;
		if (IsNumber(Left) && IsNumber(Right))
		{
			bEqual = (IsScalar(Left, EBangoBaseType::Int) && IsScalar(Right, EBangoBaseType::Int))
				? Left.GetInt() == Right.GetInt()
				: Left.GetNumberAsDouble() == Right.GetNumberAsDouble();
		}
		else if (Left.GetType() != Right.GetType())
		{
			return RuntimeFail(FString::Printf(TEXT("Can't compare %s with %s."), *ValueTypeText(Left), *ValueTypeText(Right)));
		}
		else
		{
			bEqual = Left == Right;
		}
		Stack.Add(FBangoValue::MakeBool(Op == EBangoOp::Equal ? bEqual : !bEqual));
		return true;
	}

	const int32 LeftSize = Left.GetType().GetVectorSize();
	const int32 RightSize = Right.GetType().GetVectorSize();
	if (LeftSize > 0 || RightSize > 0)
	{
		const int32 Size = LeftSize > 0 ? LeftSize : RightSize;
		double Parts[3] = { 0.0, 0.0, 0.0 };
		if ((Op == EBangoOp::Add || Op == EBangoOp::Subtract) && LeftSize == RightSize)
		{
			for (int32 i = 0; i < Size; ++i)
			{
				Parts[i] = Op == EBangoOp::Add ? Left.GetVectorPart(i) + Right.GetVectorPart(i) : Left.GetVectorPart(i) - Right.GetVectorPart(i);
			}
		}
		else if (Op == EBangoOp::Multiply && (LeftSize > 0 ? IsNumber(Right) : IsNumber(Left)))
		{
			const FBangoValue& Vector = LeftSize > 0 ? Left : Right;
			const double Scale = (LeftSize > 0 ? Right : Left).GetNumberAsDouble();
			for (int32 i = 0; i < Size; ++i)
			{
				Parts[i] = Vector.GetVectorPart(i) * Scale;
			}
		}
		else if (Op == EBangoOp::Divide && LeftSize > 0 && IsNumber(Right))
		{
			const double Divisor = Right.GetNumberAsDouble();
			if (Divisor == 0.0)
			{
				return RuntimeFail(TEXT("Division by zero."));
			}
			for (int32 i = 0; i < Size; ++i)
			{
				Parts[i] = Left.GetVectorPart(i) / Divisor;
			}
		}
		else
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' can't be used with %s and %s."), Symbol, *ValueTypeText(Left), *ValueTypeText(Right)));
		}
		Stack.Add(MakeVector(Size, Parts));
		return true;
	}

	if (Op == EBangoOp::Add && IsScalar(Left, EBangoBaseType::String) && IsScalar(Right, EBangoBaseType::String))
	{
		Stack.Add(FBangoValue::MakeString(Left.GetString() + Right.GetString()));
		return true;
	}

	if (!IsNumber(Left) || !IsNumber(Right))
	{
		if (Op == EBangoOp::Add)
		{
			return RuntimeFail(FString::Printf(TEXT("'+' needs two numbers or two strings, but got %s and %s."), *ValueTypeText(Left), *ValueTypeText(Right)));
		}
		return RuntimeFail(FString::Printf(TEXT("'%s' needs numbers, but got %s and %s."), Symbol, *ValueTypeText(Left), *ValueTypeText(Right)));
	}

	const bool bInts = IsScalar(Left, EBangoBaseType::Int) && IsScalar(Right, EBangoBaseType::Int);

	switch (Op)
	{
	case EBangoOp::Less:
	case EBangoOp::LessEqual:
	case EBangoOp::Greater:
	case EBangoOp::GreaterEqual:
	{
		int32 Compare = 0;
		if (bInts)
		{
			Compare = Left.GetInt() < Right.GetInt() ? -1 : (Left.GetInt() > Right.GetInt() ? 1 : 0);
		}
		else
		{
			const double A = Left.GetNumberAsDouble();
			const double B = Right.GetNumberAsDouble();
			Compare = A < B ? -1 : (A > B ? 1 : 0);
		}
		bool bResult = false;
		switch (Op)
		{
		case EBangoOp::Less:      bResult = Compare < 0; break;
		case EBangoOp::LessEqual: bResult = Compare <= 0; break;
		case EBangoOp::Greater:   bResult = Compare > 0; break;
		default:                  bResult = Compare >= 0; break;
		}
		Stack.Add(FBangoValue::MakeBool(bResult));
		return true;
	}
	default:
		break;
	}

	if (bInts)
	{
		const int64 A = Left.GetInt();
		const int64 B = Right.GetInt();
		int64 Result = 0;
		switch (Op)
		{
		case EBangoOp::Add:      Result = A + B; break;
		case EBangoOp::Subtract: Result = A - B; break;
		case EBangoOp::Multiply: Result = A * B; break;
		case EBangoOp::Divide:
			if (B == 0)
			{
				return RuntimeFail(TEXT("Division by zero."));
			}
			Result = A / B;
			break;
		case EBangoOp::Modulo:
			if (B == 0)
			{
				return RuntimeFail(TEXT("Modulo by zero."));
			}
			Result = A % B;
			break;
		default:
			return RuntimeFail(TEXT("Internal error: unknown operator."));
		}
		if (!FitsInt(Result))
		{
			return RuntimeFail(FString::Printf(TEXT("Integer overflow: %lld %s %lld is outside the int range (-2147483648 to 2147483647). Use floats for larger numbers."), A, Symbol, B));
		}
		Stack.Add(FBangoValue::MakeInt((int32)Result));
		return true;
	}

	const double A = Left.GetNumberAsDouble();
	const double B = Right.GetNumberAsDouble();
	double Result = 0.0;
	switch (Op)
	{
	case EBangoOp::Add:      Result = A + B; break;
	case EBangoOp::Subtract: Result = A - B; break;
	case EBangoOp::Multiply: Result = A * B; break;
	case EBangoOp::Divide:
		if (B == 0.0)
		{
			return RuntimeFail(TEXT("Division by zero."));
		}
		Result = A / B;
		break;
	case EBangoOp::Modulo:
		if (B == 0.0)
		{
			return RuntimeFail(TEXT("Modulo by zero."));
		}
		Result = FMath::Fmod(A, B);
		break;
	default:
		return RuntimeFail(TEXT("Internal error: unknown operator."));
	}
	Stack.Add(FBangoValue::MakeDouble(Result));
	return true;
}

bool FBangoInstance::CallReflected(int32 NameIndex, int32 ArgCount, bool bWantResult, const FBangoExecContext& Context)
{
	if (!Program->Names.IsValidIndex(NameIndex) || ArgCount < 0)
	{
		return RuntimeFail(TEXT("Internal error: bad call."));
	}
	TArray<FBangoValue> Args;
	Args.SetNum(ArgCount);
	for (int32 i = ArgCount - 1; i >= 0; --i)
	{
		Args[i] = PopValue();
	}
	const FBangoValue Target = PopValue();
	if (Status == EBangoInstanceStatus::Failed)
	{
		return false;
	}
	const FString& Name = Program->Names[NameIndex];
	if (Target.IsNil())
	{
		return RuntimeFail(FString::Printf(TEXT("'call %s': the target is nil."), *Name));
	}
	if (!Context.CallReflected)
	{
		return RuntimeFail(FString::Printf(TEXT("'call %s': Blueprint functions can't be called here."), *Name));
	}
	FBangoValue Result;
	FString CallError;
	if (!Context.CallReflected(Target, Name, Args, bWantResult, Result, CallError))
	{
		return RuntimeFail(CallError);
	}
	if (bWantResult)
	{
		Stack.Add(MoveTemp(Result));
	}
	return true;
}

FBangoValue* FBangoInstance::FindPlaceToChange(const FBangoPlace& Place, TConstArrayView<int64> Indexes, FBangoGlobalStore& Globals, const FString& What)
{
	const FString& RootName = DescribePlace(Place);
	FBangoValue* Target = nullptr;
	if (Place.Kind != EBangoPlaceKind::Global)
	{
		if (Frames.Num() == 0 || !GetPlaceSlots(Place).IsValidIndex(Place.Index))
		{
			RuntimeFail(TEXT("Internal error: bad local slot."));
			return nullptr;
		}
		Target = &GetPlaceSlots(Place)[Place.Index];
	}
	else
	{
		Target = Globals.FindMutable(RootName); // which notes that it changed
		if (!Target)
		{
			RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist."), *RootName));
			return nullptr;
		}
	}
	if (Indexes.Num() > 0)
	{
		FString Message;
		Target = Target->FindElementAtPath(Indexes, Message);
		if (!Target)
		{
			RuntimeFail(FString::Printf(TEXT("%s on '%s': %s"), *What, *RootName, *Message));
			return nullptr;
		}
	}
	return Target;
}

bool FBangoInstance::PrepareCommandCall(const FBangoCommandSignature& Signature, FBangoCommandCall& Call, const FBangoExecContext& Context)
{
	// Its type parameters, from the target and arguments as they are now (an undeclared global passed for one is
	// only checked here), and those arguments as the types they came out as.
	if (Signature.TypeParams.Num() > 0)
	{
		FString BindError;
		if (!Signature.BindTypeParams(Call.bHasTarget ? &Call.Target : nullptr, Call.Args, Call.TypeArgs, BindError))
		{
			return RuntimeFail(FString::Printf(TEXT("%s: %s"), *Signature.Name, *BindError));
		}
	}

	if (Context.Entities)
	{
		auto CheckEntity = [&](const FBangoValue& Value) -> bool
		{
			if (!IsScalar(Value, EBangoBaseType::Entity) || Context.Entities->IsEntityRegistered(Value.GetEntityName()))
			{
				return true;
			}
			return RuntimeFail(FString::Printf(TEXT("%s: %s"), *Signature.Name, *Bango::DescribeMissingEntity(Value.GetEntityName())));
		};
		if (Call.bHasTarget && !CheckEntity(Call.Target))
		{
			return false;
		}
		for (int32 i = 0; i < Call.Args.Num() && i < Signature.Params.Num(); ++i)
		{
			if (!Signature.Params[i].bMayBeNil && !CheckEntity(Call.Args[i]))
			{
				return false;
			}
		}
	}
	return true;
}

bool FBangoInstance::CallCommandInPlace(const FBangoPlace& Place, int32 NameIndex, int32 ArgCount, bool bResultUsed, FBangoGlobalStore& Globals, const FBangoExecContext& Context)
{
	const FBangoCommand* Command = BoundCommands.IsValidIndex(NameIndex) ? BoundCommands[NameIndex] : nullptr;
	if (!Command)
	{
		return RuntimeFail(TEXT("Internal error: command not bound."));
	}
	const FBangoCommandSignature& Signature = Command->Signature;

	FBangoCommandCall Call;
	Call.InstanceId = Id;
	Call.WorldContext = Context.WorldContext;
	Call.DeltaSeconds = Context.DeltaSeconds;
	Call.bResultUsed = bResultUsed;
	Call.Args.SetNum(ArgCount);
	for (int32 i = ArgCount - 1; i >= 0; --i)
	{
		Call.Args[i] = PopValue();
	}
	TArray<int64> Indexes;
	if (Status == EBangoInstanceStatus::Failed || !PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	// Everything about the call is checked with the target still in its place...
	FBangoValue* Target = FindPlaceToChange(Place, Indexes, Globals, Signature.Name);
	if (!Target)
	{
		return false;
	}
	const FBangoType TargetType = Target->GetType();
	if (Signature.TypeParams.Num() > 0)
	{
		FString BindError;
		if (!Signature.BindTypeParams(Target, Call.Args, Call.TypeArgs, BindError))
		{
			return RuntimeFail(FString::Printf(TEXT("%s on '%s': %s"), *Signature.Name, *DescribePlace(Place), *BindError));
		}
	}
	if (Context.Entities)
	{
		for (int32 i = 0; i < Call.Args.Num() && i < Signature.Params.Num(); ++i)
		{
			const FBangoValue& Arg = Call.Args[i];
			if (!Signature.Params[i].bMayBeNil && IsScalar(Arg, EBangoBaseType::Entity) && !Context.Entities->IsEntityRegistered(Arg.GetEntityName()))
			{
				return RuntimeFail(FString::Printf(TEXT("%s: %s"), *Signature.Name, *Bango::DescribeMissingEntity(Arg.GetEntityName())));
			}
		}
	}

	// ...then it's taken out for the command to change, and put back when it's done. No copy of it is made (an
	// array may be large).
	Call.bHasTarget = true;
	Call.Target = MoveTemp(*Target);
	*Target = FBangoValue::MakeNil(TargetType);
	const EBangoCommandResult Result = Command->Start(Call);

	// Found again, as the command may have changed other globals (never this instance's frames).
	const bool bSameType = Call.Target.GetType() == TargetType;
	FString Unused;
	Target = Place.Kind != EBangoPlaceKind::Global
		? (Frames.Num() > 0 && GetPlaceSlots(Place).IsValidIndex(Place.Index) ? &GetPlaceSlots(Place)[Place.Index] : nullptr)
		: Globals.FindMutable(DescribePlace(Place));
	Target = Target && Indexes.Num() > 0 ? Target->FindElementAtPath(Indexes, Unused) : Target;
	if (Target && bSameType)
	{
		*Target = MoveTemp(Call.Target);
	}
	Call.Target = FBangoValue();
	if (Status != EBangoInstanceStatus::Ready)
	{
		return false; // the command stopped this instance
	}
	if (!Target)
	{
		return RuntimeFail(FString::Printf(TEXT("Command '%s': its target, '%s', wasn't there to put back when it finished."), *Signature.Name, *DescribePlace(Place)));
	}
	if (!bSameType)
	{
		return RuntimeFail(FString::Printf(TEXT("Command '%s' changed the type of its target, '%s' (it must stay %s)."), *Signature.Name, *DescribePlace(Place), *TypeText(TargetType)));
	}
	if (Result == EBangoCommandResult::Pending)
	{
		return RuntimeFail(FString::Printf(TEXT("Command '%s' changes its target, so it can't wait, but it returned Pending."), *Signature.Name));
	}
	if (Result == EBangoCommandResult::Failed)
	{
		// Named with what it was called on.
		return RuntimeFail(FString::Printf(TEXT("%s on '%s': %s"), *Signature.Name, *DescribePlace(Place), Call.Error.IsEmpty() ? TEXT("the command failed.") : *Call.Error));
	}
	PendingCommandName = NameIndex;
	PendingCall = MoveTemp(Call);
	return FinishCommand(Result);
}

bool FBangoInstance::CallCommand(int32 NameIndex, int32 ArgCount, int32 Flags, const FBangoExecContext& Context)
{
	const bool bHasTarget = (Flags & Bango::CallHasTarget) != 0;

	const FBangoCommand* Command = BoundCommands.IsValidIndex(NameIndex) ? BoundCommands[NameIndex] : nullptr;
	if (!Command)
	{
		return RuntimeFail(TEXT("Internal error: command not bound."));
	}
	const FBangoCommandSignature& Signature = Command->Signature;

	if (Signature.bMutableTarget)
	{
		return RuntimeFail(FString::Printf(TEXT("Internal error: '%s' changes its target, but was called without a place for it."), *Signature.Name));
	}

	FBangoCommandCall Call;
	Call.InstanceId = Id;
	Call.WorldContext = Context.WorldContext;
	Call.DeltaSeconds = Context.DeltaSeconds;
	Call.bResultUsed = (Flags & Bango::CallResultUsed) != 0;
	Call.Args.SetNum(ArgCount);
	for (int32 i = ArgCount - 1; i >= 0; --i)
	{
		Call.Args[i] = PopValue();
	}
	if (bHasTarget)
	{
		Call.bHasTarget = true;
		Call.Target = PopValue();
	}
	if (Status == EBangoInstanceStatus::Failed || !PrepareCommandCall(Signature, Call, Context))
	{
		return false;
	}

	const EBangoCommandResult Result = Command->Start(Call);
	if (Status != EBangoInstanceStatus::Ready)
	{
		// The command terminated this instance (or something else stopped it).
		return false;
	}
	PendingCommandName = NameIndex;
	PendingCall = MoveTemp(Call);

	if (Result == EBangoCommandResult::Pending)
	{
		if (WaitGuard.Set >= 0)
		{
			return RuntimeFail(FString::Printf(TEXT("A waitUntil's condition must answer at once, but '%s' waits."), *Signature.Name));
		}
		if (!Command->Poll)
		{
			return RuntimeFail(FString::Printf(TEXT("Command '%s' returned Pending but has no Poll handler."), *Signature.Name));
		}
		// Arguments aren't needed any more; the command keeps what it needs in LatentState.
		PendingCall.Args.Reset();
		PendingCall.Target = FBangoValue();
		Status = EBangoInstanceStatus::Waiting;
		if (ReloadProgram.IsValid() && bReloadTried)
		{
			// It ran from one pause to the next without reaching a point where the new version fits.
			FinishReload(EBangoReloadResult::KeptOld, ReloadFailure);
		}
		return true;
	}
	return FinishCommand(Result);
}

bool FBangoInstance::FinishCommand(EBangoCommandResult Result)
{
	const FBangoCommand* Command = BoundCommands.IsValidIndex(PendingCommandName) ? BoundCommands[PendingCommandName] : nullptr;
	if (!Command)
	{
		return RuntimeFail(TEXT("Internal error: finished command not bound."));
	}
	const FBangoCommandSignature& Signature = Command->Signature;

	if (Result == EBangoCommandResult::Failed)
	{
		return RuntimeFail(FString::Printf(TEXT("%s: %s"), *Signature.Name, PendingCall.Error.IsEmpty() ? TEXT("the command failed.") : *PendingCall.Error));
	}

	if (Signature.bReturnsValue && Signature.TypeParams.IsValidIndex(Signature.ReturnTypeVar))
	{
		// The type its arguments made its type parameter, which the compiler expects (Max: an int for ints).
		if (!PendingCall.TypeArgs.IsValidIndex(Signature.ReturnTypeVar))
		{
			return RuntimeFail(FString::Printf(TEXT("Command '%s': the type of its result isn't known (its type parameters weren't kept)."), *Signature.Name));
		}
		const FBangoType Expected = FBangoCommandSignature::GetTypeOf(PendingCall.TypeArgs[Signature.ReturnTypeVar], Signature.ReturnTypeVarDepth);
		FBangoValue Converted;
		if (PendingCall.Result.IsNil() && !PendingCall.bResultUsed && Signature.bDiscardable)
		{
			Converted = FBangoValue::MakeNil(Expected); // thrown away (Pop of a nil element, on a line of its own)
		}
		else if (PendingCall.Result.IsNil() || !PendingCall.Result.TryConvertTo(Expected, Converted))
		{
			return RuntimeFail(FString::Printf(TEXT("Command '%s' returned %s, but with these arguments it should return %s."),
				*Signature.Name, *ValueTypeText(PendingCall.Result), *TypeText(Expected)));
		}
		Stack.Add(MoveTemp(Converted));
	}
	else if (Signature.bReturnsValue)
	{
		FBangoValue Converted;
		if (PendingCall.Result.IsNil())
		{
			Converted = FBangoValue::MakeNil(Signature.ReturnType);
		}
		else if (!PendingCall.Result.TryConvertTo(Signature.ReturnType, Converted))
		{
			return RuntimeFail(FString::Printf(TEXT("Command '%s' returned %s, but it should return %s."),
				*Signature.Name, *ValueTypeText(PendingCall.Result), *TypeText(Signature.ReturnType)));
		}
		Stack.Add(MoveTemp(Converted));
	}

	PendingCommandName = -1;
	PendingCall = FBangoCommandCall();
	Status = EBangoInstanceStatus::Ready;
	return true;
}

bool FBangoInstance::Execute(const FBangoExecContext& Context)
{
	FBangoGlobalStore& Globals = *Context.Globals;
	int32 Budget = Context.MaxInstructionsPerResume;
	// Counts what this call runs, however it's left (GetInstructionsRun): the budget is counted down anyway.
	struct FCountInstructions
	{
		uint64& Total;
		const int32& Left;
		const int32 Start;
		~FCountInstructions() { Total += (uint64)FMath::Max(0, Start - FMath::Max(Left, 0)); }
	} CountInstructions{ InstructionsRun, Budget, Budget };

	while (Status == EBangoInstanceStatus::Ready)
	{
		if (Frames.Num() == 0)
		{
			Status = EBangoInstanceStatus::Finished;
			break;
		}
		if (ReloadProgram.IsValid())
		{
			TryReload(Globals); // between statements, it may switch to the new version (so Program may change here)
		}
		const int32 FunctionIndex = Frames.Last().Function;
		const FBangoFunctionCode& Function = Program->Functions[FunctionIndex];
		const int32 IP = Frames.Last().IP;
		CurrentFunction = FunctionIndex;
		CurrentIP = IP;

		if (--Budget < 0)
		{
			return RuntimeFail(FString::Printf(TEXT("The script ran %d instructions without pausing, so it is probably stuck in an infinite loop. Long loops need a waiting command such as Sleep."),
				Context.MaxInstructionsPerResume));
		}
		if (!Function.Code.IsValidIndex(IP))
		{
			return RuntimeFail(TEXT("Internal error: ran past the end of the code."));
		}
		const FBangoInstruction I = Function.Code[IP];
		Frames.Last().IP = IP + 1;
		if (bRecordTrail && Function.Debug.IsValidIndex(IP))
		{
			// For the debugger's trail: each node it moves on to (a node's instructions run one after another).
			const int64 NodeID = Function.Debug[IP].NodeID;
			if (NodeID >= 0 && NodeID != LastTrailNode)
			{
				if (Trail.Num() < TrailSize)
				{
					Trail.Add(NodeID);
				}
				else
				{
					Trail[TrailCount % TrailSize] = NodeID;
				}
				++TrailCount;
				LastTrailNode = NodeID;
			}
			// The same by line: each line it moves on to.
			const int32 TrailLine = Function.Debug[IP].Line;
			if (TrailLine > 0 && TrailLine != LastTrailLine)
			{
				if (LineTrail.Num() < TrailSize)
				{
					LineTrail.Add(TrailLine);
				}
				else
				{
					LineTrail[LineTrailCount % TrailSize] = TrailLine;
				}
				++LineTrailCount;
				LastTrailLine = TrailLine;
			}
		}

		switch (I.Op)
		{
		case EBangoOp::Nop:
			break;

		case EBangoOp::PushConst:
			if (!Program->Constants.IsValidIndex(I.A))
			{
				return RuntimeFail(TEXT("Internal error: bad constant."));
			}
		{
			const FBangoValue& Constant = Program->Constants[I.A];
			if (Constant.GetType().Base != EBangoBaseType::Entity)
			{
				Stack.Add(Constant);
				break;
			}
			FBangoValue Resolved;
			if (!ResolveSelf(Constant, Resolved))
			{
				return RuntimeFail(FString::Printf(TEXT("'self' can't be used here: %s"), *GetSelfProblem()));
			}
			Stack.Add(MoveTemp(Resolved));
			break;
		}

		case EBangoOp::PushNil:
			if (!Program->Types.IsValidIndex(I.A))
			{
				return RuntimeFail(TEXT("Internal error: bad type."));
			}
			Stack.Add(FBangoValue::MakeNil(Program->Types[I.A]));
			break;

		case EBangoOp::Pop:
			PopValue();
			break;

		case EBangoOp::DupN:
		{
			const int32 Base = Stack.Num() - I.A;
			if (I.A < 0 || Base < Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: bad DupN."));
			}
			for (int32 k = 0; k < I.A; ++k)
			{
				FBangoValue Copy = Stack[Base + k];
				Stack.Add(MoveTemp(Copy));
			}
			break;
		}

		case EBangoOp::LoadPlace:
		case EBangoOp::StorePlace:
		case EBangoOp::IsNilPlace:
		case EBangoOp::CallCommandInPlace:
		{
			if (!Function.Places.IsValidIndex(I.A))
			{
				return RuntimeFail(TEXT("Internal error: bad place."));
			}
			const FBangoPlace Place = Function.Places[I.A];
			switch (I.Op)
			{
			case EBangoOp::LoadPlace:      LoadPlace(Place, Globals, Context.Entities, I.B != 0); break;
			case EBangoOp::StorePlace:     StorePlace(Place, Globals); break;
			case EBangoOp::IsNilPlace:     IsNilPlace(Place, Globals); break;
			default:
				CallCommandInPlace(Place, I.B, I.C & Bango::InPlaceArgCountMask, ((I.C >> Bango::InPlaceFlagShift) & Bango::CallResultUsed) != 0, Globals, Context);
				break;
			}
			break;
		}

		case EBangoOp::Index:
		{
			const FBangoValue IndexValue = PopValue();
			const FBangoValue ArrayValue = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!IsScalar(IndexValue, EBangoBaseType::Int))
			{
				return RuntimeFail(FString::Printf(TEXT("An index must be an int, but it is %s."), *ValueTypeText(IndexValue)));
			}
			if (BangoInstancePrivate::IsElementIndexable(ArrayValue))
			{
				FBangoValue Element;
				FString ElementError;
				if (!BangoInstancePrivate::ReadElement(ArrayValue, IndexValue.GetInt(), ArrayValue.GetType().IsVector() ? TEXT("the vector") : TEXT("the string"), Element, ElementError))
				{
					return RuntimeFail(ElementError);
				}
				Stack.Add(MoveTemp(Element));
				break;
			}
			if (ArrayValue.IsNil() || !ArrayValue.GetType().IsArray())
			{
				return RuntimeFail(FString::Printf(TEXT("Only arrays, vectors and strings can be indexed, but this is %s."), *ValueTypeText(ArrayValue)));
			}
			const FBangoValue* Element = ArrayValue.GetElement(IndexValue.GetInt());
			if (!Element)
			{
				return RuntimeFail(FString::Printf(TEXT("Index %d is out of range: the array has %lld element(s)."), IndexValue.GetInt(), ArrayValue.ArrayCount()));
			}
			if (Element->IsNil())
			{
				return RuntimeFail(FString::Printf(TEXT("Element %d of the array is nil."), IndexValue.GetInt()));
			}
			Stack.Add(*Element);
			break;
		}

		case EBangoOp::Convert:
		{
			if (!Program->Types.IsValidIndex(I.A))
			{
				return RuntimeFail(TEXT("Internal error: bad type."));
			}
			const FBangoType& Target = Program->Types[I.A];
			const FBangoValue Value = PopValue();
			FBangoValue Converted;
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!Value.TryConvertTo(Target, Converted))
			{
				return RuntimeFail(FString::Printf(TEXT("Expected %s here, but the value is %s."), *TypeText(Target), *ValueTypeText(Value)));
			}
			Stack.Add(MoveTemp(Converted));
			break;
		}

		case EBangoOp::MakeArray:
		{
			if (!Program->Types.IsValidIndex(I.B) || I.A < 0 || Stack.Num() - I.A < Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: bad MakeArray."));
			}
			FBangoValue Array = FBangoValue::MakeArray(Program->Types[I.B]);
			const int32 First = Stack.Num() - I.A;
			FString Message;
			for (int32 k = First; k < Stack.Num(); ++k)
			{
				if (!Array.Push(Stack[k], Message))
				{
					return RuntimeFail(FString::Printf(TEXT("Building an array: %s"), *Message));
				}
			}
			Stack.SetNum(First, EAllowShrinking::No);
			Stack.Add(MoveTemp(Array));
			break;
		}

		case EBangoOp::MakeVector:
		{
			if ((I.A != 2 && I.A != 3) || Stack.Num() - I.A < Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: bad MakeVector."));
			}
			const int32 First = Stack.Num() - I.A;
			double Parts[3] = { 0.0, 0.0, 0.0 };
			for (int32 k = 0; k < I.A; ++k)
			{
				const FBangoValue& Part = Stack[First + k];
				if (!IsNumber(Part))
				{
					return RuntimeFail(FString::Printf(TEXT("A vector's %s must be a number, but this is %s."), PartName(k), *ValueTypeText(Part)));
				}
				Parts[k] = Part.GetNumberAsDouble();
			}
			Stack.SetNum(First, EAllowShrinking::No);
			Stack.Add(MakeVector(I.A, Parts));
			break;
		}

		case EBangoOp::GetPart:
		{
			const FBangoValue Value = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (Value.IsNil() || Value.GetType().GetVectorSize() <= I.A || I.A < 0)
			{
				return RuntimeFail(FString::Printf(TEXT("'.%s' needs a vector that has that part, but got %s."), PartName(I.A), *ValueTypeText(Value)));
			}
			Stack.Add(FBangoValue::MakeDouble(Value.GetVectorPart(I.A)));
			break;
		}

		case EBangoOp::Not:
		{
			const FBangoValue Value = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!IsScalar(Value, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("'!' needs a bool, but got %s."), *ValueTypeText(Value)));
			}
			Stack.Add(FBangoValue::MakeBool(!Value.GetBool()));
			break;
		}

		case EBangoOp::Negate:
		{
			const FBangoValue Value = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (IsScalar(Value, EBangoBaseType::Int))
			{
				if (Value.GetInt() == MIN_int32)
				{
					return RuntimeFail(TEXT("Integer overflow: -2147483648 has no positive int."));
				}
				Stack.Add(FBangoValue::MakeInt(-Value.GetInt()));
			}
			else if (IsScalar(Value, EBangoBaseType::Double))
			{
				Stack.Add(FBangoValue::MakeDouble(-Value.GetDouble()));
			}
			else if (!Value.IsNil() && Value.GetType().IsVector())
			{
				const int32 Size = Value.GetType().GetVectorSize();
				double Parts[3] = { 0.0, 0.0, 0.0 };
				for (int32 k = 0; k < Size; ++k)
				{
					Parts[k] = -Value.GetVectorPart(k);
				}
				Stack.Add(MakeVector(Size, Parts));
			}
			else
			{
				return RuntimeFail(FString::Printf(TEXT("'-' needs a number or a vector, but got %s."), *ValueTypeText(Value)));
			}
			break;
		}

		case EBangoOp::Add:
		case EBangoOp::Subtract:
		case EBangoOp::Multiply:
		case EBangoOp::Divide:
		case EBangoOp::Modulo:
		case EBangoOp::Less:
		case EBangoOp::LessEqual:
		case EBangoOp::Greater:
		case EBangoOp::GreaterEqual:
		case EBangoOp::Equal:
		case EBangoOp::NotEqual:
			BinaryOp(I.Op);
			break;

		case EBangoOp::Jump:
			Frames.Last().IP = I.A;
			break;

		case EBangoOp::WaitBegin:
		{
			// Its values come off in reverse: IgnoreHold, the timeout's seconds, the hold's seconds (those it has).
			bool bIgnoreHold = false;
			if (I.B & Bango::WaitHasIgnoreHold)
			{
				const FBangoValue Flag = PopValue();
				if (Status == EBangoInstanceStatus::Failed)
				{
					return false;
				}
				if (!IsScalar(Flag, EBangoBaseType::Bool))
				{
					return RuntimeFail(Flag.IsNil() ? FString(TEXT("This timeout's second value (IgnoreHold) is nil. It needs true or false."))
						: FString::Printf(TEXT("A timeout's second value (IgnoreHold) needs true or false, but this is %s."), *ValueTypeText(Flag)));
				}
				bIgnoreHold = Flag.GetBool();
			}
			// What: "timeout" or "heldFor".
			const auto PopSeconds = [this](const TCHAR* What, double& OutTime) -> bool
			{
				const FBangoValue Seconds = PopValue();
				if (Status == EBangoInstanceStatus::Failed)
				{
					return false;
				}
				if (Seconds.IsNil())
				{
					return RuntimeFail(FString::Printf(TEXT("This waitUntil's %s is nil. It needs a number of seconds."), What));
				}
				if (!IsScalar(Seconds, EBangoBaseType::Double) && !IsScalar(Seconds, EBangoBaseType::Int))
				{
					return RuntimeFail(FString::Printf(TEXT("A waitUntil's %s needs a number of seconds, but this is %s."), What, *ValueTypeText(Seconds)));
				}
				OutTime = IsScalar(Seconds, EBangoBaseType::Int) ? (double)Seconds.GetInt() : Seconds.GetDouble();
				if (!(OutTime >= 0.0))
				{
					return RuntimeFail(FString::Printf(TEXT("A waitUntil's %s can't be negative, but this is %s seconds."), What, *Seconds.ToDebugString()));
				}
				return true;
			};
			double Time = -1.0;
			double Hold = -1.0;
			if (((I.B & Bango::WaitHasTimeout) && !PopSeconds(TEXT("timeout"), Time))
				|| ((I.B & Bango::WaitHasHold) && !PopSeconds(TEXT("heldFor"), Hold)))
			{
				return false;
			}
			WaitTimeLeft = Time;
			WaitHoldTime = Hold;
			WaitHoldLeft = -1.0;
			bWaitIgnoreHold = bIgnoreHold;
			break;
		}

		case EBangoOp::WaitCheck:
			if (WaitGuard.Set >= 0)
			{
				return RuntimeFail(TEXT("A waitUntil can't be used while working out another waitUntil's condition (in a function it calls)."));
			}
			WaitGuard.Set = I.A;
			WaitGuard.NumFrames = Frames.Num();
			WaitGuard.StackSize = Stack.Num();
			WaitGuard.CheckIP = IP;
			break;

		case EBangoOp::WaitTimeoutCheck:
			if (WaitGuard.Set >= 0)
			{
				return RuntimeFail(TEXT("A waitUntil can't be used while working out another waitUntil's condition (in a function it calls)."));
			}
			WaitGuard.Set = I.A;
			WaitGuard.NumFrames = Frames.Num();
			WaitGuard.StackSize = Stack.Num();
			WaitGuard.CheckIP = I.B;
			WaitGuard.bTimeout = true;
			break;

		case EBangoOp::WaitTimeoutResult:
		{
			const FBangoValue Condition = PopValue();
			WaitGuard = FWaitGuard();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!IsScalar(Condition, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("A timeout's condition must be a bool, but this is %s."), *ValueTypeText(Condition)));
			}
			if (Condition.GetBool())
			{
				ClearWaitTimes(); // the wait is over: on into the timeout's block
			}
			else
			{
				EnterWait(I.A, I.B, Globals);
			}
			break;
		}

		case EBangoOp::WaitResult:
		{
			const FBangoValue Condition = PopValue();
			WaitGuard = FWaitGuard();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!IsScalar(Condition, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("A condition must be a bool, but this is %s."), *ValueTypeText(Condition)));
			}
			if (!Condition.GetBool())
			{
				PauseAtWait(I.A, I.B, Globals);
			}
			else
			{
				HoldAtWait(I.A, I.B, Globals); // true in time: carries on, unless it must stay true for longer first
			}
			break;
		}

		case EBangoOp::SpawnBlock:
		{
			// The block runs as a new instance (which may run straight away, until it first pauses); this one goes on.
			if (!Context.SpawnBlock)
			{
				return RuntimeFail(TEXT("'spawn' can't start its block here: only a script running in a game world can spawn."));
			}
			FString SpawnError;
			int64 SpawnedId = 0;
			if (!Context.SpawnBlock(*this, I.A, SpawnedId, SpawnError))
			{
				return RuntimeFail(FString::Printf(TEXT("'spawn' couldn't start its block: %s"), *SpawnError));
			}
			if (Status != EBangoInstanceStatus::Ready)
			{
				return false; // the copy aborted this script while it ran
			}
			if (I.B != 0)
			{
				Stack.Add(FBangoValue::MakeInt((int32)SpawnedId));
			}
			break;
		}

		case EBangoOp::JumpIfFalse:
		{
			const FBangoValue Condition = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (!IsScalar(Condition, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("A condition must be a bool, but this is %s."), *ValueTypeText(Condition)));
			}
			if (!Condition.GetBool())
			{
				Frames.Last().IP = I.A;
			}
			break;
		}

		case EBangoOp::TryUnknown:
		{
			FUnknownGuard& Guard = UnknownGuards.AddDefaulted_GetRef();
			Guard.NumFrames = Frames.Num();
			Guard.StackSize = Stack.Num();
			Guard.HandlerIP = I.A;
			Guard.NumPending = PendingUnknowns.Num();
			break;
		}

		case EBangoOp::EndTryUnknown:
			if (UnknownGuards.Num() == 0 || UnknownGuards.Last().NumFrames != Frames.Num())
			{
				return RuntimeFail(TEXT("Internal error: no unknown guard to end."));
			}
			UnknownGuards.Pop();
			break;

		case EBangoOp::ResolveUnknown:
		{
			if (PendingUnknowns.Num() == 0 || PendingUnknowns.Last().NumFrames != Frames.Num() || PendingUnknowns.Last().HandlerIP != I.A)
			{
				break; // the left side had its value: the right side is the answer
			}
			const FPendingUnknown Pending = PendingUnknowns.Pop();
			if (Stack.Num() <= Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: the value stack is empty."));
			}
			const FBangoValue& Right = Stack.Last();
			if (!IsScalar(Right, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("'%s' needs bools, but got %s."), I.B ? TEXT("||") : TEXT("&&"), *ValueTypeText(Right)));
			}
			if (Right.GetBool() == (I.B != 0))
			{
				break; // unknown || true is true, unknown && false is false
			}
			// Still unknown: the left side's unset global counts after all, where it was read.
			CurrentFunction = Pending.Function;
			CurrentIP = Pending.IP;
			return UnsetGlobalFail(Pending.Message, Globals);
		}

		case EBangoOp::JumpIfFalseOrPop:
		case EBangoOp::JumpIfTrueOrPop:
		{
			if (Stack.Num() <= Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: the value stack is empty."));
			}
			const FBangoValue& Condition = Stack.Last();
			if (!IsScalar(Condition, EBangoBaseType::Bool))
			{
				return RuntimeFail(FString::Printf(TEXT("'%s' needs bools, but got %s."), I.Op == EBangoOp::JumpIfFalseOrPop ? TEXT("&&") : TEXT("||"), *ValueTypeText(Condition)));
			}
			const bool bJumpOn = I.Op == EBangoOp::JumpIfTrueOrPop;
			if (Condition.GetBool() == bJumpOn)
			{
				Frames.Last().IP = I.A;
			}
			else
			{
				Stack.Pop(EAllowShrinking::No);
			}
			break;
		}

		case EBangoOp::CallFunction:
		{
			if (!Program->Functions.IsValidIndex(I.A))
			{
				return RuntimeFail(TEXT("Internal error: bad function."));
			}
			if (Frames.Num() >= Context.MaxCallDepth)
			{
				return RuntimeFail(FString::Printf(TEXT("Too many nested function calls (more than %d). Is a function calling itself forever?"), Context.MaxCallDepth));
			}
			const FBangoFunctionCode& Callee = Program->Functions[I.A];
			if (I.B != Callee.NumParams || Stack.Num() - I.B < Frames.Last().StackBase)
			{
				return RuntimeFail(TEXT("Internal error: bad call."));
			}
			FBangoFrame NewFrame;
			NewFrame.Function = I.A;
			for (const FBangoType& SlotType : Callee.SlotTypes)
			{
				NewFrame.Slots.Add(FBangoValue::MakeNil(SlotType));
			}
			const int32 First = Stack.Num() - I.B;
			for (int32 k = 0; k < I.B; ++k)
			{
				FBangoValue Converted;
				if (!Stack[First + k].TryConvertTo(Callee.SlotTypes[k], Converted))
				{
					return RuntimeFail(FString::Printf(TEXT("Argument %d of '%s' should be %s, but it is %s."), k + 1, *Callee.Name, *TypeText(Callee.SlotTypes[k]), *ValueTypeText(Stack[First + k])));
				}
				NewFrame.Slots[k] = MoveTemp(Converted);
			}
			Stack.SetNum(First, EAllowShrinking::No);
			NewFrame.StackBase = Stack.Num();
			Frames.Add(MoveTemp(NewFrame));
			break;
		}

		case EBangoOp::CallCommand:
			CallCommand(I.A, I.B, I.C, Context);
			break;

		case EBangoOp::CallReflected:
			CallReflected(I.A, I.B, I.C != 0, Context);
			break;

		case EBangoOp::Return:
		case EBangoOp::ReturnValue:
		{
			FBangoValue ReturnValue;
			if (I.Op == EBangoOp::ReturnValue)
			{
				ReturnValue = PopValue();
				if (Status == EBangoInstanceStatus::Failed)
				{
					return false;
				}
			}
			Stack.SetNum(FMath::Min(Stack.Num(), Frames.Last().StackBase), EAllowShrinking::No);
			Frames.Pop(EAllowShrinking::No);
			if (Frames.Num() == 0)
			{
				ConditionResult = MoveTemp(ReturnValue); // a condition's answer (GetResult)
				Status = EBangoInstanceStatus::Finished;
				break;
			}
			if (I.Op == EBangoOp::ReturnValue)
			{
				Stack.Add(MoveTemp(ReturnValue));
			}
			break;
		}

		case EBangoOp::Halt:
			Status = EBangoInstanceStatus::Finished;
			Stack.Reset();
			break;

		case EBangoOp::ErrorMissingReturn:
			if (Function.Name.IsEmpty())
			{
				return RuntimeFail(TEXT("The condition reached its end without returning true or false."));
			}
			return RuntimeFail(FString::Printf(TEXT("Function '%s' reached its end without returning a value."), *Function.Name));

		case EBangoOp::ForEachNext:
		{
			FBangoFrame& Frame = Frames.Last();
			if (!Frame.Slots.IsValidIndex(I.A) || !Frame.Slots.IsValidIndex(I.A + 1) || !Frame.Slots.IsValidIndex(I.B))
			{
				return RuntimeFail(TEXT("Internal error: bad forEach slots."));
			}
			const FBangoValue& Array = Frame.Slots[I.A];
			const FBangoValue& Position = Frame.Slots[I.A + 1];
			if (Array.IsNil() || !Array.GetType().IsArray() || !IsScalar(Position, EBangoBaseType::Int))
			{
				return RuntimeFail(TEXT("Internal error: bad forEach state."));
			}
			const int64 Pos = Position.GetInt();
			if (Pos >= Array.ArrayCount())
			{
				Frame.IP = I.C;
				break;
			}
			FBangoValue Element = *Array.GetElement(Pos);
			Frame.Slots[I.B] = MoveTemp(Element);
			Frame.Slots[I.A + 1] = FBangoValue::MakeInt((int32)(Pos + 1));
			break;
		}

		default:
			return RuntimeFail(TEXT("Internal error: unknown instruction."));
		}
	}
	return Status != EBangoInstanceStatus::Failed;
}

// ---------------------------------------------------------------------------
// Hot reload
// ---------------------------------------------------------------------------

namespace BangoInstancePrivate
{
	/** The simple statement whose code ends at IP, or null. */
	static const FBangoStatementRange* FindStatementEndingAt(const FBangoFunctionCode& Function, int32 IP)
	{
		for (const FBangoStatementRange& Range : Function.Statements)
		{
			if (Range.End == IP)
			{
				return &Range;
			}
		}
		return nullptr;
	}

	static const FBangoStatementRange* FindStatement(const FBangoFunctionCode& Function, int64 NodeID)
	{
		for (const FBangoStatementRange& Range : Function.Statements)
		{
			if (Range.NodeID == NodeID)
			{
				return &Range;
			}
		}
		return nullptr;
	}

	/** The simple statement whose code holds instruction IP, or null. */
	static const FBangoStatementRange* FindStatementHolding(const FBangoFunctionCode& Function, int32 IP)
	{
		for (const FBangoStatementRange& Range : Function.Statements)
		{
			if (IP >= Range.Begin && IP < Range.End)
			{
				return &Range;
			}
		}
		return nullptr;
	}

	/** The one CallFunction in Range's code, or INDEX_NONE if it has none or more than one. */
	static int32 FindOnlyFunctionCall(const FBangoFunctionCode& Function, const FBangoStatementRange& Range)
	{
		int32 Found = INDEX_NONE;
		for (int32 i = Range.Begin; i < Range.End && i < Function.Code.Num(); ++i)
		{
			if (Function.Code[i].Op == EBangoOp::CallFunction)
			{
				if (Found != INDEX_NONE)
				{
					return INDEX_NONE;
				}
				Found = i;
			}
		}
		return Found;
	}

	/** The index of the function called Name in Program (the top level for an empty name), or INDEX_NONE. */
	static int32 FindFunctionByName(const FBangoProgram& Program, const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return Program.Functions.Num() > 0 ? 0 : INDEX_NONE;
		}
		for (int32 i = 1; i < Program.Functions.Num(); ++i)
		{
			if (Program.Functions[i].Name.Equals(Name, ESearchCase::CaseSensitive))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	static FString DescribeFunction(const FBangoFunctionCode& Function)
	{
		return Function.Name.IsEmpty() ? FString(TEXT("the script's main graph")) : FString::Printf(TEXT("function '%s'"), *Function.Name);
	}

	static int64 SlotNode(const FBangoFunctionCode& Function, int32 Slot)
	{
		return Function.SlotNodeIDs.IsValidIndex(Slot) ? Function.SlotNodeIDs[Slot] : -2;
	}
}

bool FBangoInstance::RequestReload(const TSharedPtr<const FBangoProgram>& NewProgram, const FBangoCommandTable& Commands, FString& OutError)
{
	if (!NewProgram.IsValid() || NewProgram->Functions.Num() == 0)
	{
		OutError = TEXT("There is no compiled script to switch to.");
		return false;
	}
	if (!IsRunning())
	{
		OutError = TEXT("The script isn't running.");
		return false;
	}
	TArray<const FBangoCommand*> NewCommands;
	if (!BindCommandsFor(*NewProgram, Commands, NewCommands, OutError))
	{
		return false;
	}
	ReloadProgram = NewProgram;
	ReloadCommands = MoveTemp(NewCommands);
	bReloadTried = false;
	ReloadFailure.Reset();
	ReloadResult = EBangoReloadResult::Pending;
	ReloadMessage.Reset();
	return true;
}

EBangoReloadResult FBangoInstance::TakeReloadResult(FString& OutMessage)
{
	const EBangoReloadResult Result = ReloadResult;
	OutMessage = ReloadMessage;
	if (Result != EBangoReloadResult::Pending)
	{
		ReloadResult = EBangoReloadResult::None;
		ReloadMessage.Reset();
	}
	return Result;
}

void FBangoInstance::FinishReload(EBangoReloadResult Result, const FString& Message)
{
	ReloadMessage = Message; // first: Message may be ReloadFailure, which is cleared below
	ReloadProgram.Reset();
	ReloadCommands.Reset();
	bReloadTried = false;
	ReloadFailure.Reset();
	ReloadResult = Result;
}

bool FBangoInstance::TryReload(FBangoGlobalStore& Globals)
{
	using namespace BangoInstancePrivate;
	if (Frames.Num() == 0 || Status != EBangoInstanceStatus::Ready)
	{
		return false;
	}
	// Only right after a simple statement, with nothing half worked out on the stack (or in a waitUntil's condition).
	const FBangoFrame& Top = Frames.Last();
	if (WaitGuard.Set >= 0 || UnknownGuards.Num() > 0 || PendingUnknowns.Num() > 0 || !Program->Functions.IsValidIndex(Top.Function) || Stack.Num() != Top.StackBase
		|| !FindStatementEndingAt(Program->Functions[Top.Function], Top.IP))
	{
		return false;
	}

	bReloadTried = true;
	TArray<FBangoFrame> NewFrames;
	TArray<FBangoValue> NewScriptSlots;
	FString Reason;
	if (!MakeReloadedFrames(NewFrames, NewScriptSlots, Reason))
	{
		ReloadFailure = Reason;
		return false;
	}

	// The new version's globals, as when an instance starts (a global that already exists keeps its value).
	{
		FString Message;
		int32 GlobalLine = 0;
		if (!DeclareGlobals(*ReloadProgram, Globals, Message, GlobalLine))
		{
			FinishReload(EBangoReloadResult::KeptOld, FString::Printf(TEXT("The new version's global on line %d doesn't fit: %s"), GlobalLine, *Message));
			return false;
		}
	}

	const int64 NodeID = FindStatementEndingAt(Program->Functions[Top.Function], Top.IP)->NodeID;
	Program = ReloadProgram;
	BoundCommands = MoveTemp(ReloadCommands);
	Frames = MoveTemp(NewFrames);
	ScriptSlots = MoveTemp(NewScriptSlots);
	Error.FileName = Program->FileName;
	CurrentFunction = Frames.Last().Function;
	CurrentIP = Frames.Last().IP;
	FinishReload(EBangoReloadResult::Reloaded, FString::Printf(TEXT("Switched to the new version after node %lld."), NodeID));
	return true;
}

bool FBangoInstance::MakeReloadedFrames(TArray<FBangoFrame>& OutFrames, TArray<FBangoValue>& OutScriptSlots, FString& OutReason) const
{
	using namespace BangoInstancePrivate;
	const FBangoProgram& NewProgram = *ReloadProgram;
	if (Frames.Num() > 0 && Frames[0].Function != 0)
	{
		// A spawn block's copy made in a function: its copies of the script's inputs and script variables.
		if (!MakeReloadedSlots(Program->Functions[0], NewProgram.Functions[0], ScriptSlots, OutScriptSlots, OutReason))
		{
			return false;
		}
	}
	for (int32 k = 0; k < Frames.Num(); ++k)
	{
		const FBangoFrame& Old = Frames[k];
		if (!Program->Functions.IsValidIndex(Old.Function))
		{
			OutReason = TEXT("Internal error: bad frame.");
			return false;
		}
		const FBangoFunctionCode& OldFn = Program->Functions[Old.Function];
		const int32 NewIndex = FindFunctionByName(NewProgram, OldFn.Name);
		if (NewIndex == INDEX_NONE)
		{
			OutReason = FString::Printf(TEXT("It's running %s, which the new version doesn't have."), *DescribeFunction(OldFn));
			return false;
		}
		const FBangoFunctionCode& NewFn = NewProgram.Functions[NewIndex];

		FBangoFrame NewFrame;
		NewFrame.Function = NewIndex;
		NewFrame.StackBase = Old.StackBase;

		// Where it carries on.
		if (k == Frames.Num() - 1)
		{
			// The innermost call: after the statement it just finished.
			const FBangoStatementRange* OldRange = FindStatementEndingAt(OldFn, Old.IP);
			const FBangoStatementRange* NewRange = OldRange ? FindStatement(NewFn, OldRange->NodeID) : nullptr;
			if (!NewRange)
			{
				OutReason = FString::Printf(TEXT("Node %lld, where it is in %s, isn't in the new version, or is a different kind of node."),
					OldRange ? OldRange->NodeID : (int64)-1, *DescribeFunction(OldFn));
				return false;
			}
			NewFrame.IP = NewRange->End;
		}
		else
		{
			// A call in progress: it goes back to the same call, in the same node, in the new version.
			const int32 CallIP = Old.IP - 1;
			const FBangoStatementRange* OldRange = FindStatementHolding(OldFn, CallIP);
			if (!OldFn.Code.IsValidIndex(CallIP) || OldFn.Code[CallIP].Op != EBangoOp::CallFunction || !OldRange
				|| FindOnlyFunctionCall(OldFn, *OldRange) != CallIP || Frames[k + 1].StackBase != Old.StackBase)
			{
				const int64 CallNode = OldFn.Debug.IsValidIndex(CallIP) ? OldFn.Debug[CallIP].NodeID : -1;
				OutReason = FString::Printf(TEXT("In %s, the call at node %lld is part of a bigger calculation, so where it goes back to can't be found in the new version. Calls on a line of their own (or the value of an assignment) can be switched."),
					*DescribeFunction(OldFn), CallNode);
				return false;
			}
			const FString& CalleeName = Program->Functions[OldFn.Code[CallIP].A].Name;
			const FBangoStatementRange* NewRange = FindStatement(NewFn, OldRange->NodeID);
			const int32 NewCallIP = NewRange ? FindOnlyFunctionCall(NewFn, *NewRange) : INDEX_NONE;
			if (NewCallIP == INDEX_NONE || !NewProgram.Functions.IsValidIndex(NewFn.Code[NewCallIP].A)
				|| !NewProgram.Functions[NewFn.Code[NewCallIP].A].Name.Equals(CalleeName, ESearchCase::CaseSensitive))
			{
				OutReason = FString::Printf(TEXT("In %s, node %lld (which is calling '%s') isn't in the new version, or doesn't make that call any more."),
					*DescribeFunction(OldFn), OldRange->NodeID, *CalleeName);
				return false;
			}
			NewFrame.IP = NewCallIP + 1;
		}

		if (!MakeReloadedSlots(OldFn, NewFn, Old.Slots, NewFrame.Slots, OutReason))
		{
			return false;
		}
		OutFrames.Add(MoveTemp(NewFrame));
	}
	return true;
}

bool FBangoInstance::MakeReloadedSlots(const FBangoFunctionCode& OldFn, const FBangoFunctionCode& NewFn, const TArray<FBangoValue>& OldSlots, TArray<FBangoValue>& OutSlots, FString& OutReason) const
{
	using namespace BangoInstancePrivate;
	{
		// Locals, parameters and loop variables, by name (and a loop's by its node).
		OutSlots.SetNum(NewFn.SlotTypes.Num());
		for (int32 i = 0; i < NewFn.SlotTypes.Num(); ++i)
		{
			const FString& Name = NewFn.SlotNames[i];
			const int64 Node = SlotNode(NewFn, i);
			int32 OldSlot = INDEX_NONE;
			if (Node != -2)
			{
				for (int32 j = 0; j < OldFn.SlotTypes.Num(); ++j)
				{
					if (SlotNode(OldFn, j) == Node && OldFn.SlotNames[j].Equals(Name, ESearchCase::CaseSensitive))
					{
						OldSlot = j;
						break;
					}
				}
			}
			if (OldSlot != INDEX_NONE)
			{
				if (OldFn.SlotTypes[OldSlot] != NewFn.SlotTypes[i])
				{
					OutReason = FString::Printf(TEXT("In %s, '%s' was %s and is %s in the new version."),
						*DescribeFunction(OldFn), *Name, *OldFn.SlotTypes[OldSlot].ToString(), *NewFn.SlotTypes[i].ToString());
					return false;
				}
				OutSlots[i] = OldSlots.IsValidIndex(OldSlot) ? OldSlots[OldSlot] : FBangoValue::MakeNil(NewFn.SlotTypes[i]);
				continue;
			}
			if (i < NewFn.NumParams)
			{
				OutReason = FString::Printf(TEXT("The new version of %s has a new parameter, '%s', and there's no value for it."), *DescribeFunction(OldFn), *Name);
				return false;
			}
			if (Node >= 0)
			{
				// A local's declaration node has one slot, a loop has its variable and its bookkeeping. A local renamed
				// in its node is a new local, which starts as nil like any other the new version adds.
				int32 OldNodeSlots = 0;
				int32 NewNodeSlots = 0;
				for (int32 j = 0; j < OldFn.SlotTypes.Num(); ++j)
				{
					OldNodeSlots += SlotNode(OldFn, j) == Node ? 1 : 0;
				}
				for (int32 j = 0; j < NewFn.SlotTypes.Num(); ++j)
				{
					NewNodeSlots += SlotNode(NewFn, j) == Node ? 1 : 0;
				}
				if (OldNodeSlots > 0 && !(OldNodeSlots == 1 && NewNodeSlots == 1))
				{
					// The loop is the same, but its variable changed: a loop under way can't be carried over.
					OutReason = FString::Printf(TEXT("In %s, the loop at node %lld has a new variable, '%s'."), *DescribeFunction(OldFn), Node, *Name);
					return false;
				}
			}
			// A start value of self is the owner's Bango ID, as when the local's declaration runs (nil if there's no owner).
			if (!NewFn.SlotStartValues.IsValidIndex(i) || !ResolveSelf(NewFn.SlotStartValues[i], OutSlots[i]))
			{
				OutSlots[i] = FBangoValue::MakeNil(NewFn.SlotTypes[i]);
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Saving and loading
// ---------------------------------------------------------------------------

void FBangoInstance::Serialize(FArchive& Ar)
{
	int32 Version = InstanceSaveVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != InstanceSaveVersion)
	{
		Ar.SetError();
		return;
	}

	Ar << Id;
	Ar << Owner.BangoName;
	Ar << Owner.Problem;
	uint8 StatusByte = (uint8)Status;
	Ar << StatusByte;
	SerializeError(Ar, Error);

	int32 FrameCount = Frames.Num();
	Ar << FrameCount;
	if (Ar.IsLoading())
	{
		if (FrameCount < 0 || FrameCount > 100000)
		{
			Ar.SetError();
			return;
		}
		Frames.Reset();
		Frames.SetNum(FrameCount);
	}
	for (FBangoFrame& Frame : Frames)
	{
		Ar << Frame.Function;
		Ar << Frame.IP;
		Ar << Frame.StackBase;
		Ar << Frame.Slots;
	}
	Ar << ScriptSlots;

	Ar << Stack;
	Ar << PendingCommandName;
	Ar << PendingCall.LatentState;
	Ar << PendingCall.TypeArgs; // what a pending command's result must be
	Ar << PendingCall.bResultUsed;
	Ar << WaitSet;
	Ar << WaitTimeLeft;
	Ar << WaitHoldTime;
	Ar << WaitHoldLeft;
	Ar << bWaitIgnoreHold;

	int32 NumGuards = UnknownGuards.Num();
	Ar << NumGuards;
	if (Ar.IsLoading())
	{
		UnknownGuards.SetNum(FMath::Clamp(NumGuards, 0, 4096));
	}
	for (FUnknownGuard& Guard : UnknownGuards)
	{
		Ar << Guard.NumFrames;
		Ar << Guard.StackSize;
		Ar << Guard.HandlerIP;
		Ar << Guard.NumPending;
	}
	int32 NumPending = PendingUnknowns.Num();
	Ar << NumPending;
	if (Ar.IsLoading())
	{
		PendingUnknowns.SetNum(FMath::Clamp(NumPending, 0, 4096));
	}
	for (FPendingUnknown& Pending : PendingUnknowns)
	{
		Ar << Pending.NumFrames;
		Ar << Pending.HandlerIP;
		Ar << Pending.Message;
		Ar << Pending.Function;
		Ar << Pending.IP;
	}

	if (Ar.IsLoading())
	{
		Status = StatusByte <= (uint8)EBangoInstanceStatus::Terminated ? (EBangoInstanceStatus)StatusByte : EBangoInstanceStatus::Failed;
		PendingCall.Args.Reset();
		PendingCall.Target = FBangoValue();
		PendingCall.Result = FBangoValue();
		PendingCall.Error.Reset();
		WaitStamp = 0;
		WaitGuard = FWaitGuard();
		bWaitTimedOut = false;
		bUnknownCaught = false;
		Program.Reset();
		BoundCommands.Reset();
	}
}

bool FBangoInstance::Bind(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FString& OutError)
{
	Program = InProgram;
	if (!Program.IsValid())
	{
		OutError = TEXT("No program to bind.");
		return false;
	}
	Error.FileName = Program->FileName;
	if (!BindCommands(Commands, OutError))
	{
		return false;
	}

	int32 PreviousBase = 0;
	for (const FBangoFrame& Frame : Frames)
	{
		if (!Program->Functions.IsValidIndex(Frame.Function)
			|| Frame.IP < 0 || Frame.IP > Program->Functions[Frame.Function].Code.Num()
			|| Frame.Slots.Num() != Program->Functions[Frame.Function].SlotTypes.Num()
			|| Frame.StackBase < PreviousBase || Frame.StackBase > Stack.Num())
		{
			OutError = TEXT("The saved script state doesn't match the script. Was the script changed since the game was saved?");
			return false;
		}
		PreviousBase = Frame.StackBase;
	}
	if (Frames.Num() > 0 && Frames[0].Function != 0 && ScriptSlots.Num() != Program->Functions[0].SlotTypes.Num())
	{
		OutError = TEXT("The saved script state doesn't match the script. Was the script changed since the game was saved?");
		return false;
	}
	if (IsRunning() && Frames.Num() == 0)
	{
		OutError = TEXT("The saved script state is empty.");
		return false;
	}
	if (Status == EBangoInstanceStatus::Waiting && WaitSet >= 0)
	{
		if (!Program->WaitSets.IsValidIndex(WaitSet))
		{
			OutError = TEXT("The saved script state doesn't match the script. Was the script changed since the game was saved?");
			return false;
		}
	}
	else if (Status == EBangoInstanceStatus::Waiting)
	{
		const FBangoCommand* Command = BoundCommands.IsValidIndex(PendingCommandName) ? BoundCommands[PendingCommandName] : nullptr;
		if (!Command || !Command->Poll)
		{
			OutError = TEXT("The saved script was waiting on a command that doesn't exist or can't be polled.");
			return false;
		}
	}
	return true;
}
