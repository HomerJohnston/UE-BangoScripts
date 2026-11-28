#include "BangoEdGraphBuilder.h"

#include "BangoEdGraphNode.h"
#include "BangoEdGraphSchema.h"
#include "BangoGraph.h"
#include "BangoGraphLayout.h"
#include "BangoGraphEdit.h"
#include "BangoNodeDecorator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/Package.h"

namespace BangoScriptEditor
{
	static void Link(UEdGraphPin* From, UBangoEdGraphNode* To)
	{
		UEdGraphPin* ToPin = To ? To->GetInputPin() : nullptr;
		if (From && ToPin)
		{
			From->MakeLinkTo(ToPin);
		}
	}

	/**
	 * Takes out of Graph the statements of the entries their command's decorator draws in their rows
	 * (IBangoNodeDecorator::DrawsEntryStatements), where they hold only what the command's entries may hold.
	 */
	static void FoldEntriesForRows(FBangoGraph& Graph, const TSharedPtr<const FBangoNodeLayoutContext>& LayoutContext, TArray<FBangoFoldedNode>& OutFolded)
	{
		OutFolded.Reset();
		const IBangoCommandLookup* Commands = LayoutContext.IsValid() ? LayoutContext->Commands : nullptr;
		if (!Commands)
		{
			return;
		}
		TMap<FString, FBangoEntryHold> Holds;
		Bango::FoldEntries(Graph, [&Holds, Commands](const FBangoGraphNode& Node, int32 Entry) -> const FBangoEntryHold*
		{
			const FString Name = Bango::GetNodeCommandName(Node);
			const FBangoCommandSignature* Signature = Name.IsEmpty() ? nullptr : Commands->FindCommand(Name);
			const TSharedPtr<IBangoNodeDecorator> Decorator = Signature && Signature->bEntries && !Signature->EntriesHoldCommands.IsEmpty()
				? Bango::FindNodeDecorator(Name) : nullptr;
			if (!Decorator.IsValid())
			{
				return nullptr;
			}
			FBangoNodeDecoratorContext Context;
			Context.CommandName = Name;
			Context.NodeText = Node.Text;
			Context.bBlock = true;
			Context.bDisabled = Node.bDisabled;
			Context.Comment = Node.Comment;
			Context.bEntries = true;
			Context.EntryConditions = Node.CaseValues;
			Context.EntryDisabled = Node.EntryDisabled;
			Context.EntryDisabled.SetNum(Node.CaseValues.Num());
			Context.DefaultEntry = Node.bEntryDefault && Node.CaseValues.Num() > 0 ? Node.CaseValues.Num() - 1 : INDEX_NONE;
			if (!Decorator->DrawsEntryStatements(Context, Entry))
			{
				return nullptr;
			}
			if (!Holds.Contains(Name))
			{
				Holds.Add(Name, FBangoEntryHold::FromSignature(*Signature));
			}
			return Holds.Find(Name);
		}, OutFolded);
	}

