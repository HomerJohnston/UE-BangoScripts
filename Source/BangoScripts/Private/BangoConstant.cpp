#include "BangoConstant.h"

#include "BangoActorRegistry.h"
#include "BangoCompiler.h"
#include "BangoIDActorComponent.h"
#include "BangoInlineScript.h"
#include "GameFramework/Actor.h"

namespace BangoConstantPrivate
{
	/** An actor's entity value; nil for no actor. */
	bool GetActorEntity(AActor* Actor, FBangoValue& OutValue, FString& OutError)
	{
		OutValue = FBangoValue::MakeNil(FBangoType::Entity());
		if (!Actor)
		{
			return true;
		}
		// Maybe worked out before this actor has begun play: its Bango ID name is registered first, so it isn't given
		// another ID (registering again does nothing).
		if (UBangoIDActorComponent* ID = Actor->FindComponentByClass<UBangoIDActorComponent>())
		{
			ID->RegisterName();
		}
		FBangoValue Entity;
		if (!Bango::ActorToEntity(Actor, Entity, OutError))
		{
			return false;
		}
		OutValue = Entity;
		return true;
	}
}

bool FBangoConstant::GetValue(FBangoValue& OutValue, FString& OutError) const
{
	FBangoType ParsedType;
	if (!FBangoType::FromString(Type, ParsedType))
	{
		OutValue = FBangoValue();
		OutError = FString::Printf(TEXT("'%s' isn't a type."), *Type);
		return false;
	}
	OutValue = FBangoValue::MakeNil(ParsedType);

	if (IsActorType())
	{
		FBangoValue Entity;
		if (!BangoConstantPrivate::GetActorEntity(Actor, Entity, OutError))
		{
			return false;
		}
		OutValue = Entity;
		return true;
	}

	if (IsActorArrayType())
	{
		TArray<FBangoValue> Elements;
		for (int32 Index = 0; Index < Actors.Num(); ++Index)
		{
			FBangoValue Entity;
			FString ActorError;
			if (!BangoConstantPrivate::GetActorEntity(Actors[Index], Entity, ActorError))
			{
				OutError = FString::Printf(TEXT("Its actor %d: %s"), Index, *ActorError);
				return false;
			}
			Elements.Add(Entity);
		}
		OutValue = FBangoValue::MakeArray(FBangoType::Entity(), Elements);
		return true;
	}

	FBangoValue Parsed;
	if (!Bango::ParseFixedValue(Type, Value, Parsed, OutError))
	{
		return false;
	}
	OutValue = Parsed;
	return true;
}

namespace Bango
{
	TArray<FBangoInputDecl> GetConstantInputs(const TArray<FBangoConstant>& Constants)
	{
		TArray<FBangoInputDecl> Inputs;
		for (const FBangoConstant& Constant : Constants)
		{
			Inputs.Emplace(Constant.GetDeclaration());
		}
		return Inputs;
	}

	bool SetConstantInputs(const TArray<FBangoConstant>& Constants, TArrayView<FBangoInlineScriptBase* const> Scripts, FString* OutError)
	{
		// The same list for each: a wrong one is shown as each script's own error (in the editor too), so only the
		// first is passed on.
		const TArray<FBangoInputDecl> Inputs = GetConstantInputs(Constants);
		bool bAllGood = true;
		FString FirstError;
		for (FBangoInlineScriptBase* Script : Scripts)
		{
			FString Error;
			if (Script && !Script->SetInstanceInputs(Inputs, &Error))
			{
				if (bAllGood)
				{
					FirstError = Error;
				}
				bAllGood = false;
			}
		}
		if (OutError)
		{
			*OutError = FirstError;
		}
		return bAllGood;
	}

	bool GetConstantValues(const TArray<FBangoConstant>& Constants, TArray<FBangoValue>& OutValues, TArray<FString>* OutErrors)
	{
		OutValues.Reset();
		bool bAllGood = true;
		for (const FBangoConstant& Constant : Constants)
		{
			FBangoValue Value;
			FString Error;
			if (!Constant.GetValue(Value, Error))
			{
				bAllGood = false;
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("%s: %s"), *Constant.Name, *Error));
				}
			}
			OutValues.Add(Value);
		}
		return bAllGood;
	}
}
