#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoGlobalStore.h"
#include "BangoTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// Run these from the editor: Tools > Session Frontend > Automation, filter "Bango".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoTypeTest, "Bango.Core.Types",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoTypeTest::RunTest(const FString& Parameters)
{
	FBangoType Parsed;
	TestTrue(TEXT("parse int"), FBangoType::FromString(TEXT("int"), Parsed) && Parsed == FBangoType::Int());
	TestTrue(TEXT("parse float[]"), FBangoType::FromString(TEXT("float[]"), Parsed) && Parsed == FBangoType::ArrayOf(EBangoBaseType::Double));
	TestFalse(TEXT("types are case-sensitive"), FBangoType::FromString(TEXT("Int"), Parsed));
	TestTrue(TEXT("parse int[][]"), FBangoType::FromString(TEXT("int[][]"), Parsed) && Parsed == FBangoType::ArrayOf(EBangoBaseType::Int, 2));
	TestTrue(TEXT("max depth parses"), FBangoType::FromString(TEXT("int[][][][][][][][]"), Parsed) && Parsed.ArrayDepth == FBangoType::MaxArrayDepth);
	TestFalse(TEXT("too deep fails"), FBangoType::FromString(TEXT("int[][][][][][][][][]"), Parsed));
	TestTrue(TEXT("element type"), FBangoType::ArrayOf(EBangoBaseType::Int, 2).GetElementType() == FBangoType::ArrayOf(EBangoBaseType::Int));
	TestTrue(TEXT("ArrayOf(type)"), FBangoType::ArrayOf(FBangoType::ArrayOf(EBangoBaseType::Int)) == FBangoType::ArrayOf(EBangoBaseType::Int, 2));
	TestTrue(TEXT("int[][] -> float[][]"), FBangoType::ArrayOf(EBangoBaseType::Double, 2).IsAssignableFrom(FBangoType::ArrayOf(EBangoBaseType::Int, 2)));
	TestFalse(TEXT("int[] -> float[][]"), FBangoType::ArrayOf(EBangoBaseType::Double, 2).IsAssignableFrom(FBangoType::ArrayOf(EBangoBaseType::Int)));
	TestEqual(TEXT("ToString"), FBangoType::ArrayOf(EBangoBaseType::Entity).ToString(), FString(TEXT("entity[]")));

	TestTrue(TEXT("int -> float"), FBangoType::Double().IsAssignableFrom(FBangoType::Int()));
	TestTrue(TEXT("int[] -> float[]"), FBangoType::ArrayOf(EBangoBaseType::Double).IsAssignableFrom(FBangoType::ArrayOf(EBangoBaseType::Int)));
	TestFalse(TEXT("float -> int"), FBangoType::Int().IsAssignableFrom(FBangoType::Double()));
	TestFalse(TEXT("int -> int[]"), FBangoType::ArrayOf(EBangoBaseType::Int).IsAssignableFrom(FBangoType::Int()));
	TestFalse(TEXT("string -> entity"), FBangoType::Entity().IsAssignableFrom(FBangoType::String()));
	TestTrue(TEXT("parse vector3[]"), FBangoType::FromString(TEXT("vector3[]"), Parsed) && Parsed == FBangoType::ArrayOf(EBangoBaseType::Vector3));
	TestEqual(TEXT("vector2 name"), FBangoType::Vector2().ToString(), FString(TEXT("vector2")));
	TestEqual(TEXT("vector sizes"), FBangoType::Vector2().GetVectorSize() * 10 + FBangoType::Vector3().GetVectorSize(), 23);
	TestFalse(TEXT("vector3[] is not a vector"), FBangoType::ArrayOf(EBangoBaseType::Vector3).IsVector());
	TestFalse(TEXT("float[] -> vector3"), FBangoType::Vector3().IsAssignableFrom(FBangoType::ArrayOf(EBangoBaseType::Double)));
	TestFalse(TEXT("vector2 -> vector3"), FBangoType::Vector3().IsAssignableFrom(FBangoType::Vector2()));
	TestFalse(TEXT("vector3 is reserved"), Bango::IsValidGlobalName(TEXT("vector3")));

	TestTrue(TEXT("global name"), Bango::IsValidGlobalName(TEXT("john")));
	TestTrue(TEXT("a global may start with '_'"), Bango::IsValidGlobalName(TEXT("_john")));
	TestFalse(TEXT("reserved word"), Bango::IsValidGlobalName(TEXT("forEach")));
	TestFalse(TEXT("starts with digit"), Bango::IsValidGlobalName(TEXT("17door")));
	TestTrue(TEXT("digits later"), Bango::IsValidGlobalName(TEXT("door17")));
	TestTrue(TEXT("Bango ID name"), Bango::IsValidBangoIDName(TEXT("door17")));
	TestTrue(TEXT("Bango ID names may start with '_'"), Bango::IsValidBangoIDName(TEXT("_john")));
	TestTrue(TEXT("Bango ID names may be keywords"), Bango::IsValidBangoIDName(TEXT("if")));
	TestFalse(TEXT("Bango ID names can't start with a digit"), Bango::IsValidBangoIDName(TEXT("17door")));
	TestFalse(TEXT("Bango ID names have no spaces"), Bango::IsValidBangoIDName(TEXT("big door")));
	TestFalse(TEXT("empty Bango ID name"), Bango::IsValidBangoIDName(TEXT("")));

	TestEqual(TEXT("format values: none"), Bango::CountFormatValues(TEXT("plain")), 0);
	TestEqual(TEXT("format values: highest wins"), Bango::CountFormatValues(TEXT("{0} and {2}")), 3);
	TestEqual(TEXT("format values: an escaped brace isn't one"), Bango::CountFormatValues(TEXT("`{5} of {0}")), 1);
	TestEqual(TEXT("format values: two digits"), Bango::CountFormatValues(TEXT("{12}")), 13);
	TestEqual(TEXT("format values: a named or unclosed brace isn't one"), Bango::CountFormatValues(TEXT("{Name} {3")), 0);
	TestEqual(TEXT("format values: % is just a character now"), Bango::CountFormatValues(TEXT("100% sure, 50%")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoValueTest, "Bango.Core.Values",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoValueTest::RunTest(const FString& Parameters)
{
	const FBangoValue NilInt = FBangoValue::MakeNil(FBangoType::Int());
	TestTrue(TEXT("nil is nil"), NilInt.IsNil());
	TestTrue(TEXT("nil keeps its type"), NilInt.GetType() == FBangoType::Int());

	FBangoValue Converted;
	TestTrue(TEXT("int converts to float"), FBangoValue::MakeInt(3).TryConvertTo(FBangoType::Double(), Converted));
	TestEqual(TEXT("converted value"), Converted.GetDouble(), 3.0);
	TestTrue(TEXT("nil int converts to nil float"), NilInt.TryConvertTo(FBangoType::Double(), Converted) && Converted.IsNil() && Converted.GetType() == FBangoType::Double());
	TestFalse(TEXT("float does not convert to int"), FBangoValue::MakeDouble(1.5).TryConvertTo(FBangoType::Int(), Converted));

	TestEqual(TEXT("debug string"), FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), NilInt }).ToDebugString(), FString(TEXT("[1, nil]")));
	TestEqual(TEXT("entity debug string"), FBangoValue::MakeEntity(TEXT("john")).ToDebugString(), FString(TEXT("@john")));
	TestEqual(TEXT("vector debug string"), FBangoValue::MakeVector3(1.0, -2.5, 3.0).ToDebugString(), FString(TEXT("(1.0, -2.5, 3.0)")));
	TestEqual(TEXT("vector2 display string"), FBangoValue::MakeVector2(0.5, 4.0).ToDisplayString(), FString(TEXT("(0.5, 4.0)")));
	TestTrue(TEXT("vectors compare by value"), FBangoValue::MakeVector3(1.0, 2.0, 3.0) == FBangoValue::MakeVector3(1.0, 2.0, 3.0));
	TestFalse(TEXT("vectors of different kinds differ"), FBangoValue::MakeVector2(1.0, 2.0) == FBangoValue::MakeVector3(1.0, 2.0, 0.0));
	FBangoValue Moved = FBangoValue::MakeVector3(1.0, 2.0, 3.0);
	Moved.SetVectorPart(1, 7.0);
	TestEqual(TEXT("set a vector part"), Moved.GetVectorPart(1), 7.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoArrayTest, "Bango.Core.Arrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoArrayTest::RunTest(const FString& Parameters)
{
	FString Error;
	FBangoValue A = FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) });

	// Writing past the end grows the array with nil.
	TestTrue(TEXT("grow on write"), A.SetElement(5, FBangoValue::MakeInt(9), Error));
	TestEqual(TEXT("grown size"), A.ArrayCount(), (int64)6);
	TestEqual(TEXT("grown contents"), A.ToDebugString(), FString(TEXT("[1, 2, nil, nil, nil, 9]")));

	// Reading past the end never grows.
	TestNull(TEXT("read past end"), A.GetElement(6));
	TestEqual(TEXT("read did not grow"), A.ArrayCount(), (int64)6);
	TestNull(TEXT("negative read"), A.GetElement(-1));

	TestFalse(TEXT("negative write fails"), A.SetElement(-1, FBangoValue::MakeInt(0), Error));
	TestFalse(TEXT("wrong element type fails"), A.SetElement(0, FBangoValue::MakeString(TEXT("x")), Error));
	TestFalse(TEXT("huge index fails"), A.SetElement(FBangoValue::MaxArraySize, FBangoValue::MakeInt(0), Error));

	// Push / InsertAt / Pop / RemoveAt
	FBangoValue B = FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2), FBangoValue::MakeInt(3) });
	TestTrue(TEXT("push"), B.Push(FBangoValue::MakeInt(8), Error));
	TestTrue(TEXT("insert at 0"), B.InsertAt(0, FBangoValue::MakeInt(8), Error));
	TestEqual(TEXT("after push/insert"), B.ToDebugString(), FString(TEXT("[8, 1, 2, 3, 8]")));

	TestTrue(TEXT("insert past end"), B.InsertAt(7, FBangoValue::MakeInt(4), Error));
	TestEqual(TEXT("insert past end fills nil"), B.ToDebugString(), FString(TEXT("[8, 1, 2, 3, 8, nil, nil, 4]")));

	FBangoValue Out;
	TestTrue(TEXT("pop"), B.Pop(Out, Error));
	TestEqual(TEXT("popped value"), Out.GetInt(), 4);
	TestTrue(TEXT("pop nil element is allowed"), B.Pop(Out, Error) && Out.IsNil());
	TestTrue(TEXT("remove at 0"), B.RemoveAt(0, Out, Error));
	TestEqual(TEXT("removed value"), Out.GetInt(), 8);
	TestFalse(TEXT("remove out of range"), B.RemoveAt(10, Out, Error));
	TestEqual(TEXT("after removes"), B.ToDebugString(), FString(TEXT("[1, 2, 3, 8, nil]")));

	FBangoValue Empty = FBangoValue::MakeArray(FBangoType::String());
	TestFalse(TEXT("pop empty fails"), Empty.Pop(Out, Error));

	FBangoValue NilArray = FBangoValue::MakeNil(FBangoType::ArrayOf(EBangoBaseType::Int));
	TestFalse(TEXT("push on nil array fails"), NilArray.Push(FBangoValue::MakeInt(1), Error));

	// int elements go into double arrays.
	FBangoValue D = FBangoValue::MakeArray(FBangoType::Double());
	TestTrue(TEXT("push int into float[]"), D.Push(FBangoValue::MakeInt(2), Error));
	TestTrue(TEXT("stored as float"), D.GetElement(0)->GetType() == FBangoType::Double());

	// Arrays are values.
	FBangoValue Copy = B;
	TestTrue(TEXT("push to copy"), Copy.Push(FBangoValue::MakeInt(99), Error));
	TestTrue(TEXT("original unchanged"), B.ArrayCount() == 5 && Copy.ArrayCount() == 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoNestedArrayTest, "Bango.Core.NestedArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoNestedArrayTest::RunTest(const FString& Parameters)
{
	FString Error;
	const FBangoType IntArray = FBangoType::ArrayOf(EBangoBaseType::Int);

	// int[][] _grid = [[1, 2], [3]];
	FBangoValue Grid = FBangoValue::MakeArray(IntArray, {
		FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeInt(2) }),
		FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(3) }) });
	TestEqual(TEXT("grid type"), Grid.GetType().ToString(), FString(TEXT("int[][]")));
	TestEqual(TEXT("grid contents"), Grid.ToDebugString(), FString(TEXT("[[1, 2], [3]]")));

	// Reads
	const FBangoValue* Element = Grid.GetElementAtPath({ 0, 1 });
	TestTrue(TEXT("_grid[0][1] == 2"), Element && Element->GetInt() == 2);
	TestNull(TEXT("_grid[1][5] out of range"), Grid.GetElementAtPath({ 1, 5 }));
	TestNull(TEXT("_grid[5][0] out of range"), Grid.GetElementAtPath({ 5, 0 }));
	TestNull(TEXT("too many indices"), Grid.GetElementAtPath({ 0, 0, 0 }));
	const FBangoValue* Row = Grid.GetElementAtPath({ 1 });
	TestTrue(TEXT("_grid[1] is an int[]"), Row && Row->GetType() == IntArray);

	// Option A: writing creates missing inner arrays.
	TestTrue(TEXT("_grid[3][2] = 7"), Grid.SetElementAtPath({ 3, 2 }, FBangoValue::MakeInt(7), Error));
	TestEqual(TEXT("auto-created inner array (the gap at [2] stays nil)"), Grid.ToDebugString(), FString(TEXT("[[1, 2], [3], nil, [nil, nil, 7]]")));

	// An explicitly nil row is also created on write.
	TestTrue(TEXT("_grid[1] = nil"), Grid.SetElement(1, FBangoValue::MakeNil(IntArray), Error));
	TestNull(TEXT("can't read through a nil row"), Grid.GetElementAtPath({ 1, 0 }));
	TestTrue(TEXT("_grid[1][0] = 5"), Grid.SetElementAtPath({ 1, 0 }, FBangoValue::MakeInt(5), Error));
	TestEqual(TEXT("nil row recreated"), Grid.ToDebugString(), FString(TEXT("[[1, 2], [5], nil, [nil, nil, 7]]")));

	// Failed writes change nothing.
	const FString Before = Grid.ToDebugString();
	TestFalse(TEXT("wrong type"), Grid.SetElementAtPath({ 9, 0 }, FBangoValue::MakeString(TEXT("x")), Error));
	TestFalse(TEXT("negative index"), Grid.SetElementAtPath({ 9, -1 }, FBangoValue::MakeInt(1), Error));
	TestFalse(TEXT("too many indices"), Grid.SetElementAtPath({ 0, 0, 0 }, FBangoValue::MakeInt(1), Error));
	TestEqual(TEXT("unchanged after failures"), Grid.ToDebugString(), Before);

	// A whole row can be written with one index.
	TestTrue(TEXT("_grid[0] = [9]"), Grid.SetElementAtPath({ 0 }, FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(9) }), Error));
	TestFalse(TEXT("_grid[0] = 9 fails"), Grid.SetElementAtPath({ 0 }, FBangoValue::MakeInt(9), Error));

	// In-place commands: _grid[0] Push(4);
	FBangoValue* Target = Grid.FindElementAtPath({ 0 }, Error);
	TestTrue(TEXT("find row"), Target && Target->Push(FBangoValue::MakeInt(4), Error));
	TestEqual(TEXT("pushed into row"), Grid.ToDebugString(), FString(TEXT("[[9, 4], [5], nil, [nil, nil, 7]]")));
	TestNull(TEXT("find out of range fails"), Grid.FindElementAtPath({ 10 }, Error));

	// The root array must not be nil.
	FBangoValue NilGrid = FBangoValue::MakeNil(FBangoType::ArrayOf(EBangoBaseType::Int, 2));
	TestFalse(TEXT("write into nil root fails"), NilGrid.SetElementAtPath({ 0, 0 }, FBangoValue::MakeInt(1), Error));

	// int[][] converts to double[][].
	FBangoValue AsDouble;
	TestTrue(TEXT("convert int[][] to float[][]"), Grid.TryConvertTo(FBangoType::ArrayOf(EBangoBaseType::Double, 2), AsDouble));
	const FBangoValue* Converted = AsDouble.GetElementAtPath({ 3, 2 });
	TestTrue(TEXT("converted element is float"), Converted && Converted->GetType() == FBangoType::Double() && Converted->GetDouble() == 7.0);

	// Copies are deep.
	FBangoValue Copy = Grid;
	TestTrue(TEXT("change copy"), Copy.SetElementAtPath({ 0, 0 }, FBangoValue::MakeInt(100), Error));
	TestTrue(TEXT("original unchanged"), Grid.GetElementAtPath({ 0, 0 })->GetInt() == 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoSerializationTest, "Bango.Core.Serialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoSerializationTest::RunTest(const FString& Parameters)
{
	TArray<FBangoValue> Originals =
	{
		FBangoValue::MakeBool(true),
		FBangoValue::MakeInt(-42),
		FBangoValue::MakeDouble(2.5),
		FBangoValue::MakeString(TEXT("Hello \"there\"")),
		FBangoValue::MakeEntity(TEXT("john")),
		FBangoValue::MakeNil(FBangoType::String()),
		FBangoValue::MakeVector2(1.5, -2.0),
		FBangoValue::MakeVector3(100.0, 50.0, 75.25),
		FBangoValue::MakeNil(FBangoType::Vector3()),
		FBangoValue::MakeArray(FBangoType::Vector3(), { FBangoValue::MakeVector3(1.0, 2.0, 3.0), FBangoValue::MakeNil(FBangoType::Vector3()) }),
		FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1), FBangoValue::MakeNil(FBangoType::Int()) }),
		FBangoValue::MakeArray(FBangoType::ArrayOf(EBangoBaseType::Int), {
			FBangoValue::MakeArray(FBangoType::Int(), { FBangoValue::MakeInt(1) }),
			FBangoValue::MakeNil(FBangoType::ArrayOf(EBangoBaseType::Int)) }),
	};

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		Writer << Originals;
	}

	TArray<FBangoValue> Loaded;
	{
		FMemoryReader Reader(Bytes);
		Reader << Loaded;
		TestFalse(TEXT("no read error"), Reader.IsError());
	}

	TestEqual(TEXT("same count"), Loaded.Num(), Originals.Num());
	for (int32 i = 0; i < FMath::Min(Loaded.Num(), Originals.Num()); ++i)
	{
		TestTrue(FString::Printf(TEXT("round trip %s"), *Originals[i].ToDebugString()), Loaded[i] == Originals[i]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoGlobalsTest, "Bango.Core.Globals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoGlobalsTest::RunTest(const FString& Parameters)
{
	FBangoGlobalStore Store;
	FBangoGlobalStore* Globals = &Store;
	FString Error;

	TestFalse(TEXT("never-created global is not defined"), Globals->IsDefined(TEXT("gold")));
	TestNull(TEXT("never-created global is not found"), Globals->Find(TEXT("gold")));
	TestFalse(TEXT("can't set undeclared global"), Globals->Set(TEXT("gold"), FBangoValue::MakeInt(1), Error));

	TestTrue(TEXT("declare"), Globals->Declare(TEXT("gold"), FBangoType::Int(), Error));
	TestFalse(TEXT("declared global starts nil"), Globals->IsDefined(TEXT("gold")));
	TestTrue(TEXT("same-type redeclare is fine"), Globals->Declare(TEXT("gold"), FBangoType::Int(), Error));
	TestFalse(TEXT("different type conflicts"), Globals->Declare(TEXT("gold"), FBangoType::Double(), Error));
	TestTrue(TEXT("conflict keeps original type"), Globals->Find(TEXT("gold"))->GetType() == FBangoType::Int());

	TestTrue(TEXT("set"), Globals->Set(TEXT("gold"), FBangoValue::MakeInt(5), Error));
	TestTrue(TEXT("now defined"), Globals->IsDefined(TEXT("gold")));
	TestFalse(TEXT("wrong type set fails"), Globals->Set(TEXT("gold"), FBangoValue::MakeString(TEXT("5")), Error));
	TestTrue(TEXT("set to nil"), Globals->Set(TEXT("gold"), FBangoValue::MakeNil(FBangoType::Int()), Error));
	TestFalse(TEXT("nil again"), Globals->IsDefined(TEXT("gold")));

	TestTrue(TEXT("declare float"), Globals->Declare(TEXT("speed"), FBangoType::Double(), Error));
	TestTrue(TEXT("int into float global"), Globals->Set(TEXT("speed"), FBangoValue::MakeInt(3), Error));
	TestTrue(TEXT("stored as float"), Globals->Find(TEXT("speed"))->GetType() == FBangoType::Double());

	TestFalse(TEXT("names are case-sensitive"), Globals->Find(TEXT("Speed")) != nullptr);
	{
		FBangoGlobalStore Spare; // not the store being saved below
		TestTrue(TEXT("a name starting with '_' is a valid global"), Spare.Declare(TEXT("_x"), FBangoType::Int(), Error));
	}

	// Save, reset, load.
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		Globals->Serialize(Writer);
	}
	Globals->ResetAll();
	TestEqual(TEXT("reset"), Globals->Num(), 0);
	{
		FMemoryReader Reader(Bytes);
		Globals->Serialize(Reader);
		TestFalse(TEXT("no read error"), Reader.IsError());
	}
	TestEqual(TEXT("loaded count"), Globals->Num(), 2);
	TestTrue(TEXT("loaded speed"), Globals->Find(TEXT("speed")) && *Globals->Find(TEXT("speed")) == FBangoValue::MakeDouble(3.0));
	TestTrue(TEXT("loaded gold still nil int"), Globals->Find(TEXT("gold")) && *Globals->Find(TEXT("gold")) == FBangoValue::MakeNil(FBangoType::Int()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
