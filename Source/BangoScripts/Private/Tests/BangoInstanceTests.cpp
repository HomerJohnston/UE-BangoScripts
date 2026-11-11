#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoCompiler.h"
#include "BangoInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "BangoInstanceTestCases.inl"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoRuntimeCasesTest, "Bango.Runtime.Cases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoRuntimeCasesTest::RunTest(const FString& Parameters)
{
	for (const FBangoRuntimeTestCase& Case : GBangoRuntimeTestCases)
	{
		const FString Failure = BangoCheckRuntimeCase(Case);
		if (!Failure.IsEmpty())
		{
			AddError(Failure);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoRuntimeScenariosTest, "Bango.Runtime.Scenarios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoRuntimeScenariosTest::RunTest(const FString& Parameters)
{
	TArray<FString> Failures;
	BangoRunInstanceScenarios(Failures);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoRuntimeArgumentsTest, "Bango.Runtime.Arguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoRuntimeArgumentsTest::RunTest(const FString& Parameters)
{
	TArray<FString> Failures;
	BangoRunArgumentTests(Failures);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
