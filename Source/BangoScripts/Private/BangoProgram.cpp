#include "BangoProgram.h"

namespace BangoProgramPrivate
{
	static FString DescribePlace(const FBangoProgram& Program, const FBangoFunctionCode& Function, int32 PlaceIndex)
	{
		if (!Function.Places.IsValidIndex(PlaceIndex))
		{
			return TEXT("<bad place>");
		}
		const FBangoPlace& Place = Function.Places[PlaceIndex];
		FString Result;
		if (Place.Kind == EBangoPlaceKind::Local)
		{
			Result = Function.SlotNames.IsValidIndex(Place.Index) ? Function.SlotNames[Place.Index] : FString(TEXT("<bad slot>"));
		}
		else
		{
			Result = Program.Names.IsValidIndex(Place.Index) ? Program.Names[Place.Index] : FString(TEXT("<bad name>"));
		}
		for (int32 i = 0; i < Place.NumIndexes; ++i)
		{
			Result += i == Place.NumIndexes - 1 && Place.Part == FBangoPlace::ElementPart ? TEXT("[part]") : TEXT("[]");
		}
		if (Place.Part >= 0 && Place.Part < 3)
		{
			Result += Place.Part == 0 ? TEXT(".x") : Place.Part == 1 ? TEXT(".y") : TEXT(".z");
		}
		return Result;
	}

	static FString DescribeType(const FBangoProgram& Program, int32 TypeIndex)
	{
		return Program.Types.IsValidIndex(TypeIndex) ? Program.Types[TypeIndex].ToString() : FString(TEXT("<bad type>"));
	}

	static FString DescribeSlot(const FBangoFunctionCode& Function, int32 Slot)
	{
		return Function.SlotNames.IsValidIndex(Slot) ? Function.SlotNames[Slot] : FString(TEXT("<bad slot>"));
	}

	static FString DescribeOperands(const FBangoProgram& Program, const FBangoFunctionCode& Function, const FBangoInstruction& I)
	{
		switch (I.Op)
		{
		case EBangoOp::PushConst:
			return Program.Constants.IsValidIndex(I.A) ? Program.Constants[I.A].ToDebugString() : FString(TEXT("<bad constant>"));
		case EBangoOp::PushNil:
		case EBangoOp::Convert:
			return DescribeType(Program, I.A);
		case EBangoOp::DupN:
			return FString::Printf(TEXT("%d"), I.A);
		case EBangoOp::LoadPlace:
			return DescribePlace(Program, Function, I.A) + (I.B != 0 ? TEXT(" (nil ok)") : TEXT(""));
		case EBangoOp::StorePlace:
		case EBangoOp::IsNilPlace:
			return DescribePlace(Program, Function, I.A);
		case EBangoOp::MakeArray:
			return FString::Printf(TEXT("%d %s"), I.A, *DescribeType(Program, I.B));
		case EBangoOp::MakeVector:
			return FString::Printf(TEXT("%d"), I.A);
		case EBangoOp::GetPart:
			return I.A == 0 ? TEXT("x") : I.A == 1 ? TEXT("y") : TEXT("z");
		case EBangoOp::Jump:
		case EBangoOp::JumpIfFalse:
		case EBangoOp::JumpIfFalseOrPop:
		case EBangoOp::JumpIfTrueOrPop:
		case EBangoOp::SpawnBlock:
			return FString::Printf(TEXT("-> %d"), I.A);
		case EBangoOp::CallFunction:
			return FString::Printf(TEXT("%s %d"), Program.Functions.IsValidIndex(I.A) ? *Program.Functions[I.A].Name : TEXT("<bad function>"), I.B);
		case EBangoOp::CallCommand:
			return FString::Printf(TEXT("%s %d%s"), Program.Names.IsValidIndex(I.A) ? *Program.Names[I.A] : TEXT("<bad name>"), I.B, (I.C & Bango::CallHasTarget) ? TEXT(" +target") : TEXT(""));
		case EBangoOp::CallReflected:
			return FString::Printf(TEXT("%s %d%s"), Program.Names.IsValidIndex(I.A) ? *Program.Names[I.A] : TEXT("<bad name>"), I.B, I.C ? TEXT(" +result") : TEXT(""));
		case EBangoOp::CallCommandInPlace:
			return FString::Printf(TEXT("%s %s %d%s"), *DescribePlace(Program, Function, I.A), Program.Names.IsValidIndex(I.B) ? *Program.Names[I.B] : TEXT("<bad name>"),
				I.C & Bango::InPlaceArgCountMask, ((I.C >> Bango::InPlaceFlagShift) & Bango::CallResultUsed) ? TEXT(" +result") : TEXT(""));
		case EBangoOp::ForEachNext:
			return FString::Printf(TEXT("%s %s -> %d"), *DescribeSlot(Function, I.A), *DescribeSlot(Function, I.B), I.C);
		case EBangoOp::WaitBegin:
		case EBangoOp::WaitCheck:
			return FString::Printf(TEXT("#%d"), I.A);
		case EBangoOp::WaitResult:
			return FString::Printf(TEXT("#%d -> %d"), I.A, I.B);
		default:
			return FString();
		}
	}
}

