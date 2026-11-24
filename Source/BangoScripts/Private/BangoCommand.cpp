#include "BangoCommand.h"

namespace BangoCommandPrivate
{
	/** "an int", "a float", "an entity[]". */
	static FString WithArticle(const FBangoType& Type)
	{
		const bool bVowel = Type.Base == EBangoBaseType::Int || Type.Base == EBangoBaseType::Entity;
		return (bVowel ? TEXT("an ") : TEXT("a ")) + Type.ToString();
	}
}

FString FBangoCommandSignature::ToString() const
{
	FString Result;
	if (bHasTarget)
	{
		Result += GetTargetTypeName();
		Result += TEXT(" ");
	}
	Result += Name;
	Result += TEXT("(");
	const int32 Required = GetNumRequiredParams();
	for (int32 i = 0; i < Params.Num(); ++i)
	{
		if (i > 0)
		{
			Result += TEXT(", ");
		}
		Result += GetParamTypeName(i);
		Result += TEXT(" ");
		Result += Params[i].Name;
		if (i >= Required)
		{
			Result += TEXT(" (optional)");
		}
	}
	if (bVariadic)
	{
		Result += Params.Num() > 0 ? TEXT(", ...") : TEXT("...");
	}
	Result += TEXT(")");
	if (bReturnsValue)
	{
		Result += TEXT(" -> ");
		Result += GetReturnTypeName();
	}
	return Result;
}

FBangoCommandSignature FBangoCommandSignature::Make(const FString& InName)
{
	FBangoCommandSignature Signature;
	Signature.Name = InName;
	return Signature;
}

FBangoCommandSignature& FBangoCommandSignature::Target(const FBangoType& Type)
{
	bHasTarget = true;
	TargetType = Type;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Param(const FString& ParamName, const FBangoType& Type, const FString& ParamDescription)
{
	FBangoCommandParam NewParam;
	NewParam.Name = ParamName;
	NewParam.Type = Type;
	NewParam.Description = ParamDescription;
	Params.Add(NewParam);
	return *this;
}

// ---- Type parameters ----

bool FBangoTypeParam::Allows(const FBangoType& Type) const
{
	return bAnyType || (bAnyArray && Type.IsArray()) || Types.Contains(Type);
}

FString FBangoTypeParam::GetDisplayName(int32 ArrayDepth) const
{
	FString Result = GetDisplayName();
	if (ArrayDepth > 0 && bAnyType)
	{
		Result = TEXT("array"); // an array of anything
		--ArrayDepth;
	}
	for (int32 i = 0; i < ArrayDepth; ++i)
	{
		Result += TEXT("[]");
	}
	return Result;
}

FString FBangoTypeParam::Describe(int32 ArrayDepth) const
{
	if (ArrayDepth <= 0)
	{
		return Describe();
	}
	return bAnyType && ArrayDepth == 1 ? FString(TEXT("an array")) : FString::Printf(TEXT("a %s"), *GetDisplayName(ArrayDepth));
}

bool FBangoTypeParam::Resolve(const FBangoType& Type, FBangoType& OutType) const
{
	if (Allows(Type))
	{
		OutType = Type;
		return true;
	}
	if (Type.Base == EBangoBaseType::Int)
	{
		const FBangoType AsFloat(EBangoBaseType::Double, Type.ArrayDepth);
		if (Allows(AsFloat))
		{
			OutType = AsFloat;
			return true;
		}
	}
	return false;
}

FString FBangoTypeParam::GetDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	if (bAnyType)
	{
		return TEXT("any");
	}
	FString Result = bAnyArray ? FString(TEXT("array")) : FString();
	for (const FBangoType& Type : Types)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT("|");
		}
		Result += Type.ToString();
	}
	return Result;
}

