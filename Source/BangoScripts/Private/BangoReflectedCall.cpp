#include "BangoReflectedCall.h"

#include "BangoActorRegistry.h"
#include "BangoIDActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace BangoReflectedCallPrivate
{
	/** Parameters a caller passes, in order (inputs, including ones passed by reference). */
	static bool IsInput(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm)
			&& (!Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ReferenceParm));
	}

	/** What it gives back: its return value, or an output (a Blueprint function's outputs are out parameters). */
	static bool IsOutput(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_ReturnParm)
			|| (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm));
	}

	static void GetParams(const UFunction* Function, TArray<FProperty*>& OutInputs, TArray<FProperty*>& OutOutputs)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (IsOutput(*It))
			{
				OutOutputs.Add(*It);
			}
			else if (IsInput(*It))
			{
				OutInputs.Add(*It);
			}
		}
	}

	static bool IsValidBangoName(const FString& Name)
	{
		if (Name.IsEmpty() || FChar::IsDigit(Name[0]))
		{
			return false;
		}
		for (const TCHAR C : Name)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	/** In the category "Bango" (or under it). Only known in editor builds: a packaged game takes it as yes. */
	static bool IsInBangoCategory(const UFunction* Function)
	{
#if WITH_METADATA
		const FString& Category = Function->GetMetaData(TEXT("Category"));
		return Category == TEXT("Bango") || Category.StartsWith(TEXT("Bango|"), ESearchCase::CaseSensitive);
#else
		return true;
#endif
	}

	/** A latent function (it waits), which `call` can't wait for. Only known in editor builds. */
	static bool IsLatent(const UFunction* Function)
	{
#if WITH_METADATA
		return Function->HasMetaData(TEXT("Latent"));
#else
		return false;
#endif
	}

	static bool IsBlueprintCallable(const UFunction* Function)
	{
		return Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent) && !Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate);
	}

	static FString TypeName(const FProperty* Property)
	{
		FBangoType Type;
		return Bango::BangoTypeOfProperty(Property, Type) ? Type.ToString() : FString::Printf(TEXT("<%s>"), *Property->GetCPPType());
	}

	static FString DescribeSignature(const UFunction* Function)
	{
		TArray<FProperty*> Inputs;
		TArray<FProperty*> Outputs;
		GetParams(Function, Inputs, Outputs);
		TArray<FString> Parts;
		for (const FProperty* Input : Inputs)
		{
			Parts.Add(TypeName(Input) + TEXT(" ") + Input->GetName());
		}
		FString Text = FString::Printf(TEXT("%s(%s)"), *Function->GetName(), *FString::Join(Parts, TEXT(", ")));
		if (Outputs.Num() == 1)
		{
			Text += TEXT(" -> ") + TypeName(Outputs[0]);
		}
		else if (Outputs.Num() > 1)
		{
			Text += FString::Printf(TEXT(" -> %d values"), Outputs.Num());
		}
		return Text;
	}

	/** "@light (BP_Light)" */
	static FString DescribeTarget(const FString& EntityName, const AActor* Actor)
	{
		FString ClassName = Actor->GetClass()->GetName();
		ClassName.RemoveFromEnd(TEXT("_C"));
		return FString::Printf(TEXT("@%s (%s)"), *EntityName, *ClassName);
	}

	/** Without spaces and other characters a Bango name can't have, for "did you mean". */
	static FString Squeezed(const FString& Name)
	{
		FString Out;
		for (const TCHAR C : Name)
		{
			if (FChar::IsAlnum(C) || C == TEXT('_'))
			{
				Out.AppendChar(FChar::ToLower(C));
			}
		}
		return Out;
	}

	static UFunction* FindFunction(const UClass* Class, const FString& Name, FString& OutError, const FString& TargetText)
	{
		UFunction* Found = nullptr;
		const UFunction* Similar = nullptr;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FString FunctionName = It->GetName();
			if (FunctionName.Equals(Name, ESearchCase::CaseSensitive))
			{
				Found = *It;
				break;
			}
			if (!Similar && Squeezed(FunctionName) == Squeezed(Name))
			{
				Similar = *It;
			}
		}
		if (!Found)
		{
			OutError = FString::Printf(TEXT("'call %s': %s has no Blueprint function named '%s'."), *Name, *TargetText, *Name);
			if (Similar)
			{
				const FString SimilarName = Similar->GetName();
				OutError += IsValidBangoName(SimilarName)
					? FString::Printf(TEXT(" Did you mean '%s'? Names are case-sensitive."), *SimilarName)
					: FString::Printf(TEXT(" '%s' can't be called from Bango: rename it without spaces or special characters."), *SimilarName);
			}
			return nullptr;
		}
		if (!IsBlueprintCallable(Found))
		{
			OutError = FString::Printf(TEXT("'call %s': %s's '%s' can't be called from Blueprint, so Bango can't call it either."), *Name, *TargetText, *Name);
			return nullptr;
		}
		if (!IsInBangoCategory(Found))
		{
			OutError = FString::Printf(TEXT("'call %s': %s's '%s' isn't in the Bango category, so scripts can't call it. Put it in the category 'Bango' (or call a function there that calls it)."),
				*Name, *TargetText, *Name);
			return nullptr;
		}
		if (IsLatent(Found))
		{
			OutError = FString::Printf(TEXT("'call %s': it's a latent function (it waits), which 'call' can't wait for."), *Name);
			return nullptr;
		}
		return Found;
	}

	static AActor* ResolveActor(const UObject* WorldContext, const FBangoValue& Entity, FString& OutError)
	{
		UBangoActorRegistry* Registry = UBangoActorRegistry::Get(WorldContext);
		AActor* Actor = Registry ? Registry->FindActor(Entity.GetEntityName()) : nullptr;
		if (!Actor)
		{
			OutError = Bango::DescribeMissingEntity(Entity.GetEntityName());
		}
		return Actor;
	}

	/** Puts Value in the parameter at ValuePtr. */
	static bool WriteParam(const UObject* WorldContext, const FProperty* Property, void* ValuePtr, const FBangoValue& Value, FString& OutError)
	{
		FBangoType ParamType;
		if (!Bango::BangoTypeOfProperty(Property, ParamType))
		{
			OutError = FString::Printf(TEXT("its parameter '%s' is a %s, which Bango has no type for."), *Property->GetName(), *Property->GetCPPType());
			return false;
		}
		if (Value.IsNil())
		{
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			OutError = FString::Printf(TEXT("'%s' (%s) can't be nil."), *Property->GetName(), *ParamType.ToString());
			return false;
		}
		const FBangoType& Type = Value.GetType();
		auto WrongType = [&]()
		{
			OutError = FString::Printf(TEXT("'%s' takes: %s. This is: %s."), *Property->GetName(), *ParamType.ToString(), *Type.ToString());
			return false;
		};
		if (Type.IsArray())
		{
			return WrongType();
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			if (Type.Base != EBangoBaseType::Bool)
			{
				return WrongType();
			}
			BoolProperty->SetPropertyValue(ValuePtr, Value.GetBool());
			return true;
		}
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Numeric = EnumProperty->GetUnderlyingProperty();
		}
		if (Numeric)
		{
			if (Numeric->IsInteger())
			{
				if (Type.Base != EBangoBaseType::Int)
				{
					return WrongType();
				}
				Numeric->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value.GetInt()));
				return true;
			}
			if (Type.Base != EBangoBaseType::Int && Type.Base != EBangoBaseType::Double)
			{
				return WrongType();
			}
			Numeric->SetFloatingPointPropertyValue(ValuePtr, Value.GetNumberAsDouble());
			return true;
		}
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			if (Type.Base != EBangoBaseType::String)
			{
				return WrongType();
			}
			StrProperty->SetPropertyValue(ValuePtr, Value.GetString());
			return true;
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (Type.Base != EBangoBaseType::String)
			{
				return WrongType();
			}
			NameProperty->SetPropertyValue(ValuePtr, FName(*Value.GetString()));
			return true;
		}
		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			if (Type.Base == EBangoBaseType::Text)
			{
				TextProperty->SetPropertyValue(ValuePtr, Value.GetText());
				return true;
			}
			if (Type.Base == EBangoBaseType::String)
			{
				TextProperty->SetPropertyValue(ValuePtr, FText::FromString(Value.GetString()));
				return true;
			}
			return WrongType();
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				if (Type.Base != EBangoBaseType::Vector3)
				{
					return WrongType();
				}
				*static_cast<FVector*>(ValuePtr) = FVector(Value.GetVectorPart(0), Value.GetVectorPart(1), Value.GetVectorPart(2));
				return true;
			}
			if (Type.Base != EBangoBaseType::Vector2)
			{
				return WrongType();
			}
			*static_cast<FVector2D*>(ValuePtr) = FVector2D(Value.GetVectorPart(0), Value.GetVectorPart(1));
			return true;
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (Type.Base != EBangoBaseType::Entity)
			{
				return WrongType();
			}
			AActor* Actor = ResolveActor(WorldContext, Value, OutError);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("'%s': %s"), *Property->GetName(), *OutError);
				return false;
			}
			if (!Actor->IsA(ObjectProperty->PropertyClass))
			{
				OutError = FString::Printf(TEXT("'%s' must be a %s, but @%s is a %s."), *Property->GetName(), *ObjectProperty->PropertyClass->GetName(),
					*Value.GetEntityName(), *Actor->GetClass()->GetName());
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(ValuePtr, Actor);
			return true;
		}
		return WrongType();
	}

	/** The value of the result at ValuePtr. */
	static bool ReadResult(const UObject* WorldContext, const FProperty* Property, const void* ValuePtr, FBangoValue& OutValue, FString& OutError)
	{
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			OutValue = FBangoValue::MakeBool(BoolProperty->GetPropertyValue(ValuePtr));
			return true;
		}
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Numeric = EnumProperty->GetUnderlyingProperty();
		}
		if (Numeric)
		{
			if (Numeric->IsInteger())
			{
				const int64 Value = Numeric->GetSignedIntPropertyValue(ValuePtr);
				if (Value < MIN_int32 || Value > MAX_int32)
				{
					OutError = FString::Printf(TEXT("it returned %lld, which is too big for a Bango int."), Value);
					return false;
				}
				OutValue = FBangoValue::MakeInt(static_cast<int32>(Value));
				return true;
			}
			OutValue = FBangoValue::MakeDouble(Numeric->GetFloatingPointPropertyValue(ValuePtr));
			return true;
		}
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			OutValue = FBangoValue::MakeString(StrProperty->GetPropertyValue(ValuePtr));
			return true;
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			OutValue = FBangoValue::MakeString(NameProperty->GetPropertyValue(ValuePtr).ToString());
			return true;
		}
		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			OutValue = FBangoValue::MakeText(TextProperty->GetPropertyValue(ValuePtr));
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector& V = *static_cast<const FVector*>(ValuePtr);
				OutValue = FBangoValue::MakeVector3(V.X, V.Y, V.Z);
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
			{
				const FVector2D& V = *static_cast<const FVector2D*>(ValuePtr);
				OutValue = FBangoValue::MakeVector2(V.X, V.Y);
				return true;
			}
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const AActor* Actor = Cast<AActor>(ObjectProperty->GetObjectPropertyValue(ValuePtr));
			if (!Actor)
			{
				OutValue = FBangoValue::MakeNil(FBangoType::Entity());
				return true;
			}
			FString EntityError;
			if (!Bango::ActorToEntity(Actor, OutValue, EntityError))
			{
				OutError = FString::Printf(TEXT("it returned the actor %s, which a script can't use: %s"), *Actor->GetActorNameOrLabel(), *EntityError);
				return false;
			}
			return true;
		}
		OutError = FString::Printf(TEXT("it returns a %s, which Bango has no type for."), *Property->GetCPPType());
		return false;
	}
}

