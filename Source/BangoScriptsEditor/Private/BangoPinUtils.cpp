#include "BangoPinUtils.h"

#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/Class.h"

namespace BangoPinUtils
{
	bool MakePinType(const FBangoType& Type, FEdGraphPinType& OutPinType, FString& OutError)
	{
		if (Type.ArrayDepth > 1)
		{
			OutError = FString::Printf(TEXT("%s can't be passed from Blueprint (Blueprint has no arrays of arrays)."), *Type.ToString());
			return false;
		}

		OutPinType = FEdGraphPinType();
		switch (Type.Base)
		{
		case EBangoBaseType::Bool:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			break;
		case EBangoBaseType::Int:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			break;
		case EBangoBaseType::Double:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			break;
		case EBangoBaseType::String:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			break;
		case EBangoBaseType::Text:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			break;
		case EBangoBaseType::Entity:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = AActor::StaticClass();
			break;
		case EBangoBaseType::Vector2:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
			break;
		case EBangoBaseType::Vector3:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			break;
		default:
			OutError = TEXT("Unknown type.");
			return false;
		}
		OutPinType.ContainerType = Type.IsArray() ? EPinContainerType::Array : EPinContainerType::None;
		return true;
	}

	bool PinTypesMatch(const FEdGraphPinType& A, const FEdGraphPinType& B)
	{
		return A.PinCategory == B.PinCategory
			&& A.PinSubCategory == B.PinSubCategory
			&& A.PinSubCategoryObject == B.PinSubCategoryObject
			&& A.ContainerType == B.ContainerType;
	}

	FString DefaultValueText(const FBangoScriptParamInfo& Param)
	{
		if (!Param.bHasDefault || Param.Default.IsNil() || Param.Type.IsArray())
		{
			return FString();
		}
		switch (Param.Type.Base)
		{
		case EBangoBaseType::Bool:   return Param.Default.GetBool() ? TEXT("true") : TEXT("false");
		case EBangoBaseType::Int:    return FString::Printf(TEXT("%d"), Param.Default.GetInt());
		case EBangoBaseType::Double: return FString::SanitizeFloat(Param.Default.GetDouble());
		case EBangoBaseType::String: return Param.Default.GetString();
		// The formats Blueprint vector pins read: "X,Y,Z" for a Vector, "(X=..,Y=..)" for a Vector 2D.
		case EBangoBaseType::Vector2:
			return FString::Printf(TEXT("(X=%s,Y=%s)"), *FString::SanitizeFloat(Param.Default.GetVectorPart(0)), *FString::SanitizeFloat(Param.Default.GetVectorPart(1)));
		case EBangoBaseType::Vector3:
			return FString::Printf(TEXT("%s,%s,%s"), *FString::SanitizeFloat(Param.Default.GetVectorPart(0)),
				*FString::SanitizeFloat(Param.Default.GetVectorPart(1)), *FString::SanitizeFloat(Param.Default.GetVectorPart(2)));
		default:                     return FString();
		}
	}

	bool IsUnchangedDefault(const FBangoScriptParamInfo& Param, const FString& PinDefaultValue)
	{
		if (!Param.bHasDefault)
		{
			return false;
		}
		if (Param.Type.IsArray() || Param.Type.Base == EBangoBaseType::Entity)
		{
			return true;
		}
		if (Param.Default.IsNil())
		{
			return PinDefaultValue.IsEmpty();
		}
		switch (Param.Type.Base)
		{
		case EBangoBaseType::Bool:
			return PinDefaultValue.ToBool() == Param.Default.GetBool() && !PinDefaultValue.IsEmpty();
		case EBangoBaseType::Int:
			return !PinDefaultValue.IsEmpty() && FCString::Atoi(*PinDefaultValue) == Param.Default.GetInt();
		case EBangoBaseType::Double:
			return !PinDefaultValue.IsEmpty() && FCString::Atod(*PinDefaultValue) == Param.Default.GetDouble();
		case EBangoBaseType::String:
			return PinDefaultValue.Equals(Param.Default.GetString(), ESearchCase::CaseSensitive);
		case EBangoBaseType::Vector2:
		{
			FVector2D Vector = FVector2D::ZeroVector;
			return !PinDefaultValue.IsEmpty() && Vector.InitFromString(PinDefaultValue)
				&& Vector.X == Param.Default.GetVectorPart(0) && Vector.Y == Param.Default.GetVectorPart(1);
		}
		case EBangoBaseType::Vector3:
		{
			FVector Vector = FVector::ZeroVector;
			return !PinDefaultValue.IsEmpty() && FDefaultValueHelper::ParseVector(PinDefaultValue, Vector)
				&& Vector.X == Param.Default.GetVectorPart(0) && Vector.Y == Param.Default.GetVectorPart(1) && Vector.Z == Param.Default.GetVectorPart(2);
		}
		case EBangoBaseType::Text:
			// A text pin starts empty (the script's default is localized; it isn't copied into the pin), so an
			// empty pin means "use the script's default". PinDefaultValue is the pin's text here.
			return PinDefaultValue.IsEmpty();
		default:
			return false;
		}
	}

	FName GetAddFunctionName(const FBangoType& Type)
	{
		const bool bArray = Type.IsArray();
		switch (Type.Base)
		{
		case EBangoBaseType::Bool:   return bArray ? FName(TEXT("AddBangoArgBoolArray")) : FName(TEXT("AddBangoArgBool"));
		case EBangoBaseType::Int:    return bArray ? FName(TEXT("AddBangoArgIntArray")) : FName(TEXT("AddBangoArgInt"));
		case EBangoBaseType::Double: return bArray ? FName(TEXT("AddBangoArgDoubleArray")) : FName(TEXT("AddBangoArgDouble"));
		case EBangoBaseType::String: return bArray ? FName(TEXT("AddBangoArgStringArray")) : FName(TEXT("AddBangoArgString"));
		case EBangoBaseType::Text:   return bArray ? FName(TEXT("AddBangoArgTextArray")) : FName(TEXT("AddBangoArgText"));
		case EBangoBaseType::Entity: return bArray ? FName(TEXT("AddBangoArgActorArray")) : FName(TEXT("AddBangoArgActor"));
		case EBangoBaseType::Vector2: return bArray ? FName(TEXT("AddBangoArgVector2Array")) : FName(TEXT("AddBangoArgVector2"));
		case EBangoBaseType::Vector3: return bArray ? FName(TEXT("AddBangoArgVector3Array")) : FName(TEXT("AddBangoArgVector3"));
		default:                     return NAME_None;
		}
	}

	FString PinLabel(const FString& ParamName)
	{
		return ParamName.StartsWith(TEXT("_"), ESearchCase::CaseSensitive) ? ParamName.Mid(1) : ParamName;
	}
}