	UEdGraph* CreateEdGraph(const FBangoGraph& SourceGraph, const TSharedPtr<const FBangoNodeLayoutContext>& LayoutContext)
	{
		UEdGraph* EdGraph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
		EdGraph->Schema = UBangoEdGraphSchema::StaticClass();

		// Entries drawn in their rows: their statements aren't nodes here; they're kept on their command's node.
		FBangoGraph Graph = SourceGraph;
		TArray<FBangoFoldedNode> Folded;
		FoldEntriesForRows(Graph, LayoutContext, Folded);

		TArray<UBangoEdGraphNode*> Nodes;
		for (const FBangoGraphNode& Source : Graph.Nodes)
		{
			UBangoEdGraphNode* Node = NewObject<UBangoEdGraphNode>(EdGraph, NAME_None, RF_Transactional);
			Node->KindValue = (uint8)Source.Kind;
			Node->NodeID = Source.NodeID;
			Node->Text = Source.Text;
			if (Source.Kind == EBangoGraphNodeKind::Sequence)
			{
				Node->SequenceOutputs = FMath::Max(Source.Branches.Num(), 1);
			}
			if (Source.Kind == EBangoGraphNodeKind::Switch)
			{
				Node->CaseValues = Source.CaseValues;
			}
			if (Source.Kind == EBangoGraphNodeKind::BlockCall && Source.bEntries)
			{
				Node->bEntries = true;
				Node->CaseValues = Source.CaseValues;
				Node->EntryDisabled = Source.EntryDisabled;
				Node->EntryDisabled.SetNum(Node->CaseValues.Num());
				Node->bEntryDefault = Source.bEntryDefault && Node->CaseValues.Num() > 0;
				Node->EntryRows.SetNum(Node->CaseValues.Num());
				if (const FBangoFoldedNode* FoldedNode = Folded.FindByPredicate([&Source](const FBangoFoldedNode& Each) { return Each.NodeID == Source.NodeID; }))
				{
					for (const FBangoFoldedEntry& FoldedEntry : FoldedNode->Entries)
					{
						if (!Node->EntryRows.IsValidIndex(FoldedEntry.Entry))
						{
							continue;
						}
						FBangoEdEntryRow& Row = Node->EntryRows[FoldedEntry.Entry];
						Row.bInRow = true;
						for (const FBangoGraphNode& Statement : FoldedEntry.Statements)
						{
							FBangoEdStatement& Kept = Row.Statements.AddDefaulted_GetRef();
							Kept.KindValue = (uint8)Statement.Kind;
							Kept.NodeID = Statement.NodeID;
							Kept.Text = Statement.Text;
							Kept.Comment = Statement.Comment;
							Kept.bDisabled = Statement.bDisabled;
							Kept.X = Statement.X;
							Kept.Y = Statement.Y;
							Kept.PinKey = Kept.HasBlock() ? Node->NextStatementPinKey++ : 0;
						}
					}
				}
			}
			Node->bInFunctionGraph = Graph.bIsFunction;
			Node->LayoutContext = LayoutContext;
			Node->CreateNewGuid();
			Node->NodePosX = Source.X * BangoGraphGridSize;
			Node->NodePosY = Source.Y * BangoGraphGridSize;
			Node->NodeComment = Source.Comment;
			if (Source.bDisabled && Node->CanBeDisabled())
			{
				Node->SetEnabledState(ENodeEnabledState::Disabled, /*bUserAction*/ false);
			}
			Node->bCommentBubbleVisible = !Source.Comment.IsEmpty();
			Node->bCommentBubblePinned = Node->bCommentBubbleVisible;
			if (Source.Kind == EBangoGraphNodeKind::Entry)
			{
				for (const FBangoGraphDecl& Local : Graph.ScriptVars)
				{
					Node->ScriptVars.Add(FBangoEdDecl::From(Local));
				}
			}
			Node->UpdateLayout();
			Node->AllocateDefaultPins();
			EdGraph->AddNode(Node, /*bUserAction*/ false, /*bSelectNewNode*/ false);
			Nodes.Add(Node);
		}

		for (int32 i = 0; i < Graph.Nodes.Num(); ++i)
		{
			const FBangoGraphNode& Source = Graph.Nodes[i];
			if (Nodes.IsValidIndex(Source.Next))
			{
				Link(Nodes[i]->GetNextPin(), Nodes[Source.Next]);
			}
			for (int32 Branch = 0; Branch < Bango::GetGraphNodeBranchCount(Source) && Branch < Nodes[i]->GetBranchCount(); ++Branch)
			{
				if (Nodes.IsValidIndex(Source.Branches[Branch]))
				{
					Link(Nodes[i]->GetBranchPin(Branch), Nodes[Source.Branches[Branch]]);
				}
			}
		}
		// The blocks of statements drawn in rows, from their own pins.
		for (const FBangoFoldedNode& FoldedNode : Folded)
		{
			const int32 Owner = Graph.Nodes.IndexOfByPredicate([&FoldedNode](const FBangoGraphNode& Each) { return Each.NodeID == FoldedNode.NodeID; });
			if (!Nodes.IsValidIndex(Owner))
			{
				continue;
			}
			for (const FBangoFoldedEntry& FoldedEntry : FoldedNode.Entries)
			{
				for (int32 Index = 0; Index < FoldedEntry.Statements.Num(); ++Index)
				{
					const int32 Body = FoldedEntry.Statements[Index].Branches.IsValidIndex(0) ? FoldedEntry.Statements[Index].Branches[0] : INDEX_NONE;
					if (Nodes.IsValidIndex(Body))
					{
						Link(Nodes[Owner]->GetStatementPin(FoldedEntry.Entry, Index), Nodes[Body]);
					}
				}
			}
		}
		return EdGraph;
	}

	void SetLocNamespace(UEdGraph* MainGraph, bool bHasLocNamespace, const FString& LocNamespace)
	{
		for (UEdGraphNode* EdNode : MainGraph ? MainGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(EdNode);
			if (Node && Node->IsScriptEntry())
			{
				Node->bHasLocNamespace = bHasLocNamespace;
				Node->LocNamespace = bHasLocNamespace ? LocNamespace : FString();
				Node->UpdateLayout();
			}
		}
	}

	void SetUsesGraphEditor(UEdGraph* MainGraph, bool bUsesGraphEditor)
	{
		for (UEdGraphNode* EdNode : MainGraph ? MainGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(EdNode);
			if (Node && Node->IsScriptEntry())
			{
				Node->bUsesGraphEditor = bUsesGraphEditor;
			}
		}
	}

