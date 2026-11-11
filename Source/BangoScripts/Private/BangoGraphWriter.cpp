#include "BangoGraph.h"
#include "BangoGraphEdit.h"

// Graph -> text. See BangoGraph.h for the graph shape.

namespace BangoGraphWriterPrivate
{
	/** Matches the parser's nesting limit, so everything written can be read back. */
	static constexpr int32 MaxNestingDepth = 64;

	/** Lines longer than this don't push every //@ comment further right. */
	static constexpr int32 MaxMetadataColumn = 100;

	static constexpr int32 TabWidth = 4;

	struct FOutLine
	{
		/** Tabs before the line (before the //~ marker on disconnected lines). */
		int32 Indent = 0;
		/** For disconnected lines: tabs after the //~ marker. -1 for ordinary lines. */
		int32 DetachedIndent = -1;
		FString Code;
		/** An empty line between two nodes of a disconnected chain: written as a bare `//~`, so the chain stays in one piece. */
		bool bDetachedSpacer = false;
		bool bHasMetadata = false;
		int64 NodeID = 0;
		int32 X = 0;
		int32 Y = 0;
		/** A reroute line's later reroutes, written after the first: `//@7(4, 9) @8(8, 13)`. */
		TArray<const FBangoGraphNode*> MoreReroutes;

		int32 Width() const
		{
			int32 Result = Indent * TabWidth + Code.Len();
			if (DetachedIndent >= 0)
			{
				Result += 4 + DetachedIndent * TabWidth; // "//~ "
			}
			return Result;
		}
	};

	class FWriter
	{
	public:
		explicit FWriter(FString& InError) : Error(InError) {}

		bool Run(const FBangoGraphDocument& Doc, FString& OutText)
		{
			if (!CheckGraph(Doc.Main, TEXT("the main graph")))
			{
				return false;
			}
			for (const FBangoGraph& Function : Doc.Functions)
			{
				if (!CheckGraph(Function, FString::Printf(TEXT("function '%s'"), *Function.Name)))
				{
					return false;
				}
			}

			// Main graph
			const FBangoGraphNode& MainEntry = Doc.Main.Nodes[0];
			AddComment(MainEntry.Comment, 0, -1);
			if (!MainEntry.Text.IsEmpty() || (!MainEntry.Comment.IsEmpty() && !Doc.bHasLocNamespace))
			{
				// An empty params line still anchors the entry node's comment (the namespace line does too).
				AddCode(0, -1, FString(TEXT("#define params (")) + MainEntry.Text + TEXT(")"), nullptr);
			}
			if (Doc.bHasLocNamespace)
			{
				AddCode(0, -1, FString(TEXT("#define LOCTEXT_NAMESPACE ")) + Bango::MakeStringLiteral(Doc.LocNamespace), nullptr);
			}
			if (Out.Num() > 0 && MainEntry.Next != INDEX_NONE)
			{
				AddBlank(); // a blank line between nodes, here between the #define lines and the first statement
			}
			if (!WriteGraphBody(Doc.Main, 0))
			{
				return false;
			}

			// Functions, depth-first by first use
			for (int32 FunctionIndex : Bango::GetFunctionWriteOrder(Doc))
			{
				const FBangoGraph& Function = Doc.Functions[FunctionIndex];
				AddBlank();
				const FBangoGraphNode& Entry = Function.Nodes[0];
				AddComment(Entry.Comment, 0, -1);
				AddCode(0, -1, FString(TEXT("function ")) + Entry.Text, nullptr);
				AddCode(0, -1, TEXT("{"), nullptr);
				if (!WriteGraphBody(Function, 1))
				{
					return false;
				}
				AddCode(0, -1, TEXT("};"), nullptr);
			}

			if (Doc.bHasLocNamespace)
			{
				if (Out.Num() > 0)
				{
					AddBlank();
				}
				AddCode(0, -1, TEXT("#undef LOCTEXT_NAMESPACE"), nullptr);
			}

			Format(OutText);
			return true;
		}

