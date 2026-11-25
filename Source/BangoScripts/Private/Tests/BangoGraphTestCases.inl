#pragma once

// Graph (text <-> node graph) test cases, shared by the Unreal automation test (BangoGraphTests.cpp)
// and the stand-alone test harness used during development.
// Needs: BangoGraph.h, BangoParser.h

/**
 * Text -> graph -> text must keep the script's meaning (same syntax tree), and doing it again
 * must change nothing: the same graph, and the same text. Returns an empty string on success.
 * Sources that don't parse are skipped, and so are sources whose declarations the graph can't hold.
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
		if (Error.Message.Contains(TEXT("declaration")) || Error.Message.Contains(TEXT("is declared here")))
		{
			return FString(); // declarations the graph can't hold (a global or scriptVar not at the top, or a global with a value): fixed in the text
		}
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
	// `#inputs ()` means the same as no inputs line, and an empty else block the same as none;
	// the graph doesn't keep those apart.
	auto Normalize = [](const FString& Dump)
	{
		FString Result = Dump.Replace(TEXT(" else ()"), TEXT(""), ESearchCase::CaseSensitive);
		if (Result.StartsWith(TEXT("(inputs) "), ESearchCase::CaseSensitive))
		{
			Result = Result.Mid(9, Result.Len() - 9);
		}
		else if (Result == FString(TEXT("(inputs)")))
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

	// Written compact (no headings, no metadata), it still means the same, and reads back to the same compact text.
	FBangoGraphWriteOptions Compact;
	Compact.bCompact = true;
	FString CompactText;
	if (!Bango::WriteGraphText(Doc1, CompactText, WriteError, Compact))
	{
		return FString::Printf(TEXT("[%s] writing compact failed: %s"), Name, *WriteError);
	}
	if (CompactText.Contains(TEXT("//@")) || CompactText.Contains(TEXT("// ====")))
	{
		return FString::Printf(TEXT("[%s] the compact text has metadata or headings.\n--- text ---\n%s"), Name, *CompactText);
	}
	FBangoScript CompactScript;
	if (!Bango::ParseScript(CompactText, TEXT("test.bango"), CompactScript, Error))
	{
		return FString::Printf(TEXT("[%s] the compact text doesn't parse: %s\n--- text ---\n%s"), Name, *Error.ToString(), *CompactText);
	}
	{
		TArray<TUniquePtr<FBangoFunction>> Reordered;
		for (const TUniquePtr<FBangoFunction>& Written : CompactScript.Functions)
		{
			for (const TUniquePtr<FBangoFunction>& Function : Reparsed.Functions)
			{
				if (Function && Function->Name.Equals(Written->Name, ESearchCase::CaseSensitive))
				{
					Reordered.Add(MoveTemp(const_cast<TUniquePtr<FBangoFunction>&>(Function)));
					break;
				}
			}
		}
		Reparsed.Functions = MoveTemp(Reordered);
	}
	// Compact text has no reroutes (they're only metadata), and no #USES_GRAPH_EDITOR (it asks for the metadata).
	auto WithoutReroutes = [](FString Dump)
	{
		Dump.ReplaceInline(TEXT("(usesGraphEditor) "), TEXT(""), ESearchCase::CaseSensitive);
		Dump.ReplaceInline(TEXT("(reroute) "), TEXT(""), ESearchCase::CaseSensitive);
		Dump.ReplaceInline(TEXT(" (reroute)"), TEXT(""), ESearchCase::CaseSensitive);
		Dump.ReplaceInline(TEXT("(reroute)"), TEXT(""), ESearchCase::CaseSensitive);
		return Dump;
	};
	const FString DumpCompact = Normalize(Bango::DumpScript(CompactScript));
	const FString DumpReparsed = Normalize(WithoutReroutes(Bango::DumpScript(Reparsed)));
	if (!DumpCompact.Equals(DumpReparsed, ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s] the compact text means something else.\n  before: %s\n  after:  %s\n--- text ---\n%s"), Name, *DumpReparsed, *DumpCompact, *CompactText);
	}
	FBangoGraphDocument CompactDoc;
	FString CompactText2;
	if (!Bango::BuildGraph(CompactText, TEXT("test.bango"), CompactDoc, Error) || !Bango::WriteGraphText(CompactDoc, CompactText2, WriteError, Compact)
		|| !CompactText2.Equals(CompactText, ESearchCase::CaseSensitive))
	{
		return FString::Printf(TEXT("[%s] reading the compact text back and writing it again changed it: %s\n--- first ---\n%s--- second ---\n%s"), Name, *Error.ToString(), *CompactText, *CompactText2);
	}
	return FString();
}

/** Extra scripts for the round-trip check: comments, disconnected nodes, functions, long lines. */
static const TCHAR* const GBangoGraphRoundTripSources[] =
{
	// World and persistent globals: in the Globals list, and set in the code (a Global node).
	TEXT("global int level ?= 1;\npersistent global int gold ?= 5;\n// Kept between levels.\npersistent global string[] log;\n\ngold = gold + level;\npersistent global bool rich = gold > 100;\nglobal bool here = true;\n"),
	TEXT("persistent global int gold;\n\nfunction void Pay()\n{\n\tpersistent global bool paid = true;\n}\n\n/*\npersistent global bool off = true;\n*/\nPay();\n"),
	// Commands with a block (section 7): a node with a Body output; its block's statements are nodes of their own.
	TEXT("@john Speech(0, 1)\n{\n\tLine(INVTEXT(\"Hello\"));\n\tif (metBefore)\n\t{\n\t\tLine(INVTEXT(\"Again\"));\n\t}\n\tLine(INVTEXT(\"Bye\"));\n}\nWriteLog(\"done\");\n"),
	TEXT("Speech(0) {\n\tLine(1);\n}\n"),
	TEXT("Speech(0)        //@4(2, 3)\n{\n}\nSpeech(1)        //@5(2, 6)\n{\n\tLine(1);     //@6(4, 7)\n}\n"),
	TEXT("global bool hasKey;\n\nwhile (true)\n{\n\t@john Prompt()\n\t{\n\t\tOption(INVTEXT(\"Who are you?\"))\n\t\t{\n\t\t\t@john Speech(0, 1)\n\t\t\t{\n\t\t\t\tLine(INVTEXT(\"I'm John.\"));\n\t\t\t}\n\t\t}\n\t\tif (hasKey)\n\t\t{\n\t\t\tOption(INVTEXT(\"I found your key.\"))\n\t\t\t{\n\t\t\t\thasKey = false;\n\t\t\t}\n\t\t}\n\t\tOption(INVTEXT(\"Goodbye.\"))\n\t\t{\n\t\t\tbreak;\n\t\t}\n\t}\n}\n"),
	TEXT("function void Talk(entity _who)\n{\n\t_who Speech(0, 1)\n\t{\n\t\tLine(1);\n\t\treturn;\n\t}\n}\n\n/*\nSpeech(0)\n{\n\tLine(1);\n}\n*/\nTalk(@john);\n"),
	// The spec's example (section 13).
	TEXT("global int foundThings;            //@1(0, 0)\n")
	TEXT("global bool playerHasGun;          //@2(0, 1)\n")
	TEXT("global string[] questLog;          //@3(0, 2)\n")
	TEXT("\n")
	TEXT("function void Nag()\n")
	TEXT("{\n")
	TEXT("\tjohn Speak(\"Find a gun.\");     //@20(0, 0)\n")
	TEXT("\tAwaitSpeak();                  //@21(0, 1)\n")
	TEXT("}\n")
	TEXT("\n")
	TEXT("if (!isNil(foundThings) && foundThings >= 10)   //@4(0, 3)\n")
	TEXT("{\n")
	TEXT("\tTalk(\"You found at least 10 things!\");         //@5(1, 4)\n")
	TEXT("\tquestLog Push(\"Found 10 things\");              //@6(1, 5)\n")
	TEXT("}\n")
	TEXT("label Check;                       //@10(0, 9)\n")
	TEXT("if (isNil(playerHasGun))      //@11(0, 10)\n")
	TEXT("{\n")
	TEXT("\treturn;                        //@14(1, 13)\n")
	TEXT("}\n")
	TEXT("else\n")
	TEXT("{\n")
	TEXT("\tif (!playerHasGun)             //@15(2, 11)\n")
	TEXT("\t{\n")
	TEXT("\t\tNag();                     //@16(3, 12)\n")
	TEXT("\t\tgoto Check;                //@18(3, 14)\n")
	TEXT("\t}\n")
	TEXT("}\n")
	TEXT("forEach (_entry in questLog)       //@19(0, 15)\n")
	TEXT("{\n")
	TEXT("\tif (!isNil(_entry))         //@22(1, 16)\n")
	TEXT("\t{\n")
	TEXT("\t\tTalk(_entry);              //@23(2, 17)\n")
	TEXT("\t}\n")
	TEXT("}\n"),

	// Comments everywhere, no metadata at all, CRLF line endings.
	TEXT("// File header\r\n// second line\r\ninputs (string _name, int _n = 3);\r\n\r\n//   indented comment\r\nTalk(\"{0}\", _name); // trailing\r\nif (_n > 2)\r\n{\r\n\t// inside\r\n\tTalk(\"big\");\r\n\t// at the end of the block\r\n} // after the block\r\n//no space\r\nfor (int _i = 0; _i < _n; _i++)\r\n{\r\n};\r\n// at the end of the file\r\n"),

	// Functions with comments, and code after a return/goto (only reachable through a label).
	TEXT("// about F\nfunction int F(int _a, string[] _b) // on the function line\n{\n\t// first\n\treturn _a;\n\tlabel After;\n\tTalk(\"x\");\n}\ngoto Skip;\nTalk(\"never\");\nlabel Skip;\nTalk(\"{0}\", F(1, []));\n"),

	// Disconnected nodes in the main graph and in a function.
	TEXT("Talk(\"a\");    //@1(0, 0)\n")
	TEXT("\n")
	TEXT("//~ // a disconnected chain\n")
	TEXT("//~ Talk(\"b\");    //@2(0, 20)\n")
	TEXT("//~ if (true)    //@3(20, 20)\n")
	TEXT("//~ {\n")
	TEXT("//~ \tTalk(\"c\");    //@4(40, 30)\n")
	TEXT("//~ }\n")
	TEXT("\n")
	TEXT("//~ while (false)\n")
	TEXT("//~ {\n")
	TEXT("//~ }\n")
	TEXT("function void G()\n")
	TEXT("{\n")
	TEXT("\tTalk(\"g\");\n")
	TEXT("\n")
	TEXT("\t//~ Talk(\"loose\");    //@9(5, 5)\n")
	TEXT("}\n"),

	// else-if chains, an else holding an if followed by more code, empty blocks, nested loops.
	TEXT("if (true)\n{\n}\nelse if (false)\n{\n\tTalk(\"1\");\n}\nelse\n{\n\tif (true)\n\t{\n\t}\n\tTalk(\"2\");\n}\n")
	TEXT("while (false)\n{\n\tfor (;;)\n\t{\n\t\tforEach (_x in [1, 2])\n\t\t{\n\t\t\tbreak;\n\t\t}\n\t\tcontinue;\n\t}\n}\n"),

	// A very long line doesn't push every //@ column out.
	TEXT("Talk(\"short\");\nTalk(\"this line is far longer than one hundred characters, so its metadata is written four spaces after it and the others stay put\");\nTalk(\"short again\");\n"),

	// Only comments.
	TEXT("// nothing here yet\n"),

	// An empty inputs line.
	TEXT("#inputs ()\nTalk(\"x\");\n"),

	// Reroutes: between statements, first and last in blocks, alone in a block, in an else before an if,
	// in a function, with a comment, and in a disconnected chain.
	TEXT("#inputs (int _n)\n")
	TEXT("                    //@1(4, 0)\n")
	TEXT("Talk(\"a\");           //@2(8, 0)\n")
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
	TEXT("\t\tTalk(\"neg\");     //@8(28, 8)\n")
	TEXT("\t\t                //@9(32, 8)\n")
	TEXT("\t}\n")
	TEXT("}\n")
	TEXT("while (_n > 0)      //@10(40, 0)\n")
	TEXT("{\n")
	TEXT("\t_n--;              //@11(44, 0)\n")
	TEXT("}\n")
	TEXT("                    //@12(48, 0)\n")
	TEXT("//~ Talk(\"loose\");   //@13(0, 20)\n")
	TEXT("//~\n")
	TEXT("//~                 //@14(4, 20)\n")
	TEXT("\n")
	TEXT("//~                 //@15(0, 30)\n")
	TEXT("function void F()\n")
	TEXT("{\n")
	TEXT("\t                    //@16(4, 0)\n")
	TEXT("\tTalk(\"f\");          //@17(8, 0)\n")
	TEXT("}\n"),
	// New nodes from the add-node menu, with their fields still empty: they don't compile, but must load and save.
	TEXT("if ()                    //@1(0, 0)\n")
	TEXT("{\n")
	TEXT("\t= ;                    //@2(4, 0)\n")
	TEXT("\n")
	TEXT("\t++;                    //@3(8, 0)\n")
	TEXT("}\n")
	TEXT("\n")
	TEXT("forEach (_item in)       //@4(12, 0)\n")
	TEXT("{\n")
	TEXT("\tlabel ;                //@5(16, 0)\n")
	TEXT("\n")
	TEXT("\tgoto ;                 //@6(20, 0)\n")
	TEXT("}\n")
	TEXT("\n")
	TEXT("for (int _i = ; _i <= ; _i++) //@7(24, 0)\n")
	TEXT("{\n")
	TEXT("}\n")
	TEXT("\n")
	TEXT("//~ while ()              //@8(0, 10)\n")
	TEXT("//~ {\n")
	TEXT("//~ }\n")
	TEXT("\n")
	TEXT("//~ = ;                   //@9(0, 14)\n"),

	// waitUntil: a node with a condition and one way on, in a loop, disabled, and disconnected.
	TEXT("while (true)    //@1(0, 0)\n{\n\twaitUntil (stage == 3 && Count(found) > 0);    //@2(2, 0)\n\tTalk(\"go\");    //@3(4, 0)\n\t/* waitUntil (false); */    //@4(6, 0)\n}\n//~ waitUntil (ready);    //@5(0, 4)\n"),

	// waitUntil with a timeout: its block is its Timeout output. Empty, in a function, disabled, and disconnected.
	TEXT("waitUntil (stage == 3) timeout (20)    //@1(0, 0)\n{\n\tTalk(\"late\");    //@2(2, 2)\n\tgoto Again;    //@3(4, 2)\n}\nlabel Again;    //@4(2, 0)\nwaitUntil (ready) timeout (_t * 2)    //@5(4, 0)\n{\n}\n/*\nwaitUntil (ready) timeout (1)    //@6(6, 0)\n{\n\tTalk(\"off\");    //@7(8, 2)\n}\n*/\nfunction void F()\n{\n\twaitUntil (ready) timeout (0.5)    //@8(0, 0)\n\t{\n\t\treturn;    //@9(2, 2)\n\t}\n}\n//~ waitUntil (ready) timeout (3)    //@10(0, 6)\n//~ {\n//~ \tTalk(\"x\");    //@11(2, 8)\n//~ }\n"),

	// waitUntil with heldFor, alone and with a timeout (and its IgnoreHold): in a function, disabled, disconnected.
	TEXT("waitUntil (stage == 3) heldFor (2);    //@1(0, 0)\nwaitUntil (ready) heldFor (_h * 2) timeout (20)    //@2(2, 0)\n{\n\tTalk(\"late\");    //@3(4, 2)\n}\nwaitUntil (Count(found) > 0) heldFor (Max(1, _h)) timeout (Max(2, _t), true)    //@4(4, 0)\n{\n}\n/* waitUntil (ready) heldFor (1); */    //@5(6, 0)\nfunction void F()\n{\n\twaitUntil (ready) heldFor (0.5) timeout (3, false)    //@6(0, 0)\n\t{\n\t\treturn;    //@7(2, 2)\n\t}\n}\n//~ waitUntil (ready) heldFor (3);    //@8(0, 6)\n"),

	// Disabled nodes, among comments and reroutes, in a function and a disconnected chain.
	TEXT("// first\n/* Talk(\"b\"); */ //@2(4, 0)\n/*\n// a loop\nwhile (true) //@3(1, 1)\n{\n\t/* break; */ //@4(2, 2)\n\t//@5(3, 3)\n}\n*/\nfunction void F()\n{\n\t/* return; */\n}\n//~ /*\n//~ if (a) //@6(9, 9)\n//~ {\n//~ \t/* /* b(); */ */\n//~ }\n//~ */\n"),
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

	// ---- waitUntil: a Wait Until node, holding its condition ----
	{
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(TEXT("waitUntil (stage == 3);    //@1(0, 0)\nTalk(\"go\");    //@2(2, 0)"), TEXT("wait.bango"), Doc, Error);
		const FBangoGraphNode* Wait = bBuilt && Doc.Main.Nodes.Num() > 1 ? &Doc.Main.Nodes[1] : nullptr;
		Expect(Wait && Wait->Kind == EBangoGraphNodeKind::WaitUntil && Wait->Text == TEXT("stage == 3") && Wait->Next != INDEX_NONE
			&& Bango::GetGraphNodeKindName(Wait->Kind) == TEXT("Wait Until"), FString(TEXT("waitUntil: a Wait Until node with its condition: ")) + Error.ToString());
	}

	// ---- waitUntil with a timeout: its text holds both, and its Timeout output is its block ----
	{
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(TEXT("waitUntil (stage == 3) timeout (20)    //@1(0, 0)\n{\n\tTalk(\"late\");    //@2(2, 2)\n}\nTalk(\"go\");    //@3(4, 0)\nwaitUntil (ready);    //@4(6, 0)"), TEXT("wait.bango"), Doc, Error);
		Expect(bBuilt && Doc.Main.Nodes.Num() == 5, FString(TEXT("waitUntil timeout: builds: ")) + Error.ToString());
		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			FBangoGraphNode* Wait = nullptr;
			FBangoGraphNode* Plain = nullptr;
			for (FBangoGraphNode& Node : Doc.Main.Nodes)
			{
				Wait = Node.NodeID == 1 ? &Node : Wait;
				Plain = Node.NodeID == 4 ? &Node : Plain;
			}
			Expect(Wait && Wait->Kind == EBangoGraphNodeKind::WaitUntil && Wait->Text == TEXT("stage == 3) timeout (20") && Bango::GetGraphNodeBranchCount(*Wait) == 1
				&& Doc.Main.Nodes.IsValidIndex(Wait->Branches[0]) && Doc.Main.Nodes[Wait->Branches[0]].NodeID == 2
				&& Doc.Main.Nodes.IsValidIndex(Wait->Next) && Doc.Main.Nodes[Wait->Next].NodeID == 3, TEXT("waitUntil timeout: its text, its Timeout block and what follows"));
			Expect(Bango::GetGraphNodeBranchCount(EBangoGraphNodeKind::WaitUntil) == 1 && Bango::GetGraphNodeBranchName(EBangoGraphNodeKind::WaitUntil, 0) == FString(TEXT("Timeout"))
				&& Bango::GetGraphNodeNextName(EBangoGraphNodeKind::WaitUntil) == FString(TEXT("Then")), TEXT("waitUntil timeout: outputs Timeout and Then"));
			Expect(Plain && Plain->Text == TEXT("ready") && Bango::GetGraphNodeBranchCount(*Plain) == 1 && Plain->Branches[0] == INDEX_NONE && !Bango::HasWaitUntilTimeout(*Plain) && Bango::HasWaitUntilTimeout(*Wait),
				TEXT("waitUntil timeout: one without has the output, unwired"));

			const FBangoNodeLayoutContext Context = Bango::MakeNodeLayoutContext(Doc, nullptr);
			FBangoNodeLayout Layout = Bango::GetNodeLayout(*Wait, false, Context);
			Expect(Layout.Fields.Num() == 3 && Layout.Fields[0].Text == TEXT("stage == 3") && Layout.Fields[0].bHideLabel
				&& Layout.Fields[2].Label == TEXT("Timeout") && Layout.Fields[2].Text == TEXT("20") && Layout.Fields[2].bPresent && Layout.Fields[2].bOptional && Layout.Fields[2].TypeHint.IsEmpty(),
				TEXT("waitUntil timeout: fields are the condition and Timeout"));
			FString EditError;
			Expect(Bango::SetNodeField(*Wait, false, Context, 2, TEXT("_t * 2"), EditError) && Wait->Text == TEXT("stage == 3) timeout (_t * 2"), FString(TEXT("waitUntil timeout: changing the seconds: ")) + EditError);
			Expect(Bango::SetNodeField(*Wait, false, Context, 0, TEXT("Ready(1)"), EditError) && Wait->Text == TEXT("Ready(1)) timeout (_t * 2"), FString(TEXT("waitUntil timeout: changing the condition: ")) + EditError);
			Expect(!Bango::SetNodeField(*Wait, false, Context, 2, TEXT("1 2"), EditError) && Wait->Text == TEXT("Ready(1)) timeout (_t * 2"), TEXT("waitUntil timeout: seconds that don't read are refused"));

			// Its Timeout output still wired, without seconds: written `timeout ()`, to fill in.
			Expect(Bango::SetNodeField(*Wait, false, Context, 2, FString(), EditError) && Wait->Text == TEXT("Ready(1)"), FString(TEXT("waitUntil timeout: clearing the seconds leaves the condition: ")) + EditError);
			FString Text;
			FString WriteError;
			Expect(Bango::WriteGraphText(Doc, Text, WriteError) && Text.Contains(TEXT("waitUntil (Ready(1)) timeout ()")) && Text.Contains(TEXT("Talk(\"late\");")),
				FString(TEXT("waitUntil timeout: wired without seconds, written with an empty timeout: ")) + Text);
			FBangoGraphDocument Again;
			Expect(Bango::BuildGraph(Text, TEXT("wait.bango"), Again, Error), FString(TEXT("waitUntil timeout: that reads again: ")) + Error.ToString());
			for (const FBangoGraphNode& Node : Again.Main.Nodes)
			{
				if (Node.NodeID == 1)
				{
					Expect(Node.Text == TEXT("Ready(1)) timeout (") && Node.Branches[0] != INDEX_NONE, TEXT("waitUntil timeout: an empty timeout reads back, its block wired"));
					const FBangoNodeLayout EmptyLayout = Bango::GetNodeLayout(Node, false, Context);
					FBangoGraphNode Copy = Node;
					Expect(EmptyLayout.Fields.Num() == 3 && EmptyLayout.Fields[0].Text == TEXT("Ready(1)") && !EmptyLayout.Fields[2].bPresent && !EmptyLayout.Fields[2].bOptional,
						TEXT("waitUntil timeout: its seconds are missing, not optional"));
					Expect(Bango::SetNodeField(Copy, false, Context, 2, TEXT("3"), EditError) && Copy.Text == TEXT("Ready(1)) timeout (3"), FString(TEXT("waitUntil timeout: filling missing seconds in: ")) + Copy.Text);
					Copy = Node;
					Expect(Bango::SetNodeField(Copy, false, Context, 2, FString(), EditError) && Copy.Text == TEXT("Ready(1)"), FString(TEXT("waitUntil timeout: clearing missing seconds takes the timeout away: ")) + Copy.Text);
				}
			}

			// Unwired too, it's a plain waitUntil again. Filling Timeout in on a plain one adds the timeout.
			Wait->Branches[0] = INDEX_NONE;
			Expect(Bango::WriteGraphText(Doc, Text, WriteError) && Text.Contains(TEXT("waitUntil (Ready(1));")), FString(TEXT("waitUntil timeout: unwired and without seconds, a plain waitUntil: ")) + Text);
			Layout = Bango::GetNodeLayout(*Plain, false, Context);
			Expect(Layout.Fields.Num() == 3 && Layout.Fields[0].Text == TEXT("ready") && !Layout.Fields[2].bPresent && Layout.Fields[2].bOptional, TEXT("waitUntil timeout: a plain one has an empty Timeout field"));
			Expect(Bango::SetNodeField(*Plain, false, Context, 2, TEXT("5"), EditError) && Plain->Text == TEXT("ready) timeout (5"), FString(TEXT("waitUntil timeout: filling Timeout in: ")) + EditError);
			Expect(Bango::WriteGraphText(Doc, Text, WriteError) && Text.Contains(TEXT("waitUntil (ready) timeout (5)")), FString(TEXT("waitUntil timeout: written with its (empty) block: ")) + Text);
			Expect(Bango::BuildGraph(Text, TEXT("wait.bango"), Again, Error), FString(TEXT("waitUntil timeout: and that reads again: ")) + Error.ToString());

			// ---- heldFor: a Held For row, and with a timeout too, an Ignore Hold row ----
			FBangoGraphDocument HeldDoc;
			const bool bHeldBuilt = Bango::BuildGraph(TEXT("waitUntil (stage == 3) heldFor (2) timeout (20, true)    //@1(0, 0)\n{\n}\nwaitUntil (ready);    //@2(2, 0)\n"), TEXT("held.bango"), HeldDoc, Error);
			Expect(bHeldBuilt, FString(TEXT("heldFor: builds: ")) + Error.ToString());
			FBangoGraphNode* Held = nullptr;
			FBangoGraphNode* Bare = nullptr;
			for (FBangoGraphNode& Node : HeldDoc.Main.Nodes)
			{
				Held = Node.NodeID == 1 ? &Node : Held;
				Bare = Node.NodeID == 2 ? &Node : Bare;
			}
			if (bHeldBuilt && Held && Bare)
			{
				const FBangoNodeLayoutContext HeldContext = Bango::MakeNodeLayoutContext(HeldDoc, nullptr);
				Expect(Held->Text == TEXT("stage == 3) heldFor (2) timeout (20, true"), FString(TEXT("heldFor: the node's text holds it all: ")) + Held->Text);
				Layout = Bango::GetNodeLayout(*Held, false, HeldContext);
				Expect(Layout.Fields.Num() == 4 && Layout.Fields[0].Text == TEXT("stage == 3")
					&& Layout.Fields[1].Label == TEXT("Held For") && Layout.Fields[1].Text == TEXT("2") && Layout.Fields[1].bPresent && Layout.Fields[1].bOptional && Layout.Fields[1].TypeHint == TEXT("float")
					&& Layout.Fields[2].Label == TEXT("Timeout") && Layout.Fields[2].Text == TEXT("20") && Layout.Fields[2].bPresent
					&& Layout.Fields[3].Label == TEXT("Ignore Hold") && Layout.Fields[3].Text == TEXT("true") && Layout.Fields[3].bPresent && Layout.Fields[3].bOptional && Layout.Fields[3].TypeHint == TEXT("bool"),
					TEXT("heldFor: fields are the condition, Held For, Timeout and Ignore Hold"));
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 3, FString(), EditError) && Held->Text == TEXT("stage == 3) heldFor (2) timeout (20"), FString(TEXT("heldFor: clearing Ignore Hold: ")) + Held->Text);
				Layout = Bango::GetNodeLayout(*Held, false, HeldContext);
				Expect(Layout.Fields.Num() == 4 && !Layout.Fields[3].bPresent && Layout.Fields[3].bOptional && Layout.Fields[2].Text == TEXT("20"), TEXT("heldFor: Ignore Hold is there to fill in, with both"));
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 3, TEXT("_cut"), EditError) && Held->Text == TEXT("stage == 3) heldFor (2) timeout (20, _cut"), FString(TEXT("heldFor: filling Ignore Hold in: ")) + EditError);
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 2, TEXT("Max(1, 2)"), EditError) && Held->Text == TEXT("stage == 3) heldFor (2) timeout (Max(1, 2), _cut"), FString(TEXT("heldFor: Timeout's seconds end at the comma: ")) + Held->Text);
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 1, TEXT("_h + 1"), EditError) && Held->Text == TEXT("stage == 3) heldFor (_h + 1) timeout (Max(1, 2), _cut"), FString(TEXT("heldFor: editing Held For: ")) + Held->Text);
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 0, TEXT("Ready(1)"), EditError) && Held->Text == TEXT("Ready(1)) heldFor (_h + 1) timeout (Max(1, 2), _cut"), FString(TEXT("heldFor: editing the condition: ")) + Held->Text);
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 1, FString(), EditError) && Held->Text == TEXT("Ready(1)) timeout (Max(1, 2)"), FString(TEXT("heldFor: clearing Held For takes Ignore Hold with it: ")) + Held->Text);
				Layout = Bango::GetNodeLayout(*Held, false, HeldContext);
				Expect(Layout.Fields.Num() == 3 && !Layout.Fields[1].bPresent && Layout.Fields[1].bOptional && Layout.Fields[2].Text == TEXT("Max(1, 2)"), TEXT("heldFor: without it, no Ignore Hold row"));
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 1, TEXT("1.5"), EditError) && Held->Text == TEXT("Ready(1)) heldFor (1.5) timeout (Max(1, 2)"), FString(TEXT("heldFor: filling Held For in before a timeout: ")) + Held->Text);
				Expect(Bango::SetNodeField(*Held, false, HeldContext, 2, FString(), EditError) && Held->Text == TEXT("Ready(1)) heldFor (1.5"), FString(TEXT("heldFor: clearing Timeout leaves the hold: ")) + Held->Text);
				Expect(Bango::SetNodeField(*Bare, false, HeldContext, 1, TEXT("4"), EditError) && Bare->Text == TEXT("ready) heldFor (4"), FString(TEXT("heldFor: filling Held For in on a plain one: ")) + EditError);
				FString HeldText;
				Expect(Bango::WriteGraphText(HeldDoc, HeldText, WriteError) && HeldText.Contains(TEXT("waitUntil (Ready(1)) heldFor (1.5);")) && HeldText.Contains(TEXT("waitUntil (ready) heldFor (4);")),
					FString(TEXT("heldFor: written without a block: ")) + HeldText);
				Expect(Bango::SetNodeField(*Bare, false, HeldContext, 2, TEXT("9"), EditError) && Bare->Text == TEXT("ready) heldFor (4) timeout (9"), FString(TEXT("heldFor: filling Timeout in after it: ")) + Bare->Text);
				Expect(Bango::WriteGraphText(HeldDoc, HeldText, WriteError) && HeldText.Contains(TEXT("waitUntil (ready) heldFor (4) timeout (9)")) && Bango::BuildGraph(HeldText, TEXT("held.bango"), Again, Error),
					FString(TEXT("heldFor: written with its timeout's block, and read again: ")) + HeldText + Error.ToString());
				const Bango::FBangoWaitUntilText Parts = Bango::SplitWaitUntilText(TEXT("F(a, b)) heldFor (G(1)[0]) timeout (Max(1, 2), _x"));
				Expect(Parts.ConditionEnd == 7 && Parts.bHold && Parts.HoldBegin == 18 && Parts.HoldEnd == 25 && Parts.bTimeout && Parts.TimeoutOuterBegin == 25 && Parts.SecondsBegin == 36
					&& Parts.bIgnoreHold && Parts.SecondsEnd == 45 && Parts.IgnoreHoldBegin == 46, TEXT("heldFor: the parts of a Wait Until's text"));
				const Bango::FBangoWaitUntilText None = Bango::SplitWaitUntilText(TEXT("heldFor(a) && timeout(b)"));
				Expect(!None.bHold && !None.bTimeout && None.ConditionEnd == 24, TEXT("heldFor: only after the condition's bracket"));
			}
		}
	}

	// ---- Large Script Threshold: below it, written compact (no headings, no metadata) ----
	{
		const TCHAR* Source =
			TEXT("#LOCTEXT_NAMESPACE \"N\"\n")
			TEXT("\n")
			TEXT("// ============ SCRIPT INPUTS ===================\n")
			TEXT("\n")
			TEXT("#inputs (int _n)\n")
			TEXT("\n")
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("Sleep(4);                 //@1(0, 0)\n")
			TEXT("                          //@3(5, 0)\n")
			TEXT("Talk(\"You got a pistol!\");    //@2(10, 0)\n");
		const TCHAR* Compact =
			TEXT("#LOCTEXT_NAMESPACE \"N\"\n")
			TEXT("\n")
			TEXT("#inputs (int _n)\n")
			TEXT("\n")
			TEXT("Sleep(4);\n")
			TEXT("Talk(\"You got a pistol!\");\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("small.bango"), Doc, Error);
		Expect(bBuilt && Bango::CountGraphNodes(Doc) == 3, FString(TEXT("threshold: two statements and a reroute are three nodes: ")) + Error.ToString());
		// Following the script's flag (the editor's saves): without #USES_GRAPH_EDITOR, compact.
		FBangoGraphWriteOptions Options;
		Options.bFollowUsesGraphEditor = true;
		Expect(bBuilt && !Doc.bUsesGraphEditor && Bango::IsWrittenCompact(Doc, Options) && Bango::WriteGraphText(Doc, Text, WriteError, Options) && Text == FString(Compact),
			FString(TEXT("uses graph editor: without the flag, written compact:\n")) + Text);
		Expect(bBuilt && !Bango::IsWrittenCompact(Doc, FBangoGraphWriteOptions()), TEXT("uses graph editor: not following the flag, always in full"));

		// With the flag: in full, and the flag is written back, after the namespace.
		FBangoGraphDocument Own;
		const bool bOwnBuilt = Bango::BuildGraph(FString(TEXT("#USES_GRAPH_EDITOR\n")) + Source, TEXT("own.bango"), Own, Error);
		Expect(bOwnBuilt && Own.bUsesGraphEditor && !Bango::IsWrittenCompact(Own, Options), FString(TEXT("uses graph editor: with the flag, in full: ")) + Error.ToString());
		Expect(bOwnBuilt && Bango::WriteGraphText(Own, Text, WriteError, Options) && Text.StartsWith(TEXT("#LOCTEXT_NAMESPACE \"N\"\n#USES_GRAPH_EDITOR\n"), ESearchCase::CaseSensitive)
			&& Text.Contains(TEXT("//@1(0, 0)")) && Text.Contains(TEXT("// ============ SCRIPT BODY")),
			FString(TEXT("uses graph editor: the #USES_GRAPH_EDITOR line is written after the namespace, and the node metadata:\n")) + Text);
		FBangoGraphDocument Changed = Own;
		Changed.bUsesGraphEditor = false;
		Expect(Changed != Own, TEXT("uses graph editor: part of what makes two documents different"));

		// Always compact (an inline script made editable in its box): the flag goes too.
		FBangoGraphWriteOptions AlwaysCompact;
		AlwaysCompact.bCompact = true;
		Expect(bOwnBuilt && Bango::IsWrittenCompact(Own, AlwaysCompact) && Bango::WriteGraphText(Own, Text, WriteError, AlwaysCompact) && !Text.Contains(TEXT("USES_GRAPH_EDITOR")) && !Text.Contains(TEXT("//@")),
			FString(TEXT("uses graph editor: bCompact drops the flag and the metadata:\n")) + Text);

		// A compact script without a description that starts with a comment: a blank line first, so it isn't the description.
		FBangoGraphDocument Commented;
		const bool bCommentedBuilt = Bango::BuildGraph(TEXT("\n// first\nSleep(1);    //@1(0, 0)\n"), TEXT("c.bango"), Commented, Error);
		Options.bCompact = true;
		Expect(bCommentedBuilt && Bango::WriteGraphText(Commented, Text, WriteError, Options) && Text == FString(TEXT("\n// first\nSleep(1);\n")),
			FString(TEXT("compact: a leading comment isn't made the description:\n")) + Text);
	}

	// ---- Disabled nodes: inline for simple nodes, marker lines around block nodes, nested, in disconnected chains ----
	{
		const TCHAR* Source =
			TEXT("Talk(\"a\");          //@1(0, 0)\n")
			TEXT("// off for now\n")
			TEXT("/* Talk(\"b\"); */    //@2(4, 0)\n")
			TEXT("/*\n")
			TEXT("if (x)              //@3(8, 0)\n")
			TEXT("{\n")
			TEXT("\t/* y(); */      //@4(12, 0)\n")
			TEXT("\tz();            //@5(12, 4)\n")
			TEXT("}\n")
			TEXT("else if (w)         //@6(8, 4)\n")
			TEXT("{\n")
			TEXT("}\n")
			TEXT("*/\n")
			TEXT("//~ /* q(); */      //@9(0, 20)\n");
		const TCHAR* Expected =
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("Talk(\"a\");          //@1(0, 0)\n")
			TEXT("\n")
			TEXT("// off for now\n")
			TEXT("/* Talk(\"b\"); */    //@2(4, 0)\n")
			TEXT("\n")
			TEXT("/*\n")
			TEXT("if (x)              //@3(8, 0)\n")
			TEXT("{\n")
			TEXT("\t/* y(); */      //@4(12, 0)\n")
			TEXT("\tz();            //@5(12, 4)\n")
			TEXT("}\n")
			TEXT("else if (w)         //@6(8, 4)\n")
			TEXT("{\n")
			TEXT("}\n")
			TEXT("*/\n")
			TEXT("\n")
			TEXT("//~ /* q(); */      //@9(0, 20)\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("disabled.bango"), Doc, Error);
		auto IsDisabled = [&Doc](int64 Id)
		{
			for (const FBangoGraphNode& Node : Doc.Main.Nodes)
			{
				if (Node.NodeID == Id)
				{
					return Node.bDisabled;
				}
			}
			return false;
		};
		Expect(bBuilt && !IsDisabled(1) && IsDisabled(2) && IsDisabled(3) && IsDisabled(4) && !IsDisabled(5) && !IsDisabled(6) && IsDisabled(9),
			FString(TEXT("Disabled nodes: read: ")) + Error.ToString());
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("Disabled nodes: written:\n")) + Text);

		// Disabling the else-if node: it can't stay `else if`, so it's written in an else block.
		for (FBangoGraphNode& Node : Doc.Main.Nodes)
		{
			if (Node.NodeID == 6)
			{
				Node.bDisabled = true;
			}
		}
		const TCHAR* ExpectedElse = TEXT("}\nelse\n{\n\t/*\n\tif (w) ");
		const TCHAR* ExpectedElseEnd = TEXT("\t{\n\t}\n\t*/\n}\n*/\n");
		FString ElseText;
		Expect(bBuilt && Bango::WriteGraphText(Doc, ElseText, WriteError) && ElseText.Contains(ExpectedElse) && ElseText.Contains(ExpectedElseEnd), FString(TEXT("Disabled nodes: a disabled else-if: ")) + WriteError + TEXT("\n") + ElseText);
		FBangoGraphDocument ElseDoc;
		Expect(Bango::BuildGraph(ElseText, TEXT("disabled.bango"), ElseDoc, Error) && ElseDoc == Doc, FString(TEXT("Disabled nodes: a disabled else-if reads back the same: ")) + Error.ToString());
	}

	// ---- LOCTEXT_NAMESPACE and text: #LOCTEXT_NAMESPACE above the inputs (read after them too), text literals kept as written ----
	{
		const TCHAR* Source =
			TEXT("\n") // so the comment below isn't the script's description (that's only on line 1)
			TEXT("// About the script\n")
			TEXT("#inputs (text _line = nil)\n")
			TEXT("#LOCTEXT_NAMESPACE \"Bango.Intro\"\n")
			TEXT("// About the hint\n")
			TEXT("Say(NSNOTELOCTEXT(\"Other\", \"K\", \"Hello {0}\", \"Said slowly\"), 1); //@1(10, 0)\n")
			TEXT("function void F()\n{\n\tSay(LOCTEXT(\"F\", \"In F\")); //@2(0, 0)\n}\n");
		const TCHAR* Expected =
			TEXT("#LOCTEXT_NAMESPACE \"Bango.Intro\"\n")
			TEXT("\n")
			TEXT("// ============ SCRIPT INPUTS ===================\n")
			TEXT("\n")
			TEXT("// About the script\n")
			TEXT("#inputs (text _line = nil)\n")
			TEXT("\n")
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("// About the hint\n")
			TEXT("Say(NSNOTELOCTEXT(\"Other\", \"K\", \"Hello {0}\", \"Said slowly\"), 1);    //@1(10, 0)\n")
			TEXT("\n")
			TEXT("// ============ FUNCTIONS =======================\n")
			TEXT("\n")
			TEXT("function void F()\n")
			TEXT("{\n")
			TEXT("\tSay(LOCTEXT(\"F\", \"In F\"));                                      //@2(0, 0)\n")
			TEXT("}\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("text.bango"), Doc, Error);
		Expect(bBuilt && Doc.bHasLocNamespace && Doc.LocNamespace == FString(TEXT("Bango.Intro")), FString(TEXT("LOCTEXT_NAMESPACE: read into the document: ")) + Error.ToString());
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("LOCTEXT_NAMESPACE: saved above the inputs:\n")) + Text);

		// The namespace line comes after the description, then the inputs.
		{
			const TCHAR* Configured =
				TEXT("// Description line\n")
				TEXT("// More description lines\n")
				TEXT("\n")
				TEXT("#LOCTEXT_NAMESPACE \"BangoTest\"\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT INPUTS ===================\n")
				TEXT("\n")
				TEXT("#inputs (entity _who, int _count = 3)\n")
				TEXT("\n")
				TEXT("// ============ GLOBAL VARS =====================\n")
				TEXT("\n")
				TEXT("global bool _newGlobal;\n");
			FBangoGraphDocument ConfigDoc;
			FString ConfigText;
			const bool bConfigBuilt = Bango::BuildGraph(Configured, TEXT("config.bango"), ConfigDoc, Error);
			Expect(bConfigBuilt && ConfigDoc.Description == FString(TEXT("Description line\nMore description lines")) && ConfigDoc.LocNamespace == FString(TEXT("BangoTest"))
				&& ConfigDoc.Main.Nodes[0].Comment.IsEmpty(), FString(TEXT("LOCTEXT_NAMESPACE: read under the description: ")) + Error.ToString());
			Expect(bConfigBuilt && Bango::WriteGraphText(ConfigDoc, ConfigText, WriteError) && ConfigText == FString(Configured),
				FString(TEXT("LOCTEXT_NAMESPACE: under the description, written back the same:\n")) + ConfigText + WriteError);
		}

		// The FUNCTIONS heading: written on save, skipped when read. A line of dashes between functions (files used to
		// have one) is skipped when read too, outside function bodies, and not written again. A function's own
		// comment, and the same line inside a function body, are kept.
		{
			const TCHAR* Separated =
				TEXT("// ============ SCRIPT BODY =====================\n")
				TEXT("\n")
				TEXT("Talk(\"x\");        //@1(0, 0)\n")
				TEXT("\n")
				TEXT("// ============ FUNCTIONS =======================\n")
				TEXT("\n")
				TEXT("// What F does\n")
				TEXT("function void F()\n")
				TEXT("{\n")
				TEXT("\t// ----------------------------------------------\n")
				TEXT("\tTalk(\"f\");    //@2(0, 0)\n")
				TEXT("}\n")
				TEXT("\n")
				TEXT("// ----------------------------------------------\n")
				TEXT("\n")
				TEXT("function void G()\n")
				TEXT("{\n")
				TEXT("}\n");
			FBangoGraphDocument SepDoc;
			FString SepText;
			const bool bSepBuilt = Bango::BuildGraph(Separated, TEXT("sep.bango"), SepDoc, Error);
			Expect(bSepBuilt && SepDoc.Functions.Num() == 2 && SepDoc.Main.Nodes[1].Comment.IsEmpty()
				&& SepDoc.Functions[0].Nodes[0].Comment == FString(TEXT("What F does")) && SepDoc.Functions[1].Nodes[0].Comment.IsEmpty()
				&& SepDoc.Functions[0].Nodes[1].Comment == FString(TEXT("----------------------------------------------")),
				FString(TEXT("function separators: not read as comments: ")) + Error.ToString());
			const FString SeparatedWritten = FString(Separated).Replace(TEXT("}\n\n// ----------------------------------------------\n\nfunction"), TEXT("}\n\nfunction"));
			Expect(bSepBuilt && Bango::WriteGraphText(SepDoc, SepText, WriteError) && SepText == SeparatedWritten && SepText != FString(Separated),
				FString(TEXT("function separators: written back without the line between the functions:\n")) + SepText + WriteError);
			// A compact script keeps the FUNCTIONS heading, and only that one; an inline script's box has none.
			FBangoGraphWriteOptions FollowOptions;
			FollowOptions.bFollowUsesGraphEditor = true;
			FBangoGraphWriteOptions BoxOptions;
			BoxOptions.bCompact = true;
			FString FollowText;
			FString BoxText;
			Expect(bSepBuilt && Bango::WriteGraphText(SepDoc, FollowText, WriteError, FollowOptions)
				&& FollowText.Contains(TEXT("Talk(\"x\");\n\n// ============ FUNCTIONS =======================\n\n// What F does\nfunction void F()"))
				&& FollowText.Contains(TEXT("}\n\nfunction void G()")) && !FollowText.Contains(TEXT("SCRIPT BODY")),
				FString(TEXT("function separators: a compact script has the FUNCTIONS heading:\n")) + FollowText + WriteError);
			Expect(bSepBuilt && Bango::WriteGraphText(SepDoc, BoxText, WriteError, BoxOptions) && !BoxText.Contains(TEXT("====")),
				FString(TEXT("function separators: an inline script's box has no heading:\n")) + BoxText + WriteError);
			FBangoGraphDocument FollowDoc;
			FString FollowAgain;
			Expect(Bango::BuildGraph(FollowText, TEXT("sep.bango"), FollowDoc, Error) && Bango::WriteGraphText(FollowDoc, FollowAgain, WriteError, FollowOptions) && FollowAgain == FollowText,
				FString(TEXT("function separators: a compact script saved again is the same:\n")) + FollowAgain);
			// Only functions, compact: the heading is the first line, and isn't read back as the description.
			FBangoGraphDocument OnlyDoc;
			FString OnlyText;
			Expect(Bango::BuildGraph(TEXT("function void F()\n{\n}\n"), TEXT("only.bango"), OnlyDoc, Error) && Bango::WriteGraphText(OnlyDoc, OnlyText, WriteError, FollowOptions)
				&& OnlyText.StartsWith(TEXT("// ============ FUNCTIONS =======================\n\nfunction void F()"), ESearchCase::CaseSensitive),
				FString(TEXT("function separators: a script of only functions starts with the heading:\n")) + OnlyText);
			FBangoGraphDocument OnlyAgain;
			Expect(Bango::BuildGraph(OnlyText, TEXT("only.bango"), OnlyAgain, Error) && OnlyAgain.Description.IsEmpty(), TEXT("function separators: the heading isn't the description"));
		}

		// The description: the // lines from line 1 on, until a line that isn't one. Not a node's comment; saved
		// first, then a blank line. A bare // in it is an empty line of it.
		{
			const TCHAR* Described =
				TEXT("// Opens the gate.\n")
				TEXT("//\n")
				TEXT("//   Needs the key.\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT INPUTS ===================\n")
				TEXT("\n")
				TEXT("// Start's own comment\n")
				TEXT("#inputs (int _a)\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT BODY =====================\n")
				TEXT("\n")
				TEXT("Talk(\"x\");    //@1(0, 0)\n");
			FBangoGraphDocument DescDoc;
			FString DescText;
			const bool bDescBuilt = Bango::BuildGraph(Described, TEXT("desc.bango"), DescDoc, Error);
			Expect(bDescBuilt && DescDoc.Description == FString(TEXT("Opens the gate.\n\n  Needs the key.")) && DescDoc.Main.Nodes[0].Comment == FString(TEXT("Start's own comment"))
				&& DescDoc.Main.Nodes[1].Comment.IsEmpty(), FString(TEXT("description: read from the top: ")) + DescDoc.Description + Error.ToString());
			Expect(bDescBuilt && Bango::WriteGraphText(DescDoc, DescText, WriteError) && DescText == FString(Described),
				FString(TEXT("description: written back the same:\n")) + DescText + WriteError);

			// Ends at code with no blank line too; a comment that isn't on line 1 isn't one.
			FBangoGraphDocument Short;
			Expect(Bango::BuildGraph(TEXT("// Just this\nTalk(\"x\");\n"), TEXT("desc.bango"), Short, Error) && Short.Description == FString(TEXT("Just this"))
				&& Short.Main.Nodes.Num() == 2 && Short.Main.Nodes[1].Comment.IsEmpty() && Bango::WriteGraphText(Short, Text, WriteError)
				&& Text.StartsWith(TEXT("// Just this\n\n// ============ SCRIPT BODY"), ESearchCase::CaseSensitive), FString(TEXT("description: then code: ")) + Text);
			FBangoGraphDocument NotFirst;
			Expect(Bango::BuildGraph(TEXT("\n// About Talk\nTalk(\"x\");\n"), TEXT("desc.bango"), NotFirst, Error) && NotFirst.Description.IsEmpty()
				&& NotFirst.Main.Nodes[1].Comment == FString(TEXT("About Talk")), TEXT("description: not after a blank first line"));
			FBangoGraphDocument Heading;
			Expect(Bango::BuildGraph(TEXT("// ============ SCRIPT BODY =====================\n\nTalk(\"x\");\n"), TEXT("desc.bango"), Heading, Error) && Heading.Description.IsEmpty(),
				TEXT("description: a heading on line 1 isn't one"));
		}

		FBangoGraphDocument NoParams;
		Expect(Bango::BuildGraph(TEXT("#LOCTEXT_NAMESPACE \"A\"\n"), TEXT("ns.bango"), NoParams, Error) && Bango::WriteGraphText(NoParams, Text, WriteError)
			&& Text == FString(TEXT("#LOCTEXT_NAMESPACE \"A\"\n")), FString(TEXT("LOCTEXT_NAMESPACE: without inputs or statements:\n")) + Text);
		FBangoGraphDocument Commented;
		Expect(Bango::BuildGraph(TEXT("\n// Hi\n#LOCTEXT_NAMESPACE \"A\"\n"), TEXT("ns.bango"), Commented, Error) && Bango::WriteGraphText(Commented, Text, WriteError)
			&& Text == FString(TEXT("#LOCTEXT_NAMESPACE \"A\"\n\n// ============ SCRIPT INPUTS ===================\n\n// Hi\n#inputs ()\n")), FString(TEXT("LOCTEXT_NAMESPACE: a comment on it is Script Start's, written above an empty inputs line:\n")) + Text);
		FBangoGraphDocument Changed = NoParams;
		Changed.LocNamespace = TEXT("B");
		Expect(Changed != NoParams, TEXT("LOCTEXT_NAMESPACE: part of what makes two documents different"));
	}

	// ---- Reroutes: a line with only //@ is a reroute node; saving writes it back the same way ----
	{
		const TCHAR* Source =
			TEXT("Talk(\"a\"); //@1(0, 0)\n")
			TEXT("   //@2(10, 0)\n")
			TEXT("if (true) //@3(20, 0)\n")
			TEXT("{\n")
			TEXT("//@4(30, 4)\n")
			TEXT("}\n");
		const TCHAR* Expected =
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("Talk(\"a\");    //@1(0, 0)\n")
			TEXT("              //@2(10, 0)\n")
			TEXT("\n")
			TEXT("if (true)     //@3(20, 0)\n")
			TEXT("{\n")
			TEXT("\t          //@4(30, 4)\n")
			TEXT("}\n");
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
	}
	{
		// A sequence: an output per block (an empty block is an output with nothing on it), and Next for what follows.
		const TCHAR* Source =
			TEXT("sequence      //@1(20, 0)\n")
			TEXT("{\n")
			TEXT("\tTalk(\"a\");  //@2(40, 10)\n")
			TEXT("}\n")
			TEXT("{\n")
			TEXT("}\n")
			TEXT("{\n")
			TEXT("\tTalk(\"c\");  //@3(40, 30)\n")
			TEXT("}\n")
			TEXT("\n")
			TEXT("Talk(\"d\");    //@4(40, 0)\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("sequence.bango"), Doc, Error);
		Expect(bBuilt && Doc.Main.Nodes.Num() == 5, FString(TEXT("sequence: builds: ")) + Error.ToString());
		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(N[1].Kind == EBangoGraphNodeKind::Sequence && N[1].Text.IsEmpty() && N[1].Branches.Num() == 3 && Bango::GetGraphNodeBranchCount(N[1]) == 3,
				TEXT("sequence: a node with three outputs"));
			Expect(N[N[1].Branches[0]].Text == FString(TEXT("Talk(\"a\")")) && N[1].Branches[1] == INDEX_NONE && N[N[1].Branches[2]].Text == FString(TEXT("Talk(\"c\")")),
				TEXT("sequence: outputs 0 and 2 have their blocks, 1 is empty"));
			Expect(N[N[1].Next].Text == FString(TEXT("Talk(\"d\")")), TEXT("sequence: what follows is on Then"));
			Expect(Bango::GetGraphNodeBranchName(EBangoGraphNodeKind::Sequence, 2) == FString(TEXT("Scope 2")) && Bango::GetGraphNodeNextName(EBangoGraphNodeKind::Sequence) == FString(TEXT("Then")),
				TEXT("sequence: outputs named Scope 0, Scope 1, Scope 2 and Then"));
		}
		const TCHAR* Expected =
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("sequence          //@1(20, 0)\n")
			TEXT("{\n")
			TEXT("\tTalk(\"a\");    //@2(40, 10)\n")
			TEXT("}\n")
			TEXT("{\n")
			TEXT("}\n")
			TEXT("{\n")
			TEXT("\tTalk(\"c\");    //@3(40, 30)\n")
			TEXT("}\n")
			TEXT("\n")
			TEXT("Talk(\"d\");        //@4(40, 0)\n");
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("sequence: written in the saved layout:\n")) + Text);

		// Adding an output (as the editor's Add pin does) writes an empty block; with none, it isn't written.
		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			FBangoGraphDocument More = Doc;
			More.Main.Nodes[1].Branches.Add(INDEX_NONE);
			Expect(Bango::WriteGraphText(More, Text, WriteError) && Text.Contains(TEXT("}\n{\n}\n\nTalk(\"d\")")), FString(TEXT("sequence: a fourth, empty output:\n")) + Text);
			FBangoGraphDocument None = Doc;
			None.Main.Nodes[1].Branches.Reset();
			Expect(!Bango::WriteGraphText(None, Text, WriteError) && WriteError.Contains(TEXT("no outputs")), FString(TEXT("sequence: needs an output: ")) + WriteError);
		}
	}
	{
		// A switch: a row per case (its values as written) with an output, Default last, and Next for what follows.
		const TCHAR* Source =
			TEXT("switch (_x)   //@1(20, 0)\n")
			TEXT("{\n")
			TEXT("\tcase 1, -2:\n")
			TEXT("\t{\n")
			TEXT("\t\tTalk(\"a\");  //@2(40, 10)\n")
			TEXT("\t}\n")
			TEXT("\tcase 3:\n")
			TEXT("\t{\n")
			TEXT("\t}\n")
			TEXT("\tdefault:\n")
			TEXT("\t{\n")
			TEXT("\t\tTalk(\"c\");  //@3(40, 30)\n")
			TEXT("\t}\n")
			TEXT("}\n")
			TEXT("\n")
			TEXT("Talk(\"d\");    //@4(40, 0)\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		FString Text;
		FString WriteError;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("switch.bango"), Doc, Error);
		Expect(bBuilt && Doc.Main.Nodes.Num() == 5, FString(TEXT("switch: builds: ")) + Error.ToString());
		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(N[1].Kind == EBangoGraphNodeKind::Switch && N[1].Text == FString(TEXT("_x")) && N[1].Branches.Num() == 3 && Bango::GetGraphNodeBranchCount(N[1]) == 3
				&& N[1].CaseValues.Num() == 2 && N[1].CaseValues[0] == FString(TEXT("1, -2")) && N[1].CaseValues[1] == FString(TEXT("3")),
				TEXT("switch: a node with two cases and Default"));
			Expect(N[N[1].Branches[0]].Text == FString(TEXT("Talk(\"a\")")) && N[1].Branches[1] == INDEX_NONE && N[N[1].Branches[2]].Text == FString(TEXT("Talk(\"c\")")),
				TEXT("switch: case 0 and Default have their blocks, case 1 is empty"));
			Expect(N[N[1].Next].Text == FString(TEXT("Talk(\"d\")")), TEXT("switch: what follows is on Then"));
			Expect(Bango::GetGraphNodeBranchName(N[1], 0) == FString(TEXT("1, -2")) && Bango::GetGraphNodeBranchName(N[1], 2) == FString(TEXT("Default"))
				&& Bango::GetGraphNodeNextName(EBangoGraphNodeKind::Switch) == FString(TEXT("Then")),
				TEXT("switch: outputs named by their values, Default and Then"));
		}
		const TCHAR* Expected =
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("switch (_x)           //@1(20, 0)\n")
			TEXT("{\n")
			TEXT("\tcase 1, -2:\n")
			TEXT("\t{\n")
			TEXT("\t\tTalk(\"a\");    //@2(40, 10)\n")
			TEXT("\t}\n")
			TEXT("\tcase 3:\n")
			TEXT("\t{\n")
			TEXT("\t}\n")
			TEXT("\tdefault:\n")
			TEXT("\t{\n")
			TEXT("\t\tTalk(\"c\");    //@3(40, 30)\n")
			TEXT("\t}\n")
			TEXT("}\n")
			TEXT("\n")
			TEXT("Talk(\"d\");            //@4(40, 0)\n");
		Expect(bBuilt && Bango::WriteGraphText(Doc, Text, WriteError) && Text == FString(Expected), FString(TEXT("switch: written in the saved layout:\n")) + Text);

		if (bBuilt && Doc.Main.Nodes.Num() == 5)
		{
			// Default with nothing on it isn't written; a new case (as Add pin makes) goes before Default.
			FBangoGraphDocument NoDefault = Doc;
			NoDefault.Main.Nodes[1].Branches[2] = INDEX_NONE;
			Expect(Bango::WriteGraphText(NoDefault, Text, WriteError) && !Text.Contains(TEXT("default")), FString(TEXT("switch: an unwired Default isn't written:\n")) + Text);
			FBangoGraphDocument More = Doc;
			More.Main.Nodes[1].CaseValues.Add(TEXT("4"));
			More.Main.Nodes[1].Branches.Insert(INDEX_NONE, 2);
			Expect(Bango::WriteGraphText(More, Text, WriteError) && Text.Contains(TEXT("\tcase 4:\n\t{\n\t}\n\tdefault:")), FString(TEXT("switch: a third case:\n")) + Text);
			FBangoGraphDocument Mismatch = Doc;
			Mismatch.Main.Nodes[1].CaseValues.Add(TEXT("4"));
			Expect(!Bango::WriteGraphText(Mismatch, Text, WriteError) && WriteError.Contains(TEXT("one per case")), FString(TEXT("switch: outputs must match cases: ")) + WriteError);
		}

		// Case values: fixed values only; an error on a case line points at that case.
		FString ValueError;
		Expect(Bango::ValidateCaseValues(TEXT("1, -2, \"a\", @guard"), ValueError), FString(TEXT("switch: valid case values: ")) + ValueError);
		Expect(!Bango::ValidateCaseValues(TEXT(""), ValueError) && ValueError.Contains(TEXT("needs a value")), FString(TEXT("switch: an empty case: ")) + ValueError);
		Expect(!Bango::ValidateCaseValues(TEXT("_y"), ValueError) && ValueError.Contains(TEXT("fixed value")), FString(TEXT("switch: a name as a case value: ")) + ValueError);
		Expect(!Bango::ValidateCaseValues(TEXT("1 +"), ValueError), TEXT("switch: case values that don't read"));
		Expect(!Bango::ValidateCaseValues(TEXT("1:"), ValueError), TEXT("switch: a colon in case values"));
		if (bBuilt)
		{
			FBangoCompileError CaseError;
			CaseError.NodeID = 1;
			CaseError.Line = 9; // `\tcase 3:` in Expected
			CaseError.Column = 7;
			Bango::FBangoErrorPlace Place;
			Expect(Bango::FindErrorPlace(Doc, Expected, CaseError, Place) && Place.Node == 1 && Place.Case == 1 && Place.Offset == 0,
				FString::Printf(TEXT("switch: an error on a case line finds the case (node %d, case %d, offset %d)"), Place.Node, Place.Case, Place.Offset));
		}

		// Statements of the same kind sit together; a blank line goes between different kinds, around flow
		// control, and above a statement with a comment.
		{
			const TCHAR* GroupSource =
				TEXT("global int b;\nscriptVar int _a = 1;\n_a = 2; //@3(0,2)\n_a++; //@4(0,3)\nTalk(\"x\"); //@5(0,4)\nLog(\"y\"); //@6(0,5)\n")
				TEXT("// about z\nLog(\"z\"); //@7(0,6)\nif (true) //@8(0,7)\n{\nLog(\"in\"); //@9(1,8)\nbreak; //@10(1,9)\n}\nreturn; //@11(0,10)\n");
			const TCHAR* GroupExpected =
				TEXT("// ============ GLOBAL VARS =====================\n")
				TEXT("\n")
				TEXT("global int b;\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT VARS =====================\n")
				TEXT("\n")
				TEXT("scriptVar int _a = 1;\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT BODY =====================\n")
				TEXT("\n")
				TEXT("_a = 2;           //@3(0, 2)\n")
				TEXT("_a++;             //@4(0, 3)\n")
				TEXT("\n")
				TEXT("Talk(\"x\");        //@5(0, 4)\n")
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
				TEXT("}\n")
				TEXT("\n")
				TEXT("return;           //@11(0, 10)\n");
			FBangoGraphDocument GroupDoc;
			FString GroupText;
			Expect(Bango::BuildGraph(GroupSource, TEXT("groups.bango"), GroupDoc, Error) && Bango::WriteGraphText(GroupDoc, GroupText, WriteError)
				&& GroupText == FString(GroupExpected), FString(TEXT("spacing groups: unexpected text:\n")) + GroupText + Error.ToString() + WriteError);
		}

		// Global and scriptVar declarations aren't nodes: the file's globals and each graph's script variables, with
		// their comments, written at the top in a fixed layout.
		{
			const TCHAR* DeclSource =
				TEXT("#LOCTEXT_NAMESPACE \"Decls\"\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT INPUTS ===================\n")
				TEXT("\n")
				TEXT("#inputs (int _a)\n")
				TEXT("\n")
				TEXT("// ============ GLOBAL VARS =====================\n")
				TEXT("\n")
				TEXT("// the gold\n")
				TEXT("global int gold;\n")
				TEXT("global string[] names;\n")
				TEXT("global int stage ?= 1;\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT VARS =====================\n")
				TEXT("\n")
				TEXT("// how many\n")
				TEXT("scriptVar int _count = 0;\n")
				TEXT("scriptVar text _t = LOCTEXT(\"k\", \"a = b\");\n")
				TEXT("scriptVar vector3 _p = [0, -1, 2.5];\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT BODY =====================\n")
				TEXT("\n")
				TEXT("Talk(\"x\");        //@1(0, 0)\n")
				TEXT("\n")
				TEXT("// ============ FUNCTIONS =======================\n")
				TEXT("\n")
				TEXT("function void F()\n")
				TEXT("{\n")
				TEXT("\tscriptVar bool _done = true;\n")
				TEXT("\n")
				TEXT("\tTalk(\"y\");    //@2(0, 0)\n")
				TEXT("}\n")
				TEXT("\n")
				TEXT("function void G()\n")
				TEXT("{\n")
				TEXT("\tscriptVar int _unused;\n")
				TEXT("}\n");
			FBangoGraphDocument DeclDoc;
			FString DeclText;
			const bool bDeclBuilt = Bango::BuildGraph(DeclSource, TEXT("decls.bango"), DeclDoc, Error);
			Expect(bDeclBuilt, FString(TEXT("declarations: build: ")) + Error.ToString());
			if (bDeclBuilt)
			{
				Expect(DeclDoc.Globals.Num() == 3 && DeclDoc.Globals[0].Name == FString(TEXT("gold")) && DeclDoc.Globals[0].Comment == FString(TEXT("the gold"))
					&& DeclDoc.Globals[1].Type == FString(TEXT("string[]")) && !DeclDoc.Globals[1].bHasValue
					&& DeclDoc.Globals[2].bHasValue && DeclDoc.Globals[2].Value == FString(TEXT("1")), TEXT("declarations: globals, with their comments and defaults"));
				Expect(DeclDoc.Main.ScriptVars.Num() == 3 && DeclDoc.Main.ScriptVars[0].Value == FString(TEXT("0")) && DeclDoc.Main.ScriptVars[0].Comment == FString(TEXT("how many"))
					&& DeclDoc.Main.ScriptVars[1].Value == FString(TEXT("LOCTEXT(\"k\", \"a = b\")")) && DeclDoc.Main.ScriptVars[2].Value == FString(TEXT("[0, -1, 2.5]")),
					TEXT("declarations: the main graph's script variables and their values"));
				Expect(DeclDoc.Main.Nodes.Num() == 2 && DeclDoc.Functions.Num() == 2 && DeclDoc.Functions[0].ScriptVars.Num() == 1 && DeclDoc.Functions[0].ScriptVars[0].Value == FString(TEXT("true"))
					&& DeclDoc.Functions[1].ScriptVars.Num() == 1 && !DeclDoc.Functions[1].ScriptVars[0].bHasValue, TEXT("declarations: no nodes for them; functions have their own script variables"));
				Expect(Bango::WriteGraphText(DeclDoc, DeclText, WriteError) && DeclText == FString(DeclSource),
					FString(TEXT("declarations: written back the same:\n")) + DeclText + WriteError);
			}

			// What the graph can't hold is refused, with a message saying how to fix the text.
			const TCHAR* const Refused[] =
			{
				TEXT("Talk(\"x\");\nscriptVar int _late;\n"),
				TEXT("if (true)\n{\n\tscriptVar int _inner;\n}\n"),
				TEXT("int _x;\nglobal int gold;\n"),
				TEXT("function void F()\n{\n\tglobal int g;\n}\n"),
				TEXT("Talk(\"a\");\n//~ scriptVar int _gone;\n"),
			};
			for (const TCHAR* Bad : Refused)
			{
				FBangoGraphDocument BadDoc;
				Expect(!Bango::BuildGraph(Bad, TEXT("bad.bango"), BadDoc, Error) && (Error.Message.Contains(TEXT("declaration")) || Error.Message.Contains(TEXT("is declared here"))),
					FString(TEXT("declarations: should be refused: ")) + Bad + TEXT(" -> ") + Error.ToString());
			}
		}

		// Reroutes in a row share one line; one with a comment starts a new line under its comment.
		const TCHAR* RunSource =
			TEXT("Talk(\"a\"); //@1(0,0)\n//@2(4,0)\n//@3(8,0)\n// note\n//@4(12,0)\n//@5(16,0)\nTalk(\"b\"); //@6(20,0)\n//@7(24,0) @8( 28 , 0 )\n");
		const TCHAR* RunExpected =
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("Talk(\"a\");    //@1(0, 0)\n")
			TEXT("              //@2(4, 0) @3(8, 0)\n")
			TEXT("// note\n")
			TEXT("              //@4(12, 0) @5(16, 0)\n")
			TEXT("Talk(\"b\");    //@6(20, 0)\n")
			TEXT("              //@7(24, 0) @8(28, 0)\n");
		FBangoGraphDocument RunDoc;
		const bool bRunBuilt = Bango::BuildGraph(RunSource, TEXT("runs.bango"), RunDoc, Error);
		Expect(bRunBuilt && RunDoc.Main.Nodes.Num() == 9 && RunDoc.Main.Nodes[8].Kind == EBangoGraphNodeKind::Reroute && RunDoc.Main.Nodes[8].NodeID == 8
			&& RunDoc.Main.Nodes[7].Next == 8 && RunDoc.Main.Nodes[4].Comment == FString(TEXT("note")), FString(TEXT("reroute runs: read as a chain: ")) + Error.ToString());
		Expect(bRunBuilt && Bango::WriteGraphText(RunDoc, Text, WriteError) && Text == FString(RunExpected), FString(TEXT("reroute runs: saved on shared lines:\n")) + Text);

		FBangoScript Script;
		Expect(!Bango::ParseScript(TEXT("if (true)\n//@1(0, 0)\n{\n}\n"), TEXT("bad.bango"), Script, Error)
			&& Error.Line == 2 && Error.Message.Contains(TEXT("reroute")), FString(TEXT("reroute: not allowed between 'if (...)' and '{': ")) + Error.ToString());
		Expect(!Bango::ParseScript(TEXT("//@1(0, 0)\n#inputs (int _a)\n"), TEXT("bad.bango"), Script, Error)
			&& (Error.Message.Contains(TEXT("must come at the top")) || Error.Message.Contains(TEXT("reroute"))), FString(TEXT("reroute: not before #inputs: ")) + Error.ToString());
	}

	// ---- Building: structure, text, metadata, comments, automatic IDs and positions ----
	{
		const TCHAR* Source =
			TEXT("\n") // so the comment below is Script Start's, not the script's description (that's only on line 1)
			TEXT("// Header comment\n")
			TEXT("#inputs (entity _who, int _n = 2)\n")
			TEXT("global int count;            //@5(10, 0)\n")
			TEXT("if (_n > 1)                  //@6(20, 0)\n")
			TEXT("{\n")
			TEXT("\tTalk(\"big\");             //@7(30, 5)\n")
			TEXT("}\n")
			TEXT("else if (_n == 1)            //@8(30, 10)\n")
			TEXT("{\n")
			TEXT("\tTalk(\"one\");\n")
			TEXT("}\n")
			TEXT("else\n")
			TEXT("{\n")
			TEXT("\tTalk(\"none\");            //@9(40, 20)\n")
			TEXT("}\n")
			TEXT("_who Speak(\"hi\"); // trailing\n");

		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error);
		Expect(bBuilt, FString(TEXT("build: ")) + Error.ToString());
		if (bBuilt)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(N.Num() == 7, FString::Printf(TEXT("build: expected 7 nodes, got %d"), N.Num()));
			Expect(Doc.Globals.Num() == 1 && Doc.Globals[0].Name == FString(TEXT("count")) && Doc.Globals[0].Type == FString(TEXT("int")) && !Doc.Globals[0].bHasValue,
				TEXT("build: the global declaration is in Globals, not a node"));
			if (N.Num() == 7)
			{
				Expect(N[0].Kind == EBangoGraphNodeKind::Entry && N[0].NodeID == 0 && N[0].X == 0 && N[0].Y == 0, TEXT("build: entry is node 0 at (0,0)"));
				Expect(N[0].Text == FString(TEXT("entity _who, int _n = 2")), FString(TEXT("build: entry text: ")) + N[0].Text);
				Expect(N[0].Comment == FString(TEXT("Header comment")), FString(TEXT("build: entry comment: ")) + N[0].Comment);

				const FBangoGraphNode& If = N[N[0].Next];
				Expect(If.Kind == EBangoGraphNodeKind::If && If.Text == FString(TEXT("_n > 1")) && If.NodeID == 6, TEXT("build: if node"));
				Expect(N[If.Branches[0]].Text == FString(TEXT("Talk(\"big\")")), TEXT("build: True branch"));

				const FBangoGraphNode& ElseIf = N[If.Branches[1]];
				Expect(ElseIf.Kind == EBangoGraphNodeKind::If && ElseIf.NodeID == 8 && ElseIf.Next == INDEX_NONE, TEXT("build: else if is an if node on the False pin, with nothing after it"));
				const FBangoGraphNode& One = N[ElseIf.Branches[0]];
				Expect(One.NodeID == 10 && One.X == 50 && One.Y == 20, FString::Printf(TEXT("build: node without metadata gets the next ID and a place right of and below its if: got %lld (%d,%d)"), One.NodeID, One.X, One.Y));
				Expect(N[ElseIf.Branches[1]].Text == FString(TEXT("Talk(\"none\")")), TEXT("build: else branch of the else-if"));

				const FBangoGraphNode& Speak = N[If.Next];
				Expect(Speak.Text == FString(TEXT("_who Speak(\"hi\")")) && Speak.Comment == FString(TEXT("trailing")), TEXT("build: Then pin leads to the statement after the if, trailing comment attached"));
				Expect(Speak.NodeID == 11 && Speak.X == 40 && Speak.Y == 0, FString::Printf(TEXT("build: node after the if is placed to its right: got %lld (%d,%d)"), Speak.NodeID, Speak.X, Speak.Y));
				Expect(Speak.Next == INDEX_NONE, TEXT("build: last node has no Next"));
			}

			FString Text;
			FString WriteError;
			Expect(Bango::WriteGraphText(Doc, Text, WriteError), FString(TEXT("write: ")) + WriteError);
			const FString Expected =
				TEXT("// ============ SCRIPT INPUTS ===================\n")
				TEXT("\n")
				TEXT("// Header comment\n")
				TEXT("#inputs (entity _who, int _n = 2)\n")
				TEXT("\n")
				TEXT("// ============ GLOBAL VARS =====================\n")
				TEXT("\n")
				TEXT("global int count;\n")
				TEXT("\n")
				TEXT("// ============ SCRIPT BODY =====================\n")
				TEXT("\n")
				TEXT("if (_n > 1)          //@6(20, 0)\n")
				TEXT("{\n")
				TEXT("\tTalk(\"big\");     //@7(30, 5)\n")
				TEXT("}\n")
				TEXT("else if (_n == 1)    //@8(30, 10)\n")
				TEXT("{\n")
				TEXT("\tTalk(\"one\");     //@10(50, 20)\n")
				TEXT("}\n")
				TEXT("else\n")
				TEXT("{\n")
				TEXT("\tTalk(\"none\");    //@9(40, 20)\n")
				TEXT("}\n")
				TEXT("\n")
				TEXT("// trailing\n")
				TEXT("_who Speak(\"hi\");    //@11(40, 0)\n");
			Expect(Text.Equals(Expected, ESearchCase::CaseSensitive), FString(TEXT("write: unexpected text:\n")) + Text);
		}
	}

	// ---- Disconnected (//~) nodes ----
	{
		const TCHAR* Source =
			TEXT("Talk(\"a\");    //@1(0, 0)\n")
			TEXT("//~ Talk(\"b\");    //@2(0, 20)\n")
			TEXT("//~ if (true)    //@3(20, 20)\n")
			TEXT("//~ {\n")
			TEXT("//~ \tTalk(\"c\");    //@4(40, 30)\n")
			TEXT("//~ }\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		Expect(Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error), FString(TEXT("detached: ")) + Error.ToString());
		const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
		Expect(N.Num() == 5, TEXT("detached: 5 nodes"));
		if (N.Num() == 5)
		{
			Expect(N[N[0].Next].Next == INDEX_NONE, TEXT("detached: the main chain is just Talk(\"a\")"));
			Expect(N[2].NodeID == 2 && N[2].Next == 3 && N[3].Kind == EBangoGraphNodeKind::If && N[N[3].Branches[0]].NodeID == 4,
				TEXT("detached: the //~ lines are a chain that nothing leads into"));
		}

		FBangoScript Script;
		Expect(Bango::ParseScript(Source, TEXT("test.bango"), Script, Error) && Script.TopLevel.Num() == 1, TEXT("detached: the compiler doesn't see //~ lines"));

		FBangoGraphDocument Broken;
		bool bBuilt = Bango::BuildGraph(TEXT("Talk(\"a\");\n//~ Talk(\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("In the disconnected nodes")) && Error.Line == 2,
			FString(TEXT("detached: a broken //~ line is an error on its line: ")) + Error.ToString());
		bBuilt = Bango::BuildGraph(TEXT("Talk(\"a\");    //@1(0, 0)\n//~ Talk(\"b\");    //@1(0, 0)\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("used more than once")),
			FString(TEXT("detached: duplicate IDs across //~ lines are an error: ")) + Error.ToString());
		bBuilt = Bango::BuildGraph(TEXT("//~ #inputs (int _a)\n"), TEXT("test.bango"), Broken, Error);
		Expect(!bBuilt && Error.Message.Contains(TEXT("inputs")), FString(TEXT("detached: no inputs line in //~ lines: ")) + Error.ToString());
	}

	// ---- Functions ----
	{
		const TCHAR* Source =
			TEXT("Talk(\"main\");\n")
			TEXT("// Doubles it\n")
			TEXT("function int Twice(int _n, string[][] _s)\n")
			TEXT("{\n")
			TEXT("\treturn _n * 2;\n")
			TEXT("}\n");
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
			TEXT("Talk(\"A()\");\n")              // in a string: not a call
			TEXT("A();\n")
			TEXT("//~ D();\n")                    // disconnected nodes count too, after the connected ones
			TEXT("function void A()\n{\n\tif (C())\n\t{\n\t}\n}\n")
			TEXT("function void B()\n{\n\tA();\n\tB();\n}\n")   // B calls A (placed right after B), and itself
			TEXT("function bool C()\n{\n\treturn true;\n}\n")
			TEXT("function void E()\n{\n\tF();\n}\n")            // nothing calls E: it goes after the used ones, with F after it
			TEXT("function void D()\n{\n\tD();\n}\n")
			TEXT("function void F()\n{\n\tE();\n}\n");         // E and F call each other
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
		const int32 A = Add(EBangoGraphNodeKind::Call, TEXT("Talk(\"a\")"), 1);
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
			TEXT("// ============ SCRIPT BODY =====================\n")
			TEXT("\n")
			TEXT("Talk(\"a\");          //@1(1, 0)\n")
			TEXT("\n")
			TEXT("while (true)        //@2(2, 0)\n")
			TEXT("{\n")
			TEXT("\tbreak;          //@3(3, 0)\n")
			TEXT("}\n")
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
			const FString Source = TEXT("Talk(\"a\"); //@1(0, 0)\n\n//~ Talk(\"b\"); //@2(0, 1)\n//~ _c = 1; //@3(0, 2)\n");
			FString LooseText;
			FBangoGraphDocument Back;
			const bool bBuilt = Bango::BuildGraph(Source, TEXT("loose.bango"), LooseDoc, LooseError);
			const bool bWritten = bBuilt && Bango::WriteGraphText(LooseDoc, LooseText, WriteError);
			const bool bReadBack = bWritten && Bango::BuildGraph(LooseText, TEXT("loose.bango"), Back, LooseError);
			Expect(bReadBack, FString(TEXT("loose chain: build, write and read back: ")) + LooseError.ToString() + WriteError);
			if (bReadBack)
			{
				Expect(LooseText.Contains(TEXT("//~ Talk(\"b\");")) && LooseText.Contains(TEXT("\n//~\n//~ _c = 1;")), FString(TEXT("loose chain: expected a bare //~ between its nodes:\n")) + LooseText);
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
		Bad.Main.Nodes[A].Text = TEXT("Talk(\n\"a\")");
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("line break")), FString(TEXT("error: line break in text: ")) + WriteError);

		Bad = Doc;
		Bad.Main.Nodes[A].Branches[0] = Body;
		Expect(!Bango::WriteGraphText(Bad, Text, WriteError) && WriteError.Contains(TEXT("output it doesn't have")), FString(TEXT("error: branch on a call node: ")) + WriteError);

		// An entry with a comment but no inputs still writes an (empty) inputs line to hold the comment.
		FBangoGraphDocument Commented = Doc;
		Commented.Main.Nodes[0].Comment = TEXT("Hello");
		Expect(Bango::WriteGraphText(Commented, Text, WriteError) && Text.StartsWith(TEXT("// ============ SCRIPT INPUTS ===================\n\n// Hello\n#inputs ()\n"), ESearchCase::CaseSensitive),
			FString(TEXT("entry comment without inputs: ")) + Text);
	}

	// ---- A global set in the code (`global bool open = true;`): a Set Global node ----
	{
		const FString Source =
			TEXT("global int gold ?= 0;\n")
			TEXT("Talk(\"x\");    //@1(0, 0)\n")
			TEXT("global bool open = isNil(gold);    //@2(4, 0)\n")
			TEXT("/* global int stage = 1; */    //@3(8, 0)\n")
			TEXT("open = false;    //@4(12, 0)\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("set_global.bango"), Doc, Error);
		Expect(bBuilt, FString(TEXT("set global: build: ")) + Error.ToString());
		if (bBuilt)
		{
			const TArray<FBangoGraphNode>& N = Doc.Main.Nodes;
			Expect(Doc.Globals.Num() == 1 && Doc.Globals[0].Name == FString(TEXT("gold")) && Doc.Globals[0].bHasValue && Doc.Globals[0].Value == FString(TEXT("0")),
				TEXT("set global: only the one at the top is in Globals, with its default"));
			Expect(N.Num() == 5 && N[2].Kind == EBangoGraphNodeKind::Global && N[2].Text == FString(TEXT("bool open = isNil(gold)"))
				&& N[3].Kind == EBangoGraphNodeKind::Global && N[3].bDisabled && N[3].Text == FString(TEXT("int stage = 1")),
				FString(TEXT("set global: the node, without its keyword: ")) + (N.Num() > 2 ? N[2].Text : FString()));
			FString Text;
			FString WriteError;
			Expect(Bango::WriteGraphText(Doc, Text, WriteError) && Text.Contains(TEXT("global bool open = isNil(gold);")) && Text.Contains(TEXT("global int gold ?= 0;"))
				&& Text.Contains(TEXT("/* global int stage = 1; */")), FString(TEXT("set global: written back: ")) + Text + WriteError);

			const FBangoNodeLayoutContext Context = Bango::MakeNodeLayoutContext(Doc, nullptr);
			if (N.Num() == 5)
			{
				const FBangoNodeLayout Layout = Bango::GetNodeLayout(N[2], false, Context);
				Expect(Layout.Title == FString(TEXT("Set Global Bool")) && Layout.Fields.Num() == 2 && Layout.Fields[0].Label == FString(TEXT("Name"))
					&& Layout.Fields[0].Text == FString(TEXT("open")) && Layout.Fields[1].Label == FString(TEXT("To")) && Layout.Fields[1].Text == FString(TEXT("isNil(gold)")),
					FString(TEXT("set global: layout: ")) + Layout.Title);
				FBangoGraphNode Array;
				Array.Kind = EBangoGraphNodeKind::Global;
				Array.Text = TEXT("string[] names =");
				const FBangoNodeLayout NewLayout = Bango::GetNodeLayout(Array, false, Context);
				Expect(NewLayout.Title == FString(TEXT("Set Global String Array")) && NewLayout.Fields.Num() == 2 && !NewLayout.Fields[1].bPresent,
					FString(TEXT("set global: a new node's layout: ")) + NewLayout.Title);
				// The Set node after it knows open's type.
				const FBangoNodeLayout SetLayout = Bango::GetNodeLayout(N[4], false, Context);
				Expect(SetLayout.Fields.Num() == 2 && SetLayout.Fields[0].VariableType == FString(TEXT("bool")), TEXT("set global: its type is known to the nodes"));
			}
			TArray<FBangoGraphVariable> Variables;
			Bango::GetGraphVariables(Doc, INDEX_NONE, Context, Variables);
			const FBangoGraphVariable* Open = Variables.FindByPredicate([](const FBangoGraphVariable& V) { return V.Name == FString(TEXT("open")); });
			Expect(Open && Open->Kind == FBangoGraphVariable::EKind::Global && Open->Type == FString(TEXT("bool")) && Open->Index == INDEX_NONE,
				TEXT("set global: listed with the globals, with its type"));

			FString Problem;
			Expect(Bango::ValidateNodeText(EBangoGraphNodeKind::Global, false, TEXT("bool open = true"), Problem), FString(TEXT("set global: valid text: ")) + Problem);
			Expect(!Bango::ValidateNodeText(EBangoGraphNodeKind::Global, false, TEXT("bool open"), Problem) && Problem.Contains(TEXT("This should set a global")),
				FString(TEXT("set global: needs a value: ")) + Problem);
			Expect(!Bango::ValidateNodeText(EBangoGraphNodeKind::Global, false, TEXT("bool open ?= true"), Problem) && Problem.Contains(TEXT("Globals list")),
				FString(TEXT("set global: not a default: ")) + Problem);
		}
	}

	// ---- Lines of dashes above and between functions are dividers, not comments: saving drops them ----
	{
		const TCHAR* Source =
			TEXT("global int step;\n")
			TEXT("step = 1;    //@1(4, 0)\n")
			TEXT("\n")
			TEXT("// ============ FUNCTIONS =======================\n")
			TEXT("\n")
			TEXT("// -------------------------------------------------\n")
			TEXT("function void First()\n")
			TEXT("{\n")
			TEXT("\tstep = 2;    //@2(4, 0)\n")
			TEXT("}\n")
			TEXT("\n")
			TEXT("// ----------------------------------------------\n")
			TEXT("\n")
			TEXT("// -------------------------------------------------\n")
			TEXT("// Puts it back.\n")
			TEXT("function void Second()\n")
			TEXT("{\n")
			TEXT("\tstep = 0;    //@3(4, 0)\n")
			TEXT("}\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("dividers.bango"), Doc, Error);
		Expect(bBuilt && Doc.Functions.Num() == 2, FString(TEXT("function dividers: build: ")) + Error.ToString());
		FString Text;
		FString WriteError;
		if (bBuilt && Bango::WriteGraphText(Doc, Text, WriteError))
		{
			int32 Dividers = 0;
			for (int32 i = 0; i + 6 <= Text.Len(); ++i)
			{
				Dividers += Text.Mid(i, 6) == FString(TEXT("// ---")) ? 1 : 0;
			}
			Expect(Dividers == 0 && Text.Contains(TEXT("FUNCTIONS =======================\n\nfunction void First()")) && Text.Contains(TEXT("}\n\n// Puts it back.\nfunction void Second()")),
				FString(TEXT("function dividers: no dividers, and the real comment kept:\n")) + Text);
			FBangoGraphDocument Again;
			FString Text2;
			Expect(Bango::BuildGraph(Text, TEXT("dividers.bango"), Again, Error) && Bango::WriteGraphText(Again, Text2, WriteError) && Text2 == Text, TEXT("function dividers: saving again changes nothing"));
		}
		else
		{
			Expect(false, FString(TEXT("function dividers: write: ")) + WriteError);
		}
		Expect(!Bango::IsFunctionSeparatorComment(TEXT(" -- note --")) && !Bango::IsFunctionSeparatorComment(TEXT(" ---")) && Bango::IsFunctionSeparatorComment(TEXT(" ------------")),
			TEXT("function dividers: only a line of ten or more dashes"));
	}

	// ---- Functions see the script's inputs and script variables (not its locals) ----
	{
		const TCHAR* Source =
			TEXT("#inputs (string[] _names)\n")
			TEXT("scriptVar int _score = 0;\n")
			TEXT("int _plain = 1;    //@1(4, 0)\n")
			TEXT("Tally();    //@2(8, 0)\n")
			TEXT("function void Tally()\n")
			TEXT("{\n")
			TEXT("\tscriptVar bool _own = true;\n")
			TEXT("\t_score = _plain;    //@4(4, 0)\n")
			TEXT("\tforEach (_name in _names)    //@5(8, 0)\n")
			TEXT("\t{\n")
			TEXT("\t}\n")
			TEXT("}\n");
		FBangoGraphDocument Doc;
		FBangoCompileError Error;
		const bool bBuilt = Bango::BuildGraph(Source, TEXT("shared.bango"), Doc, Error);
		Expect(bBuilt && Doc.Functions.Num() == 1, FString(TEXT("script variables in functions: build: ")) + Error.ToString());
		if (bBuilt && Doc.Functions.Num() == 1)
		{
			const FBangoNodeLayoutContext Context = Bango::MakeNodeLayoutContext(Doc, nullptr);
			TArray<FBangoGraphVariable> Variables;
			Bango::GetGraphVariables(Doc, 0, Context, Variables);
			FString Listed;
			for (const FBangoGraphVariable& V : Variables)
			{
				Listed += FString::Printf(TEXT("%s:%s:%d "), *V.Name, *V.Type, (int32)V.Kind);
			}
			const int32 ScriptVarKind = (int32)FBangoGraphVariable::EKind::ScriptVar;
			const int32 LoopKind = (int32)FBangoGraphVariable::EKind::Loop;
			const int32 GlobalKind = (int32)FBangoGraphVariable::EKind::Global;
			const int32 ScriptKind = (int32)FBangoGraphVariable::EKind::Script;
			const FString Want = FString::Printf(TEXT("_own:bool:%d _name:string:%d _names:string[]:%d _score:int:%d _plain::%d "), ScriptVarKind, LoopKind, ScriptKind, ScriptKind, GlobalKind);
			Expect(Listed == Want, FString(TEXT("script variables in functions: the function's variables: ")) + Listed);
			Bango::GetGraphVariables(Doc, INDEX_NONE, Context, Variables);
			Expect(!Variables.ContainsByPredicate([](const FBangoGraphVariable& V) { return V.Kind == FBangoGraphVariable::EKind::Script || V.Name == FString(TEXT("_own")); }),
				TEXT("script variables in functions: the main graph lists none of the function's"));
			// The function's nodes know the script variables' types.
			const TArray<FBangoGraphNode>& F = Doc.Functions[0].Nodes;
			if (F.Num() >= 2)
			{
				const FBangoNodeLayout SetLayout = Bango::GetNodeLayout(F[1], true, Context);
				Expect(SetLayout.Fields.Num() == 2 && SetLayout.Fields[0].VariableType == FString(TEXT("int")),
					FString(TEXT("script variables in functions: a Set node knows the type: ")) + (SetLayout.Fields.Num() > 0 ? SetLayout.Fields[0].VariableType : FString()));
			}
		}
	}

	// ---- Globals declared below the top of the file (HoistLateGlobals, the text pane's Apply) ----
	{
		struct FHoistCase { const TCHAR* Name; const TCHAR* Source; const TCHAR* Want; const TCHAR* Error; };
		const FHoistCase HoistCases[] = {
			{ TEXT("a global set in the code is code: it stays"),
				TEXT("global int gold;\nTalk(\"x\");\nglobal bool open = isNil(gold);\nif (true)\n{\n\tglobal int stage = 2;\n}\n"),
				TEXT("global int gold;\nTalk(\"x\");\nglobal bool open = isNil(gold);\nif (true)\n{\n\tglobal int stage = 2;\n}\n"), nullptr },
			{ TEXT("a default is moved with it"),
				TEXT("Talk(\"x\");\nglobal int stage ?= 2;\n"),
				TEXT("global int stage ?= 2;\nTalk(\"x\");\n"), nullptr },
			{ TEXT("no value: just moved"),
				TEXT("#inputs (int _a)\nTalk(\"x\");\nif (true)\n{\n\tglobal int seen;\n\tseen = 1;\n}\n"),
				TEXT("#inputs (int _a)\nglobal int seen;\nTalk(\"x\");\nif (true)\n{\n\tseen = 1;\n}\n"), nullptr },
			{ TEXT("above the script variables, and the first line's comment"),
				TEXT("#inputs ()\n// the count\nscriptVar int _n = 0;\nglobal bool open;\n"),
				TEXT("#inputs ()\nglobal bool open;\n// the count\nscriptVar int _n = 0;\n"), nullptr },
			{ TEXT("in a function"),
				TEXT("Talk(\"x\");\nfunction void F()\n{\n\tglobal string who; who = \"a;b\";\n}\n"),
				TEXT("global string who;\nTalk(\"x\");\nfunction void F()\n{\n\t who = \"a;b\";\n}\n"), nullptr },
			{ TEXT("already declared at the top, same type: only the declaration goes"),
				TEXT("global int gold;\nTalk(\"x\");\nglobal int gold;\n"),
				TEXT("global int gold;\nTalk(\"x\");\n"), nullptr },
			{ TEXT("already declared at the top, another type: an error"),
				TEXT("global int gold;\nTalk(\"x\");\nglobal bool gold;\n"), nullptr, TEXT("declared as bool here, but as int") },
			{ TEXT("nothing to move"),
				TEXT("global int gold ?= 1;\nTalk(\"x\");\nglobal int gold = 2;\n"), TEXT("global int gold ?= 1;\nTalk(\"x\");\nglobal int gold = 2;\n"), nullptr },
		};
		for (const FHoistCase& Case : HoistCases)
		{
			FString Hoisted;
			FBangoCompileError HoistError;
			const bool bOk = Bango::HoistLateGlobals(Case.Source, Hoisted, HoistError);
			if (Case.Error)
			{
				Expect(!bOk && HoistError.Message.Contains(Case.Error), FString(TEXT("hoist globals: ")) + Case.Name + TEXT(": ") + HoistError.Message);
				continue;
			}
			Expect(bOk && Hoisted == Case.Want, FString(TEXT("hoist globals: ")) + Case.Name + TEXT(":\n") + Hoisted);
			// What it gives compiles, as Apply then reads it.
			FBangoGraphDocument HoistedDoc;
			FBangoCompileError BuildError;
			Expect(Bango::BuildGraph(Hoisted, TEXT("hoisted.bango"), HoistedDoc, BuildError), FString(TEXT("hoist globals: builds: ")) + Case.Name + TEXT(": ") + BuildError.ToString());
		}
	}
}
