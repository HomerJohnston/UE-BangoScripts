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
	TestEqual(TEXT("test commands registered"), Commands.Num(), 16);

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
	TestFalse(TEXT("built-in name"), Table.Add(FBangoCommandSignature::Make(TEXT("Push")), Error));
	TestFalse(TEXT("keyword"), Table.Add(FBangoCommandSignature::Make(TEXT("while")), Error));
	TestFalse(TEXT("local-style name"), Table.Add(FBangoCommandSignature::Make(TEXT("_x")), Error));
	TestNotNull(TEXT("find"), Table.FindCommand(TEXT("Speak")));
	TestNull(TEXT("case-sensitive"), Table.FindCommand(TEXT("speak")));

	const FBangoCommandSignature Sig = FBangoCommandSignature::Make(TEXT("AwaitSpeak")).Param(TEXT("a"), FBangoType::Int()).Optional().Param(TEXT("pause"), FBangoType::Double()).Variadic().Returns(FBangoType::Bool());
	TestEqual(TEXT("required"), Sig.GetNumRequiredParams(), 1);
	TestEqual(TEXT("ToString"), Sig.ToString(), FString(TEXT("AwaitSpeak(int a, float pause (optional), ...) -> bool")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
