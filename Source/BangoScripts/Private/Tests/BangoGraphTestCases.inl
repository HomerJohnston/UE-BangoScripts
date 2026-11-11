#pragma once

// Graph (text <-> node graph) test cases, shared by the Unreal automation test (BangoGraphTests.cpp)
// and the stand-alone test harness used during development.
// Needs: BangoGraph.h, BangoParser.h

/**
 * Text -> graph -> text must keep the script's meaning (same syntax tree), and doing it again
 * must change nothing: the same graph, and the same text. Returns an empty string on success.
 * Sources that don't parse are skipped.
 */
static FString BangoCheckGraphRoundTrip(const TCHAR* Name, const FString& Source)
{
	FBangoScript Original;
	FBangoCompileError Error;
	if (!Bango::ParseScript(Source, TEXT("test.bango"), Original, Error))
	{
		return FString();
	}

	FBangoGraphDocument Doc1;
	if (!Bango::BuildGraph(Source, TEXT("test.bango"), Doc1, Error))
	{
		return FString::Printf(TEXT("[%s] BuildGraph failed: %s"), Name, *Error.ToString());
	}
	FString Text1;
	FString WriteError;
	if (!Bango::WriteGraphText(Doc1, Text1, WriteError))
	{
		return FString::Printf(TEXT("[%s] WriteGraphText failed: %s"), Name, *WriteError);
	}

	FBangoScript Reparsed;
	if (!Bango::ParseScript(Text1, TEXT("test.bango"), Reparsed, Error))
	{
		return FString::Printf(TEXT("[%s] the written text doesn't parse: %s\n--- text ---\n%s"), Name, *Error.ToString(), *Text1);
	}
	// Saving puts functions in depth-first order (Bango::GetFunctionWriteOrder), so compare them by name.
	{
		TArray<TUniquePtr<FBangoFunction>> Reordered;
		for (const TUniquePtr<FBangoFunction>& Written : Reparsed.Functions)
		{
			for (TUniquePtr<FBangoFunction>& Function : Original.Functions)
			{
				if (Function && Function->Name.Equals(Written->Name, ESearchCase::CaseSensitive))
				{
					Reordered.Add(MoveTemp(Function));
					break;
				}
			}
		}
		if (Reordered.Num() != Original.Functions.Num())
		{
			return FString::Printf(TEXT("[%s] the written text has different functions.\n--- text ---\n%s"), Name, *Text1);
		}
		Original.Functions = MoveTemp(Reordered);
	}
	// `#define params ()` means the same as no params line, and an empty else block the same as none;
	// the graph doesn't keep those apart.
	auto Normalize = [](const FString& Dump)
	{
		FString Result = Dump.Replace(TEXT(" else ()"), TEXT(""), ESearchCase::CaseSensitive);
		if (Result.StartsWith(TEXT("(params) "), ESearchCase::CaseSensitive))
		{
			Result = Result.Mid(9, Result.Len() - 9);
		}
		else if (Result == FString(TEXT("(params)")))
		{
			Result = FString();
		}
		return Result;
	};
	const FString DumpBefore = Normalize(Bango::DumpScript(Original));
	const FString DumpAfter = Normalize(Bango::DumpScript(Reparsed));
	if (!DumpBefore.Equals(DumpAfter, ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s] the written text means something else.\n  before: %s\n  after:  %s\n--- text ---\n%s"), Name, *DumpBefore, *DumpAfter, *Text1);
	}

	FBangoGraphDocument Doc2;
	if (!Bango::BuildGraph(Text1, TEXT("test.bango"), Doc2, Error))
	{
		return FString::Printf(TEXT("[%s] BuildGraph of the written text failed: %s"), Name, *Error.ToString());
	}
	if (Doc2 != Doc1)
	{
		return FString::Printf(TEXT("[%s] reading the written text back gives a different graph.\n--- text ---\n%s"), Name, *Text1);
	}
	FString Text2;
	if (!Bango::WriteGraphText(Doc2, Text2, WriteError) || !Text2.Equals(Text1, ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s] writing a second time changed the text.\n--- first ---\n%s--- second ---\n%s"), Name, *Text1, *Text2);
	}
	return FString();
}

