#include "BangoGraph.h"

#include "BangoAST.h"
#include "BangoLexer.h"
#include "BangoParser.h"

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

bool FBangoGraphNode::operator==(const FBangoGraphNode& Other) const
{
	return Kind == Other.Kind && NodeID == Other.NodeID && X == Other.X && Y == Other.Y
		&& Text.Equals(Other.Text, ESearchCase::CaseSensitive) && Comment.Equals(Other.Comment, ESearchCase::CaseSensitive) && Next == Other.Next
		&& Branches[0] == Other.Branches[0] && Branches[1] == Other.Branches[1];
}

bool FBangoGraphDecl::operator==(const FBangoGraphDecl& Other) const
{
	return Type.Equals(Other.Type, ESearchCase::CaseSensitive) && Name.Equals(Other.Name, ESearchCase::CaseSensitive)
		&& bHasValue == Other.bHasValue && (!bHasValue || Value.Equals(Other.Value, ESearchCase::CaseSensitive))
		&& Comment.Equals(Other.Comment, ESearchCase::CaseSensitive);
}

bool FBangoGraph::operator==(const FBangoGraph& Other) const
{
	if (bIsFunction != Other.bIsFunction || !Name.Equals(Other.Name, ESearchCase::CaseSensitive) || Nodes.Num() != Other.Nodes.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Nodes[i] != Other.Nodes[i])
		{
			return false;
		}
	}
	if (Locals.Num() != Other.Locals.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Locals.Num(); ++i)
	{
		if (Locals[i] != Other.Locals[i])
		{
			return false;
		}
	}
	return true;
}

bool FBangoGraphDocument::operator==(const FBangoGraphDocument& Other) const
{
	if (bHasLocNamespace != Other.bHasLocNamespace || !LocNamespace.Equals(Other.LocNamespace, ESearchCase::CaseSensitive)
		|| Main != Other.Main || Functions.Num() != Other.Functions.Num() || Globals.Num() != Other.Globals.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Globals.Num(); ++i)
	{
		if (Globals[i] != Other.Globals[i])
		{
			return false;
		}
	}
	for (int32 i = 0; i < Functions.Num(); ++i)
	{
		if (Functions[i] != Other.Functions[i])
		{
			return false;
		}
	}
	return true;
}

namespace Bango
{
	int32 GetGraphNodeBranchCount(EBangoGraphNodeKind Kind)
	{
		switch (Kind)
		{
		case EBangoGraphNodeKind::If:      return 2;
		case EBangoGraphNodeKind::While:
		case EBangoGraphNodeKind::For:
		case EBangoGraphNodeKind::ForEach: return 1;
		default:                           return 0;
		}
	}

	FString GetGraphNodeNextName(EBangoGraphNodeKind Kind)
	{
		switch (Kind)
		{
		case EBangoGraphNodeKind::If:
		case EBangoGraphNodeKind::While:
		case EBangoGraphNodeKind::For:
		case EBangoGraphNodeKind::ForEach: return TEXT("Then");
		default:                           return FString();
		}
	}

	FString GetGraphNodeBranchName(EBangoGraphNodeKind Kind, int32 Branch)
	{
		if (Kind == EBangoGraphNodeKind::If)
		{
			return Branch == 0 ? TEXT("Do") : TEXT("Else");
		}
		return GetGraphNodeBranchCount(Kind) > Branch ? TEXT("Do") : TEXT("");
	}

	FString GetGraphNodeKindName(EBangoGraphNodeKind Kind)
	{
		switch (Kind)
		{
		case EBangoGraphNodeKind::Entry:    return TEXT("Entry");
		case EBangoGraphNodeKind::Assign:   return TEXT("Set");
		case EBangoGraphNodeKind::IncDec:   return TEXT("Set");
		case EBangoGraphNodeKind::Call:     return TEXT("Call");
		case EBangoGraphNodeKind::If:       return TEXT("If");
		case EBangoGraphNodeKind::While:    return TEXT("While");
		case EBangoGraphNodeKind::For:      return TEXT("For");
		case EBangoGraphNodeKind::ForEach:  return TEXT("For Each");
		case EBangoGraphNodeKind::Break:    return TEXT("Break");
		case EBangoGraphNodeKind::Continue: return TEXT("Continue");
		case EBangoGraphNodeKind::Return:   return TEXT("Return");
		case EBangoGraphNodeKind::Label:    return TEXT("Label");
		case EBangoGraphNodeKind::Goto:     return TEXT("Go To");
		case EBangoGraphNodeKind::Reroute:  return TEXT("Reroute");
		}
		return TEXT("?");
	}
}