FString FBangoTypeParam::Describe() const
{
	if (bAnyType)
	{
		return DisplayName.IsEmpty() ? FString(TEXT("a value")) : FString::Printf(TEXT("a %s"), *DisplayName);
	}
	TArray<FString> Names;
	if (bAnyArray)
	{
		Names.Add(TEXT("an array"));
	}
	for (const FBangoType& Type : Types)
	{
		Names.Add(BangoCommandPrivate::WithArticle(Type));
	}
	FString List;
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		List += i == 0 ? TEXT("") : (i == Names.Num() - 1 ? TEXT(" or ") : TEXT(", "));
		List += Names[i];
	}
	return DisplayName.IsEmpty() ? List : FString::Printf(TEXT("a %s (%s)"), *DisplayName, *List);
}

bool FBangoTypeParam::Unify(const FBangoType& A, const FBangoType& B, FBangoType& OutType)
{
	if (A.IsAssignableFrom(B)) // the same, or B is the int to A's float
	{
		OutType = A;
		return true;
	}
	if (B.IsAssignableFrom(A))
	{
		OutType = B;
		return true;
	}
	return false;
}

FBangoCommandSignature& FBangoCommandSignature::TypeParam(const FBangoTypeVar& Var, const TArray<FBangoType>& Types, const FString& DisplayName)
{
	if (FindTypeParam(Var.Name) != INDEX_NONE)
	{
		BuildError = FString::Printf(TEXT("'%s' has two type parameters named '%s'."), *Name, *Var.Name);
		return *this;
	}
	FBangoTypeParam& NewParam = TypeParams.AddDefaulted_GetRef();
	NewParam.Name = Var.Name;
	NewParam.Types = Types;
	NewParam.DisplayName = DisplayName;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::AnyTypeParam(const FBangoTypeVar& Var, const FString& DisplayName)
{
	const int32 NumBefore = TypeParams.Num();
	TypeParam(Var, TArray<FBangoType>(), DisplayName);
	if (TypeParams.Num() > NumBefore)
	{
		TypeParams.Last().bAnyType = true;
	}
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Target(const FBangoTypeVar& Var)
{
	const int32 Index = FindTypeParam(Var.Name);
	if (Index == INDEX_NONE)
	{
		BuildError = FString::Printf(TEXT("'%s': its target uses the type parameter '%s', which hasn't been added (TypeParam comes first)."), *Name, *Var.Name);
		return *this;
	}
	Target(GetTypeOf(TypeParams[Index].Types.Num() > 0 ? TypeParams[Index].Types[0] : FBangoType(), Var.ArrayDepth));
	TargetTypeVar = Index;
	TargetTypeVarDepth = Var.ArrayDepth;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::MutableTarget()
{
	if (!bHasTarget)
	{
		BuildError = FString::Printf(TEXT("'%s': MutableTarget comes after Target (there's no target to change)."), *Name);
		return *this;
	}
	bMutableTarget = true;
	return *this;
}

FString FBangoCommandSignature::GetTargetTypeName() const
{
	if (!bHasTarget)
	{
		return FString();
	}
	return TypeParams.IsValidIndex(TargetTypeVar) ? TypeParams[TargetTypeVar].GetDisplayName(TargetTypeVarDepth) : TargetType.ToString();
}

FBangoCommandSignature& FBangoCommandSignature::OrAnyArray()
{
	if (TypeParams.Num() > 0)
	{
		TypeParams.Last().bAnyArray = true;
	}
	return *this;
}

int32 FBangoCommandSignature::FindTypeParam(const FString& VarName) const
{
	return TypeParams.IndexOfByPredicate([&VarName](const FBangoTypeParam& TypeParam) { return TypeParam.Name.Equals(VarName, ESearchCase::CaseSensitive); });
}

FBangoCommandSignature& FBangoCommandSignature::Param(const FString& ParamName, const FBangoTypeVar& Var, const FString& ParamDescription)
{
	const int32 Index = FindTypeParam(Var.Name);
	if (Index == INDEX_NONE)
	{
		BuildError = FString::Printf(TEXT("'%s': parameter '%s' uses the type parameter '%s', which hasn't been added (TypeParam comes first)."), *Name, *ParamName, *Var.Name);
		return *this;
	}
	const FBangoTypeParam& TypeParam = TypeParams[Index];
	Param(ParamName, GetTypeOf(TypeParam.Types.Num() > 0 ? TypeParam.Types[0] : FBangoType(), Var.ArrayDepth), ParamDescription);
	Params.Last().TypeVar = Index;
	Params.Last().TypeVarDepth = Var.ArrayDepth;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Returns(const FBangoTypeVar& Var)
{
	const int32 Index = FindTypeParam(Var.Name);
	if (Index == INDEX_NONE)
	{
		BuildError = FString::Printf(TEXT("'%s' returns the type parameter '%s', which hasn't been added (TypeParam comes first)."), *Name, *Var.Name);
		return *this;
	}
	Returns(GetTypeOf(TypeParams[Index].Types.Num() > 0 ? TypeParams[Index].Types[0] : FBangoType(), Var.ArrayDepth));
	ReturnTypeVar = Index;
	ReturnTypeVarDepth = Var.ArrayDepth;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Pure()
{
	bPure = true;
	return *this;
}

FString FBangoCommandSignature::GetParamTypeName(int32 ParamIndex) const
{
	if (!Params.IsValidIndex(ParamIndex))
	{
		return FString();
	}
	const FBangoCommandParam& Param = Params[ParamIndex];
	return TypeParams.IsValidIndex(Param.TypeVar) ? TypeParams[Param.TypeVar].GetDisplayName(Param.TypeVarDepth) : Param.Type.ToString();
}

bool FBangoCommandSignature::BindTypeParams(const FBangoValue* Target, TArray<FBangoValue>& Args, TArray<FBangoType>& OutTypeArgs, FString& OutError) const
{
	using namespace BangoCommandPrivate;
	OutTypeArgs.Reset();
	OutTypeArgs.SetNum(TypeParams.Num());

	// Everything given whose type is a type parameter: the target (if it is), then the arguments.
	struct FSlot
	{
		int32 Var = INDEX_NONE;
		int32 Depth = 0;
		FBangoType Type;
		int32 ArgIndex = INDEX_NONE; // INDEX_NONE: the target
	};
	// (This runs for every call of such a command, so nothing here allocates unless the call is wrong: the slots are
	// kept inline, and what a slot is called is only worked out for an error.)
	TArray<FSlot, TInlineAllocator<8>> Slots;
	// "the target", "argument 2 ('B')"
	auto What = [this](const FSlot& Slot) -> FString
	{
		return Slot.ArgIndex == INDEX_NONE ? FString(TEXT("the target")) : FString::Printf(TEXT("argument %d ('%s')"), Slot.ArgIndex + 1, *Params[Slot.ArgIndex].Name);
	};
	// "the target", "'B'"
	auto NameOf = [this](const FSlot& Slot) -> FString
	{
		return Slot.ArgIndex == INDEX_NONE ? FString(TEXT("the target")) : FString::Printf(TEXT("'%s'"), *Params[Slot.ArgIndex].Name);
	};
	if (Target && TypeParams.IsValidIndex(TargetTypeVar))
	{
		FSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Var = TargetTypeVar;
		Slot.Depth = TargetTypeVarDepth;
		Slot.Type = Target->GetType();
	}
	for (int32 i = 0; i < Args.Num() && i < Params.Num(); ++i)
	{
		if (TypeParams.IsValidIndex(Params[i].TypeVar))
		{
			FSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Var = Params[i].TypeVar;
			Slot.Depth = Params[i].TypeVarDepth;
			Slot.Type = Args[i].GetType();
			Slot.ArgIndex = i;
		}
	}

	for (int32 Var = 0; Var < TypeParams.Num(); ++Var)
	{
		const FBangoTypeParam& TypeParam = TypeParams[Var];
		// Its arrays (and the target) say what it is, exactly.
		const FSlot* Anchor = nullptr;
		for (const FSlot& Slot : Slots)
		{
			if (Slot.Var != Var || (Slot.Depth == 0 && Slot.ArgIndex != INDEX_NONE))
			{
				continue;
			}
			if (Slot.Type.ArrayDepth < Slot.Depth)
			{
				OutError = FString::Printf(TEXT("%s must be %s, but it is %s."), *What(Slot), *TypeParam.Describe(Slot.Depth), *WithArticle(Slot.Type));
				return false;
			}
			const FBangoType Inner(Slot.Type.Base, (uint8)(Slot.Type.ArrayDepth - Slot.Depth));
			if (!TypeParam.Allows(Inner))
			{
				OutError = FString::Printf(TEXT("%s must be %s, but it is %s."), *What(Slot), *TypeParam.Describe(Slot.Depth), *WithArticle(Slot.Type));
				return false;
			}
			if (Anchor && !(Inner == OutTypeArgs[Var]))
			{
				OutError = FString::Printf(TEXT("%s and %s must go together, but they are %s and %s."), *NameOf(*Anchor), *NameOf(Slot), *WithArticle(Anchor->Type), *WithArticle(Slot.Type));
				return false;
			}
			if (!Anchor)
			{
				Anchor = &Slot;
				OutTypeArgs[Var] = Inner;
			}
		}

		const FSlot* First = nullptr;
		for (const FSlot& Slot : Slots)
		{
			if (Slot.Var != Var || Slot.Depth != 0 || Slot.ArgIndex == INDEX_NONE)
			{
				continue;
			}
			if (Anchor)
			{
				// It must fit what the array holds (an int into a float array is fine).
				if (!OutTypeArgs[Var].IsAssignableFrom(Slot.Type))
				{
					OutError = FString::Printf(TEXT("%s must be %s, but it is %s."), *What(Slot), *WithArticle(OutTypeArgs[Var]), *WithArticle(Slot.Type));
					return false;
				}
				continue;
			}
			FBangoType ArgType;
			if (!TypeParam.Resolve(Slot.Type, ArgType))
			{
				OutError = FString::Printf(TEXT("%s must be %s, but it is %s."), *What(Slot), *TypeParam.Describe(), *WithArticle(Slot.Type));
				return false;
			}
			if (!First)
			{
				First = &Slot;
				OutTypeArgs[Var] = ArgType;
				continue;
			}
			FBangoType Unified;
			if (!FBangoTypeParam::Unify(OutTypeArgs[Var], ArgType, Unified))
			{
				OutError = FString::Printf(TEXT("%s and %s must be the same type, but they are %s and %s."), *NameOf(*First), *NameOf(Slot), *WithArticle(First->Type), *WithArticle(Slot.Type));
				return false;
			}
			OutTypeArgs[Var] = Unified;
		}
	}

	// Each argument as its type parameter came out (an int beside a float becomes one). The target is never converted.
	for (const FSlot& Slot : Slots)
	{
		if (Slot.ArgIndex == INDEX_NONE)
		{
			continue;
		}
		const FBangoType Wanted = GetTypeOf(OutTypeArgs[Slot.Var], Slot.Depth);
		if (Slot.Type == Wanted)
		{
			continue;
		}
		FBangoValue Converted;
		if (!Args[Slot.ArgIndex].TryConvertTo(Wanted, Converted))
		{
			OutError = FString::Printf(TEXT("%s can't be passed as %s."), *What(Slot), *WithArticle(Wanted));
			return false;
		}
		Args[Slot.ArgIndex] = MoveTemp(Converted);
	}
	return true;
}

FBangoCommandSignature& FBangoCommandSignature::Optional()
{
	NumRequiredParams = Params.Num();
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Variadic(const FString& LabelPrefix, int32 FirstIndex, const FString& LabelSuffix)
{
	bVariadic = true;
	VariadicLabelPrefix = LabelPrefix;
	VariadicFirstIndex = FirstIndex;
	VariadicLabelSuffix = LabelSuffix;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::WithPlaceholders()
{
	PlaceholderParam = Params.Num() - 1;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Returns(const FBangoType& Type)
{
	bReturnsValue = true;
	ReturnType = Type;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::MayBeNil()
{
	if (Params.Num() > 0)
	{
		Params.Last().bMayBeNil = true;
	}
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Discardable()
{
	bDiscardable = true;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::InCategory(const FString& InCategory)
{
	CategoryName = InCategory;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::Describe(const FString& InDescription)
{
	CommandDescription = InDescription;
	return *this;
}

bool FBangoCommandTable::Add(const FBangoCommandSignature& Signature, FString& OutError)
{
	FBangoCommand Command;
	Command.Signature = Signature;
	return Add(Command, OutError);
}

bool FBangoCommandTable::Add(const FBangoCommand& Command, FString& OutError)
{
	const FString& Name = Command.Signature.Name;
	if (!Bango::IsValidIdentifier(Name) || Bango::IsReservedWord(Name) || Bango::IsLocalName(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid command name."), *Name);
		return false;
	}
	if (!Command.Signature.BuildError.IsEmpty())
	{
		OutError = Command.Signature.BuildError;
		return false;
	}
	for (int32 i = 0; i < Command.Signature.TypeParams.Num(); ++i)
	{
		const FBangoTypeParam& TypeParam = Command.Signature.TypeParams[i];
		if (TypeParam.Types.Num() == 0 && !TypeParam.bAnyArray && !TypeParam.bAnyType)
		{
			OutError = FString::Printf(TEXT("'%s': the type parameter '%s' allows no types."), *Name, *TypeParam.Name);
			return false;
		}
		// The result's type must come from an argument that's always given.
		const int32 Required = Command.Signature.GetNumRequiredParams();
		const bool bGiven = Command.Signature.TargetTypeVar == i
			|| (Command.Signature.Params.ContainsByPredicate([i](const FBangoCommandParam& Param) { return Param.TypeVar == i; })
				&& Command.Signature.Params.IndexOfByPredicate([i](const FBangoCommandParam& Param) { return Param.TypeVar == i; }) < Required);
		if (Command.Signature.ReturnTypeVar == i && !bGiven)
		{
			OutError = FString::Printf(TEXT("'%s' returns the type parameter '%s', so its target or a required parameter must use it (its type comes from them)."), *Name, *TypeParam.Name);
			return false;
		}
	}
	if (Command.Signature.bMutableTarget && Command.Poll)
	{
		OutError = FString::Printf(TEXT("'%s' changes its target (MutableTarget), so it can't wait: it can't have a Poll handler."), *Name);
		return false;
	}
	if (Commands.Contains(Name))
	{
		OutError = FString::Printf(TEXT("A command named '%s' is already registered."), *Name);
		return false;
	}
	Commands.Add(Name, MakeShared<FBangoCommand>(Command));
	return true;
}

const FBangoCommandSignature* FBangoCommandTable::FindCommand(const FString& Name) const
{
	const TSharedPtr<FBangoCommand>* Command = Commands.Find(Name);
	return Command ? &(*Command)->Signature : nullptr;
}

const FBangoCommand* FBangoCommandTable::FindCommandImpl(const FString& Name) const
{
	const TSharedPtr<FBangoCommand>* Command = Commands.Find(Name);
	return Command ? Command->Get() : nullptr;
}

FString FBangoCommandTable::FindNameIgnoringCase(const FString& Name) const
{
	for (const auto& Pair : Commands)
	{
		if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return Pair.Key;
		}
	}
	return FString();
}

void FBangoCommandTable::GetCommandNames(TArray<FString>& OutNames) const
{
	for (const auto& Pair : Commands)
	{
		OutNames.Add(Pair.Key);
	}
}
