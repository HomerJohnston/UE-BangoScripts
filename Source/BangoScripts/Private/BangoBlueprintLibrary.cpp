#include "BangoBlueprintLibrary.h"

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoDefines.h"
#include "BangoGlobalsSubsystem.h"
#include "BangoScriptAsset.h"
#include "BangoLog.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace BangoBlueprintPrivate
{
	static UBangoScriptSubsystem* GetScripts(UObject* WorldContextObject, const TCHAR* What)
	{
		UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(WorldContextObject);
		if (!Subsystem)
		{
			Bango::ReportError(FString::Printf(TEXT("%s: Bango scripts only run in a game world (play in editor or a packaged game)."), What));
		}
		return Subsystem;
	}

	static FBangoGlobalStore* GetStore(UObject* WorldContextObject, const TCHAR* What)
	{
		UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(WorldContextObject);
		if (!Globals)
		{
			Bango::ReportError(FString::Printf(TEXT("%s: Bango globals need a game instance (play in editor or a packaged game)."), What));
			return nullptr;
		}
		return &Globals->GetStore();
	}

	static bool SetGlobal(UObject* WorldContextObject, const FString& Name, const FBangoValue& Value, const TCHAR* What, bool bPersistent = false)
	{
		FBangoGlobalStore* Store = GetStore(WorldContextObject, What);
		if (!Store)
		{
			return false;
		}
		FString Error;
		if ((!Store->Find(Name) && !Store->Declare(Name, Value.GetType(), Error, bPersistent)) || !Store->Set(Name, Value, Error))
		{
			Bango::ReportError(FString::Printf(TEXT("%s: %s"), What, *Error));
			return false;
		}
		return true;
	}

	/** The global's value if it exists, isn't nil and has the given type; otherwise nullptr (with an error for a wrong type). */
	/** The define named Name as Type (converted, as an int to a float), or false. */
	static bool GetDefine(const FString& Name, const FBangoType& Type, FBangoValue& OutValue)
	{
		const FBangoDefineEntry* Define = Bango::FindDefine(Name);
		return Define && !Define->Value.IsNil() && Define->Value.TryConvertTo(Type, OutValue);
	}

	static const FBangoValue* GetGlobal(UObject* WorldContextObject, const FString& Name, const FBangoType& Type, const TCHAR* What)
	{
		FBangoGlobalStore* Store = GetStore(WorldContextObject, What);
		const FBangoValue* Value = Store ? Store->Find(Name) : nullptr;
		if (!Value || Value->IsNil())
		{
			return nullptr;
		}
		if (Value->GetType() != Type)
		{
			Bango::ReportError(FString::Printf(TEXT("%s: global '%s' is a %s, not a %s."), What, *Name, *Value->GetType().ToString(), *Type.ToString()));
			return nullptr;
		}
		return Value;
	}
}

using namespace BangoBlueprintPrivate;

namespace Bango
{
	bool ResolveBlueprintArgs(const FBangoBlueprintArgs& Args, const TArray<FBangoScriptParamInfo>& Params, TArray<FBangoValue>& OutValues, FString& OutError)
	{
		if (!Args.Error.IsEmpty())
		{
			OutError = Args.Error;
			return false;
		}

		// Trailing arguments that weren't passed are simply left off.
		int32 Count = Args.Values.Num();
		while (Count > 0 && Args.NotPassed.IsValidIndex(Count - 1) && Args.NotPassed[Count - 1])
		{
			--Count;
		}

		OutValues.Reset();
		for (int32 i = 0; i < Count; ++i)
		{
			if (!Args.NotPassed[i])
			{
				OutValues.Add(Args.Values[i]);
				continue;
			}
			if (!Params.IsValidIndex(i) || !Params[i].bHasDefault)
			{
				OutError = FString::Printf(TEXT("Argument %d was left out, but that parameter has no default. (On a Spawn Bango Script node, refresh the node.)"), i + 1);
				return false;
			}
			OutValues.Add(Params[i].Default);
		}
		return true;
	}
}