// ---------------------------------------------------------------------------
// Function order
// ---------------------------------------------------------------------------

namespace BangoGraphOrderPrivate
{
	/** Calls nodes in the order the writer writes them: the entry's chain (blocks inside out), then disconnected chains. */
	template <typename FCallback>
	static void VisitChain(const FBangoGraph& Graph, int32 Start, TArray<bool>& Visited, FCallback& Callback)
	{
		for (int32 i = Start; Graph.Nodes.IsValidIndex(i) && !Visited[i]; i = Graph.Nodes[i].Next)
		{
			Visited[i] = true;
			Callback(Graph.Nodes[i]);
			for (int32 Branch = 0; Branch < Bango::GetGraphNodeBranchCount(Graph.Nodes[i].Kind); ++Branch)
			{
				VisitChain(Graph, Graph.Nodes[i].Branches[Branch], Visited, Callback);
			}
		}
	}

	template <typename FCallback>
	static void VisitInWrittenOrder(const FBangoGraph& Graph, FCallback Callback)
	{
		if (Graph.Nodes.Num() == 0)
		{
			return;
		}
		TArray<bool> Visited;
		Visited.Init(false, Graph.Nodes.Num());
		TArray<bool> HasIncoming;
		HasIncoming.Init(false, Graph.Nodes.Num());
		for (const FBangoGraphNode& Node : Graph.Nodes)
		{
			const int32 Links[3] = { Node.Next, Node.Branches[0], Node.Branches[1] };
			for (int32 Link : Links)
			{
				if (Graph.Nodes.IsValidIndex(Link))
				{
					HasIncoming[Link] = true;
				}
			}
		}
		Visited[0] = true;
		VisitChain(Graph, Graph.Nodes[0].Next, Visited, Callback);
		for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
		{
			if (!HasIncoming[i] && !Visited[i])
			{
				VisitChain(Graph, i, Visited, Callback);
			}
		}
	}

	struct FOrderer
	{
		const FBangoGraphDocument& Doc;
		TArray<int32> Order;
		TArray<bool> Placed;

		explicit FOrderer(const FBangoGraphDocument& InDoc) : Doc(InDoc)
		{
			Placed.Init(false, Doc.Functions.Num());
		}

		int32 FindFunction(const FString& Name) const
		{
			for (int32 i = 0; i < Doc.Functions.Num(); ++i)
			{
				if (Doc.Functions[i].Name.Equals(Name, ESearchCase::CaseSensitive))
				{
					return i;
				}
			}
			return INDEX_NONE;
		}

		void Place(int32 Function)
		{
			Placed[Function] = true;
			Order.Add(Function);
			Scan(Doc.Functions[Function]);
		}

		/** Places, depth-first, every not-yet-placed function this graph calls. */
		void Scan(const FBangoGraph& Graph)
		{
			VisitInWrittenOrder(Graph, [this](const FBangoGraphNode& Node)
			{
				FBangoLexResult Lexed;
				FBangoCompileError Ignored;
				if (!Bango::Lex(Node.Text, Lexed, Ignored))
				{
					return; // text that doesn't read as code calls nothing
				}
				for (int32 t = 0; t + 1 < Lexed.Tokens.Num(); ++t)
				{
					if (Lexed.Tokens[t].Kind == EBangoTokenKind::Identifier && Lexed.Tokens[t + 1].Kind == EBangoTokenKind::LParen)
					{
						const int32 Function = FindFunction(Lexed.Tokens[t].Text);
						if (Function != INDEX_NONE && !Placed[Function])
						{
							Place(Function);
						}
					}
				}
			});
		}
	};
}