	UBangoEdGraphNode* FindEntryNode(const UEdGraph* Graph)
	{
		for (UEdGraphNode* EdNode : Graph ? Graph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(EdNode);
			if (Node && Node->IsEntry())
			{
				return Node;
			}
		}
		return nullptr;
	}

	void SetGlobals(UEdGraph* MainGraph, const TArray<FBangoGraphDecl>& Globals)
	{
		if (UBangoEdGraphNode* Entry = FindEntryNode(MainGraph))
		{
			Entry->Globals.Reset();
			for (const FBangoGraphDecl& Global : Globals)
			{
				Entry->Globals.Add(FBangoEdDecl::From(Global));
			}
		}
	}

	void SetDescription(UEdGraph* MainGraph, const FString& Description)
	{
		if (UBangoEdGraphNode* Entry = FindEntryNode(MainGraph))
		{
			Entry->Description = Description;
		}
	}

	/** The index of the node a pin's wire leads to, or INDEX_NONE. */
	static int32 Target(const UEdGraphPin* Pin, const TMap<const UEdGraphNode*, int32>& Index)
	{
		if (!Pin || Pin->LinkedTo.Num() == 0 || !Pin->LinkedTo[0])
		{
			return INDEX_NONE;
		}
		const int32* Found = Index.Find(Pin->LinkedTo[0]->GetOwningNode());
		return Found ? *Found : INDEX_NONE;
	}

	/** Gives every node added in the editor (NodeID 0) a NodeID no other node in the file has. */
	static void AssignNewNodeIDs(const TArray<UEdGraph*>& EdGraphs)
	{
		int64 MaxId = 0;
		TArray<UBangoEdGraphNode*> New;
		TArray<FBangoEdStatement*> NewStatements; // statements drawn in rows are nodes in the text too
		for (const UEdGraph* EdGraph : EdGraphs)
		{
			for (UEdGraphNode* EdNode : EdGraph ? EdGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(EdNode);
				if (!Node || Node->IsEntry())
				{
					continue;
				}
				if (Node->NodeID <= 0)
				{
					New.Add(Node);
				}
				MaxId = Node->NodeID > MaxId ? Node->NodeID : MaxId;
				for (FBangoEdEntryRow& Row : Node->EntryRows)
				{
					for (FBangoEdStatement& Statement : Row.Statements)
					{
						if (Statement.NodeID <= 0)
						{
							NewStatements.Add(&Statement);
						}
						MaxId = Statement.NodeID > MaxId ? Statement.NodeID : MaxId;
					}
				}
			}
		}
		for (UBangoEdGraphNode* Node : New)
		{
			Node->NodeID = ++MaxId;
		}
		for (FBangoEdStatement* Statement : NewStatements)
		{
			Statement->NodeID = ++MaxId;
		}
	}