namespace BangoBlueprintPrivate
{
	static FBangoBlueprintArgs AddEntity(const FBangoBlueprintArgs& Args, const UObject* Object, FBangoValue& OutValue)
	{
		FBangoBlueprintArgs Result = Args;
		FString Error;
		if (!Bango::ObjectToEntity(Object, OutValue, Error) && Result.Error.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Argument %d: %s"), Args.Values.Num() + 1, *Error);
		}
		return Result;
	}

	template<typename T, typename FMakeElement>
	static FBangoBlueprintArgs AddArray(const FBangoBlueprintArgs& Args, const TArray<T>& Items, const FBangoType& ElementType, FMakeElement MakeElement)
	{
		TArray<FBangoValue> Elements;
		Elements.Reserve(Items.Num());
		for (const T& Item : Items)
		{
			Elements.Add(MakeElement(Item));
		}
		FBangoBlueprintArgs Result = Args;
		Result.Add(FBangoValue::MakeArray(ElementType, Elements));
		return Result;
	}
}

FBangoBlueprintArgs UBangoBlueprintLibrary::MakeBangoArgs()
{
	return FBangoBlueprintArgs();
}

FBangoBlueprintArgs UBangoBlueprintLibrary::SkipBangoArg(const FBangoBlueprintArgs& Args)
{
	FBangoBlueprintArgs Result = Args;
	Result.AddNotPassed();
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgBool(const FBangoBlueprintArgs& Args, bool Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeBool(Value));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgInt(const FBangoBlueprintArgs& Args, int32 Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeInt(Value));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgDouble(const FBangoBlueprintArgs& Args, double Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeDouble(Value));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgString(const FBangoBlueprintArgs& Args, const FString& Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeString(Value));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgText(const FBangoBlueprintArgs& Args, const FText& Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeText(Value));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgActor(const FBangoBlueprintArgs& Args, AActor* Value)
{
	FBangoValue Entity;
	FBangoBlueprintArgs Result = AddEntity(Args, Value, Entity);
	Result.Add(Entity);
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgComponent(const FBangoBlueprintArgs& Args, UActorComponent* Value)
{
	FBangoValue Entity;
	FBangoBlueprintArgs Result = AddEntity(Args, Value, Entity);
	Result.Add(Entity);
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgVector2(const FBangoBlueprintArgs& Args, FVector2D Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeVector2(Value.X, Value.Y));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgVector3(const FBangoBlueprintArgs& Args, FVector Value)
{
	FBangoBlueprintArgs Result = Args;
	Result.Add(FBangoValue::MakeVector3(Value.X, Value.Y, Value.Z));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgBoolArray(const FBangoBlueprintArgs& Args, const TArray<bool>& Value)
{
	return AddArray(Args, Value, FBangoType::Bool(), [](bool Item) { return FBangoValue::MakeBool(Item); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgIntArray(const FBangoBlueprintArgs& Args, const TArray<int32>& Value)
{
	return AddArray(Args, Value, FBangoType::Int(), [](int32 Item) { return FBangoValue::MakeInt(Item); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgDoubleArray(const FBangoBlueprintArgs& Args, const TArray<double>& Value)
{
	return AddArray(Args, Value, FBangoType::Double(), [](double Item) { return FBangoValue::MakeDouble(Item); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgStringArray(const FBangoBlueprintArgs& Args, const TArray<FString>& Value)
{
	return AddArray(Args, Value, FBangoType::String(), [](const FString& Item) { return FBangoValue::MakeString(Item); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgTextArray(const FBangoBlueprintArgs& Args, const TArray<FText>& Value)
{
	return AddArray(Args, Value, FBangoType::Text(), [](const FText& Item) { return FBangoValue::MakeText(Item); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgActorArray(const FBangoBlueprintArgs& Args, const TArray<AActor*>& Value)
{
	FBangoBlueprintArgs Result = Args;
	TArray<FBangoValue> Elements;
	for (const AActor* Actor : Value)
	{
		FBangoValue Entity;
		FString Error;
		if (!Bango::ActorToEntity(Actor, Entity, Error) && Result.Error.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Argument %d: %s"), Args.Values.Num() + 1, *Error);
		}
		Elements.Add(Entity);
	}
	Result.Add(FBangoValue::MakeArray(FBangoType::Entity(), Elements));
	return Result;
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgVector2Array(const FBangoBlueprintArgs& Args, const TArray<FVector2D>& Value)
{
	return AddArray(Args, Value, FBangoType::Vector2(), [](const FVector2D& Item) { return FBangoValue::MakeVector2(Item.X, Item.Y); });
}

FBangoBlueprintArgs UBangoBlueprintLibrary::AddBangoArgVector3Array(const FBangoBlueprintArgs& Args, const TArray<FVector>& Value)
{
	return AddArray(Args, Value, FBangoType::Vector3(), [](const FVector& Item) { return FBangoValue::MakeVector3(Item.X, Item.Y, Item.Z); });
}

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoScriptWithArgs(UObject* WorldContextObject, const FString& ScriptPath, const FBangoBlueprintArgs& Args, UObject* Owner)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Spawn Bango Script"));
	if (!Subsystem)
	{
		return FBangoScriptHandle();
	}

	TArray<FBangoScriptParamInfo> Params;
	FString Error;
	if (!FBangoModule::Get().GetScriptParams(ScriptPath, Params, Error))
	{
		Bango::ReportError(Error);
		return FBangoScriptHandle();
	}

	TArray<FBangoValue> Values;
	if (!Bango::ResolveBlueprintArgs(Args, Params, Values, Error))
	{
		Bango::ReportError(FString::Printf(TEXT("Spawn Bango Script '%s': %s"), *ScriptPath, *Error));
		return FBangoScriptHandle();
	}
	return Subsystem->Spawn(ScriptPath, Values, Owner);
}

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoScriptAssetWithArgs(UObject* WorldContextObject, const UBangoScriptAsset* Script, const FBangoBlueprintArgs& Args, UObject* Owner)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Spawn Bango Script"));
	if (!Subsystem)
	{
		return FBangoScriptHandle();
	}
	if (!Script)
	{
		Bango::ReportError(TEXT("Spawn Bango Script: no script (none was picked on the node, or the one connected is empty)."));
		return FBangoScriptHandle();
	}

	TArray<FBangoScriptParamInfo> Params;
	FString Error;
	if (!FBangoModule::Get().GetScriptParams(*Script, Params, Error))
	{
		Bango::ReportError(Error);
		return FBangoScriptHandle();
	}

	TArray<FBangoValue> Values;
	if (!Bango::ResolveBlueprintArgs(Args, Params, Values, Error))
	{
		Bango::ReportError(FString::Printf(TEXT("Spawn Bango Script '%s': %s"), *Script->GetScriptName(), *Error));
		return FBangoScriptHandle();
	}
	return Subsystem->Spawn(Script, Values, Owner);
}

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoInlineScript(UObject* WorldContextObject, const FBangoInlineScript& Script, const FBangoBlueprintArgs& Args, UObject* Owner)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Spawn Bango Inline Script"));
	if (!Subsystem || Script.IsEmpty())
	{
		return FBangoScriptHandle();
	}
	const FString Name = UBangoScriptSubsystem::GetInlineScriptName(Owner);
	TSharedPtr<const FBangoProgram> Program;
	FString Error;
	if (!FBangoModule::Get().GetInlineProgram(Script, Name, Program, Error, nullptr, Owner))
	{
		Bango::ReportError(Error);
		return FBangoScriptHandle();
	}
	TArray<FBangoValue> Values;
	if (!Bango::ResolveBlueprintArgs(Args, Program->Params, Values, Error))
	{
		const FString Where = Bango::DescribeInlineScriptOwner(Owner, &Script, nullptr);
		Bango::ReportError(FString::Printf(TEXT("Spawn Bango Inline Script (%s%s%s): %s"), *Name, Where.IsEmpty() ? TEXT("") : TEXT(", "), *Where, *Error));
		return FBangoScriptHandle();
	}
	return Subsystem->Spawn(Script, Values, Owner);
}

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoScript(UObject* WorldContextObject, const FString& ScriptPath, UObject* Owner)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Spawn Bango Script"));
	return Subsystem ? Subsystem->Spawn(ScriptPath, TArray<FBangoValue>(), Owner) : FBangoScriptHandle();
}

void UBangoBlueprintLibrary::TerminateBangoScript(UObject* WorldContextObject, FBangoScriptHandle Handle, bool bAndChildren)
{
	if (UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Terminate Bango Script")))
	{
		Subsystem->Terminate(Handle, bAndChildren);
	}
}

bool UBangoBlueprintLibrary::IsBangoScriptRunning(UObject* WorldContextObject, FBangoScriptHandle Handle)
{
	const UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsRunning(Handle);
}

bool UBangoBlueprintLibrary::IsBangoGlobalPersistent(UObject* WorldContextObject, const FString& Name)
{
	FBangoGlobalStore* Store = GetStore(WorldContextObject, TEXT("Is Bango Global Persistent"));
	return Store && Store->IsPersistent(Name);
}

bool UBangoBlueprintLibrary::SetBangoGlobalBool(UObject* WorldContextObject, const FString& Name, bool Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeBool(Value), TEXT("Set Bango Global Bool"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalInt(UObject* WorldContextObject, const FString& Name, int32 Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeInt(Value), TEXT("Set Bango Global Int"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalDouble(UObject* WorldContextObject, const FString& Name, double Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeDouble(Value), TEXT("Set Bango Global Float"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalString(UObject* WorldContextObject, const FString& Name, const FString& Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeString(Value), TEXT("Set Bango Global String"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalText(UObject* WorldContextObject, const FString& Name, const FText& Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeText(Value), TEXT("Set Bango Global Text"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalVector2(UObject* WorldContextObject, const FString& Name, FVector2D Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeVector2(Value.X, Value.Y), TEXT("Set Bango Global Vector2"), bPersistent);
}

bool UBangoBlueprintLibrary::SetBangoGlobalVector3(UObject* WorldContextObject, const FString& Name, FVector Value, bool bPersistent)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeVector3(Value.X, Value.Y, Value.Z), TEXT("Set Bango Global Vector3"), bPersistent);
}

bool UBangoBlueprintLibrary::GetBangoGlobalBool(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Bool(), TEXT("Get Bango Global Bool"));
	bIsDefined = Value != nullptr;
	return Value ? Value->GetBool() : false;
}

int32 UBangoBlueprintLibrary::GetBangoGlobalInt(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Int(), TEXT("Get Bango Global Int"));
	bIsDefined = Value != nullptr;
	return Value ? Value->GetInt() : 0;
}

double UBangoBlueprintLibrary::GetBangoGlobalDouble(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Double(), TEXT("Get Bango Global Float"));
	bIsDefined = Value != nullptr;
	return Value ? Value->GetDouble() : 0.0;
}

FString UBangoBlueprintLibrary::GetBangoGlobalString(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::String(), TEXT("Get Bango Global String"));
	bIsDefined = Value != nullptr;
	return Value ? Value->GetString() : FString();
}

FText UBangoBlueprintLibrary::GetBangoGlobalText(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Text(), TEXT("Get Bango Global Text"));
	bIsDefined = Value != nullptr;
	return Value ? Value->GetText() : FText::GetEmpty();
}

FVector2D UBangoBlueprintLibrary::GetBangoGlobalVector2(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Vector2(), TEXT("Get Bango Global Vector2"));
	bIsDefined = Value != nullptr;
	return Value ? FVector2D(Value->GetVectorPart(0), Value->GetVectorPart(1)) : FVector2D::ZeroVector;
}

FVector UBangoBlueprintLibrary::GetBangoGlobalVector3(UObject* WorldContextObject, const FString& Name, bool& bIsDefined)
{
	const FBangoValue* Value = GetGlobal(WorldContextObject, Name, FBangoType::Vector3(), TEXT("Get Bango Global Vector3"));
	bIsDefined = Value != nullptr;
	return Value ? FVector(Value->GetVectorPart(0), Value->GetVectorPart(1), Value->GetVectorPart(2)) : FVector::ZeroVector;
}

bool UBangoBlueprintLibrary::GetBangoDefineBool(const FString& Name, bool& bIsDefined)
{
	FBangoValue Value;
	bIsDefined = GetDefine(Name, FBangoType::Bool(), Value);
	return bIsDefined && Value.GetBool();
}

int32 UBangoBlueprintLibrary::GetBangoDefineInt(const FString& Name, bool& bIsDefined)
{
	FBangoValue Value;
	bIsDefined = GetDefine(Name, FBangoType::Int(), Value);
	return bIsDefined ? Value.GetInt() : 0;
}

double UBangoBlueprintLibrary::GetBangoDefineDouble(const FString& Name, bool& bIsDefined)
{
	FBangoValue Value;
	bIsDefined = GetDefine(Name, FBangoType::Double(), Value);
	return bIsDefined ? Value.GetDouble() : 0.0;
}

FString UBangoBlueprintLibrary::GetBangoDefineString(const FString& Name, bool& bIsDefined)
{
	FBangoValue Value;
	bIsDefined = GetDefine(Name, FBangoType::String(), Value);
	return bIsDefined ? Value.GetString() : FString();
}

FText UBangoBlueprintLibrary::GetBangoDefineText(const FString& Name, bool& bIsDefined)
{
	FBangoValue Value;
	bIsDefined = GetDefine(Name, FBangoType::Text(), Value);
	return bIsDefined ? Value.GetText() : FText::GetEmpty();
}

bool UBangoBlueprintLibrary::IsBangoDefine(const FString& Name)
{
	return Bango::FindDefine(Name) != nullptr;
}

void UBangoBlueprintLibrary::ClearBangoGlobal(UObject* WorldContextObject, const FString& Name)
{
	FBangoGlobalStore* Store = GetStore(WorldContextObject, TEXT("Clear Bango Global"));
	if (const FBangoValue* Value = Store ? Store->Find(Name) : nullptr)
	{
		FString Error;
		Store->Set(Name, FBangoValue::MakeNil(Value->GetType()), Error);
	}
}

bool UBangoBlueprintLibrary::SaveBangoState(UObject* WorldContextObject, TArray<uint8>& Data)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Save Bango State"));
	return Subsystem && Subsystem->SaveState(Data);
}

bool UBangoBlueprintLibrary::LoadBangoState(UObject* WorldContextObject, const TArray<uint8>& Data)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Load Bango State"));
	return Subsystem && Subsystem->LoadState(Data);
}

void UBangoBlueprintLibrary::ResetBangoState(UObject* WorldContextObject)
{
	if (UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Reset Bango State")))
	{
		Subsystem->ResetState();
	}
}

void UBangoBlueprintLibrary::SkipNextBangoLevelStartScript(UObject* WorldContextObject)
{
	if (UBangoGlobalsSubsystem* Globals = UBangoGlobalsSubsystem::Get(WorldContextObject))
	{
		Globals->SkipNextLevelStartScript();
	}
	else
	{
		Bango::ReportError(TEXT("Skip Next Bango Level Start Script: it needs a game instance (play in editor or a packaged game)."));
	}
}