namespace Bango
{
	const TCHAR* GetOpName(EBangoOp Op)
	{
		switch (Op)
		{
		case EBangoOp::Nop:                return TEXT("Nop");
		case EBangoOp::PushConst:          return TEXT("PushConst");
		case EBangoOp::PushNil:            return TEXT("PushNil");
		case EBangoOp::Pop:                return TEXT("Pop");
		case EBangoOp::DupN:               return TEXT("DupN");
		case EBangoOp::LoadPlace:          return TEXT("LoadPlace");
		case EBangoOp::StorePlace:         return TEXT("StorePlace");
		case EBangoOp::IsNilPlace:         return TEXT("IsNilPlace");
		case EBangoOp::Index:              return TEXT("Index");
		case EBangoOp::Convert:            return TEXT("Convert");
		case EBangoOp::MakeArray:          return TEXT("MakeArray");
		case EBangoOp::MakeVector:         return TEXT("MakeVector");
		case EBangoOp::GetPart:            return TEXT("GetPart");
		case EBangoOp::Not:                return TEXT("Not");
		case EBangoOp::Negate:             return TEXT("Negate");
		case EBangoOp::Add:                return TEXT("Add");
		case EBangoOp::Subtract:           return TEXT("Subtract");
		case EBangoOp::Multiply:           return TEXT("Multiply");
		case EBangoOp::Divide:             return TEXT("Divide");
		case EBangoOp::Modulo:             return TEXT("Modulo");
		case EBangoOp::Less:               return TEXT("Less");
		case EBangoOp::LessEqual:          return TEXT("LessEqual");
		case EBangoOp::Greater:            return TEXT("Greater");
		case EBangoOp::GreaterEqual:       return TEXT("GreaterEqual");
		case EBangoOp::Equal:              return TEXT("Equal");
		case EBangoOp::NotEqual:           return TEXT("NotEqual");
		case EBangoOp::Jump:               return TEXT("Jump");
		case EBangoOp::JumpIfFalse:        return TEXT("JumpIfFalse");
		case EBangoOp::JumpIfFalseOrPop:   return TEXT("JumpIfFalseOrPop");
		case EBangoOp::JumpIfTrueOrPop:    return TEXT("JumpIfTrueOrPop");
		case EBangoOp::CallFunction:       return TEXT("CallFunction");
		case EBangoOp::CallCommand:        return TEXT("CallCommand");
		case EBangoOp::CallReflected:      return TEXT("CallReflected");
		case EBangoOp::Return:             return TEXT("Return");
		case EBangoOp::ReturnValue:        return TEXT("ReturnValue");
		case EBangoOp::Halt:               return TEXT("Halt");
		case EBangoOp::ErrorMissingReturn: return TEXT("ErrorMissingReturn");
		case EBangoOp::SpawnBlock:         return TEXT("SpawnBlock");
		case EBangoOp::CallCommandInPlace: return TEXT("CallCommandInPlace");
		case EBangoOp::ForEachNext:        return TEXT("ForEachNext");
		case EBangoOp::WaitBegin:          return TEXT("WaitBegin");
		case EBangoOp::WaitCheck:          return TEXT("WaitCheck");
		case EBangoOp::WaitResult:         return TEXT("WaitResult");
		default:                           return TEXT("?");
		}
	}

