#include "BangoInstance.h"

#include "Serialization/Archive.h"

namespace BangoInstancePrivate
{
	constexpr int32 SaveVersion = 2; // 2: ints are 32-bit

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
			if (Instruction.Op != EBangoOp::CallCommand)
			{
				continue;
			}
			if (!InProgram.Names.IsValidIndex(Instruction.A))
			{
				OutError = TEXT("The compiled script is damaged (bad command name).");
				return false;
			}
			const FString& Name = InProgram.Names[Instruction.A];
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
			OutBound[Instruction.A] = Command;
		}
	}
	return true;
}

bool FBangoInstance::Start(const TSharedPtr<const FBangoProgram>& InProgram, const FBangoCommandTable& Commands, FBangoGlobalStore& Globals, int64 InId, FBangoRuntimeError& OutError,
	TConstArrayView<FBangoValue> Args)
{
	Program = InProgram;
	Id = InId;
	Error = FBangoRuntimeError();
	Frames.Reset();
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

	for (const FBangoGlobalDecl& Global : Program->Globals)
	{
		if (!Globals.Declare(Global.Name, Global.Type, Message))
		{
			Error.Line = Global.Line;
			Error.Column = 1;
			Error.Message = Message;
			OutError = Error;
			return false;
		}
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

	if (Params.Num() == 0 && Args.Num() > 0)
	{
		return ArgFail(FString::Printf(TEXT("This script takes no arguments (it has no parameters), but %d were passed."), Args.Num()));
	}
	if (Args.Num() > Params.Num())
	{
		return ArgFail(FString::Printf(TEXT("Too many arguments: this script takes at most %d, but %d were passed."), Params.Num(), Args.Num()));
	}
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
			OutValues.Add(Param.Default);
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

bool FBangoInstance::RuntimeFail(const FString& Message)
{
	if (Status == EBangoInstanceStatus::Failed)
	{
		return false;
	}
	Status = EBangoInstanceStatus::Failed;
	Error.FileName = Program.IsValid() ? Program->FileName : FString();
	Error.Message = Message;
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
	const int32 IP = Status == EBangoInstanceStatus::Waiting ? Frame.IP - 1 : Frame.IP;
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
		// A frame further out has run its call, so its IP is past it; the innermost one is past its latent command while waiting.
		const bool bInnermost = k == Frames.Num() - 1;
		const int32 IP = !bInnermost || Status == EBangoInstanceStatus::Waiting ? Frame.IP - 1 : Frame.IP;
		if (Function.Debug.IsValidIndex(IP))
		{
			Info.Loc = Function.Debug[IP];
		}
		OutFrames.Add(Info);
	}
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
	if (Status != EBangoInstanceStatus::Waiting || !Program.IsValid() || !Program->Names.IsValidIndex(PendingCommandName))
	{
		return FString();
	}
	return Program->Names[PendingCommandName];
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
	if (!Context.Globals)
	{
		RuntimeFail(TEXT("No global variable store was given to run the script with."));
		return Status;
	}

	if (Status == EBangoInstanceStatus::Waiting)
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

	Execute(Context);
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
	return Stack.Pop();
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

FString FBangoInstance::DescribePlace(const FBangoPlace& Place) const
{
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		const FBangoFunctionCode& Function = Program->Functions[Frames.Last().Function];
		return Function.SlotNames.IsValidIndex(Place.Index) ? Function.SlotNames[Place.Index] : FString(TEXT("<local>"));
	}
	return Program->Names.IsValidIndex(Place.Index) ? Program->Names[Place.Index] : FString(TEXT("<global>"));
}

bool FBangoInstance::LoadPlace(const FBangoPlace& Place, FBangoGlobalStore& Globals, const IBangoEntityResolver* Entities, bool bNilOk)
{
	TArray<int64> Indexes;
	if (!PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	if (bNilOk && Place.Part < 0)
	{
		// Like IsNil: nil anywhere along the way (or a global nobody made, or past the end) gives nil.
		const FBangoValue* Root = nullptr;
		if (Place.Kind == EBangoPlaceKind::Local)
		{
			FBangoFrame& Frame = Frames.Last();
			if (!Frame.Slots.IsValidIndex(Place.Index))
			{
				return RuntimeFail(TEXT("Internal error: bad local slot."));
			}
			Root = &Frame.Slots[Place.Index];
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

	const FString RootName = DescribePlace(Place);
	const FBangoValue* Current = nullptr;
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		FBangoFrame& Frame = Frames.Last();
		if (!Frame.Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Current = &Frame.Slots[Place.Index];
	}
	else
	{
		Current = Globals.Find(RootName);
		if (!Current)
		{
			if (Entities && Entities->IsEntityRegistered(RootName))
			{
				return RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist. There is an actor with the Bango ID '%s': write @%s to use it."), *RootName, *RootName, *RootName));
			}
			return RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist. No script or Blueprint has declared it."), *RootName));
		}
	}

	FString Path = RootName;
	for (const int64 Index : Indexes)
	{
		if (Current->IsNil())
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil, so it can't be indexed."), *Path));
		}
		if (!Current->GetType().IsArray())
		{
			return RuntimeFail(FString::Printf(TEXT("'%s' is %s, not an array, so it can't be indexed."), *Path, *ValueTypeText(*Current)));
		}
		const FBangoValue* Next = Current->GetElement(Index);
		if (!Next)
		{
			if (Index < 0)
			{
				return RuntimeFail(FString::Printf(TEXT("Index %lld for '%s' is negative."), Index, *Path));
			}
			return RuntimeFail(FString::Printf(TEXT("'%s[%lld]' is past the end of '%s', which has %lld element(s). Check with IsNil first."),
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
			const FString Owner = Function.Name.IsEmpty() ? FString(TEXT("this script")) : FString::Printf(TEXT("function '%s'"), *Function.Name);
			return RuntimeFail(FString::Printf(TEXT("'%s' is nil. It's a parameter of %s; check what the caller passed."), *Path, *Owner));
		}
		if (Place.Kind == EBangoPlaceKind::Global && Indexes.Num() == 0)
		{
			return RuntimeFail(FString::Printf(TEXT("Global '%s' is nil. Give it a value before reading it, or check it with IsNil(%s)."), *Path, *Path));
		}
		return RuntimeFail(FString::Printf(TEXT("'%s' is nil. Give it a value before reading it, or check it with IsNil(%s)."), *Path, *Path));
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

	const FString RootName = DescribePlace(Place);
	FBangoValue* Root = nullptr;
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		FBangoFrame& Frame = Frames.Last();
		if (!Frame.Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Root = &Frame.Slots[Place.Index];
	}
	else
	{
		Root = Globals.FindMutable(RootName);
		if (!Root)
		{
			return RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist."), *RootName));
		}
	}

	FString Message;
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
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		FBangoFrame& Frame = Frames.Last();
		if (!Frame.Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Root = &Frame.Slots[Place.Index];
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

bool FBangoInstance::ArrayModify(const FBangoPlace& Place, int32 Kind, bool bResultUsed, FBangoGlobalStore& Globals)
{
	const EBangoArrayModify Op = (EBangoArrayModify)Kind;
	const TCHAR* OpName = Op == EBangoArrayModify::Push ? TEXT("Push") : Op == EBangoArrayModify::InsertAt ? TEXT("InsertAt") : Op == EBangoArrayModify::Pop ? TEXT("Pop") : TEXT("RemoveAt");

	FBangoValue Value;
	int64 Index = 0;
	if (Op == EBangoArrayModify::InsertAt || Op == EBangoArrayModify::RemoveAt)
	{
		const FBangoValue IndexValue = PopValue();
		if (Status == EBangoInstanceStatus::Failed)
		{
			return false;
		}
		if (!IsScalar(IndexValue, EBangoBaseType::Int))
		{
			return RuntimeFail(FString::Printf(TEXT("The index for %s must be an int, but it is %s."), OpName, *ValueTypeText(IndexValue)));
		}
		Index = IndexValue.GetInt();
	}
	if (Op == EBangoArrayModify::Push || Op == EBangoArrayModify::InsertAt)
	{
		Value = PopValue();
	}
	TArray<int64> Indexes;
	if (Status == EBangoInstanceStatus::Failed || !PopIndexes(Place.NumIndexes, Indexes))
	{
		return false;
	}

	const FString RootName = DescribePlace(Place);
	FBangoValue* Target = nullptr;
	if (Place.Kind == EBangoPlaceKind::Local)
	{
		FBangoFrame& Frame = Frames.Last();
		if (!Frame.Slots.IsValidIndex(Place.Index))
		{
			return RuntimeFail(TEXT("Internal error: bad local slot."));
		}
		Target = &Frame.Slots[Place.Index];
	}
	else
	{
		Target = Globals.FindMutable(RootName);
		if (!Target)
		{
			return RuntimeFail(FString::Printf(TEXT("Global '%s' doesn't exist."), *RootName));
		}
	}

	FString Message;
	if (Indexes.Num() > 0)
	{
		Target = Target->FindElementAtPath(Indexes, Message);
		if (!Target)
		{
			return RuntimeFail(FString::Printf(TEXT("%s on '%s': %s"), OpName, *RootName, *Message));
		}
	}

	FBangoValue Removed;
	bool bOk = false;
	switch (Op)
	{
	case EBangoArrayModify::Push:     bOk = Target->Push(Value, Message); break;
	case EBangoArrayModify::InsertAt: bOk = Target->InsertAt(Index, Value, Message); break;
	case EBangoArrayModify::Pop:      bOk = Target->Pop(Removed, Message); break;
	case EBangoArrayModify::RemoveAt: bOk = Target->RemoveAt(Index, Removed, Message); break;
	default:                          Message = TEXT("Internal error: unknown array command."); break;
	}
	if (!bOk)
	{
		return RuntimeFail(FString::Printf(TEXT("%s on '%s': %s"), OpName, *RootName, *Message));
	}

	if (bResultUsed)
	{
		if (Removed.IsNil())
		{
			return RuntimeFail(FString::Printf(TEXT("%s on '%s' removed a nil element, so its value can't be used."), OpName, *RootName));
		}
		Stack.Add(MoveTemp(Removed));
	}
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

bool FBangoInstance::CallCommand(int32 NameIndex, int32 ArgCount, bool bHasTarget, const FBangoExecContext& Context)
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
	if (Status == EBangoInstanceStatus::Failed)
	{
		return false;
	}

	// Number parameters take an int or a float; an undeclared global passed to one is only checked here.
	for (int32 i = 0; i < Call.Args.Num() && i < Signature.Params.Num(); ++i)
	{
		const FBangoValue& Arg = Call.Args[i];
		if (Signature.Params[i].bNumber && (Arg.IsNil() || Arg.GetType().IsArray()
			|| (Arg.GetType().Base != EBangoBaseType::Int && Arg.GetType().Base != EBangoBaseType::Double)))
		{
			return RuntimeFail(FString::Printf(TEXT("%s: argument %d ('%s') must be a number (an int or a float), but it is %s."),
				*Signature.Name, i + 1, *Signature.Params[i].Name, *ValueTypeText(Arg)));
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
			return RuntimeFail(FString::Printf(TEXT("%s: no actor is registered with the Bango ID name '%s'. Check the name on the actor's Bango ID component, and that the actor is in the level."),
				*Signature.Name, *Value.GetEntityName()));
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
		if (Signature.bReturnsNumber)
		{
			// Its result's type comes from the arguments, which aren't kept while a command is pending.
			return RuntimeFail(FString::Printf(TEXT("Command '%s' returns a number (ReturnsNumber), so it can't be latent, but it returned Pending."), *Signature.Name));
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

	if (Signature.bReturnsNumber)
	{
		// The compiler expects an int if every number argument was an int, and a float otherwise (like +).
		const FBangoType Expected = Signature.NumberResultIsInt(PendingCall.Args) ? FBangoType::Int() : FBangoType::Double();
		FBangoValue Converted;
		if (PendingCall.Result.IsNil() || !PendingCall.Result.TryConvertTo(Expected, Converted))
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
			Stack.Add(Program->Constants[I.A]);
			break;

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
		case EBangoOp::ArrayModify:
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
			default:                       ArrayModify(Place, I.B, I.C != 0, Globals); break;
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
			if (ArrayValue.IsNil() || !ArrayValue.GetType().IsArray())
			{
				return RuntimeFail(FString::Printf(TEXT("Only arrays can be indexed, but this is %s."), *ValueTypeText(ArrayValue)));
			}
			if (!IsScalar(IndexValue, EBangoBaseType::Int))
			{
				return RuntimeFail(FString::Printf(TEXT("An array index must be an int, but it is %s."), *ValueTypeText(IndexValue)));
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
			Stack.SetNum(First);
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
			Stack.SetNum(First);
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
				Stack.Pop();
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
			Stack.SetNum(First);
			NewFrame.StackBase = Stack.Num();
			Frames.Add(MoveTemp(NewFrame));
			break;
		}

		case EBangoOp::CallCommand:
			CallCommand(I.A, I.B, I.C != 0, Context);
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
			Stack.SetNum(FMath::Min(Stack.Num(), Frames.Last().StackBase));
			Frames.Pop();
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

		case EBangoOp::ArrayCount:
		{
			const FBangoValue Value = PopValue();
			if (Status == EBangoInstanceStatus::Failed)
			{
				return false;
			}
			if (Value.IsNil() || !Value.GetType().IsArray())
			{
				return RuntimeFail(FString::Printf(TEXT("Count needs an array, but got %s."), *ValueTypeText(Value)));
			}
			Stack.Add(FBangoValue::MakeInt((int32)Value.ArrayCount()));
			break;
		}

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
	// Only right after a simple statement, with nothing half worked out on the stack.
	const FBangoFrame& Top = Frames.Last();
	if (!Program->Functions.IsValidIndex(Top.Function) || Stack.Num() != Top.StackBase
		|| !FindStatementEndingAt(Program->Functions[Top.Function], Top.IP))
	{
		return false;
	}

	bReloadTried = true;
	TArray<FBangoFrame> NewFrames;
	FString Reason;
	if (!MakeReloadedFrames(NewFrames, Reason))
	{
		ReloadFailure = Reason;
		return false;
	}

	// The new version's globals, as when an instance starts (a global that already exists keeps its value).
	for (const FBangoGlobalDecl& Global : ReloadProgram->Globals)
	{
		FString Message;
		if (!Globals.Declare(Global.Name, Global.Type, Message))
		{
			FinishReload(EBangoReloadResult::KeptOld, FString::Printf(TEXT("The new version's global '%s' doesn't fit: %s"), *Global.Name, *Message));
			return false;
		}
	}

	const int64 NodeID = FindStatementEndingAt(Program->Functions[Top.Function], Top.IP)->NodeID;
	Program = ReloadProgram;
	BoundCommands = MoveTemp(ReloadCommands);
	Frames = MoveTemp(NewFrames);
	Error.FileName = Program->FileName;
	CurrentFunction = Frames.Last().Function;
	CurrentIP = Frames.Last().IP;
	FinishReload(EBangoReloadResult::Reloaded, FString::Printf(TEXT("Switched to the new version after node %lld."), NodeID));
	return true;
}

bool FBangoInstance::MakeReloadedFrames(TArray<FBangoFrame>& OutFrames, FString& OutReason) const
{
	using namespace BangoInstancePrivate;
	const FBangoProgram& NewProgram = *ReloadProgram;
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

		// Locals, parameters and loop variables, by name (and a loop's by its node).
		NewFrame.Slots.SetNum(NewFn.SlotTypes.Num());
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
				NewFrame.Slots[i] = Old.Slots.IsValidIndex(OldSlot) ? Old.Slots[OldSlot] : FBangoValue::MakeNil(NewFn.SlotTypes[i]);
				continue;
			}
			if (i < NewFn.NumParams)
			{
				OutReason = FString::Printf(TEXT("The new version of %s has a new parameter, '%s', and there's no value for it."), *DescribeFunction(OldFn), *Name);
				return false;
			}
			if (Node >= 0)
			{
				for (int32 j = 0; j < OldFn.SlotTypes.Num(); ++j)
				{
					if (SlotNode(OldFn, j) == Node)
					{
						// The loop is the same, but its variable changed: a loop under way can't be carried over.
						OutReason = FString::Printf(TEXT("In %s, the loop at node %lld has a new variable, '%s'."), *DescribeFunction(OldFn), Node, *Name);
						return false;
					}
				}
			}
			NewFrame.Slots[i] = NewFn.SlotStartValues.IsValidIndex(i) ? NewFn.SlotStartValues[i] : FBangoValue::MakeNil(NewFn.SlotTypes[i]);
		}
		OutFrames.Add(MoveTemp(NewFrame));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Saving and loading
// ---------------------------------------------------------------------------

void FBangoInstance::Serialize(FArchive& Ar)
{
	int32 Version = SaveVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != SaveVersion)
	{
		Ar.SetError();
		return;
	}

	Ar << Id;
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

	Ar << Stack;
	Ar << PendingCommandName;
	Ar << PendingCall.LatentState;

	if (Ar.IsLoading())
	{
		Status = StatusByte <= (uint8)EBangoInstanceStatus::Terminated ? (EBangoInstanceStatus)StatusByte : EBangoInstanceStatus::Failed;
		PendingCall.Args.Reset();
		PendingCall.Target = FBangoValue();
		PendingCall.Result = FBangoValue();
		PendingCall.Error.Reset();
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
	if (IsRunning() && Frames.Num() == 0)
	{
		OutError = TEXT("The saved script state is empty.");
		return false;
	}
	if (Status == EBangoInstanceStatus::Waiting)
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
