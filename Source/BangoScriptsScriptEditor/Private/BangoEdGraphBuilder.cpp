#include "BangoEdGraphBuilder.h"

#include "BangoEdGraphNode.h"
#include "BangoEdGraphSchema.h"
#include "BangoGraph.h"
#include "BangoGraphLayout.h"
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

	UEdGraph* CreateEdGraph(const FBangoGraph& Graph, const TSharedPtr<const FBangoNodeLayoutContext>& LayoutContext)
	{
		UEdGraph* EdGraph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transactional);
		EdGraph->Schema = UBangoEdGraphSchema::StaticClass();

		TArray<UBangoEdGraphNode*> Nodes;
		for (const FBangoGraphNode& Source : Graph.Nodes)
		{
			UBangoEdGraphNode* Node = NewObject<UBangoEdGraphNode>(EdGraph, NAME_None, RF_Transactional);
			Node->KindValue = (uint8)Source.Kind;
			Node->NodeID = Source.NodeID;
			Node->Text = Source.Text;
			Node->bInFunctionGraph = Graph.bIsFunction;
			Node->LayoutContext = LayoutContext;
			Node->CreateNewGuid();
			Node->NodePosX = Source.X * BangoGraphGridSize;
			Node->NodePosY = Source.Y * BangoGraphGridSize;
			Node->NodeComment = Source.Comment;
			Node->bCommentBubbleVisible = !Source.Comment.IsEmpty();
			Node->bCommentBubblePinned = Node->bCommentBubbleVisible;
			if (Source.Kind == EBangoGraphNodeKind::Entry)
			{
				for (const FBangoGraphDecl& Local : Graph.Locals)
				{
					Node->Locals.Add(FBangoEdDecl::From(Local));
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
			for (int32 Branch = 0; Branch < Bango::GetGraphNodeBranchCount(Source.Kind); ++Branch)
			{
				if (Nodes.IsValidIndex(Source.Branches[Branch]))
				{
					Link(Nodes[i]->GetBranchPin(Branch), Nodes[Source.Branches[Branch]]);
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
			}
		}
		for (UBangoEdGraphNode* Node : New)
		{
			Node->NodeID = ++MaxId;
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
				Out.Next = Target(Node->GetNextPin(), Index);
				for (int32 Branch = 0; Branch < Bango::GetGraphNodeBranchCount(Out.Kind); ++Branch)
				{
					Out.Branches[Branch] = Target(Node->GetBranchPin(Branch), Index);
				}
			}
			for (const FBangoEdDecl& Local : Nodes[0]->Locals)
			{
				Graph.Locals.Add(Local.ToGraphDecl());
			}
			if (Graph.bIsFunction)
			{
				Graph.Name = Nodes[0]->Layout.Title;
			}
			else
			{
				OutDocument.bHasLocNamespace = Nodes[0]->bHasLocNamespace;
				OutDocument.LocNamespace = Nodes[0]->bHasLocNamespace ? Nodes[0]->LocNamespace : FString();
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
