#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoParser.h"

#include "BangoParserTestCases.inl"

// Run from the editor: Tools > Session Frontend > Automation, filter "Bango".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoParserCasesTest, "Bango.Parser.Cases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoParserCasesTest::RunTest(const FString& Parameters)
{
	for (const FBangoParserTestCase& Case : GBangoParserTestCases)
	{
		FBangoScript Script;
		FBangoCompileError Error;
		const bool bParsed = Bango::ParseScript(Case.Source, TEXT("test.bango"), Script, Error);

		if (Case.ExpectedDump)
		{
			if (!bParsed)
			{
				AddError(FString::Printf(TEXT("[%s] should parse, but failed: %s"), Case.Name, *Error.ToString()));
				continue;
			}
			const FString Dump = Bango::DumpScript(Script);
			if (FCString::Strcmp(Case.ExpectedDump, TEXT("*")) != 0 && !Dump.Equals(Case.ExpectedDump, ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(TEXT("[%s]\n  expected: %s\n  actual:   %s"), Case.Name, Case.ExpectedDump, *Dump));
			}
		}
		else
		{
			if (bParsed)
			{
				AddError(FString::Printf(TEXT("[%s] should fail, but parsed: %s"), Case.Name, *Bango::DumpScript(Script)));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoParserMetadataTest, "Bango.Parser.NodeMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoParserMetadataTest::RunTest(const FString& Parameters)
{
	const TCHAR* Source =
		TEXT("Hint(\"x\");\n")
		TEXT("Hint(\"y\"); //@12(4,7)\n")
		TEXT("Hint(\"z\"); //@ 13 ( -1 , 7 )\n")
		TEXT("if (a) //@14(2, 2)\n")
		TEXT("{\n")
		TEXT("\tb(); //@15(3, 3)\n")
		TEXT("};\n")
		TEXT("for (int _i = 0; _i < 1; _i++) //@16(0, 0)\n")
		TEXT("{\n")
		TEXT("};");

	FBangoScript Script;
	FBangoCompileError Error;
	if (!TestTrue(TEXT("parses"), Bango::ParseScript(Source, TEXT("t"), Script, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	if (!TestEqual(TEXT("top-level count"), Script.TopLevel.Num(), 5))
	{
		return false;
	}

	const FBangoNodeInfo& Plain = Script.TopLevel[0]->Node;
	TestTrue(TEXT("line 1 has no metadata"), !Plain.bHasMetadata && Plain.Line == 1);

	const FBangoNodeInfo& WithMeta = Script.TopLevel[1]->Node;
	TestTrue(TEXT("line 2 metadata"), WithMeta.bHasMetadata && WithMeta.NodeID == 12 && WithMeta.X == 4 && WithMeta.Y == 7 && WithMeta.Line == 2);
	TestTrue(TEXT("spaces and negative X"), Script.TopLevel[2]->Node.NodeID == 13 && Script.TopLevel[2]->Node.X == -1);

	const FBangoIfStmt* If = Script.TopLevel[3]->As<FBangoIfStmt>();
	TestTrue(TEXT("if node and its body"), If && If->Node.NodeID == 14 && If->Then.Num() == 1 && If->Then[0]->Node.NodeID == 15 && If->Then[0]->Node.Line == 6);

	const FBangoForStmt* For = Script.TopLevel[4]->As<FBangoForStmt>();
	TestTrue(TEXT("for header parts are not nodes"), For && For->Node.NodeID == 16 && For->Init && For->Init->Node.Line == 0);

	// Errors report the node of the line they're on.
	FBangoScript Broken;
	FBangoCompileError BrokenError;
	TestFalse(TEXT("broken script fails"), Bango::ParseScript(TEXT("Hint(\"a\" 5); //@5(0, 0)"), TEXT("Quest/Intro.bango"), Broken, BrokenError));
	TestEqual(TEXT("error node id"), BrokenError.NodeID, (int64)5);
	TestTrue(TEXT("error text"), BrokenError.ToString().Contains(TEXT("Quest/Intro.bango(1:10) [node 5]"), ESearchCase::CaseSensitive));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
