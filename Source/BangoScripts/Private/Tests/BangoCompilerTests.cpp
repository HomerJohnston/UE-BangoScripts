#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoCompiler.h"

#include "BangoCompilerTestCases.inl"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoCompilerCasesTest, "Bango.Compiler.Cases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoCompilerCasesTest::RunTest(const FString& Parameters)
{
	FBangoCommandTable Commands;
	BangoMakeTestCommands(Commands);
	TestEqual(TEXT("test commands registered"), Commands.Num(), 24); // with the core commands (Count, Push, InsertAt, Pop, RemoveAt)

	for (const FBangoCompilerTestCase& Case : GBangoCompilerTestCases)
	{
		FBangoProgram Program;
		FBangoCompileError Error;
		const bool bCompiled = Bango::CompileSource(Case.Source, TEXT("test.bango"), Commands, Program, Error);

		if (Case.ExpectedListing)
		{
			if (!bCompiled)
			{
				AddError(FString::Printf(TEXT("[%s] should compile, but failed: %s"), Case.Name, *Error.ToString()));
				continue;
			}
			const FString Listing = Bango::Disassemble(Program);
			if (FCString::Strcmp(Case.ExpectedListing, TEXT("*")) != 0 && !Listing.Equals(Case.ExpectedListing, ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(TEXT("[%s] listing differs.\nexpected:\n%s\nactual:\n%s"), Case.Name, Case.ExpectedListing, *Listing));
			}
		}
		else
		{
			if (bCompiled)
			{
				AddError(FString::Printf(TEXT("[%s] should fail, but compiled."), Case.Name));
				continue;
			}
			const FString Text = Error.ToString();
			if (!Text.Contains(Case.ErrorLocation, ESearchCase::CaseSensitive) || !Text.Contains(Case.ErrorContains, ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(TEXT("[%s]\n  expected %s containing \"%s\"\n  actual:   %s"), Case.Name, Case.ErrorLocation, Case.ErrorContains, *Text));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoCommandTableTest, "Bango.Compiler.CommandTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoCommandTableTest::RunTest(const FString& Parameters)
{
	FBangoCommandTable Table;
	FString Error;
	TestTrue(TEXT("add"), Table.Add(FBangoCommandSignature::Make(TEXT("Speak")).Target(FBangoType::Entity()).Param(TEXT("text"), FBangoType::String()), Error));
	TestFalse(TEXT("duplicate"), Table.Add(FBangoCommandSignature::Make(TEXT("Speak")), Error));
	TestFalse(TEXT("keyword"), Table.Add(FBangoCommandSignature::Make(TEXT("while")), Error));
	TestFalse(TEXT("local-style name"), Table.Add(FBangoCommandSignature::Make(TEXT("_x")), Error));
	TestNotNull(TEXT("find"), Table.FindCommand(TEXT("Speak")));
	TestNull(TEXT("case-sensitive"), Table.FindCommand(TEXT("speak")));

	const FBangoCommandSignature Sig = FBangoCommandSignature::Make(TEXT("AwaitSpeak")).Param(TEXT("a"), FBangoType::Int()).Optional().Param(TEXT("pause"), FBangoType::Double()).Variadic().Returns(FBangoType::Bool());
	TestEqual(TEXT("required"), Sig.GetNumRequiredParams(), 1);
	TestEqual(TEXT("ToString"), Sig.ToString(), FString(TEXT("AwaitSpeak(int a, float pause (optional), ...) -> bool")));

	// Type parameters.
	TestTrue(TEXT("core commands"), Bango::RegisterCoreCommands(Table, Error) && Table.FindCommand(TEXT("Count")) != nullptr && Table.FindCommand(TEXT("Count"))->bBuiltIn);
	TestFalse(TEXT("the array commands are commands like any other: the name is taken"), Table.Add(FBangoCommandSignature::Make(TEXT("Push")), Error));
	const FBangoCommandSignature* Push = Table.FindCommand(TEXT("Push"));
	TestTrue(TEXT("Push changes its target"), Push && Push->bMutableTarget && Push->bBuiltIn);
	TestEqual(TEXT("Push: ToString"), Push ? Push->ToString() : FString(), FString(TEXT("array Push(element Value)")));
	TestEqual(TEXT("RemoveAt: ToString"), Table.FindCommand(TEXT("RemoveAt"))->ToString(), FString(TEXT("array RemoveAt(int Index) -> element")));
	TestFalse(TEXT("MutableTarget needs a target"), Table.Add(FBangoCommandSignature::Make(TEXT("NoTarget")).MutableTarget(), Error));
	FBangoCommand Waits;
	Waits.Signature = FBangoCommandSignature::Make(TEXT("WaitsInPlace")).Target(FBangoType::Int()).MutableTarget();
	Waits.Poll = [](FBangoCommandCall&) { return EBangoCommandResult::Done; };
	TestFalse(TEXT("a command that changes its target can't wait"), Table.Add(Waits, Error));
	TestFalse(TEXT("core commands twice"), Bango::RegisterCoreCommands(Table, Error));
	const FBangoTypeVar T(TEXT("T"));
	const FBangoCommandSignature Blend = FBangoCommandSignature::Make(TEXT("Blend")).TypeParam(T, { FBangoType::Double(), FBangoType::Vector3() })
		.Param(TEXT("A"), T).Param(TEXT("B"), T).Param(TEXT("Alpha"), FBangoType::Double()).Returns(T);
	TestEqual(TEXT("type param: ToString"), Blend.ToString(), FString(TEXT("Blend(float|vector3 A, float|vector3 B, float Alpha) -> float|vector3")));
	TestTrue(TEXT("type param: add"), Table.Add(Blend, Error));
	const FBangoCommandSignature Number = FBangoCommandSignature::Make(TEXT("Biggest")).TypeParam(T, { FBangoType::Int(), FBangoType::Double() }, TEXT("number")).Param(TEXT("A"), T).Returns(T);
	TestEqual(TEXT("type param: a display name"), Number.ToString(), FString(TEXT("Biggest(number A) -> number")));
	TestFalse(TEXT("type param: not added"), Table.Add(FBangoCommandSignature::Make(TEXT("NoVar")).Param(TEXT("A"), T), Error));
	TestTrue(TEXT("type param: not added, the error"), Error.Contains(TEXT("hasn't been added")));
	TestFalse(TEXT("type param: a result no required parameter gives a type"), Table.Add(FBangoCommandSignature::Make(TEXT("FromNothing")).TypeParam(T, { FBangoType::Int() }).Optional().Param(TEXT("A"), T).Returns(T), Error));
	TArray<FBangoValue> Args = { FBangoValue::MakeInt(1), FBangoValue::MakeDouble(2.5), FBangoValue::MakeDouble(0.5) };
	TArray<FBangoType> TypeArgs;
	TestTrue(TEXT("type param: binds"), Blend.BindTypeParams(nullptr, Args, TypeArgs, Error) && TypeArgs.Num() == 1 && TypeArgs[0] == FBangoType::Double() && Args[0].GetType() == FBangoType::Double());
	Args = { FBangoValue::MakeVector3(0, 0, 0), FBangoValue::MakeDouble(2.5), FBangoValue::MakeDouble(0.5) };
	TestFalse(TEXT("type param: two types"), Blend.BindTypeParams(nullptr, Args, TypeArgs, Error));
	TestTrue(TEXT("type param: two types, the error"), Error.Contains(TEXT("'A' and 'B' must be the same type, but they are a vector3 and a float.")));
	// An array says what its element is: the value is fitted to it, never the array to the value.
	const FBangoValue Floats = FBangoValue::MakeArray(FBangoType::Double());
	Args = { FBangoValue::MakeInt(1) };
	TestTrue(TEXT("type param: an array target"), Push && Push->BindTypeParams(&Floats, Args, TypeArgs, Error) && TypeArgs[0] == FBangoType::Double() && Args[0].GetType() == FBangoType::Double());
	const FBangoValue Ints = FBangoValue::MakeArray(FBangoType::Int());
	Args = { FBangoValue::MakeDouble(2.5) };
	TestFalse(TEXT("type param: a float into an int array"), Push && Push->BindTypeParams(&Ints, Args, TypeArgs, Error));
	TestTrue(TEXT("type param: a float into an int array, the error"), Error.Contains(TEXT("argument 1 ('Value') must be an int, but it is a float.")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