/** Extra scripts for the round-trip check: comments, disconnected nodes, functions, long lines. */
static const TCHAR* const GBangoGraphRoundTripSources[] =
{
	// The spec's example (section 13).
	TEXT("global int foundThings;            //@1(0, 0)\n")
	TEXT("global bool playerHasGun;          //@2(0, 1)\n")
	TEXT("global string[] questLog;          //@3(0, 2)\n")
	TEXT("\n")
	TEXT("function void Nag()\n")
	TEXT("{\n")
	TEXT("\tjohn Speak(\"Find a gun.\");     //@20(0, 0)\n")
	TEXT("\tAwaitSpeak();                  //@21(0, 1)\n")
	TEXT("};\n")
	TEXT("\n")
	TEXT("if (IsDefined(foundThings) && foundThings >= 10)   //@4(0, 3)\n")
	TEXT("{\n")
	TEXT("\tHint(\"You found at least 10 things!\");         //@5(1, 4)\n")
	TEXT("\tquestLog Push(\"Found 10 things\");              //@6(1, 5)\n")
	TEXT("};\n")
	TEXT("label Check;                       //@10(0, 9)\n")
	TEXT("if (!IsDefined(playerHasGun))      //@11(0, 10)\n")
	TEXT("{\n")
	TEXT("\treturn;                        //@14(1, 13)\n")
	TEXT("}\n")
	TEXT("else\n")
	TEXT("{\n")
	TEXT("\tif (!playerHasGun)             //@15(2, 11)\n")
	TEXT("\t{\n")
	TEXT("\t\tNag();                     //@16(3, 12)\n")
	TEXT("\t\tgoto Check;                //@18(3, 14)\n")
	TEXT("\t};\n")
	TEXT("};\n")
	TEXT("forEach (_entry in questLog)       //@19(0, 15)\n")
	TEXT("{\n")
	TEXT("\tif (IsDefined(_entry))         //@22(1, 16)\n")
	TEXT("\t{\n")
	TEXT("\t\tHint(_entry);              //@23(2, 17)\n")
	TEXT("\t};\n")
	TEXT("};\n"),

	// Comments everywhere, no metadata at all, CRLF line endings.
	TEXT("// File header\r\n// second line\r\nparams (string _name, int _n = 3);\r\n\r\n//   indented comment\r\nHint(\"%0\", _name); // trailing\r\nif (_n > 2)\r\n{\r\n\t// inside\r\n\tHint(\"big\");\r\n\t// at the end of the block\r\n}; // after the block\r\n//no space\r\nfor (int _i = 0; _i < _n; _i++)\r\n{\r\n};\r\n// at the end of the file\r\n"),

	// Functions with comments, and code after a return/goto (only reachable through a label).
	TEXT("// about F\nfunction int F(int _a, string[] _b) // on the function line\n{\n\t// first\n\treturn _a;\n\tlabel After;\n\tHint(\"x\");\n};\ngoto Skip;\nHint(\"never\");\nlabel Skip;\nHint(\"%0\", F(1, []));\n"),

	// Disconnected nodes in the main graph and in a function.
	TEXT("Hint(\"a\");    //@1(0, 0)\n")
	TEXT("\n")
	TEXT("//~ // a disconnected chain\n")
	TEXT("//~ Hint(\"b\");    //@2(0, 20)\n")
	TEXT("//~ if (true)    //@3(20, 20)\n")
	TEXT("//~ {\n")
	TEXT("//~ \tHint(\"c\");    //@4(40, 30)\n")
	TEXT("//~ };\n")
	TEXT("\n")
	TEXT("//~ while (false)\n")
	TEXT("//~ {\n")
	TEXT("//~ };\n")
	TEXT("function void G()\n")
	TEXT("{\n")
	TEXT("\tHint(\"g\");\n")
	TEXT("\n")
	TEXT("\t//~ Hint(\"loose\");    //@9(5, 5)\n")
	TEXT("};\n"),

	// else-if chains, an else holding an if followed by more code, empty blocks, nested loops.
	TEXT("if (true)\n{\n}\nelse if (false)\n{\n\tHint(\"1\");\n}\nelse\n{\n\tif (true)\n\t{\n\t};\n\tHint(\"2\");\n};\n")
	TEXT("while (false)\n{\n\tfor (;;)\n\t{\n\t\tforEach (_x in [1, 2])\n\t\t{\n\t\t\tbreak;\n\t\t};\n\t\tcontinue;\n\t};\n};\n"),

	// A very long line doesn't push every //@ column out.
	TEXT("Hint(\"short\");\nHint(\"this line is far longer than one hundred characters, so its metadata is written four spaces after it and the others stay put\");\nHint(\"short again\");\n"),

	// Only comments.
	TEXT("// nothing here yet\n"),

	// An empty params line.
	TEXT("#define params ()\nHint(\"x\");\n"),

	// Reroutes: between statements, first and last in blocks, alone in a block, in an else before an if,
	// in a function, with a comment, and in a disconnected chain.
	TEXT("#define params (int _n)\n")
	TEXT("                    //@1(4, 0)\n")
	TEXT("Hint(\"a\");           //@2(8, 0)\n")
	TEXT("// bend here\n")
	TEXT("                    //@3(12, 2)\n")
	TEXT("if (_n > 0)         //@4(16, 0)\n")
	TEXT("{\n")
	TEXT("                    //@5(20, 0)\n")
	TEXT("}\n")
	TEXT("else\n")
	TEXT("{\n")
	TEXT("                    //@6(20, 8)\n")
	TEXT("\tif (_n < 0)         //@7(24, 8)\n")
	TEXT("\t{\n")
	TEXT("\t\tHint(\"neg\");     //@8(28, 8)\n")
	TEXT("\t\t                //@9(32, 8)\n")
	TEXT("\t};\n")
	TEXT("};\n")
	TEXT("while (_n > 0)      //@10(40, 0)\n")
	TEXT("{\n")
	TEXT("\t_n--;              //@11(44, 0)\n")
	TEXT("};\n")
	TEXT("                    //@12(48, 0)\n")
	TEXT("//~ Hint(\"loose\");   //@13(0, 20)\n")
	TEXT("//~\n")
	TEXT("//~                 //@14(4, 20)\n")
	TEXT("\n")
	TEXT("//~                 //@15(0, 30)\n")
	TEXT("function void F()\n")
	TEXT("{\n")
	TEXT("\t                    //@16(4, 0)\n")
	TEXT("\tHint(\"f\");          //@17(8, 0)\n")
	TEXT("};\n"),
	// New nodes from the add-node menu, with their fields still empty: they don't compile, but must load and save.
	TEXT("if ()                    //@1(0, 0)\n")
	TEXT("{\n")
	TEXT("\t= ;                    //@2(4, 0)\n")
	TEXT("\n")
	TEXT("\t++;                    //@3(8, 0)\n")
	TEXT("};\n")
	TEXT("\n")
	TEXT("forEach (_item in)       //@4(12, 0)\n")
	TEXT("{\n")
	TEXT("\tlabel ;                //@5(16, 0)\n")
	TEXT("\n")
	TEXT("\tgoto ;                 //@6(20, 0)\n")
	TEXT("};\n")
	TEXT("\n")
	TEXT("for (int _i = ; _i <= ; _i++) //@7(24, 0)\n")
	TEXT("{\n")
	TEXT("};\n")
	TEXT("\n")
	TEXT("//~ while ()              //@8(0, 10)\n")
	TEXT("//~ {\n")
	TEXT("//~ };\n")
	TEXT("\n")
	TEXT("//~ = ;                   //@9(0, 14)\n"),
};