namespace Bango
{
	TArray<int32> GetFunctionWriteOrder(const FBangoGraphDocument& Document)
	{
		BangoGraphOrderPrivate::FOrderer Orderer(Document);
		Orderer.Scan(Document.Main);
		for (int32 i = 0; i < Document.Functions.Num(); ++i)
		{
			if (!Orderer.Placed[i])
			{
				Orderer.Place(i);
			}
		}
		return Orderer.Order;
	}
}

// ---------------------------------------------------------------------------
// Text -> graph
// ---------------------------------------------------------------------------

namespace BangoGraphPrivate
{
	/** Spacing used to place nodes that have no //@ position, in grid units. */
	static constexpr int32 AutoStepX = 20;
	static constexpr int32 AutoStepY = 10;

	static void SplitLines(const FString& Source, TArray<FString>& OutLines)
	{
		FString Current;
		for (int32 i = 0; i < Source.Len(); ++i)
		{
			const TCHAR C = Source[i];
			if (C == TEXT('\n'))
			{
				OutLines.Add(Current);
				Current.Reset();
			}
			else if (C != TEXT('\r'))
			{
				Current.AppendChar(C);
			}
		}
		OutLines.Add(Current);
	}

	static EBangoGraphNodeKind NodeKindFor(EBangoStmtKind Kind)
	{
		switch (Kind)
		{
		case EBangoStmtKind::Assign:   return EBangoGraphNodeKind::Assign;
		case EBangoStmtKind::IncDec:   return EBangoGraphNodeKind::IncDec;
		case EBangoStmtKind::Call:     return EBangoGraphNodeKind::Call;
		case EBangoStmtKind::If:       return EBangoGraphNodeKind::If;
		case EBangoStmtKind::While:    return EBangoGraphNodeKind::While;
		case EBangoStmtKind::For:      return EBangoGraphNodeKind::For;
		case EBangoStmtKind::ForEach:  return EBangoGraphNodeKind::ForEach;
		case EBangoStmtKind::Break:    return EBangoGraphNodeKind::Break;
		case EBangoStmtKind::Continue: return EBangoGraphNodeKind::Continue;
		case EBangoStmtKind::Return:   return EBangoGraphNodeKind::Return;
		case EBangoStmtKind::Label:    return EBangoGraphNodeKind::Label;
		case EBangoStmtKind::Goto:     return EBangoGraphNodeKind::Goto;
		case EBangoStmtKind::Params:   return EBangoGraphNodeKind::Entry;
		case EBangoStmtKind::Reroute:  return EBangoGraphNodeKind::Reroute;
		}
		return EBangoGraphNodeKind::Call;
	}

	/** A comment attachment target: a statement node, or an entry node with a line in the file. */
	struct FTarget
	{
		int32 Line = 0;
		/** -1 for the main graph, else the function index. */
		int32 Graph = -1;
		int32 Node = 0;
		/** For a declaration: its index in the file's Globals (bGlobal) or the graph's Locals; Node is unused. */
		int32 Decl = INDEX_NONE;
		bool bGlobal = false;
	};

	/** A node that had no //@ comment, waiting for an ID and a position. */
	struct FPending
	{
		int32 Graph = -1;
		int32 Node = 0;
	};

	class FBuilder
	{
	public:
		FBuilder(FBangoGraphDocument& InDoc, FBangoCompileError& InError, const FString& InFileName)
			: Doc(InDoc), Error(InError), FileName(InFileName)
		{
		}

