#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BangoEdGraphBuilder.h"
#include "BangoEdGraphNode.h"
#include "BangoEdGraphSchema.h"
#include "Bango.h"
#include "BangoGraph.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoParser.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "UObject/StrongObjectPtr.h"
#include "BangoCommand.h"
#include "BangoNodeDecorator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoEdGraphBuilderTest, "Bango.ScriptEditor.EdGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoEdGraphBuilderTest::RunTest(const FString& Parameters)
{
	const TCHAR* Source =
		TEXT("// About this script\n")
		TEXT("\n")
		TEXT("// Start here\n")
		TEXT("#inputs (int _n = 1)\n")
		TEXT("if (_n > 0)    //@1(10, 0)\n")
		TEXT("{\n")
		TEXT("\tSleep(1.5);    //@2(20, 5)\n")
		TEXT("};\n")
		TEXT("while (false)    //@3(20, 0)\n")
		TEXT("{\n")
		TEXT("};\n");

	FBangoGraphDocument Doc;
	FBangoCompileError Error;
	if (!TestTrue(TEXT("build"), Bango::BuildGraph(Source, TEXT("test.bango"), Doc, Error)))
	{
		AddError(Error.ToString());
		return false;
	}

	const TSharedPtr<FBangoNodeLayoutContext> Context = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(Doc, &FBangoModule::Get().GetCommands()));
	TStrongObjectPtr<UEdGraph> Graph(BangoScriptEditor::CreateEdGraph(Doc.Main, Context));
	BangoScriptEditor::SetDescription(Graph.Get(), Doc.Description);
	TestEqual(TEXT("node count"), Graph->Nodes.Num(), 4);
	if (Graph->Nodes.Num() != 4)
	{
		return false;
	}

	UBangoEdGraphNode* Entry = Cast<UBangoEdGraphNode>(Graph->Nodes[0]);
	UBangoEdGraphNode* If = Cast<UBangoEdGraphNode>(Graph->Nodes[1]);
	UBangoEdGraphNode* SleepNode = Cast<UBangoEdGraphNode>(Graph->Nodes[2]);
	UBangoEdGraphNode* While = Cast<UBangoEdGraphNode>(Graph->Nodes[3]);
	if (!TestTrue(TEXT("all nodes are Bango nodes"), Entry && If && SleepNode && While))
	{
		return false;
	}

	TestTrue(TEXT("entry has no input"), Entry->IsEntry() && Entry->GetInputPin() == nullptr);
	TestEqual(TEXT("entry comment"), Entry->NodeComment, FString(TEXT("Start here")));
	TestEqual(TEXT("the script's description is kept on Script Start, not shown"), Entry->Description, FString(TEXT("About this script")));
	TestTrue(TEXT("entry comment bubble shown"), Entry->bCommentBubbleVisible);
	TestEqual(TEXT("if position X (grid units * 16)"), If->NodePosX, 10 * BangoGraphGridSize);
	TestEqual(TEXT("hint position Y"), SleepNode->NodePosY, 5 * BangoGraphGridSize);
	TestEqual(TEXT("if text"), If->Text, FString(TEXT("_n > 0")));
	TestEqual(TEXT("if pins: In, True, False, Then"), If->Pins.Num(), 4);
	TestEqual(TEXT("while pins: In, Body, Completed"), While->Pins.Num(), 3);

	auto LinkedNode = [](UEdGraphPin* Pin) -> UEdGraphNode*
	{
		return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0]->GetOwningNode() : nullptr;
	};
	TestTrue(TEXT("entry -> if"), LinkedNode(Entry->GetNextPin()) == If);
	TestTrue(TEXT("if True -> hint"), LinkedNode(If->GetBranchPin(0)) == SleepNode);
	TestTrue(TEXT("if False is empty"), If->GetBranchPin(1)->LinkedTo.Num() == 0);
	TestTrue(TEXT("if Then -> while"), LinkedNode(If->GetNextPin()) == While);
	TestTrue(TEXT("while Body is empty"), While->GetBranchPin(0)->LinkedTo.Num() == 0);

	TestEqual(TEXT("Then pin label"), If->GetPinDisplayName(If->GetNextPin()).ToString(), FString(TEXT("Then")));
	TestEqual(TEXT("Do pin label"), If->GetPinDisplayName(If->GetBranchPin(0)).ToString(), FString(TEXT("Do")));
	TestEqual(TEXT("Else pin label"), If->GetPinDisplayName(If->GetBranchPin(1)).ToString(), FString(TEXT("Else")));
	TestEqual(TEXT("plain exec out has no label"), SleepNode->GetPinDisplayName(SleepNode->GetNextPin()).ToString(), FString());
	TestEqual(TEXT("if title"), If->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), FString(TEXT("If")));
	TestEqual(TEXT("if field"), If->Layout.Fields.Num() == 1 ? If->Layout.Fields[0].Text : FString(), FString(TEXT("_n > 0")));
	TestEqual(TEXT("entry fields (one per parameter; the namespace is in SCRIPT CONFIG, not on the node)"), Entry->Layout.Fields.Num(), 1);

	// A built-in command gets its own node, with a field per parameter.
	TestEqual(TEXT("Sleep title"), SleepNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), FString(TEXT("Sleep")));
	TestTrue(TEXT("Sleep is a command node"), SleepNode->Layout.Style == EBangoNodeStyle::Command);
	TestTrue(TEXT("Sleep has a Time field holding 1.5"), SleepNode->Layout.Fields.Num() == 1 && SleepNode->Layout.Fields[0].Label.Equals(TEXT("Time"), ESearchCase::CaseSensitive) && SleepNode->Layout.Fields[0].Text == TEXT("1.5"));
	TestTrue(TEXT("Sleep's Time is described as Seconds"), SleepNode->Layout.Fields.Num() == 1 && SleepNode->Layout.Fields[0].Description.StartsWith(TEXT("Seconds"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("no error on a known command"), SleepNode->bHasCompilerMessage);

	// Reading the editor graph back gives the same document.
	{
		FBangoGraphDocument Back;
		FString BuildError;
		TArray<UEdGraph*> Graphs = { Graph.Get() };
		TestTrue(TEXT("build document"), BangoScriptEditor::BuildDocument(Graphs, Back, nullptr, BuildError));
		TestTrue(TEXT("the editor graph reads back as the same document"), Back.Main == Doc.Main);
		TestEqual(TEXT("and with the same description"), Back.Description, Doc.Description);
	}

	// Double-clicking a wire puts a reroute on it; reading the graph back gives the reroute a new NodeID.
	{
		UEdGraphPin* IfThen = If->GetNextPin();
		UEdGraphPin* WhileIn = While->GetInputPin();
		Graph->GetSchema()->OnPinConnectionDoubleCicked(IfThen, WhileIn, FVector2f(200.0f, 40.0f));
		UBangoEdGraphNode* Reroute = Graph->Nodes.Num() == 5 ? Cast<UBangoEdGraphNode>(Graph->Nodes[4]) : nullptr;
		if (TestNotNull(TEXT("a reroute was added"), Reroute))
		{
			TestTrue(TEXT("it's a reroute, with an input and one output"), Reroute->IsReroute() && Reroute->Pins.Num() == 2);
			TestTrue(TEXT("if Then -> reroute -> while"), LinkedNode(IfThen) == Reroute && LinkedNode(Reroute->GetNextPin()) == While);
			FBangoGraphDocument WithReroute;
			FString BuildError;
			FString Text;
			TArray<UEdGraph*> Graphs = { Graph.Get() };
			TestTrue(TEXT("build document with the reroute"), BangoScriptEditor::BuildDocument(Graphs, WithReroute, nullptr, BuildError));
			TestEqual(TEXT("the reroute gets the next free NodeID"), Reroute->NodeID, (int64)4);
			TestTrue(TEXT("the reroute is saved as a line with only its //@"), Bango::WriteGraphText(WithReroute, Text, BuildError)
				&& Text.Contains(FString(TEXT("}\n")) + FString::ChrN(19, TEXT(' ')) + TEXT("//@4(12, 2)\n\nwhile (false)")));
			// Put the graph back as it was, for the checks below.
			Reroute->BreakAllNodeLinks();
			Graph->RemoveNode(Reroute);
			IfThen->MakeLinkTo(WhileIn);
		}
	}

	// The add-node menu: dragged off a wired pin, the new node goes in the middle of the wire; dragged off an
	// unwired one, it's just wired to it. Its fields start empty.
	{
		UBangoEdGraphNode* Sibling = If;
		FBangoSchemaAction_NewNode NewIf(FText::FromString(TEXT("Flow Control")), FText::FromString(TEXT("If")), FText(), FText(), (uint8)EBangoGraphNodeKind::If, FString());
		UBangoEdGraphNode* Inserted = Cast<UBangoEdGraphNode>(NewIf.PerformAction(Graph.Get(), If->GetNextPin(), FVector2f(200.0f, 40.0f), false));
		if (TestNotNull(TEXT("the menu made a node"), Inserted))
		{
			TestTrue(TEXT("if Then -> new if -> while"), LinkedNode(If->GetNextPin()) == Inserted && LinkedNode(Inserted->GetNextPin()) == While);
			TestTrue(TEXT("the new node shares the layout context"), Inserted->LayoutContext == Sibling->LayoutContext);
			TestTrue(TEXT("the new If's condition is empty"), Inserted->Layout.Fields.Num() == 1 && !Inserted->Layout.Fields[0].bPresent);
			TestEqual(TEXT("the new node is on the grid"), Inserted->NodePosX % BangoGraphGridSize, 0);

			// Dragged off the (unwired) Else pin of the new If.
			FBangoSchemaAction_NewNode NewSet(FText::FromString(TEXT("Variables")), FText::FromString(TEXT("Set")), FText(), FText(), (uint8)EBangoGraphNodeKind::Assign, TEXT("="));
			UBangoEdGraphNode* Set = Cast<UBangoEdGraphNode>(NewSet.PerformAction(Graph.Get(), Inserted->GetBranchPin(1), FVector2f(300.0f, 120.0f), false));
			TestTrue(TEXT("new if Else -> set"), Set && LinkedNode(Inserted->GetBranchPin(1)) == Set && Set->GetNextPin()->LinkedTo.Num() == 0);

			// Both read back, with new NodeIDs.
			FBangoGraphDocument WithNew;
			FString BuildError;
			TArray<UEdGraph*> Graphs = { Graph.Get() };
			TestTrue(TEXT("build document with the new nodes"), BangoScriptEditor::BuildDocument(Graphs, WithNew, nullptr, BuildError));
			TestTrue(TEXT("the new nodes get NodeIDs"), Inserted->NodeID > 0 && Set && Set->NodeID > 0 && Set->NodeID != Inserted->NodeID);

			// The add-node menu itself offers them.
			FGraphContextMenuBuilder Menu(Graph.Get());
			Graph->GetSchema()->GetGraphContextActions(Menu);
			TestTrue(TEXT("the menu has entries"), Menu.GetNumActions() > 10);

			// Put the graph back as it was, for the checks below.
			if (Set)
			{
				Set->BreakAllNodeLinks();
				Graph->RemoveNode(Set);
			}
			Inserted->BreakAllNodeLinks();
			Graph->RemoveNode(Inserted);
			If->GetNextPin()->MakeLinkTo(While->GetInputPin());
		}
	}

	// Copy and paste: the nodes' text form keeps wires between copied nodes and drops wires to the rest.
	{
		TSet<UObject*> ToCopy = { If, SleepNode };
		FString Exported;
		FEdGraphUtilities::ExportNodesToText(ToCopy, Exported);
		TestTrue(TEXT("copied nodes can be pasted into a Bango graph"), FEdGraphUtilities::CanImportNodesFromText(Graph.Get(), Exported));
		TSet<UEdGraphNode*> Imported;
		FEdGraphUtilities::ImportNodesFromText(Graph.Get(), Exported, Imported);
		UBangoEdGraphNode* IfCopy = nullptr;
		UBangoEdGraphNode* SleepCopy = nullptr;
		for (UEdGraphNode* Node : Imported)
		{
			UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node);
			if (BangoNode && BangoNode->GetKind() == EBangoGraphNodeKind::If)
			{
				IfCopy = BangoNode;
			}
			else if (BangoNode && BangoNode->Text == SleepNode->Text)
			{
				SleepCopy = BangoNode;
			}
		}
		if (TestTrue(TEXT("both nodes pasted"), Imported.Num() == 2 && IfCopy && SleepCopy))
		{
			TestEqual(TEXT("the pasted If keeps its text"), IfCopy->Text, If->Text);
			TestTrue(TEXT("the wire between the copied nodes is kept"), LinkedNode(IfCopy->GetBranchPin(0)) == SleepCopy);
			TestTrue(TEXT("wires to nodes that weren't copied are dropped"), IfCopy->GetInputPin()->LinkedTo.Num() == 0 && IfCopy->GetNextPin()->LinkedTo.Num() == 0);
			TestTrue(TEXT("the originals keep their wires"), LinkedNode(Entry->GetNextPin()) == If && LinkedNode(If->GetNextPin()) == While);
			for (UEdGraphNode* Node : Imported)
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
			}
		}
		TestFalse(TEXT("an entry node is never pasted"), Entry->CanPasteHere(Graph.Get()));
		TestTrue(TEXT("other nodes can be"), If->CanPasteHere(Graph.Get()));
	}

	// Editing a field through the node changes its text and layout, and a bad edit changes nothing.
	{
		FString EditError;
		TestTrue(TEXT("edit Sleep's Time"), SleepNode->SetFieldValue(0, TEXT("2.0"), EditError));
		TestEqual(TEXT("Sleep text after the edit"), SleepNode->Text, FString(TEXT("Sleep(2.0)")));
		TestEqual(TEXT("Sleep field after the edit"), SleepNode->Layout.Fields[0].Text, FString(TEXT("2.0")));
		TestFalse(TEXT("a bad edit is refused"), SleepNode->SetFieldValue(0, TEXT("2.0) // oops"), EditError));
		TestEqual(TEXT("a refused edit leaves the text"), SleepNode->Text, FString(TEXT("Sleep(2.0)")));
		TestFalse(TEXT("the refusal says why"), EditError.IsEmpty());
	}

	// A placeholder without a value: a warning on its field (not an error), and a nil when saved.
	{
		FBangoGraphDocument LogDoc;
		if (TestTrue(TEXT("build WriteLog graph"), Bango::BuildGraph(TEXT("WriteLog(\"a {0} {1}\", 1); //@1(0, 0)\n"), TEXT("log.bango"), LogDoc, Error)))
		{
			const TSharedPtr<FBangoNodeLayoutContext> LogContext = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(LogDoc, &FBangoModule::Get().GetCommands()));
			TStrongObjectPtr<UEdGraph> LogGraph(BangoScriptEditor::CreateEdGraph(LogDoc.Main, LogContext));
			UBangoEdGraphNode* LogNode = LogGraph->Nodes.Num() == 2 ? Cast<UBangoEdGraphNode>(LogGraph->Nodes[1]) : nullptr;
			if (TestNotNull(TEXT("WriteLog node"), LogNode))
			{
				TestEqual(TEXT("rows: text, {0}, {1}"), LogNode->Layout.Fields.Num(), 3);
				TestEqual(TEXT("one warning"), LogNode->UpdateCompileWarnings(), 1);
				TestTrue(TEXT("the warning is on {1}"), LogNode->FindFieldWarning(2) != nullptr && LogNode->FindFieldWarning(1) == nullptr);
				TestTrue(TEXT("the banner is a warning, not an error"), LogNode->bHasCompilerMessage && LogNode->ErrorType == EMessageSeverity::Warning);
				LogNode->UpdateLayout();
				TestTrue(TEXT("a new layout clears the warning"), LogNode->FieldWarnings.Num() == 0 && !LogNode->bHasCompilerMessage);

				FBangoGraphDocument Saved;
				FString BuildError;
				TArray<UEdGraph*> LogGraphs = { LogGraph.Get() };
				TestTrue(TEXT("read back"), BangoScriptEditor::BuildDocument(LogGraphs, Saved, nullptr, BuildError));
				Bango::FillMissingPlaceholders(Saved, *LogContext);
				TestEqual(TEXT("saved with a nil"), Saved.Main.Nodes.Num() == 2 ? Saved.Main.Nodes[1].Text : FString(), FString(TEXT("WriteLog(\"a {0} {1}\", 1, nil)")));
				TestEqual(TEXT("the node itself still shows {1} as missing"), LogNode->Text, FString(TEXT("WriteLog(\"a {0} {1}\", 1)")));
			}
		}
	}

	// Localized text: the namespace kept on Script Start (edited in SCRIPT CONFIG), and a text field that keeps its key when retyped.
	{
		FBangoGraphDocument TextDoc;
		const TCHAR* TextSource =
			TEXT("#TEXT_NAMESPACE \"Bango.Tests\"\n")
			TEXT("\n")
			TEXT("Say(TEXT(\"Greeting\", \"Hi there\"));    //@1(0, 0)\n");
		if (TestTrue(TEXT("build text graph"), Bango::BuildGraph(TextSource, TEXT("text.bango"), TextDoc, Error)))
		{
			// No built-in command shows a text as a statement, so the test has one: Say(text).
			FBangoCommandTable SayCommands;
			FString SayError;
			SayCommands.Add(FBangoCommandSignature::Make(TEXT("Say")).Param(TEXT("Msg"), FBangoType::Text()), SayError);
			const TSharedPtr<FBangoNodeLayoutContext> TextContext = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(TextDoc, &SayCommands));
			TStrongObjectPtr<UEdGraph> TextGraph(BangoScriptEditor::CreateEdGraph(TextDoc.Main, TextContext));
			BangoScriptEditor::SetLocNamespace(TextGraph.Get(), TextDoc.bHasLocNamespace, TextDoc.LocNamespace);
			UBangoEdGraphNode* TextEntry = TextGraph->Nodes.Num() == 2 ? Cast<UBangoEdGraphNode>(TextGraph->Nodes[0]) : nullptr;
			UBangoEdGraphNode* SayNode = TextGraph->Nodes.Num() == 2 ? Cast<UBangoEdGraphNode>(TextGraph->Nodes[1]) : nullptr;
			if (TestNotNull(TEXT("Script Start"), TextEntry) && TestNotNull(TEXT("Say node"), SayNode))
			{
				TestEqual(TEXT("Script Start keeps the namespace"), TextEntry->LocNamespace, FString(TEXT("Bango.Tests")));
				TestEqual(TEXT("but has no row for it"), TextEntry->Layout.Fields.Num(), 0);
				TArray<UEdGraph*> TextGraphs = { TextGraph.Get() };
				FBangoGraphDocument Back;
				FString BuildError;
				TestTrue(TEXT("read back"), BangoScriptEditor::BuildDocument(TextGraphs, Back, nullptr, BuildError));
				TestTrue(TEXT("the namespace reads back"), Back == TextDoc);

				FString EditError;
				TestTrue(TEXT("rename the namespace"), TextEntry->SetLocNamespace(TEXT(" Bango.Other ")));
				TestEqual(TEXT("renamed, without the spaces"), TextEntry->LocNamespace, FString(TEXT("Bango.Other")));
				TestFalse(TEXT("the same name changes nothing"), TextEntry->SetLocNamespace(TEXT("Bango.Other")));
				TestTrue(TEXT("clear the namespace"), TextEntry->SetLocNamespace(FString()));
				TestFalse(TEXT("cleared"), TextEntry->bHasLocNamespace);
				TestTrue(TEXT("put it back"), TextEntry->SetLocNamespace(TEXT("Bango.Tests")));

				const FBangoNodeField& TextField = SayNode->Layout.Fields[0];
				TestTrue(TEXT("Say's Msg is a localized text field"), Bango::IsLocalizedTextField(TextField));
				TestEqual(TEXT("it shows the source text"), Bango::GetFieldDisplayText(TextField), FString(TEXT("Hi there")));
				const FString Retyped = Bango::GetFieldValueFromTyped(TextField, TEXT("Hello"));
				TestTrue(TEXT("retyping keeps the key"), SayNode->SetFieldValue(0, Retyped, EditError));
				TestEqual(TEXT("retyped"), SayNode->Text, FString(TEXT("Say(TEXT(\"Greeting\", \"Hello\"))")));

				FBangoTextLiteral Literal;
				TestTrue(TEXT("the field is a text literal"), Bango::ParseTextLiteral(SayNode->Layout.Fields[0].Text, Literal));
				Literal.Notes = TEXT("Said by the guard");
				TestTrue(TEXT("add notes"), SayNode->SetFieldValue(0, Bango::MakeTextLiteral(Literal), EditError));
				TestEqual(TEXT("with notes"), SayNode->Text, FString(TEXT("Say(TEXT(\"Greeting\", \"Hello\", \"Said by the guard\"))")));
			}
		}
	}

	// Set nodes show their variable's type: the editor node passes its NodeID, which the layout context's variable
	// types are kept by.
	{
		FBangoGraphDocument VarDoc;
		if (TestTrue(TEXT("build variable graph"), Bango::BuildGraph(TEXT("scriptVar int _n = 1;\n_n = 2;    //@2(0, 1)\n"), TEXT("vars.bango"), VarDoc, Error)))
		{
			const TSharedPtr<FBangoNodeLayoutContext> VarContext = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(VarDoc, &FBangoModule::Get().GetCommands()));
			TStrongObjectPtr<UEdGraph> VarGraph(BangoScriptEditor::CreateEdGraph(VarDoc.Main, VarContext));
			UBangoEdGraphNode* SetNode = VarGraph->Nodes.Num() == 2 ? Cast<UBangoEdGraphNode>(VarGraph->Nodes[1]) : nullptr;
			if (TestNotNull(TEXT("Set node"), SetNode))
			{
				TestTrue(TEXT("Set's Var is an int variable"), SetNode->Layout.Fields.Num() == 2 && SetNode->Layout.Fields[0].bVariable && SetNode->Layout.Fields[0].VariableType == TEXT("int"));
				TestTrue(TEXT("Set's Value is an int"), SetNode->Layout.Fields.Num() == 2 && SetNode->Layout.Fields[1].TypeHint == TEXT("int"));
			}
		}
	}

	// Breaking wires can be undone. The graph editor calls the schema without a transaction (Alt-click,
	// "Break This Link"), so the schema has to open one, or Ctrl+Z undoes the action before instead.
	if (GEditor && GEditor->Trans)
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();
		auto UndoSteps = []() { return GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount(); };

		const int32 StepsBefore = UndoSteps();
		Schema->BreakSinglePinLink(If->GetBranchPin(0), SleepNode->GetInputPin());
		TestTrue(TEXT("break one link: the wire is gone"), If->GetBranchPin(0)->LinkedTo.Num() == 0);
		TestEqual(TEXT("break one link: makes one undo step"), UndoSteps(), StepsBefore + 1);
		GEditor->UndoTransaction();
		TestTrue(TEXT("undo brings the wire back"), LinkedNode(If->GetBranchPin(0)) == SleepNode);
		TestTrue(TEXT("on both ends"), LinkedNode(SleepNode->GetInputPin()) == If);
		GEditor->RedoTransaction();
		TestTrue(TEXT("redo breaks it again"), If->GetBranchPin(0)->LinkedTo.Num() == 0);

		const int32 StepsBeforeAll = UndoSteps();
		Schema->BreakPinLinks(*If->GetNextPin(), /*bSendsNodeNotifcation*/ true);
		TestTrue(TEXT("break a pin's links: the wire is gone"), If->GetNextPin()->LinkedTo.Num() == 0);
		TestEqual(TEXT("break a pin's links: makes one undo step"), UndoSteps(), StepsBeforeAll + 1);
		GEditor->UndoTransaction();
		TestTrue(TEXT("undo brings Then -> While back"), LinkedNode(If->GetNextPin()) == While);
	}
	else
	{
		AddWarning(TEXT("No editor transaction buffer; the undo checks were skipped."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBangoEdGraphEntriesTest, "Bango.ScriptEditor.EdGraphEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBangoEdGraphEntriesTest::RunTest(const FString& Parameters)
{
	// Commands with entries (spec section 7): a row per entry, like a switch's cases, without Default.
	FBangoCommandTable Table;
	FString TableError;
	TestTrue(TEXT("Talk"), Table.Add(FBangoCommandSignature::Make(TEXT("Talk")).Param(TEXT("Mode"), FBangoType::Int()).Block().Entries(/*bOptional*/ true), TableError));
	TestTrue(TEXT("Say"), Table.Add(FBangoCommandSignature::Make(TEXT("Say")).Param(TEXT("N"), FBangoType::Int()).Inside(TEXT("Talk"), true), TableError));
	const TCHAR* Source =
		TEXT("Talk(1)    //@1(10, 0)\n")
		TEXT("{\n")
		TEXT("\tentry if (true):\n")
		TEXT("\t{\n")
		TEXT("\t\tSay(1);    //@2(20, 0)\n")
		TEXT("\t}\n")
		TEXT("\t/*\n")
		TEXT("\tentry:\n")
		TEXT("\t{\n")
		TEXT("\t}\n")
		TEXT("\t*/\n")
		TEXT("\tentry:\n")
		TEXT("\t{\n")
		TEXT("\t\tSay(3);    //@3(20, 4)\n")
		TEXT("\t}\n")
		TEXT("}\n")
		TEXT("Talk(0)    //@4(10, 8)\n")
		TEXT("{\n")
		TEXT("\tSay(4);    //@5(20, 8)\n")
		TEXT("}\n");
	FBangoGraphDocument Doc;
	FBangoCompileError Error;
	if (!TestTrue(TEXT("build"), Bango::BuildGraph(Source, TEXT("entries.bango"), Doc, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const TSharedPtr<FBangoNodeLayoutContext> Context = MakeShared<FBangoNodeLayoutContext>(Bango::MakeNodeLayoutContext(Doc, &Table));
	TStrongObjectPtr<UEdGraph> Graph(BangoScriptEditor::CreateEdGraph(Doc.Main, Context));
	if (!TestEqual(TEXT("node count"), Graph->Nodes.Num(), 6))
	{
		return false;
	}
	UBangoEdGraphNode* Talk = Cast<UBangoEdGraphNode>(Graph->Nodes[1]);
	UBangoEdGraphNode* First = Cast<UBangoEdGraphNode>(Graph->Nodes[2]);
	UBangoEdGraphNode* Third = Cast<UBangoEdGraphNode>(Graph->Nodes[3]);
	UBangoEdGraphNode* Plain = Cast<UBangoEdGraphNode>(Graph->Nodes[4]);
	if (!TestTrue(TEXT("Bango nodes"), Talk && First && Third && Plain))
	{
		return false;
	}
	auto LinkedNode = [](UEdGraphPin* Pin) -> UEdGraphNode*
	{
		return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0]->GetOwningNode() : nullptr;
	};
	TestTrue(TEXT("a command with entries"), Talk->IsEntries() && Talk->HasCaseRows() && Talk->TakesEntries() && Talk->TakesOptionalEntries());
	TestTrue(TEXT("its conditions"), Talk->CaseValues == TArray<FString>({ TEXT("true"), TEXT(""), TEXT("") }));
	TestTrue(TEXT("which are disabled"), Talk->EntryDisabled == TArray<bool>({ false, true, false }));
	TestEqual(TEXT("pins: In, an output per entry, Then"), Talk->Pins.Num(), 5);
	TestTrue(TEXT("each entry's block on its output"), LinkedNode(Talk->GetBranchPin(0)) == First && Talk->GetBranchPin(1)->LinkedTo.Num() == 0 && LinkedNode(Talk->GetBranchPin(2)) == Third);
	TestEqual(TEXT("an entry's pin knows its entry"), Talk->GetCaseOfPin(Talk->GetBranchPin(2)), 2);
	TestEqual(TEXT("entry pins have no labels (the condition is in the box)"), Talk->GetPinDisplayName(Talk->GetBranchPin(0)).ToString(), FString());
	TestTrue(TEXT("a plain block"), !Plain->IsEntries() && Plain->TakesOptionalEntries() && Plain->Pins.Num() == 3);

	auto ReadsBack = [&](const TCHAR* What)
	{
		FBangoGraphDocument Back;
		FString BuildError;
		TArray<UEdGraph*> Graphs = { Graph.Get() };
		const bool bBuilt = BangoScriptEditor::BuildDocument(Graphs, Back, nullptr, BuildError);
		FString Written;
		FString WriteError;
		const bool bWritten = bBuilt && Bango::WriteGraphText(Back, Written, WriteError);
		FBangoScript Script;
		FBangoCompileError ParseError;
		TestTrue(*FString::Printf(TEXT("%s: reads back, and its text parses"), What), bWritten && Bango::ParseScript(Written, TEXT("back.bango"), Script, ParseError));
		return Back;
	};
	TestTrue(TEXT("the editor graph reads back as the same document"), ReadsBack(TEXT("as built")).Main == Doc.Main);

	// Adding, editing, moving, disabling and removing entries.
	Talk->AddSwitchCase();
	TestTrue(TEXT("Add Entry: a fourth, empty, above Then"), Talk->CaseValues.Num() == 4 && Talk->EntryDisabled.Num() == 4 && Talk->Pins.Num() == 6 && Talk->Pins.Last() == Talk->GetNextPin());
	FString EditError;
	TestTrue(TEXT("a condition typed in"), Talk->SetCaseValues(3, TEXT("  1 > 0 "), EditError) && Talk->CaseValues[3] == TEXT("1 > 0"));
	TestFalse(TEXT("one that doesn't read"), Talk->SetCaseValues(3, TEXT("1 == \"a"), EditError));
	Talk->MoveEntry(0, 1);
	TestTrue(TEXT("Move Entry Down: condition, disabled and wire swap"), Talk->CaseValues[1] == TEXT("true") && Talk->EntryDisabled[0] && !Talk->EntryDisabled[1]
		&& LinkedNode(Talk->GetBranchPin(1)) == First && Talk->GetBranchPin(0)->LinkedTo.Num() == 0);
	Talk->MoveEntry(1, -1);
	TestTrue(TEXT("Move Entry Up: back"), Talk->CaseValues[0] == TEXT("true") && LinkedNode(Talk->GetBranchPin(0)) == First);
	Talk->SetEntryDisabled(1, false);
	TestFalse(TEXT("Enable Entry"), Talk->EntryDisabled[1]);
	Talk->RemoveSwitchCase(1);
	TestTrue(TEXT("Remove Entry: the later ones move up"), Talk->CaseValues == TArray<FString>({ TEXT("true"), TEXT(""), TEXT("1 > 0") }) && LinkedNode(Talk->GetBranchPin(1)) == Third && Talk->Pins.Num() == 5);
	{
		const FBangoGraphDocument Back = ReadsBack(TEXT("after editing"));
		const FBangoGraphNode* Node = Back.Main.Nodes.FindByPredicate([](const FBangoGraphNode& Each) { return Each.Kind == EBangoGraphNodeKind::BlockCall && Each.bEntries; });
		TestTrue(TEXT("the edits are in the document"), Node && Node->CaseValues == Talk->CaseValues && Node->Branches.Num() == 3 && Node->EntryDisabled == TArray<bool>({ false, false, false }));
	}

	// The decorator is told of the entries, and can set a condition.
	{
		const FBangoNodeDecoratorContext DecoratorContext = Talk->MakeDecoratorContext();
		TestTrue(TEXT("decorator: the entries"), DecoratorContext.bEntries && DecoratorContext.EntryConditions == Talk->CaseValues && DecoratorContext.EntryDisabled.Num() == 3 && !DecoratorContext.bHasEntryDebug);
		FString SetError;
		TestTrue(TEXT("decorator: sets a condition"), DecoratorContext.SetEntryCondition && DecoratorContext.SetEntryCondition(1, TEXT("false"), SetError) && Talk->CaseValues[1] == TEXT("false"));
		TestFalse(TEXT("decorator: not one it doesn't have"), DecoratorContext.SetEntryCondition(9, TEXT("false"), SetError));
	}

	// A default: added last, kept last, no condition, and taken out again.
	TestTrue(TEXT("Add Default: offered when every entry has a condition"), Talk->CanAddDefaultEntry());
	Talk->AddDefaultEntry();
	TestTrue(TEXT("Add Default: a fourth row, the default"), Talk->CaseValues.Num() == 4 && Talk->HasDefaultEntry() && Talk->IsDefaultEntry(3) && !Talk->IsDefaultEntry(2)
		&& Talk->Pins.Num() == 6 && Talk->Pins.Last() == Talk->GetNextPin() && !Talk->CanAddDefaultEntry());
	TestEqual(TEXT("decorator: the default"), Talk->MakeDecoratorContext().DefaultEntry, 3);
	TestFalse(TEXT("the default takes no condition"), Talk->SetCaseValues(3, TEXT("true"), EditError));
	{
		const FBangoGraphDocument Back = ReadsBack(TEXT("with a default"));
		const FBangoGraphNode* Node = Back.Main.Nodes.FindByPredicate([](const FBangoGraphNode& Each) { return Each.Kind == EBangoGraphNodeKind::BlockCall && Each.bEntries; });
		TestTrue(TEXT("the default is in the document"), Node && Node->bEntryDefault && Node->CaseValues.Num() == 4);
	}
	Talk->AddSwitchCase();
	TestTrue(TEXT("Add Entry: above the default"), Talk->CaseValues.Num() == 5 && Talk->IsDefaultEntry(4) && Talk->CaseValues[3].IsEmpty() && Talk->CaseValues[2] == TEXT("1 > 0"));
	Talk->MoveEntry(3, 1);
	TestTrue(TEXT("Move Entry Down: not past the default"), Talk->IsDefaultEntry(4) && Talk->CaseValues[3].IsEmpty());
	Talk->RemoveSwitchCase(3);
	Talk->RemoveSwitchCase(3);
	TestTrue(TEXT("Remove Default"), Talk->CaseValues.Num() == 3 && !Talk->HasDefaultEntry() && Talk->Pins.Num() == 5);

	// A command with optional entries: its ordinary block becomes one entry, and back.
	UEdGraphNode* PlainBody = LinkedNode(Plain->GetBranchPin(0));
	Plain->UseEntries();
	TestTrue(TEXT("Use Entries: one entry: holding what was in the block"), Plain->IsEntries() && Plain->CaseValues == TArray<FString>({ TEXT("") }) && LinkedNode(Plain->GetBranchPin(0)) == PlainBody && PlainBody);
	TestTrue(TEXT("Use a Plain Block is offered"), Plain->CanUsePlainBlock());
	Plain->UsePlainBlock();
	TestTrue(TEXT("Use a Plain Block: back"), !Plain->IsEntries() && Plain->CaseValues.Num() == 0 && LinkedNode(Plain->GetBranchPin(0)) == PlainBody);
	ReadsBack(TEXT("after Use Entries and back"));
	return true;
}

#endif