	private:
		FString& Error;
		TArray<FOutLine> Out;

		/** Per node of the graph being written. */
		TArray<int32> Incoming;
		TArray<bool> Written;

		bool Fail(const FString& Message)
		{
			Error = Message;
			return false;
		}

		static bool HasLineBreak(const FString& Text)
		{
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				if (Text[i] == TEXT('\n') || Text[i] == TEXT('\r'))
				{
					return true;
				}
			}
			return false;
		}

		static FString NodeName(const FBangoGraphNode& Node)
		{
			return FString::Printf(TEXT("node %lld (%s)"), Node.NodeID, *Bango::GetGraphNodeKindName(Node.Kind));
		}

		bool CheckLink(const FBangoGraph& Graph, int32 From, int32 To, const FString& GraphName)
		{
			if (To == INDEX_NONE)
			{
				return true;
			}
			if (!Graph.Nodes.IsValidIndex(To))
			{
				return Fail(FString::Printf(TEXT("In %s, %s is wired to a node that doesn't exist."), *GraphName, *NodeName(Graph.Nodes[From])));
			}
			if (To == 0)
			{
				return Fail(FString::Printf(TEXT("In %s, %s is wired into the entry node, which has no input."), *GraphName, *NodeName(Graph.Nodes[From])));
			}
			return true;
		}

		bool CheckGraph(const FBangoGraph& Graph, const FString& GraphName)
		{
			if (Graph.Nodes.Num() == 0 || Graph.Nodes[0].Kind != EBangoGraphNodeKind::Entry)
			{
				return Fail(FString::Printf(TEXT("%s has no entry node."), *GraphName));
			}
			for (int32 i = 0; i < Graph.Nodes.Num(); ++i)
			{
				const FBangoGraphNode& Node = Graph.Nodes[i];
				if (i > 0 && Node.Kind == EBangoGraphNodeKind::Entry)
				{
					return Fail(FString::Printf(TEXT("%s has more than one entry node."), *GraphName));
				}
				if (HasLineBreak(Node.Text))
				{
					return Fail(FString::Printf(TEXT("In %s, the text of %s has a line break. Each node's text must be one line."), *GraphName, *NodeName(Node)));
				}
				if (!CheckLink(Graph, i, Node.Next, GraphName))
				{
					return false;
				}
				const int32 Count = Bango::GetGraphNodeBranchCount(Node.Kind);
				for (int32 Branch = 0; Branch < 2; ++Branch)
				{
					if (Branch >= Count && Node.Branches[Branch] != INDEX_NONE)
					{
						return Fail(FString::Printf(TEXT("In %s, %s has a wire on an output it doesn't have."), *GraphName, *NodeName(Node)));
					}
					if (!CheckLink(Graph, i, Node.Branches[Branch], GraphName))
					{
						return false;
					}
				}
			}
			return true;
		}

		void CountIncoming(const FBangoGraph& Graph)
		{
			Incoming.Init(0, Graph.Nodes.Num());
			Written.Init(false, Graph.Nodes.Num());
			for (const FBangoGraphNode& Node : Graph.Nodes)
			{
				if (Node.Next != INDEX_NONE)
				{
					++Incoming[Node.Next];
				}
				for (int32 Branch = 0; Branch < 2; ++Branch)
				{
					if (Node.Branches[Branch] != INDEX_NONE)
					{
						++Incoming[Node.Branches[Branch]];
					}
				}
			}
		}

		/** The entry node's chain, then each disconnected chain as //~ lines. */
		bool WriteGraphBody(const FBangoGraph& Graph, int32 Indent)
		{
			CountIncoming(Graph);
			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				if (Incoming[i] > 1)
				{
					return Fail(FString::Printf(TEXT("%s has more than one wire into it. Each node can only be wired from one place."), *NodeName(Graph.Nodes[i])));
				}
			}