		bool Run(const FString& Source, const FBangoScript& Script)
		{
			SplitLines(Source, Lines);

			// Main graph entry
			FBangoGraphNode& MainEntry = Doc.Main.Nodes.AddDefaulted_GetRef();
			MainEntry.Kind = EBangoGraphNodeKind::Entry;
			MainEntry.NodeID = 0;
			Doc.bHasLocNamespace = Script.bHasLocNamespace;
			Doc.LocNamespace = Script.LocNamespace;

			int32 FirstTopLevel = 0;
			if (const FBangoParamsStmt* Params = Script.GetParams())
			{
				Doc.Main.Nodes[0].Text = NodeText(*Params);
				FTarget Target;
				Target.Line = Params->Node.Line;
				Target.Graph = -1;
				Target.Node = 0;
				MainTargets.Add(Target);
				FirstTopLevel = 1;
			}
			if (Script.bHasLocNamespace)
			{
				// Comments on (or just above) the #define LOCTEXT_NAMESPACE line belong to Script Start, like the params line's.
				FTarget Target;
				Target.Line = Script.LocNamespaceLoc.Line;
				Target.Graph = -1;
				Target.Node = 0;
				MainTargets.Add(Target);
			}
			FirstTopLevel = ReadDecls(-1, Script.TopLevel, FirstTopLevel, MainTargets);
			Doc.Main.Nodes[0].Next = BuildChain(-1, Script.TopLevel, FirstTopLevel, &MainTargets);

			// Functions
			for (int32 FunctionIndex = 0; FunctionIndex < Script.Functions.Num(); ++FunctionIndex)
			{
				const FBangoFunction& Function = *Script.Functions[FunctionIndex];
				FBangoGraph& Graph = Doc.Functions.AddDefaulted_GetRef();
				Graph.bIsFunction = true;
				Graph.Name = Function.Name;

				FBangoGraphNode& Entry = Graph.Nodes.AddDefaulted_GetRef();
				Entry.Kind = EBangoGraphNodeKind::Entry;
				Entry.NodeID = 0;
				Entry.Text = FunctionSignature(Function);

				FunctionTargets.AddDefaulted();
				FunctionStart.Add(Function.Loc.Line);
				FunctionEnd.Add(Function.EndLine);

				FTarget Target;
				Target.Line = Function.Loc.Line;
				Target.Graph = FunctionIndex;
				Target.Node = 0;
				FunctionTargets[FunctionIndex].Add(Target);
				MainTargets.Add(Target); // comments just above a function line belong to the function

				const int32 FirstStatement = ReadDecls(FunctionIndex, Function.Body, 0, FunctionTargets[FunctionIndex]);
				Doc.Functions[FunctionIndex].Nodes[0].Next = BuildChain(FunctionIndex, Function.Body, FirstStatement, &FunctionTargets[FunctionIndex]);
			}
			if (bFailed)
			{
				return false;
			}

			SortTargets(MainTargets);

			// Comments and disconnected (//~) nodes
			TArray<const FBangoSourceComment*> Comments;
			TArray<TArray<const FBangoSourceComment*>> DetachedRuns;
			for (const FBangoSourceComment& Comment : Script.Comments)
			{
				if (Comment.bOwnLine && Comment.Text.StartsWith(TEXT("~"), ESearchCase::CaseSensitive))
				{
					if (DetachedRuns.Num() == 0 || DetachedRuns.Last().Last()->Line != Comment.Line - 1)
					{
						DetachedRuns.AddDefaulted();
					}
					DetachedRuns.Last().Add(&Comment);
				}
				else
				{
					Comments.Add(&Comment);
				}
			}

			AttachComments(Comments);
			if (bFailed)
			{
				return false;
			}

			for (const TArray<const FBangoSourceComment*>& Run : DetachedRuns)
			{
				if (!BuildDetachedRun(Run))
				{
					return false;
				}
			}

			return AssignIdsAndPositions();
		}

	private:
		FBangoGraphDocument& Doc;
		FBangoCompileError& Error;
		FString FileName;
		TArray<FString> Lines;

		TArray<FTarget> MainTargets;
		TArray<TArray<FTarget>> FunctionTargets;
		TArray<int32> FunctionStart;
		TArray<int32> FunctionEnd;

		TArray<FPending> Pending;
		TSet<int64> UsedIds;
		int64 MaxId = 0;
		/** A failure found while building chains (which return node indexes, not success). */
		bool bFailed = false;

		FBangoGraph& GraphAt(int32 Graph) { return Graph < 0 ? Doc.Main : Doc.Functions[Graph]; }

		bool Fail(int32 Line, const FString& Message)
		{
			Error = FBangoCompileError();
			Error.FileName = FileName;
			Error.Line = Line;
			Error.Column = 1;
			Error.Message = Message;
			return false;
		}

		FString NodeText(const FBangoStmt& Stmt) const
		{
			const int32 LineIndex = Stmt.Node.Line - 1;
			if (!Lines.IsValidIndex(LineIndex) || Stmt.Node.TextEnd <= Stmt.Node.TextBegin)
			{
				return FString();
			}
			return Lines[LineIndex].Mid(Stmt.Node.TextBegin - 1, Stmt.Node.TextEnd - Stmt.Node.TextBegin);
		}

