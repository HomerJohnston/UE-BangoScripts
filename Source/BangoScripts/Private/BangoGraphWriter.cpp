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
		FWriter(FString& InError, bool bInCompact) : Error(InError), bCompact(bInCompact) {}

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

			// The description comes first: nothing may come before it. The heading after it gives it its blank line.
			AddComment(Doc.Description, 0, -1);

			// The script's settings (SCRIPT CONFIG in the editor), under the description and above every heading.
			// A compact script isn't saved with the graph editor's layout, so it doesn't keep the flag that asks for it.
			const bool bWriteUsesGraphEditor = Doc.bUsesGraphEditor && !bCompact;
			if (Doc.bHasLocNamespace || bWriteUsesGraphEditor)
			{
				if (Out.Num() > 0)
				{
					AddBlank();
				}
				if (Doc.bHasLocNamespace)
				{
					AddCode(0, -1, FString(TEXT("#LOCTEXT_NAMESPACE ")) + Bango::MakeStringLiteral(Doc.LocNamespace), nullptr);
				}
				if (bWriteUsesGraphEditor)
				{
					AddCode(0, -1, TEXT("#USES_GRAPH_EDITOR"), nullptr);
				}
			}

			// Main graph. Each part gets a section heading (// ==== ...) above it, if it has anything.
			const FBangoGraphNode& MainEntry = Doc.Main.Nodes[0];
			if (!MainEntry.Text.IsEmpty() || !MainEntry.Comment.IsEmpty())
			{
				AddHeading(Bango::GetScriptInputHeadingComment());
				AddComment(MainEntry.Comment, 0, -1);
				// An empty inputs line still anchors the entry node's comment.
				AddCode(0, -1, FString(TEXT("#inputs (")) + MainEntry.Text + TEXT(")"), nullptr);
			}
			// The declarations: globals, then the main graph's locals.
			if (Doc.Globals.Num() > 0)
			{
				AddHeading(Bango::GetGlobalVarsHeadingComment());
				WriteDecls(Doc.Globals, true, 0);
			}
			if (Doc.Main.ScriptVars.Num() > 0)
			{
				AddHeading(Bango::GetScriptVarsHeadingComment());
				WriteDecls(Doc.Main.ScriptVars, false, 0);
			}

			// The main graph's code and disconnected chains, under their heading if there are any.
			const int32 BodyStart = Out.Num();
			if (!WriteGraphBody(Doc.Main, 0))
			{
				return false;
			}
			if (Out.Num() > BodyStart)
			{
				TArray<FOutLine> Body;
				for (int32 i = BodyStart; i < Out.Num(); ++i)
				{
					if (Body.Num() > 0 || !IsBlankLine(Out[i]))
					{
						Body.Add(Out[i]); // without the blank lines it starts with: the heading has its own
					}
				}
				Out.SetNum(BodyStart);
				if (Body.Num() > 0)
				{
					AddHeading(Bango::GetScriptBodyHeadingComment());
					Out.Append(Body);
				}
			}

			// Functions, depth-first by first use: a FUNCTIONS heading before the first, and a line between the others.
			bool bFirstFunction = true;
			for (int32 FunctionIndex : Bango::GetFunctionWriteOrder(Doc))
			{
				const FBangoGraph& Function = Doc.Functions[FunctionIndex];
				AddBlank();
				if (!bCompact)
				{
					AddCode(0, -1, FString(TEXT("//")) + (bFirstFunction ? Bango::GetFunctionsHeadingComment() : Bango::GetFunctionSeparatorComment()), nullptr);
					AddBlank();
				}
				bFirstFunction = false;
				const FBangoGraphNode& Entry = Function.Nodes[0];
				AddComment(Entry.Comment, 0, -1);
				AddCode(0, -1, FString(TEXT("function ")) + Entry.Text, nullptr);
				AddCode(0, -1, TEXT("{"), nullptr);
				if (Function.ScriptVars.Num() > 0)
				{
					WriteDecls(Function.ScriptVars, false, 1);
					if (Entry.Next != INDEX_NONE)
					{
						AddBlank();
					}
				}
				if (!WriteGraphBody(Function, 1))
				{
					return false;
				}
				AddCode(0, -1, TEXT("}"), nullptr);
			}

			// Compact, a script without a description can start with a comment (no heading comes first): a blank line
			// above it keeps it from being read back as the description.
			if (bCompact)
			{
				// Skipped reroutes can leave blank lines in a row (or a disconnected chain of nothing): one is enough.
				TArray<FOutLine> Kept;
				for (FOutLine& Line : Out)
				{
					if (IsBlankLine(Line) && (Kept.Num() == 0 || IsBlankLine(Kept.Last())))
					{
						continue;
					}
					Kept.Add(MoveTemp(Line));
				}
				while (Kept.Num() > 0 && IsBlankLine(Kept.Last()))
				{
					Kept.Pop();
				}
				Out = MoveTemp(Kept);
			}
			if (bCompact && Doc.Description.IsEmpty() && Out.Num() > 0 && Out[0].DetachedIndent < 0 && Out[0].Code.StartsWith(TEXT("//"), ESearchCase::CaseSensitive))
			{
				Out.Insert(FOutLine(), 0);
			}

			Format(OutText);
			return true;
		}

	private:
		FString& Error;
		/** No section headings and no node metadata (and so no reroutes): see FBangoGraphWriteOptions. */
		bool bCompact = false;
		/** Compact: the comments of reroutes skipped since the last node written (CarryComment). */
		FString CarriedComment;
		TArray<FOutLine> Out;

		/** Per node of the graph being written. */
		TArray<int32> Incoming;
		TArray<bool> Written;

		bool Fail(const FString& Message)
		{
			Error = Message;
			return false;
		}

		/** `global int gold;` or `scriptVar int _count = 0;`, each under its comment. Declarations have no metadata. */
		void WriteDecls(const TArray<FBangoGraphDecl>& Decls, bool bGlobal, int32 Indent)
		{
			for (const FBangoGraphDecl& Decl : Decls)
			{
				AddComment(Decl.Comment, Indent, -1);
				FString Code = bGlobal ? FString(TEXT("global ")) : FString(TEXT("scriptVar "));
				Code += Decl.Type + TEXT(" ") + Decl.Name;
				if (Decl.bHasValue) // a script variable's starting value, or a global's default
				{
					Code += TEXT(" = ") + Decl.Value;
				}
				AddCode(Indent, -1, Code + TEXT(";"), nullptr);
			}
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
				const int32 Count = Bango::GetGraphNodeBranchCount(Node);
				if (Node.Kind == EBangoGraphNodeKind::Sequence && Count == 0)
				{
					return Fail(FString::Printf(TEXT("In %s, %s has no outputs: a sequence needs at least one."), *GraphName, *NodeName(Node)));
				}
				if (Node.Kind == EBangoGraphNodeKind::Switch)
				{
					if (Node.Branches.Num() != Node.CaseValues.Num() + 1)
					{
						return Fail(FString::Printf(TEXT("In %s, %s has %d outputs for %d cases: a switch has one per case, then Default."), *GraphName, *NodeName(Node), Node.Branches.Num(), Node.CaseValues.Num()));
					}
					for (const FString& Values : Node.CaseValues)
					{
						if (HasLineBreak(Values))
						{
							return Fail(FString::Printf(TEXT("In %s, a case of %s has a line break in its values. Each case's values must be one line."), *GraphName, *NodeName(Node)));
						}
					}
				}
				for (int32 Branch = 0; Branch < Node.Branches.Num(); ++Branch)
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
				for (int32 Branch = 0; Branch < Node.Branches.Num(); ++Branch)
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
				if (bReroute && bCompact)
				{
					// A reroute is only its //@ comment, so it isn't written; its comment goes above what follows.
					Written[i] = true;
					CarryComment(Node.Comment);
					Previous = i;
					continue;
				}
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
			if (!CarriedComment.IsEmpty())
			{
				// Reroutes at the end of a chain: their comments stay where the chain ends.
				const FString Comment = MoveTemp(CarriedComment);
				CarriedComment.Reset();
				AddComment(Comment, Indent, Detached);
			}
			return true;
		}

		/** Compact: a skipped reroute's comment, for the next node written. */
		void CarryComment(const FString& Comment)
		{
			if (!Comment.IsEmpty())
			{
				CarriedComment = CarriedComment.IsEmpty() ? Comment : CarriedComment + TEXT("\n") + Comment;
			}
		}

		/**
		 * Statements that sit together without blank lines between them: assignments (Set, Add To, ++ and the
		 * like), and calls. 0 for flow control (if, loops, break, continue, return, label, goto), which
		 * always gets a blank line before and after it.
		 */
		static int32 SpacingGroup(EBangoGraphNodeKind Kind)
		{
			switch (Kind)
			{
			case EBangoGraphNodeKind::Local:
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

		/** True if the node is written disabled: between slash-star and star-slash (reroutes have nothing to disable). */
		static bool IsWrittenDisabled(const FBangoGraphNode& Node)
		{
			return Node.bDisabled && Node.Kind != EBangoGraphNodeKind::Reroute && Node.Kind != EBangoGraphNodeKind::Entry;
		}

		static bool IsBlockKind(EBangoGraphNodeKind Kind)
		{
			return Kind == EBangoGraphNodeKind::If || Kind == EBangoGraphNodeKind::While
				|| Kind == EBangoGraphNodeKind::For || Kind == EBangoGraphNodeKind::ForEach || Kind == EBangoGraphNodeKind::Sequence
				|| Kind == EBangoGraphNodeKind::Switch || Kind == EBangoGraphNodeKind::Spawn;
		}

		/**
		 * A simple disabled node is written on its line: the code between the markers, then its //@ comment.
		 * A disabled block node gets the markers on lines of their own, above and below the whole block.
		 */
		bool WriteNode(const FBangoGraph& Graph, int32 Index, int32 Indent, int32 Detached, int32 Depth, bool bElseIf)
		{
			Written[Index] = true;
			const FBangoGraphNode& Node = Graph.Nodes[Index];
			if (!CarriedComment.IsEmpty())
			{
				const FString Carried = MoveTemp(CarriedComment);
				CarriedComment.Reset();
				AddComment(Node.Comment.IsEmpty() ? Carried : Carried + TEXT("\n") + Node.Comment, Indent, Detached);
			}
			else
			{
				AddComment(Node.Comment, Indent, Detached);
			}

			const bool bDisabled = !bElseIf && IsWrittenDisabled(Node);
			if (bDisabled && IsBlockKind(Node.Kind))
			{
				AddCode(Indent, Detached, TEXT("/*"), nullptr);
				if (!WriteNodeCode(Graph, Index, Indent, Detached, Depth, false))
				{
					return false;
				}
				AddCode(Indent, Detached, TEXT("*/"), nullptr);
				return true;
			}
			const int32 FirstLine = Out.Num();
			if (!WriteNodeCode(Graph, Index, Indent, Detached, Depth, bElseIf))
			{
				return false;
			}
			if (bDisabled && Out.Num() == FirstLine + 1)
			{
				Out.Last().Code = FString(TEXT("/* ")) + Out.Last().Code + TEXT(" */");
			}
			return true;
		}

		/** The node's own lines, without its comment. */
		bool WriteNodeCode(const FBangoGraph& Graph, int32 Index, int32 Indent, int32 Detached, int32 Depth, bool bElseIf)
		{
			const FBangoGraphNode& Node = Graph.Nodes[Index];
			const FString& Text = Node.Text;
			switch (Node.Kind)
			{
			case EBangoGraphNodeKind::Entry:
				return Fail(TEXT("Internal error: entry node in a chain."));

			case EBangoGraphNodeKind::Assign:
			case EBangoGraphNodeKind::IncDec:
			case EBangoGraphNodeKind::Local:
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
				AddCode(Indent, Detached, TEXT("}"), nullptr);
				return true;
			}

			case EBangoGraphNodeKind::Sequence:
			{
				// `sequence`, then each output's block in turn: the `{` of one on the line after the `}` of the last.
				AddCode(Indent, Detached, TEXT("sequence"), &Node);
				for (const int32 Output : Node.Branches)
				{
					if (!WriteBlock(Graph, Output, Indent, Detached, Depth))
					{
						return false;
					}
					AddCode(Indent, Detached, TEXT("}"), nullptr);
				}
				return true;
			}

			case EBangoGraphNodeKind::Spawn:
			{
				AddCode(Indent, Detached, Text.IsEmpty() ? FString(TEXT("spawn")) : Text + TEXT(" = spawn"), &Node);
				if (!WriteBlock(Graph, Node.Branches[0], Indent, Detached, Depth))
				{
					return false;
				}
				AddCode(Indent, Detached, TEXT("}"), nullptr);
				return true;
			}

			case EBangoGraphNodeKind::Switch:
			{
				// `switch (value)` `{`, each case line with its block one deeper, `default:` only if it's wired, `}`.
				AddCode(Indent, Detached, FString(TEXT("switch (")) + Text + TEXT(")"), &Node);
				AddCode(Indent, Detached, TEXT("{"), nullptr);
				int32 CaseIndent = Indent;
				int32 CaseDetached = Detached;
				Deeper(CaseIndent, CaseDetached);
				const int32 DefaultBranch = Node.Branches.Num() - 1;
				for (int32 Branch = 0; Branch < Node.Branches.Num(); ++Branch)
				{
					if (Branch == DefaultBranch && Node.Branches[Branch] == INDEX_NONE)
					{
						break;
					}
					AddCode(CaseIndent, CaseDetached, Branch == DefaultBranch ? FString(TEXT("default:")) : FString(TEXT("case ")) + Node.CaseValues[Branch] + TEXT(":"), nullptr);
					if (!WriteBlock(Graph, Node.Branches[Branch], CaseIndent, CaseDetached, Depth + 1))
					{
						return false;
					}
					AddCode(CaseIndent, CaseDetached, TEXT("}"), nullptr);
				}
				AddCode(Indent, Detached, TEXT("}"), nullptr);
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
					AddCode(Indent, Detached, TEXT("}"), nullptr);
					return true;
				}
				AddCode(Indent, Detached, TEXT("}"), nullptr);

				// Compact, reroutes aren't written, so a False branch that starts with some is read past them.
				int32 FalseStart = FalseBranch;
				while (bCompact && Graph.Nodes[FalseStart].Kind == EBangoGraphNodeKind::Reroute && Graph.Nodes[FalseStart].Next != INDEX_NONE && !Written[Graph.Nodes[FalseStart].Next])
				{
					FalseStart = Graph.Nodes[FalseStart].Next;
				}
				const FBangoGraphNode& FalseNode = Graph.Nodes[FalseStart];
				if (FalseNode.Kind == EBangoGraphNodeKind::If && FalseNode.Next == INDEX_NONE && !IsWrittenDisabled(FalseNode))
				{
					// A False branch that is just another if, with nothing after it, is written as `else if`.
					if (Depth + 1 > MaxNestingDepth)
					{
						return Fail(TEXT("The graph is nested too deeply (more than 64 blocks inside each other)."));
					}
					for (int32 i = FalseBranch; i != FalseStart; i = Graph.Nodes[i].Next)
					{
						Written[i] = true;
						CarryComment(Graph.Nodes[i].Comment);
					}
					return WriteNode(Graph, FalseStart, Indent, Detached, Depth + 1, true);
				}

				AddCode(Indent, Detached, TEXT("else"), nullptr);
				if (!WriteBlock(Graph, FalseBranch, Indent, Detached, Depth))
				{
					return false;
				}
				AddCode(Indent, Detached, TEXT("}"), nullptr);
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
			if (Node && !bCompact)
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

		/** A section heading: a blank line (unless it's the first line), `//` and the heading, then a blank line. */
		void AddHeading(const TCHAR* Heading)
		{
			if (Out.Num() > 0)
			{
				AddBlank();
			}
			if (bCompact)
			{
				return; // only the blank line between parts
			}
			AddCode(0, -1, FString(TEXT("//")) + Heading, nullptr);
			AddBlank();
		}

		static bool IsBlankLine(const FOutLine& Line)
		{
			return Line.Code.IsEmpty() && !Line.bDetachedSpacer && !Line.bHasMetadata && Line.DetachedIndent < 0 && Line.MoreReroutes.Num() == 0;
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
	int32 CountGraphNodes(const FBangoGraphDocument& Document)
	{
		int32 Count = 0;
		auto CountIn = [&Count](const FBangoGraph& Graph)
		{
			for (const FBangoGraphNode& Node : Graph.Nodes)
			{
				Count += Node.Kind != EBangoGraphNodeKind::Entry ? 1 : 0;
			}
		};
		CountIn(Document.Main);
		for (const FBangoGraph& Function : Document.Functions)
		{
			CountIn(Function);
		}
		return Count;
	}

	bool IsWrittenCompact(const FBangoGraphDocument& Document, const FBangoGraphWriteOptions& Options)
	{
		return Options.bCompact || (Options.bFollowUsesGraphEditor && !Document.bUsesGraphEditor);
	}

	bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError)
	{
		return WriteGraphText(Document, OutText, OutError, FBangoGraphWriteOptions());
	}

	bool WriteGraphText(const FBangoGraphDocument& Document, FString& OutText, FString& OutError, const FBangoGraphWriteOptions& Options)
	{
		OutError.Reset();
		BangoGraphWriterPrivate::FWriter Writer(OutError, IsWrittenCompact(Document, Options));
		if (!Writer.Run(Document, OutText))
		{
			OutText.Reset();
			return false;
		}
		return true;
	}
}