namespace Bango
{
	bool BangoTypeOfProperty(const FProperty* Property, FBangoType& OutType)
	{
		if (!Property || Property->ArrayDim != 1)
		{
			return false;
		}
		if (CastField<FBoolProperty>(Property))
		{
			OutType = FBangoType::Bool();
			return true;
		}
		if (CastField<FEnumProperty>(Property))
		{
			OutType = FBangoType::Int();
			return true;
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			OutType = Numeric->IsInteger() ? FBangoType::Int() : FBangoType::Double();
			return true;
		}
		if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property))
		{
			OutType = FBangoType::String();
			return true;
		}
		if (CastField<FTextProperty>(Property))
		{
			OutType = FBangoType::Text();
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				OutType = FBangoType::Vector3();
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
			{
				OutType = FBangoType::Vector2();
				return true;
			}
			return false;
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
			{
				OutType = FBangoType::Entity();
				return true;
			}
		}
		return false;
	}

	void GetBangoCallableFunctions(const UClass* Class, TArray<FBangoCallableFunction>& OutFunctions)
	{
		using namespace BangoReflectedCallPrivate;
		OutFunctions.Reset();
		if (!Class)
		{
			return;
		}
		TSet<FString> Seen;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const UFunction* Function = *It;
			const FString Name = Function->GetName();
			if (Seen.Contains(Name) || !IsValidBangoName(Name) || !IsBlueprintCallable(Function) || !IsInBangoCategory(Function) || IsLatent(Function))
			{
				continue;
			}
			Seen.Add(Name);
			FBangoCallableFunction& Callable = OutFunctions.AddDefaulted_GetRef();
			Callable.Name = Name;
			TArray<FProperty*> Inputs;
			TArray<FProperty*> Outputs;
			GetParams(Function, Inputs, Outputs);
			for (const FProperty* Input : Inputs)
			{
				FBangoType Type;
				BangoTypeOfProperty(Input, Type);
				Callable.ParamNames.Add(Input->GetName());
				Callable.ParamTypes.Add(Type);
			}
			Callable.bHasResult = Outputs.Num() == 1 && BangoTypeOfProperty(Outputs[0], Callable.ResultType);
#if WITH_METADATA
			Callable.Description = Function->GetMetaData(TEXT("ToolTip"));
#endif
			Callable.Signature = DescribeSignature(Function);
		}
	}

	bool CallBangoCallable(const UObject* WorldContext, const FBangoValue& Target, const FString& Name, const TArray<FBangoValue>& Args,
		bool bWantResult, FBangoValue& OutResult, FString& OutError)
	{
		using namespace BangoReflectedCallPrivate;
		if (Target.IsNil() || Target.GetType() != FBangoType::Entity())
		{
			OutError = FString::Printf(TEXT("'call %s': the target must be an entity."), *Name);
			return false;
		}
		FString ActorError;
		AActor* Actor = ResolveActor(WorldContext, Target, ActorError);
		if (!Actor)
		{
			OutError = FString::Printf(TEXT("'call %s': %s"), *Name, *ActorError);
			return false;
		}
		return CallBangoCallableOn(Actor, DescribeTarget(Target.GetEntityName(), Actor), WorldContext, Name, Args, bWantResult, OutResult, OutError);
	}

	bool CallBangoCallableOn(UObject* Object, const FString& TargetText, const UObject* WorldContext, const FString& Name,
		const TArray<FBangoValue>& Args, bool bWantResult, FBangoValue& OutResult, FString& OutError)
	{
		using namespace BangoReflectedCallPrivate;
		if (!Object)
		{
			OutError = FString::Printf(TEXT("'call %s': there's nothing to call it on."), *Name);
			return false;
		}
		UFunction* Function = FindFunction(Object->GetClass(), Name, OutError, TargetText);
		if (!Function)
		{
			return false;
		}

		TArray<FProperty*> Inputs;
		TArray<FProperty*> Outputs;
		GetParams(Function, Inputs, Outputs);
		if (Args.Num() != Inputs.Num())
		{
			OutError = FString::Printf(TEXT("'call %s': it takes %d argument(s), but %d were given: %s"), *Name, Inputs.Num(), Args.Num(), *DescribeSignature(Function));
			return false;
		}
		if (bWantResult && Outputs.Num() != 1)
		{
			OutError = Outputs.Num() == 0
				? FString::Printf(TEXT("'call %s': it doesn't return a value, so it can't be used here."), *Name)
				: FString::Printf(TEXT("'call %s': it returns %d values, but a script can only use a function that returns one."), *Name, Outputs.Num());
			return false;
		}

		// Its parameters, made, filled, used and destroyed here.
		uint8* Params = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		bool bOk = true;
		for (int32 i = 0; i < Inputs.Num() && bOk; ++i)
		{
			FString ParamError;
			if (!WriteParam(WorldContext, Inputs[i], Inputs[i]->ContainerPtrToValuePtr<void>(Params), Args[i], ParamError))
			{
				OutError = FString::Printf(TEXT("'call %s': %s Its signature: %s"), *Name, *ParamError, *DescribeSignature(Function));
				bOk = false;
			}
		}
		if (bOk)
		{
			Object->ProcessEvent(Function, Params);
			if (bWantResult)
			{
				FString ResultError;
				if (!ReadResult(WorldContext, Outputs[0], Outputs[0]->ContainerPtrToValuePtr<void>(Params), OutResult, ResultError))
				{
					OutError = FString::Printf(TEXT("'call %s': %s"), *Name, *ResultError);
					bOk = false;
				}
			}
		}
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
		FMemory::Free(Params);
		return bOk;
	}
}