		static FString FunctionSignature(const FBangoFunction& Function)
		{
			FString Result = Function.bReturnsVoid ? FString(TEXT("void")) : Function.ReturnType.ToString();
			Result += TEXT(" ");
			Result += Function.Name;
			Result += TEXT("(");
			for (int32 i = 0; i < Function.Params.Num(); ++i)
			{
				if (i > 0)
				{
					Result += TEXT(", ");
				}
				Result += Function.Params[i].Type.ToString();
				Result += TEXT(" ");
				Result += Function.Params[i].Name;
			}
			Result += TEXT(")");
			return Result;
		}

		/**
		 * Reads the declarations at Block[Start...] (the top of a graph) into the file's Globals and the graph's
		 * Locals, and returns the index of the first statement after them. A global with a value fails: the
		 * graph has nowhere to keep it, and it doesn't compile.
		 */
		int32 ReadDecls(int32 Graph, const FBangoBlock& Block, int32 Start, TArray<FTarget>& Targets)
		{
			int32 i = Start;
			for (; i < Block.Num(); ++i)
			{
				const FBangoVarDeclStmt* Decl = Block[i]->As<FBangoVarDeclStmt>();
				if (!Decl)
				{
					break;
				}
				// Globals go at the top of the file, before the main graph's locals.
				if (Decl->bGlobal && (Graph >= 0 || Doc.Main.Locals.Num() > 0))
				{
					break; // BuildNode fails on it
				}
				if (Decl->bGlobal && Decl->Init.IsValid())
				{
					Fail(Decl->Loc.Line, FString::Printf(TEXT("The global declaration of '%s' has a value, which isn't allowed (it would reset the global every time the script starts). Remove the value in the text (set it in the code instead), then open the script again."), *Decl->Name));
					bFailed = true;
					return Block.Num();
				}
				FBangoGraphDecl Out;
				Out.Type = Decl->Type.ToString();
				Out.Name = Decl->Name;
				if (Decl->Init.IsValid())
				{
					Out.bHasValue = true;
					Out.Value = DeclValueText(*Decl);
				}
				FTarget Target;
				Target.Line = Decl->Node.Line > 0 ? Decl->Node.Line : Decl->Loc.Line;
				Target.Graph = Graph;
				Target.bGlobal = Decl->bGlobal;
				if (Decl->bGlobal)
				{
					Target.Decl = Doc.Globals.Add(Out);
				}
				else
				{
					Target.Decl = GraphAt(Graph).Locals.Add(Out);
				}
				Targets.Add(Target);
			}
			return i;
		}

		/** The text of a declaration's value: after its `=` (the first one; a type and a name have none). */
		FString DeclValueText(const FBangoVarDeclStmt& Decl) const
		{
			const FString Text = NodeText(Decl);
			int32 Equals = INDEX_NONE;
			return Text.FindChar(TEXT('='), Equals) ? Text.Mid(Equals + 1, Text.Len() - Equals - 1).TrimStartAndEnd() : FString();
		}

		/** Builds Block[Start...] as a chain in Graph and returns the index of its first node (or INDEX_NONE). */
		int32 BuildChain(int32 Graph, const FBangoBlock& Block, int32 Start, TArray<FTarget>* Targets)
		{
			int32 First = INDEX_NONE;
			int32 Previous = INDEX_NONE;
			for (int32 i = Start; i < Block.Num(); ++i)
			{
				const int32 Node = BuildNode(Graph, *Block[i], Targets);
				if (Previous == INDEX_NONE)
				{
					First = Node;
				}
				else
				{
					GraphAt(Graph).Nodes[Previous].Next = Node;
				}
				Previous = Node;
			}
			return First;
		}