static void BangoRunGraphScenarios(TArray<FString>& OutFailures)
{
	auto Expect = [&OutFailures](bool bCondition, const FString& What)
	{
		if (!bCondition)
		{
			OutFailures.Add(What);
		}
	};

	// ---- LOCTEXT_NAMESPACE and text: #define after the params line, #undef at the end, text literals kept as written ----
	{
		const TCHAR* Source =
			TEXT("// About the script\n")
			TEXT("#define params (text _line = nil)\n")
			TEXT("#define LOCTEXT_NAMESPACE \"Bango.Intro\"\n")
			TEXT("// About the hint\n")
			TEXT("Say(NSNOTELOCTEXT(\"Other\", \"K\", \"Hello {0}\", \"Said slowly\"), 1); //@1(10, 0)\n")
			TEXT("function void F()\n{\n\tSay(LOCTEXT(\"F\", \"In F\")); //@2(0, 0)\n};\n")
			TEXT("#undef LOCTEXT_NAMESPACE // the end\n");
		const TCHAR* Expected =
			TEXT("// About the script\n")
			TEXT("#define params (text _line = nil)\n")
			TEXT("#define LOCTEXT_NAMESPACE \"Bango.Intro\"\n")
			TEXT("\n")
			TEXT("// About the hint\n")
			TEXT("Say(NSNOTELOCTEXT(\"Other\", \"K\", \"Hello {0}\", \"Said slowly\"), 1);    //@1(10, 0)\n")
			TEXT("\n")
			TEXT("// the end\n")
			TEXT("function void F()\n")
			TEXT("{\n")
			TEXT("\tSay(LOCTEXT(\"F\", \"In F\"));                                      //@2(0, 0)\n")
			TEXT("};\n")
			TEXT("\n")
			TEXT("#undef LOCTEXT_NAMESPACE\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("text.bango"), Doc, Error);
		Expect(bBuilt && Doc.bHasLocNamespace && Doc.LocNamespace == FString(TEXT("Bango.Intro")), FString(TEXT("LOCTEXT_NAMESPACE: read into the document: ")) + Error.ToString());
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("LOCTEXT_NAMESPACE: saved after params, #undef at the end:\n")) + Text);

		FBangoGraphDocument NoParams;
		Expect(Bango::BuildGraph(TEXT("#define LOCTEXT_NAMESPACE \"A\"\n#undef LOCTEXT_NAMESPACE\n"), TEXT("ns.bango"), NoParams, Error) && Bango::WriteGraphText(NoParams, Text, WriteError)
			&& Text == FString(TEXT("#define LOCTEXT_NAMESPACE \"A\"\n\n#undef LOCTEXT_NAMESPACE\n")), FString(TEXT("LOCTEXT_NAMESPACE: without params or statements:\n")) + Text);
		FBangoGraphDocument Commented;
		Expect(Bango::BuildGraph(TEXT("// Hi\n#define LOCTEXT_NAMESPACE \"A\"\n#undef LOCTEXT_NAMESPACE\n"), TEXT("ns.bango"), Commented, Error) && Bango::WriteGraphText(Commented, Text, WriteError)
			&& Text == FString(TEXT("// Hi\n#define LOCTEXT_NAMESPACE \"A\"\n\n#undef LOCTEXT_NAMESPACE\n")), FString(TEXT("LOCTEXT_NAMESPACE: anchors the entry comment without a params line:\n")) + Text);
		FBangoGraphDocument Changed = NoParams;
		Changed.LocNamespace = TEXT("B");
		Expect(Changed != NoParams, TEXT("LOCTEXT_NAMESPACE: part of what makes two documents different"));
	}

	// ---- Reroutes: a line with only //@ is a reroute node; saving writes it back the same way ----
	{
		const TCHAR* Source =
			TEXT("Hint(\"a\"); //@1(0, 0)\n")
			TEXT("   //@2(10, 0)\n")
			TEXT("if (true) //@3(20, 0)\n")
			TEXT("{\n")
			TEXT("//@4(30, 4)\n")
			TEXT("};\n");
		const TCHAR* Expected =
			TEXT("Hint(\"a\");    //@1(0, 0)\n")
			TEXT("              //@2(10, 0)\n")
			TEXT("\n")
			TEXT("if (true)     //@3(20, 0)\n")
			TEXT("{\n")
			TEXT("\t          //@4(30, 4)\n")
			TEXT("};\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("reroute.bango"), Doc, Error);
		Expect(bBuilt && Doc.Main.Nodes.Num() == 5, FString(TEXT("reroute: builds: ")) + Error.ToString());
		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(N[2].Kind == EBangoGraphNodeKind::Reroute && N[2].NodeID == 2 && N[2].X == 10 && N[2].Text.IsEmpty(), TEXT("reroute: node 2 is a reroute at (10,0)"));
			Expect(N[1].Next == 2 && N[2].Next == 3, TEXT("reroute: wired between the hint and the if"));
			Expect(N[3].Branches[0] == 4 && N[4].Kind == EBangoGraphNodeKind::Reroute && N[4].Next == INDEX_NONE, TEXT("reroute: alone in the if's Do block"));
		}
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("reroute: saved as metadata-only lines:\n")) + Text);

		FBangoProgram Program;
		FBangoCommandTable Commands;
		BangoMakeTestCommands(Commands);
		Expect(Bango::CompileSource(Source, TEXT("reroute.bango"), Commands, Program, Error), FString(TEXT("reroute: compiles: ")) + Error.ToString());

		// Statements of the same kind sit together; a blank line goes between different kinds, around flow
		// control, and above a statement with a comment.
		{
			const TCHAR* GroupSource =
				TEXT("int _a = 1; //@1(0,0)\nglobal int b; //@2(0,1)\n_a = 2; //@3(0,2)\n_a++; //@4(0,3)\nHint(\"x\"); //@5(0,4)\nLog(\"y\"); //@6(0,5)\n")
				TEXT("// about z\nLog(\"z\"); //@7(0,6)\nif (true) //@8(0,7)\n{\nLog(\"in\"); //@9(1,8)\nbreak; //@10(1,9)\n};\nreturn; //@11(0,10)\n");
			const TCHAR* GroupExpected =
				TEXT("int _a = 1;       //@1(0, 0)\n")
				TEXT("global int b;     //@2(0, 1)\n")
				TEXT("\n")
				TEXT("_a = 2;           //@3(0, 2)\n")
				TEXT("_a++;             //@4(0, 3)\n")
				TEXT("\n")
				TEXT("Hint(\"x\");        //@5(0, 4)\n")
				TEXT("Log(\"y\");         //@6(0, 5)\n")
				TEXT("\n")
				TEXT("// about z\n")
				TEXT("Log(\"z\");         //@7(0, 6)\n")
				TEXT("\n")
				TEXT("if (true)         //@8(0, 7)\n")
				TEXT("{\n")
				TEXT("\tLog(\"in\");    //@9(1, 8)\n")
				TEXT("\n")
				TEXT("\tbreak;        //@10(1, 9)\n")
				TEXT("};\n")
				TEXT("\n")
				TEXT("return;           //@11(0, 10)\n");
			FBangoGraphDocument GroupDoc;
			FString GroupText;
			Expect(Bango::BuildGraph(GroupSource, TEXT("groups.bango"), GroupDoc, Error) && Bango::WriteGraphText(GroupDoc, GroupText, WriteError)
				&& GroupText == FString(GroupExpected), FString(TEXT("spacing groups: unexpected text:\n")) + GroupText + Error.ToString() + WriteError);
		}

		// Reroutes in a row share one line; one with a comment starts a new line under its comment.
		const TCHAR* RunSource =
			TEXT("Hint(\"a\"); //@1(0,0)\n//@2(4,0)\n//@3(8,0)\n// note\n//@4(12,0)\n//@5(16,0)\nHint(\"b\"); //@6(20,0)\n//@7(24,0) @8( 28 , 0 )\n");
		const TCHAR* RunExpected =
			TEXT("Hint(\"a\");    //@1(0, 0)\n")
			TEXT("              //@2(4, 0) @3(8, 0)\n")
			TEXT("// note\n")
			TEXT("              //@4(12, 0) @5(16, 0)\n")
			TEXT("Hint(\"b\");    //@6(20, 0)\n")   // two calls in a row: no blank line between them
			TEXT("              //@7(24, 0) @8(28, 0)\n");
		FBangoGraphDocument RunDoc;
		const bool bRunBuilt = Bango::BuildGraph(RunSource, TEXT("runs.bango"), RunDoc, Error);
		Expect(bRunBuilt && RunDoc.Main.Nodes.Num() == 9 && RunDoc.Main.Nodes[8].Kind == EBangoGraphNodeKind::Reroute && RunDoc.Main.Nodes[8].NodeID == 8
			&& RunDoc.Main.Nodes[7].Next == 8 && RunDoc.Main.Nodes[4].Comment == FString(TEXT("note")), FString(TEXT("reroute runs: read as a chain: ")) + Error.ToString());
		Expect(bRunBuilt && Bango::WriteGraphText(RunDoc, Text, WriteError) && Text == FString(RunExpected), FString(TEXT("reroute runs: saved on shared lines:\n")) + Text);

		FBangoScript Script;
		Expect(!Bango::ParseScript(TEXT("if (true)\n//@1(0, 0)\n{\n};\n"), TEXT("bad.bango"), Script, Error)
			&& Error.Line == 2 && Error.Message.Contains(TEXT("reroute")), FString(TEXT("reroute: not allowed between 'if (...)' and '{': ")) + Error.ToString());
		Expect(!Bango::ParseScript(TEXT("//@1(0, 0)\n#define params (int _a)\n"), TEXT("bad.bango"), Script, Error)
			&& Error.Message.Contains(TEXT("first line")), FString(TEXT("reroute: not before #define params: ")) + Error.ToString());
	}

	// ---- Building: structure, text, metadata, comments, automatic IDs and positions ----
	{
		const TCHAR* Source =
			TEXT("// Header comment\n")
			TEXT("#define params (entity _who, int _n = 2)\n")
			TEXT("global int count;            //@5(10, 0)\n")
			TEXT("if (_n > 1)                  //@6(20, 0)\n")
			TEXT("{\n")
			TEXT("\tHint(\"big\");             //@7(30, 5)\n")
			TEXT("}\n")
			TEXT("else if (_n == 1)            //@8(30, 10)\n")
			TEXT("{\n")
			TEXT("\tHint(\"one\");\n")
			TEXT("}\n")
			TEXT("else\n")
			TEXT("{\n")
			TEXT("\tHint(\"none\");            //@9(40, 20)\n")
			TEXT("};\n")
			TEXT("_who Speak(\"hi\"); // trailing\n");

		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error);
		Expect(bBuilt, FString(TEXT("build: ")) + Error.ToString());
		if (bBuilt)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(N.Num() == 8, FString::Printf(TEXT("build: expected 8 nodes, got %d"), N.Num()));
			if (N.Num() == 8)
			{
				Expect(N[0].Kind == EBangoGraphNodeKind::Entry && N[0].NodeID == 0 && N[0].X == 0 && N[0].Y == 0, TEXT("build: entry is node 0 at (0,0)"));
				Expect(N[0].Text == FString(TEXT("entity _who, int _n = 2")), FString(TEXT("build: entry text: ")) + N[0].Text);
				Expect(N[0].Comment == FString(TEXT("Header comment")), FString(TEXT("build: entry comment: ")) + N[0].Comment);

				const FBangoGraphNode& Decl = N[N[0].Next];
				Expect(Decl.Kind == EBangoGraphNodeKind::VarDecl && Decl.Text == FString(TEXT("global int count")) && Decl.NodeID == 5 && Decl.X == 10, TEXT("build: global declaration node"));

				const FBangoGraphNode& If = N[Decl.Next];
				Expect(If.Kind == EBangoGraphNodeKind::If && If.Text == FString(TEXT("_n > 1")) && If.NodeID == 6, TEXT("build: if node"));
				Expect(N[If.Branches[0]].Text == FString(TEXT("Hint(\"big\")")), TEXT("build: True branch"));

				const FBangoGraphNode& ElseIf = N[If.Branches[1]];
				Expect(ElseIf.Kind == EBangoGraphNodeKind::If && ElseIf.NodeID == 8 && ElseIf.Next == INDEX_NONE, TEXT("build: else if is an if node on the False pin, with nothing after it"));
				const FBangoGraphNode& One = N[ElseIf.Branches[0]];
				Expect(One.NodeID == 10 && One.X == 50 && One.Y == 20, FString::Printf(TEXT("build: node without metadata gets the next ID and a place right of and below its if: got %lld (%d,%d)"), One.NodeID, One.X, One.Y));
				Expect(N[ElseIf.Branches[1]].Text == FString(TEXT("Hint(\"none\")")), TEXT("build: else branch of the else-if"));

				const FBangoGraphNode& Speak = N[If.Next];
				Expect(Speak.Text == FString(TEXT("_who Speak(\"hi\")")) && Speak.Comment == FString(TEXT("trailing")), TEXT("build: Then pin leads to the statement after the if, trailing comment attached"));
				Expect(Speak.NodeID == 11 && Speak.X == 40 && Speak.Y == 0, FString::Printf(TEXT("build: node after the if is placed to its right: got %lld (%d,%d)"), Speak.NodeID, Speak.X, Speak.Y));
				Expect(Speak.Next == INDEX_NONE, TEXT("build: last node has no Next"));
			}

			FString Text;
			FString WriteError;
			Expect(Bango::WriteGraphText(Doc, Text, WriteError), FString(TEXT("write: ")) + WriteError);
			const FString Expected =
				TEXT("// Header comment\n")
				TEXT("#define params (entity _who, int _n = 2)\n")
				TEXT("\n")
				TEXT("global int count;    //@5(10, 0)\n")
				TEXT("\n")
				TEXT("if (_n > 1)          //@6(20, 0)\n")
				TEXT("{\n")
				TEXT("\tHint(\"big\");     //@7(30, 5)\n")
				TEXT("}\n")
				TEXT("else if (_n == 1)    //@8(30, 10)\n")
				TEXT("{\n")
				TEXT("\tHint(\"one\");     //@10(50, 20)\n")
				TEXT("}\n")
				TEXT("else\n")
				TEXT("{\n")
				TEXT("\tHint(\"none\");    //@9(40, 20)\n")
				TEXT("};\n")
				TEXT("\n")
				TEXT("// trailing\n")
				TEXT("_who Speak(\"hi\");    //@11(40, 0)\n");
			Expect(Text.Equals(Expected, ESearchCase::CaseSensitive), FString(TEXT("write: unexpected text:\n")) + Text);
		}
	}

	// ---- Disconnected (//~) nodes ----
	{
		const TCHAR* Source =
			TEXT("Hint(\"a\");    //@1(0, 0)\n")
			TEXT("//~ Hint(\"b\");    //@2(0, 20)\n")
			TEXT("//~ if (true)    //@3(20, 20)\n")
			TEXT("//~ {\n")
			TEXT("//~ \tHint(\"c\");    //@4(40, 30)\n")
			TEXT("//~ };\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		Expect(Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error), FString(TEXT("detached: ")) + Error.ToString());
		const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
		Expect(N.Num() == 5, TEXT("detached: 5 nodes"));
		if (N.Num() == 5)
		{
			Expect(N[N[0].Next].Next == INDEX_NONE, TEXT("detached: the main chain is just Hint(\"a\")"));
			Expect(N[2].NodeID == 2 && N[2].Next == 3 && N[3].Kind == EBangoGraphNodeKind::If && N[N[3].Branches[0]].NodeID == 4,
				TEXT("detached: the //~ lines are a chain that nothing leads into"));
		}

		FBangoScript Script;
		Expect(Bango::ParseScript(Source, TEXT("test.bango"), Script, Error) && Script.TopLevel.Num() == 1, TEXT("detached: the compiler doesn't see //~ lines"));

		FBangoGraphDocument Broken;
		bool bBuilt = Bango::BuildGraph(TEXT("Hint(\"a\");\n//~ Hint(\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("In the disconnected nodes")) && Error.Line == 2,
			FString(TEXT("detached: a broken //~ line is an error on its line: ")) + Error.ToString());
		bBuilt = Bango::BuildGraph(TEXT("Hint(\"a\");    //@1(0, 0)\n//~ Hint(\"b\");    //@1(0, 0)\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("used more than once")),
			FString(TEXT("detached: duplicate IDs across //~ lines are an error: ")) + Error.ToString());
		bBuilt = Bango::BuildGraph(TEXT("//~ #define params (int _a)\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("params")), FString(TEXT("detached: no params line in //~ lines: ")) + Error.ToString());
	}

	// ---- Functions ----
	{
		const TCHAR* Source =
			TEXT("Hint(\"main\");\n")
			TEXT("// Doubles it\n")
			TEXT("function int Twice(int _n, string[][] _s)\n")
			TEXT("{\n")
			TEXT("\treturn _n * 2;\n")
			TEXT("};\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		Expect(Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error), FString(TEXT("function: ")) + Error.ToString());
		Expect(Doc.Functions.Num() == 1, TEXT("function: one function graph"));
		if (Doc.Functions.Num() == 1)
		{
			const FBangoGraph& F = Doc.Functions[0];
			Expect(F.bIsFunction && F.Name == FString(TEXT("Twice")), TEXT("function: name"));
			Expect(F.Nodes[0].Text == FString(TEXT("int Twice(int _n, string[][] _s)")) && F.Nodes[0].Comment == FString(TEXT("Doubles it")), FString(TEXT("function: entry text and comment: ")) + F.Nodes[0].Text);
			Expect(F.Nodes.Num() == 2 && F.Nodes[1].Kind == EBangoGraphNodeKind::Return && F.Nodes[1].Text == FString(TEXT("_n * 2")), TEXT("function: return node"));
		}
		Expect(Doc.Main.Nodes.Num() == 2 && Doc.Main.Nodes[0].Comment.IsEmpty(), TEXT("function: the function's comment isn't on the main graph"));
	}

	// ---- Function order: depth-first by first use ----
	{
		const TCHAR* Source =
			TEXT("B();\n")
			TEXT("Hint(\"A()\");\n")              // in a string: not a call
			TEXT("A();\n")
			TEXT("//~ D();\n")                    // disconnected nodes count too, after the connected ones
			TEXT("function void A()\n{\n\tif (C())\n\t{\n\t};\n};\n")
			TEXT("function void B()\n{\n\tA();\n\tB();\n};\n")   // B calls A (placed right after B), and itself
			TEXT("function bool C()\n{\n\treturn true;\n};\n")
			TEXT("function void E()\n{\n\tF();\n};\n")            // nothing calls E: it goes after the used ones, with F after it
			TEXT("function void D()\n{\n\tD();\n};\n")
			TEXT("function void F()\n{\n\tE();\n};\n");         // E and F call each other
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		Expect(Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error), FString(TEXT("order: ")) + Error.ToString());
		FString Names;
		for (const FBangoGraph& Function : Doc.Functions)
		{
			Names += Function.Name;
		}
		Expect(Names.Equals(TEXT("BACDEF"), ESearchCase::CaseSensitive), FString(TEXT("order: functions should be stored as BACDEF, got ")) + Names);

		FString Text;
		FString WriteError;
		Expect(Bango::WriteGraphText(Doc, Text, WriteError), FString(TEXT("order: write: ")) + WriteError);
		const int32 PosB = Text.Find(TEXT("function void B()"), ESearchCase::CaseSensitive);
		const int32 PosA = Text.Find(TEXT("function void A()"), ESearchCase::CaseSensitive);
		const int32 PosC = Text.Find(TEXT("function bool C()"), ESearchCase::CaseSensitive);
		const int32 PosD = Text.Find(TEXT("function void D()"), ESearchCase::CaseSensitive);
		const int32 PosE = Text.Find(TEXT("function void E()"), ESearchCase::CaseSensitive);
		const int32 PosF = Text.Find(TEXT("function void F()"), ESearchCase::CaseSensitive);
		Expect(PosB >= 0 && PosB < PosA && PosA < PosC && PosC < PosD && PosD < PosE && PosE < PosF, FString(TEXT("order: written in the wrong order:\n")) + Text);

		// The order of called functions doesn't depend on the document's order.
		FBangoGraphDocument Shuffled = Doc;
		Shuffled.Functions.Swap(0, 2); // B <-> C (both called)
		Shuffled.Functions.Swap(1, 3); // A <-> D (both called); E and F, which nothing calls, keep their order
		FString ShuffledText;
		Expect(Bango::WriteGraphText(Shuffled, ShuffledText, WriteError) && ShuffledText.Equals(Text, ESearchCase::CaseSensitive), TEXT("order: the order doesn't depend on the document's order"));
	}

	// ---- Writing a graph made in the editor ----
	{
		FBangoGraphDocument Doc;
		FBangoGraphNode& Entry = Doc.Main.Nodes.AddDefaulted_GetRef();
		Entry.Kind = EBangoGraphNodeKind::Entry;
		auto Add = [&Doc](EBangoGraphNodeKind Kind, const TCHAR* Text, int64 Id)
		{
			FBangoGraphNode& Node = Doc.Main.Nodes.AddDefaulted_GetRef();
			Node.Kind = Kind;
			Node.Text = Text;
			Node.NodeID = Id;
			Node.X = (int32)Id;
			return Doc.Main.Nodes.Num() - 1;
		};
		const int32 A = Add(EBangoGraphNodeKind::Call, TEXT("Hint(\"a\")"), 1);
		const int32 Loop = Add(EBangoGraphNodeKind::While, TEXT("true"), 2);
		const int32 Body = Add(EBangoGraphNodeKind::Break, TEXT(""), 3);
		const int32 Ret = Add(EBangoGraphNodeKind::Return, TEXT(""), 4);
		const int32 Loose = Add(EBangoGraphNodeKind::Label, TEXT("Loose"), 5);
		Doc.Main.Nodes[0].Next = A;
		Doc.Main.Nodes[A].Next = Loop;
		Doc.Main.Nodes[Loop].Branches[0] = Body;
		Doc.Main.Nodes[Loop].Next = Ret;

		FString Text;
		FString WriteError;
		Expect(Bango::WriteGraphText(Doc, Text, WriteError), FString(TEXT("editor graph: ")) + WriteError);
		const FString Expected =
			TEXT("Hint(\"a\");          //@1(1, 0)\n")
			TEXT("\n")
			TEXT("while (true)        //@2(2, 0)\n")
			TEXT("{\n")
			TEXT("\tbreak;          //@3(3, 0)\n")
			TEXT("};\n")
			TEXT("\n")
			TEXT("return;             //@4(4, 0)\n")
			TEXT("\n")
			TEXT("//~ label Loose;    //@5(5, 0)\n");
		Expect(Text.Equals(Expected, ESearchCase::CaseSensitive), FString(TEXT("editor graph: unexpected text:\n")) + Text);

		// A disconnected chain keeps its nodes together: the line between them is a bare //~, not a blank line
		// (a blank line would end the chain).
		{
			FBangoGraphDocument LooseDoc;
			FBangoCompileError LooseError;
			const FString Source = TEXT("Hint(\"a\"); //@1(0, 0)\n\n//~ Hint(\"b\"); //@2(0, 1)\n//~ int _c; //@3(0, 2)\n");
			FString LooseText;
			FBangoGraphDocument Back;
			const bool bBuilt = Bango::BuildGraph(Source, TEXT("loose.bango"), LooseDoc, LooseError);
			const bool bWritten = bBuilt && Bango::WriteGraphText(LooseDoc, LooseText, WriteError);
			const bool bReadBack = bWritten && Bango::BuildGraph(LooseText, TEXT("loose.bango"), Back, LooseError);
			Expect(bReadBack, FString(TEXT("loose chain: build, write and read back: ")) + LooseError.ToString() + WriteError);
			if (bReadBack)
			{
				Expect(LooseText.Contains(TEXT("//~ Hint(\"b\");")) && LooseText.Contains(TEXT("\n//~\n//~ int _c;")), FString(TEXT("loose chain: expected a bare //~ between its nodes:\n")) + LooseText);
				int32 B = INDEX_NONE;
				int32 C = INDEX_NONE;
				for (int32 i = 0; i < Back.Main.Nodes.Num(); ++i)
				{
					B = Back.Main.Nodes[i].NodeID == 2 ? i : B;
					C = Back.Main.Nodes[i].NodeID == 3 ? i : C;
				}
				Expect(B != INDEX_NONE && C != INDEX_NONE && Back.Main.Nodes[B].Next == C, TEXT("loose chain: b still leads to c after saving"));
			}
		}

		// Errors
		FBangoGraphDocument Bad = Doc;
		Bad.Main.Nodes[Loose].Next = Body; // Body already has a wire from the loop
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("more than one wire")), FString(TEXT("error: two wires into one node: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[Loose].Next = Loose;
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("loop of wires")), FString(TEXT("error: a node wired to itself: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[Ret].Next = 0;
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("entry node")), FString(TEXT("error: wired into the entry: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[Ret].Next = 99;
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("doesn't exist")), FString(TEXT("error: wired to a missing node: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[A].Text = TEXT("Hint(\n\"a\")");
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("line break")), FString(TEXT("error: line break in text: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[A].Branches[0] = Body;
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("output it doesn't have")), FString(TEXT("error: branch on a call node: ")) + WriteError);

		// An entry with a comment but no params still writes an (empty) params line to hold the comment.
		FBangoGraphDocument Commented = Doc;
		Commented.Main.Nodes[0].Comment = TEXT("Hello");
		Expect(Bango::WriteGraphText(Commented, Text, WriteError) && Text.StartsWith(TEXT("// Hello\n#define params ()\n"), ESearchCase::CaseSensitive),
			FString(TEXT("entry comment without params: ")) + Text);
	}
}