			if (!WriteChain(Graph, Graph.Nodes[0].Next, Indent, -1, 0))
			{
				return false;
			}

			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				if (Incoming[i] == 0 && !Written[i])
				{
					AddBlank();
					if (!WriteChain(Graph, i, Indent, 0, 0))
					{
						return false;
					}
				}
			}

			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				if (!Written[i])
				{
					return Fail(FString::Printf(TEXT("%s is in a loop of wires that nothing leads into."), *NodeName(Graph.Nodes[i])));
				}
			}
			return true;
		}

		/**
		 * Writes the chain starting at Start. For disconnected chains, Indent is the indent before
		 * the //~ marker and Detached the indent after it; Detached is -1 for ordinary lines.
		 */
		bool WriteChain(const FBangoGraph& Graph, int32 Start, int32 Indent, int32 Detached, int32 Depth)
		{
			if (Depth > MaxNestingDepth)
			{
				return Fail(TEXT("The graph is nested too deeply (more than 64 blocks inside each other)."));
			}
			bool bWroteStatement = false;
			EBangoGraphNodeKind PreviousStatement = EBangoGraphNodeKind::Reroute;
			int32 Previous = INDEX_NONE;
			for (int32 i = Start; i != INDEX_NONE; i = Graph.Nodes[i].Next)
			{
				if (Written[i])
				{
					return Fail(FString::Printf(TEXT("%s is in a loop of wires."), *NodeName(Graph.Nodes[i])));
				}
				const FBangoGraphNode& Node = Graph.Nodes[i];
				const bool bReroute = Node.Kind == EBangoGraphNodeKind::Reroute;
				if (bReroute && Previous != INDEX_NONE && Graph.Nodes[Previous].Kind == EBangoGraphNodeKind::Reroute && Node.Comment.IsEmpty())
				{
					// Reroutes in a row share one line (a reroute with a comment starts a new one, under its comment).
					Written[i] = true;
					Out.Last().MoreReroutes.Add(&Node);
					Previous = i;
					continue;
				}
				if (!bReroute && bWroteStatement && NeedsSpacer(PreviousStatement, Node))
				{
					AddSpacer(Detached); // a blank line between statements of different kinds (above the next one's comment); none around reroutes
				}
				if (!WriteNode(Graph, i, Indent, Detached, Depth, false))
				{
					return false;
				}
				if (!bReroute)
				{
					bWroteStatement = true;
					PreviousStatement = Node.Kind;
				}
				Previous = i;
			}
			return true;
		}

		/**
		 * Statements that sit together without blank lines between them: declarations, assignments (Set, Add To,
		 * ++ and the like), and calls. 0 for flow control (if, loops, break, continue, return, label, goto), which
		 * always gets a blank line before and after it.
		 */
		static int32 SpacingGroup(EBangoGraphNodeKind Kind)
		{
			switch (Kind)
			{
			case EBangoGraphNodeKind::VarDecl: return 1;
			case EBangoGraphNodeKind::Assign:
			case EBangoGraphNodeKind::IncDec:  return 2;
			case EBangoGraphNodeKind::Call:    return 3;
			default:                           return 0;
			}
		}

		/**
		 * True if a blank line goes between the statement before (of kind Previous) and Node: they're in different
		 * groups, either is flow control, or Node has a comment (a comment starts a new paragraph).
		 */
		static bool NeedsSpacer(EBangoGraphNodeKind Previous, const FBangoGraphNode& Node)
		{
			const int32 Group = SpacingGroup(Node.Kind);
			return Group == 0 || Group != SpacingGroup(Previous) || !Node.Comment.IsEmpty();
		}

		/** Indent for the next block level. */
		static void Deeper(int32& Indent, int32& Detached)
		{
			if (Detached >= 0)
			{
				++Detached;
			}
			else
			{
				++Indent;
			}
		}

		bool WriteBlock(const FBangoGraph& Graph, int32 Start, int32 Indent, int32 Detached, int32 Depth)
		{
			AddCode(Indent, Detached, TEXT("{"), nullptr);
			int32 InnerIndent = Indent;
			int32 InnerDetached = Detached;
			Deeper(InnerIndent, InnerDetached);
			if (!WriteChain(Graph, Start, InnerIndent, InnerDetached, Depth + 1))
			{
				return false;
			}
			return true;
		}

		bool WriteNode(const FBangoGraph& Graph, int32 Index, int32 Indent, int32 Detached, int32 Depth, bool bElseIf)
		{
			Written[Index] = true;
			const FBangoGraphNode& Node = Graph.Nodes[Index];
			AddComment(Node.Comment, Indent, Detached);

			const FString& Text = Node.Text;
			switch (Node.Kind)
			{
			case EBangoGraphNodeKind::Entry:
				return Fail(TEXT("Internal error: entry node in a chain."));

			case EBangoGraphNodeKind::VarDecl:
			case EBangoGraphNodeKind::Assign:
			case EBangoGraphNodeKind::IncDec:
			case EBangoGraphNodeKind::Call:
				AddCode(Indent, Detached, Text + TEXT(";"), &Node);
				return true;

			case EBangoGraphNodeKind::Break:
				AddCode(Indent, Detached, TEXT("break;"), &Node);
				return true;
			case EBangoGraphNodeKind::Continue:
				AddCode(Indent, Detached, TEXT("continue;"), &Node);
				return true;
			case EBangoGraphNodeKind::Return:
				AddCode(Indent, Detached, Text.IsEmpty() ? FString(TEXT("return;")) : FString(TEXT("return ")) + Text + TEXT(";"), &Node);
				return true;
			case EBangoGraphNodeKind::Label:
				AddCode(Indent, Detached, FString(TEXT("label ")) + Text + TEXT(";"), &Node);
				return true;
			case EBangoGraphNodeKind::Goto:
				AddCode(Indent, Detached, FString(TEXT("goto ")) + Text + TEXT(";"), &Node);
				return true;
			case EBangoGraphNodeKind::Reroute:
				AddCode(Indent, Detached, FString(), &Node); // only the //@ comment
				return true;

			case EBangoGraphNodeKind::While:
			case EBangoGraphNodeKind::For:
			case EBangoGraphNodeKind::ForEach:
			{
				const TCHAR* Keyword = Node.Kind == EBangoGraphNodeKind::While ? TEXT("while (")
					: Node.Kind == EBangoGraphNodeKind::For ? TEXT("for (") : TEXT("forEach (");
				AddCode(Indent, Detached, FString(Keyword) + Text + TEXT(")"), &Node);
				if (!WriteBlock(Graph, Node.Branches[0], Indent, Detached, Depth))
				{
					return false;
				}
				AddCode(Indent, Detached, TEXT("};"), nullptr);
				return true;
			}

			case EBangoGraphNodeKind::If:
			{
				AddCode(Indent, Detached, FString(bElseIf ? TEXT("else if (") : TEXT("if (")) + Text + TEXT(")"), &Node);
				if (!WriteBlock(Graph, Node.Branches[0], Indent, Detached, Depth))
				{
					return false;
				}
				const int32 FalseBranch = Node.Branches[1];
				if (FalseBranch == INDEX_NONE)
				{
					AddCode(Indent, Detached, TEXT("};"), nullptr);
					return true;
				}
				AddCode(Indent, Detached, TEXT("}"), nullptr);

				const FBangoGraphNode& FalseNode = Graph.Nodes[FalseBranch];
				if (FalseNode.Kind == EBangoGraphNodeKind::If && FalseNode.Next == INDEX_NONE)
				{
					// A False branch that is just another if, with nothing after it, is written as `else if`.
					if (Depth + 1 > MaxNestingDepth)
					{
						return Fail(TEXT("The graph is nested too deeply (more than 64 blocks inside each other)."));
					}
					return WriteNode(Graph, FalseBranch, Indent, Detached, Depth + 1, true);
				}

				AddCode(Indent, Detached, TEXT("else"), nullptr);
				if (!WriteBlock(Graph, FalseBranch, Indent, Detached, Depth))
				{
					return false;
				}
				AddCode(Indent, Detached, TEXT("};"), nullptr);
				return true;
			}
			}
			return Fail(TEXT("Internal error: unknown node kind."));
		}

		void AddCode(int32 Indent, int32 Detached, const FString& Code, const FBangoGraphNode* Node)
		{
			FOutLine& Line = Out.AddDefaulted_GetRef();
			Line.Indent = Indent;
			Line.DetachedIndent = Detached;
			Line.Code = Code;
			if (Node)
			{
				Line.bHasMetadata = true;
				Line.NodeID = Node->NodeID;
				Line.X = Node->X;
				Line.Y = Node->Y;
			}
		}

		void AddComment(const FString& Comment, int32 Indent, int32 Detached)
		{
			if (Comment.IsEmpty())
			{
				return;
			}
			FString Current;
			for (int32 i = 0; i <= Comment.Len(); ++i)
			{
				if (i == Comment.Len() || Comment[i] == TEXT('\n'))
				{
					AddCode(Indent, Detached, Current.IsEmpty() ? FString(TEXT("//")) : FString(TEXT("// ")) + Current, nullptr);
					Current.Reset();
				}
				else if (Comment[i] != TEXT('\r'))
				{
					Current.AppendChar(Comment[i]);
				}
			}
		}

		void AddBlank()
		{
			FOutLine& Line = Out.AddDefaulted_GetRef();
			Line.Indent = 0;
		}

		/** The empty line between two nodes: a blank line, or a bare `//~` inside a disconnected chain. */
		void AddSpacer(int32 Detached)
		{
			FOutLine& Line = Out.AddDefaulted_GetRef();
			Line.Indent = 0;
			Line.bDetachedSpacer = Detached >= 0;
		}

		void Format(FString& OutText)
		{
			int32 Column = 0;
			for (const FOutLine& Line : Out)
			{
				const int32 Width = Line.Width();
				if (Line.bHasMetadata && Width <= MaxMetadataColumn && Width > Column)
				{
					Column = Width;
				}
			}
			Column += 4;

			OutText.Reset();
			for (const FOutLine& Line : Out)
			{
				if (Line.bDetachedSpacer)
				{
					OutText += TEXT("//~\n");
					continue;
				}
				for (int32 i = 0; i < Line.Indent; ++i)
				{
					OutText += TEXT("\t");
				}
				if (Line.DetachedIndent >= 0 && (!Line.Code.IsEmpty() || Line.bHasMetadata))
				{
					OutText += TEXT("//~ ");
					for (int32 i = 0; i < Line.DetachedIndent; ++i)
					{
						OutText += TEXT("\t");
					}
				}
				OutText += Line.Code;
				if (Line.bHasMetadata)
				{
					const int32 Width = Line.Width();
					const int32 Spaces = Width + 4 > Column ? 4 : Column - Width;
					for (int32 i = 0; i < Spaces; ++i)
					{
						OutText += TEXT(" ");
					}
					OutText += FString::Printf(TEXT("//@%lld(%d, %d)"), Line.NodeID, Line.X, Line.Y);
					for (const FBangoGraphNode* More : Line.MoreReroutes)
					{
						OutText += FString::Printf(TEXT(" @%lld(%d, %d)"), More->NodeID, More->X, More->Y);
					}
				}
				OutText += TEXT("\n");
			}
		}
	};
}

namespace Bango
{
	bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError)
	{
		OutError.Reset();
		BangoGraphWriterPrivate::FWriter Writer(OutError);
		if (!Writer.Run(Document, OutText))
		{
			OutText.Reset();
			return false;
		}
		return true;
	}
}