	bool BuildDocument(const TArray<UEdGraph*>& EdGraphs, FBangoGraphDocument& OutDocument, TArray<TArray<UBangoEdGraphNode*>>* OutNodes, FString& OutError)
	{
		OutDocument = FBangoGraphDocument();
		if (OutNodes)
		{
			OutNodes->Reset();
		}
		AssignNewNodeIDs(EdGraphs);

		for (int32 GraphIndex = 0; GraphIndex < EdGraphs.Num(); ++GraphIndex)
		{
			const UEdGraph* EdGraph = EdGraphs[GraphIndex];
			FBangoGraph& Graph = GraphIndex == 0 ? OutDocument.Main : OutDocument.Functions.AddDefaulted_GetRef();
			Graph.bIsFunction = GraphIndex > 0;

			// The entry node first, then the rest in the graph's order.
			TArray<UBangoEdGraphNode*> Nodes;
			for (UEdGraphNode* EdNode : EdGraph ? EdGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				if (UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(EdNode))
				{
					if (Node->IsEntry())
					{
						Nodes.Insert(Node, 0);
					}
					else
					{
						Nodes.Add(Node);
					}
				}
			}
			if (Nodes.Num() == 0 || !Nodes[0]->IsEntry())
			{
				OutError = TEXT("A graph has no entry node.");
				return false;
			}

			TMap<const UEdGraphNode*, int32> Index;
			for (int32 i = 0; i < Nodes.Num(); ++i)
			{
				Index.Add(Nodes[i], i);
			}

			for (UBangoEdGraphNode* Node : Nodes)
			{
				FBangoGraphNode& Out = Graph.Nodes.AddDefaulted_GetRef();
				Out.Kind = Node->GetKind();
				Out.NodeID = Node->IsEntry() ? 0 : Node->NodeID;
				Out.X = Node->IsEntry() ? 0 : FMath::RoundToInt((float)Node->NodePosX / BangoGraphGridSize);
				Out.Y = Node->IsEntry() ? 0 : FMath::RoundToInt((float)Node->NodePosY / BangoGraphGridSize);
				Out.Text = Node->Text;
				Out.Comment = Node->NodeComment;
				Out.bDisabled = Node->IsDisabledNode();
				Out.Next = Target(Node->GetNextPin(), Index);
				if (Node->IsSequence() || Node->HasCaseRows())
				{
					Out.Branches.Init(INDEX_NONE, Node->GetBranchCount());
				}
				if (Node->HasCaseRows())
				{
					Out.CaseValues = Node->CaseValues;
				}
				if (Node->IsEntries())
				{
					Out.bEntries = true;
					Out.EntryDisabled = Node->EntryDisabled;
					Out.EntryDisabled.SetNum(Node->CaseValues.Num());
					Out.bEntryDefault = Node->HasDefaultEntry();
				}
				for (int32 Branch = 0; Branch < Node->GetBranchCount(); ++Branch)
				{
					Out.Branches[Branch] = Target(Node->GetBranchPin(Branch), Index);
				}
			}
			// The statements drawn in rows, put back as the nodes they are in the text (OutNodes gives their command's node for each).
			TArray<FBangoFoldedNode> Folded;
			TArray<UBangoEdGraphNode*> FoldedOwners;
			for (UBangoEdGraphNode* Node : Nodes)
			{
				FBangoFoldedNode* FoldedNode = nullptr;
				for (int32 Entry = 0; Node->IsEntries() && Entry < Node->EntryRows.Num(); ++Entry)
				{
					if (!Node->IsEntryInRow(Entry))
					{
						continue;
					}
					if (!FoldedNode)
					{
						FoldedNode = &Folded.AddDefaulted_GetRef();
						FoldedNode->NodeID = Node->NodeID;
					}
					FBangoFoldedEntry& FoldedEntry = FoldedNode->Entries.AddDefaulted_GetRef();
					FoldedEntry.Entry = Entry;
					for (int32 Position = 0; Position < Node->EntryRows[Entry].Statements.Num(); ++Position)
					{
						const FBangoEdStatement& Kept = Node->EntryRows[Entry].Statements[Position];
						FBangoGraphNode& Statement = FoldedEntry.Statements.AddDefaulted_GetRef();
						Statement.Kind = Kept.GetKind();
						Statement.NodeID = Kept.NodeID;
						Statement.X = Kept.X;
						Statement.Y = Kept.Y;
						Statement.Text = Kept.Text;
						Statement.Comment = Kept.Comment;
						Statement.bDisabled = Kept.bDisabled;
						Statement.Branches[0] = Kept.HasBlock() ? Target(Node->GetStatementPin(Entry, Position), Index) : INDEX_NONE;
						FoldedOwners.Add(Node);
					}
				}
			}
			for (FBangoGraphNode& Out : Graph.Nodes)
			{
				// An entry drawn in its row has nothing wired to its own output: its statements follow it.
				const FBangoFoldedNode* FoldedNode = Folded.FindByPredicate([&Out](const FBangoFoldedNode& Each) { return Each.NodeID == Out.NodeID; });
				for (const FBangoFoldedEntry& FoldedEntry : FoldedNode ? FoldedNode->Entries : TArray<FBangoFoldedEntry>())
				{
					if (Out.Branches.IsValidIndex(FoldedEntry.Entry))
					{
						Out.Branches[FoldedEntry.Entry] = INDEX_NONE;
					}
				}
			}
			if (!Bango::UnfoldEntries(Graph, Folded, OutError))
			{
				return false;
			}
			Nodes.Append(FoldedOwners);

			for (const FBangoEdDecl& Local : Nodes[0]->ScriptVars)
			{
				Graph.ScriptVars.Add(Local.ToGraphDecl());
			}
			if (Graph.bIsFunction)
			{
				Graph.Name = Nodes[0]->Layout.Title;
			}
			else
			{
				OutDocument.Description = Nodes[0]->Description;
				OutDocument.bHasLocNamespace = Nodes[0]->bHasLocNamespace;
				OutDocument.LocNamespace = Nodes[0]->bHasLocNamespace ? Nodes[0]->LocNamespace : FString();
				OutDocument.bUsesGraphEditor = Nodes[0]->bUsesGraphEditor;
				for (const FBangoEdDecl& Global : Nodes[0]->Globals)
				{
					OutDocument.Globals.Add(Global.ToGraphDecl());
				}
			}
			if (OutNodes)
			{
				OutNodes->Add(Nodes);
			}
		}
		return true;
	}
}