	FBangoDependencies FindDependencies(const FBangoProgram& Program, int32 Function, int32 Begin, int32 End)
	{
		FBangoDependencies Result;
		TSet<int32> Visited;
		Visited.Add(Function);
		auto Scan = [&](auto&& Self, int32 FunctionIndex, int32 From, int32 To) -> void
		{
			if (!Program.Functions.IsValidIndex(FunctionIndex))
			{
				return;
			}
			const FBangoFunctionCode& Code = Program.Functions[FunctionIndex];
			for (int32 i = FMath::Max(From, 0); i < To && i < Code.Code.Num(); ++i)
			{
				const FBangoInstruction& I = Code.Code[i];
				switch (I.Op)
				{
				case EBangoOp::LoadPlace:
				case EBangoOp::IsNilPlace:
				case EBangoOp::CallCommandInPlace:
					if (I.Op == EBangoOp::CallCommandInPlace && ((I.C >> Bango::InPlaceFlagShift) & Bango::CallIsPure) == 0
						&& Result.PollReason.IsEmpty() && Program.Names.IsValidIndex(I.B))
					{
						Result.PollReason = FString::Printf(TEXT("it calls %s"), *Program.Names[I.B]);
					}
					if (Code.Places.IsValidIndex(I.A))
					{
						const FBangoPlace& Place = Code.Places[I.A];
						if (Place.Kind == EBangoPlaceKind::Global && Program.Names.IsValidIndex(Place.Index))
						{
							Result.Globals.AddUnique(Program.Names[Place.Index]);
						}
						else if (Place.Kind == EBangoPlaceKind::Local && FunctionIndex == 0 && Place.Index < Code.NumParams && Code.SlotNames.IsValidIndex(Place.Index))
						{
							Result.Inputs.AddUnique(Code.SlotNames[Place.Index]);
						}
					}
					break;
				case EBangoOp::CallFunction:
					if (Program.Functions.IsValidIndex(I.A) && !Visited.Contains(I.A))
					{
						Visited.Add(I.A);
						Self(Self, I.A, 0, Program.Functions[I.A].Code.Num());
					}
					break;
				case EBangoOp::CallCommand:
					if ((I.C & Bango::CallIsPure) != 0)
					{
						break; // its answer only changes with its arguments
					}
					[[fallthrough]];
				case EBangoOp::CallReflected:
					if (Result.PollReason.IsEmpty() && Program.Names.IsValidIndex(I.A))
					{
						Result.PollReason = FString::Printf(TEXT("it calls %s"), *Program.Names[I.A]);
					}
					break;
				default:
					break;
				}
			}
		};
		Scan(Scan, Function, Begin, End);
		Result.Globals.Sort();

		// Inputs in their order.
		if (Program.Functions.IsValidIndex(0))
		{
			const FBangoFunctionCode& Main = Program.Functions[0];
			TArray<FString> Ordered;
			for (int32 Slot = 0; Slot < Main.NumParams && Slot < Main.SlotNames.Num(); ++Slot)
			{
				if (Result.Inputs.Contains(Main.SlotNames[Slot]))
				{
					Ordered.Add(Main.SlotNames[Slot]);
				}
			}
			Result.Inputs = MoveTemp(Ordered);
		}
		return Result;
	}