		int32 BuildNode(int32 Graph, const FBangoStmt& Stmt, TArray<FTarget>* Targets)
		{
			if (const FBangoVarDeclStmt* Decl = Stmt.As<FBangoVarDeclStmt>())
			{
				// Declarations are only at the top of a graph (ReadDecls); there's no node for one anywhere else.
				if (!bFailed)
				{
					Fail(Decl->Loc.Line, FString::Printf(TEXT("'%s' is declared here, but declarations go at the top: globals after the #define lines, and locals at the top of their script or function. Move it there in the text (set its value here with '%s = ...;' if it needs one), then open the script again."), *Decl->Name, *Decl->Name));
					bFailed = true;
				}
			}
			const int32 Index = GraphAt(Graph).Nodes.Num();
			{
				FBangoGraphNode& Node = GraphAt(Graph).Nodes.AddDefaulted_GetRef();
				Node.Kind = NodeKindFor(Stmt.Kind);
				Node.Text = NodeText(Stmt);
				if (Stmt.Node.bHasMetadata)
				{
					Node.NodeID = Stmt.Node.NodeID;
					Node.X = Stmt.Node.X;
					Node.Y = Stmt.Node.Y;
				}
				else
				{
					Node.NodeID = -1;
					FPending P;
					P.Graph = Graph;
					P.Node = Index;
					Pending.Add(P);
				}
			}
			if (Targets)
			{
				FTarget Target;
				Target.Line = Stmt.Node.Line;
				Target.Graph = Graph;
				Target.Node = Index;
				Targets->Add(Target);
			}

			if (const FBangoIfStmt* If = Stmt.As<FBangoIfStmt>())
			{
				const int32 TrueNode = BuildChain(Graph, If->Then, 0, Targets);
				const int32 FalseNode = BuildChain(Graph, If->Else, 0, Targets);
				GraphAt(Graph).Nodes[Index].Branches[0] = TrueNode;
				GraphAt(Graph).Nodes[Index].Branches[1] = FalseNode;
			}
			else if (const FBangoWhileStmt* While = Stmt.As<FBangoWhileStmt>())
			{
				const int32 Body = BuildChain(Graph, While->Body, 0, Targets);
				GraphAt(Graph).Nodes[Index].Branches[0] = Body;
			}
			else if (const FBangoForStmt* For = Stmt.As<FBangoForStmt>())
			{
				const int32 Body = BuildChain(Graph, For->Body, 0, Targets);
				GraphAt(Graph).Nodes[Index].Branches[0] = Body;
			}
			else if (const FBangoForEachStmt* ForEach = Stmt.As<FBangoForEachStmt>())
			{
				const int32 Body = BuildChain(Graph, ForEach->Body, 0, Targets);
				GraphAt(Graph).Nodes[Index].Branches[0] = Body;
			}
			return Index;
		}

		static void SortTargets(TArray<FTarget>& Targets)
		{
			// Insertion sort by line; target lists are built nearly in order.
			for (int32 i = 1; i < Targets.Num(); ++i)
			{
				const FTarget Item = Targets[i];
				int32 j = i - 1;
				while (j >= 0 && Targets[j].Line > Item.Line)
				{
					Targets[j + 1] = Targets[j];
					--j;
				}
				Targets[j + 1] = Item;
			}
		}

		static void AppendDeclComment(FBangoGraphDecl& Decl, const FString& Text)
		{
			FString Line = Text;
			if (Line.Len() > 0 && Line[0] == TEXT(' '))
			{
				Line = Line.Mid(1, Line.Len() - 1);
			}
			if (!Decl.Comment.IsEmpty())
			{
				Decl.Comment += TEXT("\n");
			}
			Decl.Comment += Line;
		}

		static void AppendComment(FBangoGraphNode& Node, const FString& Text)
		{
			FString Line = Text;
			if (Line.Len() > 0 && Line[0] == TEXT(' '))
			{
				Line = Line.Mid(1, Line.Len() - 1);
			}
			if (!Node.Comment.IsEmpty())
			{
				Node.Comment += TEXT("\n");
			}
			Node.Comment += Line;
		}

		/**
		 * Gives each comment to a node: a comment after code on a statement line goes to that statement;
		 * other comments go to the next node below them in the same graph (a function's own comments
		 * stay in it), or to the graph's last node if there is none below.
		 */
		void AttachComments(const TArray<const FBangoSourceComment*>& Comments)
		{
			for (const FBangoSourceComment* Comment : Comments)
			{
				const int32 Graph = GraphForLine(Comment->Line);
				const TArray<FTarget>& Targets = Graph < 0 ? MainTargets : FunctionTargets[Graph];
				AttachTo(Targets, *Comment);
			}
		}

