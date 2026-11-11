#include "BangoBlueprintLibrary.h"

#include "Bango.h"
#include "BangoActorRegistry.h"
#include "BangoGlobalsSubsystem.h"
#include "BangoLog.h"
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

	static bool SetGlobal(UObject* WorldContextObject, const FString& Name, const FBangoValue& Value, const TCHAR* What)
	{
		FBangoGlobalStore* Store = GetStore(WorldContextObject, What);
		if (!Store)
		{
			return false;
		}
		FString Error;
		if ((!Store->Find(Name) && !Store->Declare(Name, Value.GetType(), Error)) || !Store->Set(Name, Value, Error))
		{
			Bango::ReportError(FString::Printf(TEXT("%s: %s"), What, *Error));
			return false;
		}
		return true;
	}

	/** The global's value if it exists, isn't nil and has the given type; otherwise nullptr (with an error for a wrong type). */
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
				OutError = FString::Printf(TEXT("Argument %d was left out, but that parameter has no default. Refresh the Spawn Bango Script node."), i + 1);
				return false;
			}
			OutValues.Add(Params[i].Default);
		}
		return true;
	}
}

namespace BangoBlueprintPrivate
{
	static FBangoBlueprintArgs AddEntity(const FBangoBlueprintArgs& Args, const AActor* Actor, FBangoValue& OutValue)
	{
		FBangoBlueprintArgs Result = Args;
		FString Error;
		if (!Bango::ActorToEntity(Actor, OutValue, Error) && Result.Error.IsEmpty())
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

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoScriptWithArgs(UObject* WorldContextObject, const FString& ScriptPath, const FBangoBlueprintArgs& Args)
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
	return Subsystem->Spawn(ScriptPath, Values);
}

FBangoScriptHandle UBangoBlueprintLibrary::SpawnBangoScript(UObject* WorldContextObject, const FString& ScriptPath)
{
	UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Spawn Bango Script"));
	return Subsystem ? Subsystem->Spawn(ScriptPath) : FBangoScriptHandle();
}

void UBangoBlueprintLibrary::TerminateBangoScript(UObject* WorldContextObject, FBangoScriptHandle Handle)
{
	if (UBangoScriptSubsystem* Subsystem = GetScripts(WorldContextObject, TEXT("Terminate Bango Script")))
	{
		Subsystem->Terminate(Handle);
	}
}

bool UBangoBlueprintLibrary::IsBangoScriptRunning(UObject* WorldContextObject, FBangoScriptHandle Handle)
{
	const UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsRunning(Handle);
}

bool UBangoBlueprintLibrary::SetBangoGlobalBool(UObject* WorldContextObject, const FString& Name, bool Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeBool(Value), TEXT("Set Bango Global Bool"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalInt(UObject* WorldContextObject, const FString& Name, int32 Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeInt(Value), TEXT("Set Bango Global Int"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalDouble(UObject* WorldContextObject, const FString& Name, double Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeDouble(Value), TEXT("Set Bango Global Float"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalString(UObject* WorldContextObject, const FString& Name, const FString& Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeString(Value), TEXT("Set Bango Global String"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalText(UObject* WorldContextObject, const FString& Name, const FText& Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeText(Value), TEXT("Set Bango Global Text"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalVector2(UObject* WorldContextObject, const FString& Name, FVector2D Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeVector2(Value.X, Value.Y), TEXT("Set Bango Global Vector2"));
}

bool UBangoBlueprintLibrary::SetBangoGlobalVector3(UObject* WorldContextObject, const FString& Name, FVector Value)
{
	return SetGlobal(WorldContextObject, Name, FBangoValue::MakeVector3(Value.X, Value.Y, Value.Z), TEXT("Set Bango Global Vector3"));
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
