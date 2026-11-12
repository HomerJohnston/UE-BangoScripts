#include "BangoCommand.h"

FString FBangoCommandSignature::ToString() const
{
	FString Result;
	if (bHasTarget)
	{
		Result += TargetType.ToString();
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
		Result += Params[i].GetTypeName();
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

FBangoCommandSignature& FBangoCommandSignature::NumberParam(const FString& ParamName, const FString& ParamDescription)
{
	Param(ParamName, FBangoType::Double(), ParamDescription);
	Params.Last().bNumber = true;
	return *this;
}

FBangoCommandSignature& FBangoCommandSignature::ReturnsNumber()
{
	Returns(FBangoType::Double());
	bReturnsNumber = true;
	return *this;
}

bool FBangoCommandSignature::NumberResultIsInt(TConstArrayView<FBangoValue> Args) const
{
	for (int32 i = 0; i < Args.Num() && i < Params.Num(); ++i)
	{
		if (Params[i].bNumber && !(Args[i].GetType() == FBangoType::Int()))
		{
			return false;
		}
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
	if (Bango::IsBuiltinCommandName(Name))
	{
		OutError = FString::Printf(TEXT("'%s' is a built-in array command and can't be replaced."), *Name);
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

namespace Bango
{
	bool IsBuiltinCommandName(const FString& Name)
	{
		static const TCHAR* Builtins[] = { TEXT("Count"), TEXT("Push"), TEXT("InsertAt"), TEXT("Pop"), TEXT("RemoveAt") };
		for (const TCHAR* Builtin : Builtins)
		{
			if (Name.Equals(Builtin, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}
}