		void AttachTo(const TArray<FTarget>& Targets, const FBangoSourceComment& Comment)
		{
			const FTarget* Chosen = nullptr;
			if (!Comment.bOwnLine)
			{
				for (const FTarget& Target : Targets)
				{
					if (Target.Line == Comment.Line)
					{
						Chosen = &Target;
						break;
					}
				}
			}
			if (!Chosen)
			{
				for (const FTarget& Target : Targets)
				{
					if (Target.Line > Comment.Line)
					{
						Chosen = &Target;
						break;
					}
				}
			}
			if (!Chosen && Targets.Num() > 0)
			{
				Chosen = &Targets.Last();
			}

			if (Chosen && Chosen->Decl != INDEX_NONE)
			{
				FBangoGraphDecl& Decl = Chosen->bGlobal ? Doc.Globals[Chosen->Decl] : GraphAt(Chosen->Graph).Locals[Chosen->Decl];
				AppendDeclComment(Decl, Comment.Text);
			}
			else if (Chosen)
			{
				AppendComment(GraphAt(Chosen->Graph).Nodes[Chosen->Node], Comment.Text);
			}
			else
			{
				AppendComment(Doc.Main.Nodes[0], Comment.Text);
			}
		}

		/** -1 for the main graph, else the index of the function whose lines contain Line. */
		int32 GraphForLine(int32 Line) const
		{
			for (int32 i = 0; i < FunctionStart.Num(); ++i)
			{
				if (Line >= FunctionStart[i] && Line <= FunctionEnd[i])
				{
					return i;
				}
			}
			return -1;
		}

		/** Reads a block of `//~` lines as a chain of disconnected nodes. */
		bool BuildDetachedRun(const TArray<const FBangoSourceComment*>& Run)
		{
			const int32 FirstLine = Run[0]->Line;
			const int32 Graph = GraphForLine(FirstLine);
			if (GraphForLine(Run.Last()->Line) != Graph)
			{
				return Fail(FirstLine, TEXT("These disconnected-node (//~) lines run past the end of a function."));
			}

			// Rebuild the lines with the same line numbers, so errors and metadata line up with the file.
			FString Snippet;
			for (int32 i = 1; i < FirstLine; ++i)
			{
				Snippet += TEXT("\n");
			}
			for (int32 i = 0; i < Run.Num(); ++i)
			{
				FString Text = Run[i]->Text.Mid(1, Run[i]->Text.Len() - 1); // drop '~'
				if (Text.Len() > 0 && Text[0] == TEXT(' '))
				{
					Text = Text.Mid(1, Text.Len() - 1);
				}
				if (i > 0)
				{
					Snippet += TEXT("\n");
				}
				Snippet += Text;
			}

			FBangoScript SnippetScript;
			FBangoCompileError SnippetError;
			if (!Bango::ParseScript(Snippet, FileName, SnippetScript, SnippetError))
			{
				Error = SnippetError;
				Error.Message = FString(TEXT("In the disconnected nodes (//~ lines): ")) + SnippetError.Message;
				return false;
			}
			if (SnippetScript.Functions.Num() > 0)
			{
				return Fail(SnippetScript.Functions[0]->Loc.Line, TEXT("Disconnected nodes (//~ lines) can't contain a function."));
			}
			if (SnippetScript.GetParams())
			{
				return Fail(SnippetScript.TopLevel[0]->Loc.Line, TEXT("Disconnected nodes (//~ lines) can't contain a params line."));
			}
			if (SnippetScript.TopLevel.Num() == 0)
			{
				return true;
			}
			for (const FBangoStmtPtr& Stmt : SnippetScript.TopLevel)
			{
				if (Stmt->Kind == EBangoStmtKind::VarDecl)
				{
					return Fail(Stmt->Loc.Line, TEXT("Disconnected nodes (//~ lines) can't contain a declaration: declarations go at the top of their script or function."));
				}
			}

			TArray<FString> SavedLines = MoveTemp(Lines);
			SplitLines(Snippet, Lines);
			TArray<FTarget> SnippetTargets;
			BuildChain(Graph, SnippetScript.TopLevel, 0, &SnippetTargets);
			SortTargets(SnippetTargets);
			for (const FBangoSourceComment& Comment : SnippetScript.Comments)
			{
				AttachTo(SnippetTargets, Comment);
			}
			Lines = MoveTemp(SavedLines);
			return true;
		}

