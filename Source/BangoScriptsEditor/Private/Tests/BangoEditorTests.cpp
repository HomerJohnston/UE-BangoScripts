#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoPinUtils.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoPinUtilsTest, "Bango.Editor.PinUtils",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoPinUtilsTest::RunTest(const FString& Parameters)
{
	using namespace BangoPinUtils;
	FEdGraphPinType PinType;
	FString Error;

	TestTrue(TEXT("int"), MakePinType(FBangoType::Int(), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Int && PinType.ContainerType == EPinContainerType::None);
	TestTrue(TEXT("float"), MakePinType(FBangoType::Double(), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Real && PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double);
	TestTrue(TEXT("bool"), MakePinType(FBangoType::Bool(), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	TestTrue(TEXT("entity"), MakePinType(FBangoType::Entity(), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Object && PinType.PinSubCategoryObject == AActor::StaticClass());
	TestTrue(TEXT("string[]"), MakePinType(FBangoType::ArrayOf(EBangoBaseType::String), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_String && PinType.ContainerType == EPinContainerType::Array);
	TestFalse(TEXT("int[][] can't come from Blueprint"), MakePinType(FBangoType::ArrayOf(EBangoBaseType::Int, 2), PinType, Error));
	TestTrue(TEXT("vector3 is a Vector"), MakePinType(FBangoType::Vector3(), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get());
	TestTrue(TEXT("vector2[] is an array of Vector 2D"), MakePinType(FBangoType::ArrayOf(EBangoBaseType::Vector2), PinType, Error) && PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
		&& PinType.PinSubCategoryObject == TBaseStructure<FVector2D>::Get() && PinType.ContainerType == EPinContainerType::Array);

	auto Param = [](const FBangoType& Type, bool bHasDefault, const FBangoValue& Default)
	{
		FBangoScriptParamInfo Info;
		Info.Name = TEXT("_p");
		Info.Type = Type;
		Info.bHasDefault = bHasDefault;
		Info.Default = Default;
		return Info;
	};
	const FBangoScriptParamInfo Required = Param(FBangoType::Int(), false, FBangoValue());
	const FBangoScriptParamInfo IntFive = Param(FBangoType::Int(), true, FBangoValue::MakeInt(5));
	const FBangoScriptParamInfo DoubleHalf = Param(FBangoType::Double(), true, FBangoValue::MakeDouble(2.5));
	const FBangoScriptParamInfo BoolTrue = Param(FBangoType::Bool(), true, FBangoValue::MakeBool(true));
	const FBangoScriptParamInfo StringHi = Param(FBangoType::String(), true, FBangoValue::MakeString(TEXT("hi")));
	const FBangoScriptParamInfo IntNil = Param(FBangoType::Int(), true, FBangoValue::MakeNil(FBangoType::Int()));
	const FBangoScriptParamInfo EntityNil = Param(FBangoType::Entity(), true, FBangoValue::MakeNil(FBangoType::Entity()));
	const FBangoScriptParamInfo EntityJohn = Param(FBangoType::Entity(), true, FBangoValue::MakeEntity(TEXT("john")));
	const FBangoScriptParamInfo ArrayDefault = Param(FBangoType::ArrayOf(EBangoBaseType::Int), true, FBangoValue::MakeArray(FBangoType::Int()));

	TestEqual(TEXT("default text int"), DefaultValueText(IntFive), FString(TEXT("5")));
	TestEqual(TEXT("default text float"), DefaultValueText(DoubleHalf), FString(TEXT("2.5")));
	TestEqual(TEXT("default text bool"), DefaultValueText(BoolTrue), FString(TEXT("true")));
	TestEqual(TEXT("default text string"), DefaultValueText(StringHi), FString(TEXT("hi")));
	TestEqual(TEXT("default text nil"), DefaultValueText(IntNil), FString());
	TestEqual(TEXT("default text required"), DefaultValueText(Required), FString());

	TestFalse(TEXT("required is always passed"), IsUnchangedDefault(Required, TEXT("0")));
	TestTrue(TEXT("int unchanged"), IsUnchangedDefault(IntFive, TEXT("5")));
	TestFalse(TEXT("int changed"), IsUnchangedDefault(IntFive, TEXT("6")));
	TestFalse(TEXT("int cleared"), IsUnchangedDefault(IntFive, TEXT("")));
	TestTrue(TEXT("float compared by value"), IsUnchangedDefault(DoubleHalf, TEXT("2.500000")));
	TestFalse(TEXT("float changed"), IsUnchangedDefault(DoubleHalf, TEXT("2.4")));
	TestTrue(TEXT("bool unchanged"), IsUnchangedDefault(BoolTrue, TEXT("true")));
	TestFalse(TEXT("bool changed"), IsUnchangedDefault(BoolTrue, TEXT("false")));
	TestTrue(TEXT("string unchanged"), IsUnchangedDefault(StringHi, TEXT("hi")));
	TestFalse(TEXT("string is case-sensitive"), IsUnchangedDefault(StringHi, TEXT("Hi")));
	TestTrue(TEXT("nil default while empty"), IsUnchangedDefault(IntNil, TEXT("")));
	TestFalse(TEXT("nil default once typed"), IsUnchangedDefault(IntNil, TEXT("0")));
	TestTrue(TEXT("unconnected optional entity"), IsUnchangedDefault(EntityNil, TEXT("")));
	TestTrue(TEXT("unconnected entity with an @john default uses the script's default"), IsUnchangedDefault(EntityJohn, TEXT("")));
	TestEqual(TEXT("an Actor pin can't show @john as its default"), DefaultValueText(EntityJohn), FString());
	TestTrue(TEXT("unconnected optional array"), IsUnchangedDefault(ArrayDefault, TEXT("")));

	const FBangoScriptParamInfo Vector3Default = Param(FBangoType::Vector3(), true, FBangoValue::MakeVector3(100.0, 50.0, 75.5));
	const FBangoScriptParamInfo Vector2Default = Param(FBangoType::Vector2(), true, FBangoValue::MakeVector2(1.0, -2.0));
	TestEqual(TEXT("default text vector3"), DefaultValueText(Vector3Default), FString(TEXT("100.0,50.0,75.5")));
	TestEqual(TEXT("default text vector2"), DefaultValueText(Vector2Default), FString(TEXT("(X=1.0,Y=-2.0)")));
	TestTrue(TEXT("vector3 unchanged"), IsUnchangedDefault(Vector3Default, TEXT("100.000000,50.000000,75.500000")));
	TestFalse(TEXT("vector3 changed"), IsUnchangedDefault(Vector3Default, TEXT("100,50,75")));
	TestTrue(TEXT("vector2 unchanged"), IsUnchangedDefault(Vector2Default, TEXT("(X=1.000000,Y=-2.000000)")));
	TestFalse(TEXT("vector2 changed"), IsUnchangedDefault(Vector2Default, TEXT("(X=1.000000,Y=2.000000)")));
	TestEqual(TEXT("add function for vector3"), GetAddFunctionName(FBangoType::Vector3()), FName(TEXT("AddBangoArgVector3")));

	TestEqual(TEXT("add function"), GetAddFunctionName(FBangoType::ArrayOf(EBangoBaseType::Entity)), FName(TEXT("AddBangoArgActorArray")));
	TestEqual(TEXT("pin label"), PinLabel(TEXT("_speaker")), FString(TEXT("speaker")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
