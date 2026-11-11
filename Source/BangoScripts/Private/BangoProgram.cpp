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
			Result += TEXT("[]");
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

	static const TCHAR* ArrayModifyName(int32 Kind)
	{
		switch ((EBangoArrayModify)Kind)
		{
		case EBangoArrayModify::Push:     return TEXT("Push");
		case EBangoArrayModify::InsertAt: return TEXT("InsertAt");
		case EBangoArrayModify::Pop:      return TEXT("Pop");
		case EBangoArrayModify::RemoveAt: return TEXT("RemoveAt");
		default:                          return TEXT("?");
		}
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
		case EBangoOp::StorePlace:
		case EBangoOp::IsDefinedPlace:
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
			return FString::Printf(TEXT("-> %d"), I.A);
		case EBangoOp::CallFunction:
			return FString::Printf(TEXT("%s %d"), Program.Functions.IsValidIndex(I.A) ? *Program.Functions[I.A].Name : TEXT("<bad function>"), I.B);
		case EBangoOp::CallCommand:
			return FString::Printf(TEXT("%s %d%s"), Program.Names.IsValidIndex(I.A) ? *Program.Names[I.A] : TEXT("<bad name>"), I.B, I.C ? TEXT(" +target") : TEXT(""));
		case EBangoOp::ArrayModify:
			return FString::Printf(TEXT("%s %s%s"), *DescribePlace(Program, Function, I.A), ArrayModifyName(I.B), I.C ? TEXT(" +result") : TEXT(""));
		case EBangoOp::ForEachNext:
			return FString::Printf(TEXT("%s %s -> %d"), *DescribeSlot(Function, I.A), *DescribeSlot(Function, I.B), I.C);
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
		case EBangoOp::IsDefinedPlace:     return TEXT("IsDefinedPlace");
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
		case EBangoOp::Return:             return TEXT("Return");
		case EBangoOp::ReturnValue:        return TEXT("ReturnValue");
		case EBangoOp::Halt:               return TEXT("Halt");
		case EBangoOp::ErrorMissingReturn: return TEXT("ErrorMissingReturn");
		case EBangoOp::ArrayCount:         return TEXT("ArrayCount");
		case EBangoOp::ArrayModify:        return TEXT("ArrayModify");
		case EBangoOp::ForEachNext:        return TEXT("ForEachNext");
		default:                           return TEXT("?");
		}
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
			Result += FString::Printf(TEXT("params: %s;\n"), *FString::Join(Parts, TEXT(", ")));
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