	FString Disassemble(const FBangoProgram& Program)
	{
		FString Result;
		if (Program.bHasParamsLine)
		{
			TArray<FString> Parts;
			for (const FBangoScriptParamInfo& Param : Program.Params)
			{
				FString Part = FString::Printf(TEXT("%s %s"), *Param.Type.ToString(), *Param.Name);
				if (Param.bHasDefault)
				{
					Part += TEXT(" = ");
					Part += Param.Default.ToDebugString();
				}
				Parts.Add(Part);
			}
			Result += FString::Printf(TEXT("inputs: %s;\n"), *FString::Join(Parts, TEXT(", ")));
		}
		if (Program.Globals.Num() > 0)
		{
			Result += TEXT("globals:");
			for (const FBangoGlobalDecl& Global : Program.Globals)
			{
				Result += FString::Printf(TEXT(" %s %s;"), *Global.Type.ToString(), *Global.Name);
			}
			Result += TEXT("\n");
		}
		for (int32 i = 0; i < Program.WaitSets.Num(); ++i)
		{
			const FBangoWaitSet& Set = Program.WaitSets[i];
			Result += FString::Printf(TEXT("wait #%d: %s%s\n"), i, Set.Globals.Num() > 0 ? *FString::Join(Set.Globals, TEXT(", ")) : TEXT("(no globals)"),
				Set.PollReason.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; polled: %s"), *Set.PollReason));
		}

		for (const FBangoFunctionCode& Function : Program.Functions)
		{
			Result += FString::Printf(TEXT("function %s:\n"), Function.Name.IsEmpty() ? TEXT("<main>") : *Function.Name);
			for (int32 i = 0; i < Function.Code.Num(); ++i)
			{
				const FBangoInstruction& I = Function.Code[i];
				const FString Operands = BangoProgramPrivate::DescribeOperands(Program, Function, I);
				if (Operands.IsEmpty())
				{
					Result += FString::Printf(TEXT("  %d %s\n"), i, GetOpName(I.Op));
				}
				else
				{
					Result += FString::Printf(TEXT("  %d %s %s\n"), i, GetOpName(I.Op), *Operands);
				}
			}
		}
		return Result;
	}
}

FString FBangoInputsSignature::ToInputsText() const
{
	TArray<FString> Parts;
	for (const FBangoScriptParamInfo& Param : Params)
	{
		Parts.Add(Param.Type.ToString() + TEXT(" ") + Param.Name);
	}
	return FString::Join(Parts, TEXT(", "));
}

FString FBangoInputsSignature::GetSourceLabel(int32 Index) const
{
	return IsInstanceInput(Index) ? FString(TEXT("this object (its own inputs)")) : (SignatureLabel.IsEmpty() ? Label : SignatureLabel);
}

FString FBangoInputsSignature::GetNames() const
{
	TArray<FString> Parts;
	for (const FBangoScriptParamInfo& Param : Params)
	{
		Parts.Add(Param.Name);
	}
	return FString::Join(Parts, TEXT(", "));
}

bool Bango::CheckReflectedCall(const FBangoReflectedCallSite& Site, const TArray<FBangoCallableFunction>& Functions, const FString& TargetLabel, FString& OutError)
{
	const FBangoCallableFunction* Found = Functions.FindByPredicate([&Site](const FBangoCallableFunction& Function) { return Function.Name.Equals(Site.Name, ESearchCase::CaseSensitive); });
	if (!Found)
	{
		OutError = FString::Printf(TEXT("%s has no Bango function named '%s' (a Blueprint function in the category 'Bango')."), *TargetLabel, *Site.Name);
		if (const FBangoCallableFunction* Similar = Functions.FindByPredicate([&Site](const FBangoCallableFunction& Function) { return Function.Name.Equals(Site.Name, ESearchCase::IgnoreCase); }))
		{
			OutError += FString::Printf(TEXT(" Did you mean '%s'? Names are case-sensitive."), *Similar->Name);
		}
		return false;
	}
	if (Site.NumArgs != Found->ParamNames.Num())
	{
		OutError = FString::Printf(TEXT("%s's '%s' takes %d argument(s), but %d were given: %s"), *TargetLabel, *Site.Name, Found->ParamNames.Num(), Site.NumArgs, *Found->Signature);
		return false;
	}
	if (Site.bResultUsed && !Found->bHasResult)
	{
		OutError = FString::Printf(TEXT("%s's '%s' doesn't return a value a script can use, so it can't be used here: %s"), *TargetLabel, *Site.Name, *Found->Signature);
		return false;
	}
	return true;
}