		bool CollectIds(const FBangoGraph& Graph)
		{
			for (int32 i = 1; i < Graph.Nodes.Num(); ++i)
			{
				const int64 Id = Graph.Nodes[i].NodeID;
				if (Id < 0)
				{
					continue;
				}
				if (UsedIds.Contains(Id))
				{
					return Fail(0, FString::Printf(TEXT("Node ID %lld is used more than once in this file (check the disconnected-node //~ lines)."), Id));
				}
				UsedIds.Add(Id);
				MaxId = Id > MaxId ? Id : MaxId;
			}
			return true;
		}

		/** Places a node that has no position next to the node that leads into it. */
		void PlaceFrom(FBangoGraph& Graph, int32 From, int32 To, int32 Branch)
		{
			FBangoGraphNode& Target = Graph.Nodes[To];
			const FBangoGraphNode& Source = Graph.Nodes[From];
			Target.X = Source.X + AutoStepX;
			Target.Y = Source.Y + (Branch < 0 ? 0 : AutoStepY * (Branch + 1));
		}

		void PlaceGraph(FBangoGraph& Graph, const TSet<int64>& NeedsPosition)
		{
			// Walk from each node to the nodes it leads to, in node order (parents are always built first).
			TArray<bool> HasIncoming;
			HasIncoming.Init(false, Graph.Nodes.Num());
			int32 LowestY = 0;
			for (int32 i = 0; i < Graph.Nodes.Num(); ++i)
			{
				FBangoGraphNode& Node = Graph.Nodes[i];
				if (i > 0 && !HasIncoming[i] && NeedsPosition.Contains(Node.NodeID))
				{
					// A disconnected chain with no position: put it below everything else.
					Node.X = 0;
					Node.Y = LowestY + AutoStepY * 2;
				}
				LowestY = Node.Y > LowestY ? Node.Y : LowestY;

				const int32 Count = Bango::GetGraphNodeBranchCount(Node.Kind);
				for (int32 Branch = -1; Branch < Count; ++Branch)
				{
					const int32 To = Branch < 0 ? Node.Next : Node.Branches[Branch];
					if (To == INDEX_NONE)
					{
						continue;
					}
					HasIncoming[To] = true;
					if (NeedsPosition.Contains(Graph.Nodes[To].NodeID))
					{
						PlaceFrom(Graph, i, To, Branch);
					}
				}
			}
		}

		bool AssignIdsAndPositions()
		{
			if (!CollectIds(Doc.Main))
			{
				return false;
			}
			for (const FBangoGraph& Graph : Doc.Functions)
			{
				if (!CollectIds(Graph))
				{
					return false;
				}
			}

			TSet<int64> NeedsPosition;
			for (const FPending& P : Pending)
			{
				const int64 Id = ++MaxId;
				GraphAt(P.Graph).Nodes[P.Node].NodeID = Id;
				NeedsPosition.Add(Id);
			}

			PlaceGraph(Doc.Main, NeedsPosition);
			for (FBangoGraph& Graph : Doc.Functions)
			{
				PlaceGraph(Graph, NeedsPosition);
			}
			return true;
		}
	};
}

namespace Bango
{
	bool BuildGraph(const FString& Source, const FString& FileName, FBangoGraphDocument& OutDocument, FBangoCompileError& OutError)
	{
		OutDocument = FBangoGraphDocument();
		FBangoScript Script;
		if (!ParseScript(Source, FileName, Script, OutError))
		{
			return false;
		}
		BangoGraphPrivate::FBuilder Builder(OutDocument, OutError, FileName);
		if (!Builder.Run(Source, Script))
		{
			OutDocument = FBangoGraphDocument();
			return false;
		}

		// Store functions in the order they're written.
		const TArray<int32> Order = GetFunctionWriteOrder(OutDocument);
		TArray<FBangoGraph> Sorted;
		for (int32 Index : Order)
		{
			Sorted.Add(MoveTemp(OutDocument.Functions[Index]));
		}
		OutDocument.Functions = MoveTemp(Sorted);
		return true;
	}
}
