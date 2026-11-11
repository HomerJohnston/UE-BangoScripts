#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoGraph.h"
#include "BangoGraphLayout.h"
#include "BangoGraphEdit.h"
#include "BangoGraphMenu.h"
#include "BangoParser.h"
#include "BangoCompiler.h"
#include "BangoInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "BangoParserTestCases.inl"
#include "BangoCompilerTestCases.inl"
#include "BangoInstanceTestCases.inl"
#include "BangoGraphTestCases.inl"
#include "BangoGraphLayoutTestCases.inl"
#include "BangoGraphEditTestCases.inl"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoGraphRoundTripTest, "Bango.Graph.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoGraphRoundTripTest::RunTest(const FString& Parameters)
{
	// Every test script that parses must survive text -> graph -> text unchanged in meaning, and stay stable.
	auto Check = [this](const TCHAR* Name, const TCHAR* Source)
	{
		const FString Failure = BangoCheckGraphRoundTrip(Name, Source);
		if (!Failure.IsEmpty())
		{
			AddError(Failure);
		}
	};
	for (const FBangoParserTestCase& Case : GBangoParserTestCases)
	{
		Check(Case.Name, Case.Source);
	}
	for (const FBangoCompilerTestCase& Case : GBangoCompilerTestCases)
	{
		Check(Case.Name, Case.Source);
	}
	for (const FBangoRuntimeTestCase& Case : GBangoRuntimeTestCases)
	{
		Check(Case.Name, Case.Source);
	}
	int32 Index = 0;
	for (const TCHAR* Source : GBangoGraphRoundTripSources)
	{
		Check(*FString::Printf(TEXT("extra %d"), Index++), Source);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoGraphScenariosTest, "Bango.Graph.Scenarios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoGraphScenariosTest::RunTest(const FString& Parameters)
{
	TArray<FString> Failures;
	BangoRunGraphScenarios(Failures);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoGraphLayoutTest, "Bango.Graph.Layouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoGraphLayoutTest::RunTest(const FString& Parameters)
{
	TArray<FString> Failures;
	BangoRunGraphLayoutScenarios(Failures);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoGraphEditTest, "Bango.Graph.Edits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoGraphEditTest::RunTest(const FString& Parameters)
{
	TArray<FString> Failures;
	BangoRunGraphEditScenarios(Failures);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return true;
}

#endif
