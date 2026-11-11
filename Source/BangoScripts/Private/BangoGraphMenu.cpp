#include "BangoGraphMenu.h"

#include "BangoCommand.h"
#include "BangoLexer.h"
#include "BangoParser.h"
#include "BangoTypes.h"

namespace BangoGraphMenuPrivate
{
	/** Every name written in the graph's nodes and declared at its top, so made-up names don't clash with anything. */
	static void AddNamesInGraph(const FBangoGraph& Graph, TSet<FString>& Used)
	{
		for (const FBangoGraphDecl& Local : Graph.Locals)
		{
			Used.Add(Local.Name);
		}
		for (const FBangoGraphNode& Node : Graph.Nodes)
		{
			FBangoLexResult Lexed;
			FBangoCompileError Error;
			if (Bango::Lex(Node.Text, Lexed, Error))
			{
				for (const FBangoToken& Token : Lexed.Tokens)
				{
					if (Token.Kind == EBangoTokenKind::Identifier)
					{
						Used.Add(Token.Text);
					}
				}
			}
		}
	}

	static FString CapitalizeFirst(const FString& Text)
	{
		return Text.IsEmpty() ? Text : Text.Left(1).ToUpper() + Text.Mid(1, Text.Len() - 1);
	}

	struct FNodeMenuBuilder
	{
		const FBangoNodeLayoutContext& Context;
		TArray<FBangoNodeMenuEntry>& Out;

		/** Adds an entry; its title and description come from the node's layout, so they match the node. */
		FBangoNodeMenuEntry& Add(const FString& Category, EBangoGraphNodeKind Kind, const FString& Text, const FString& Keywords = FString())
		{
			FBangoGraphNode Node;
			Node.Kind = Kind;
			Node.Text = Text;
			const FBangoNodeLayout Layout = Bango::GetNodeLayout(Node, /*bInFunctionGraph*/ false, Context);

			FBangoNodeMenuEntry& Entry = Out.AddDefaulted_GetRef();
			Entry.Category = Category;
			Entry.Title = Layout.Title;
			Entry.Description = Layout.Description;
			Entry.Keywords = Keywords;
			Entry.Kind = Kind;
			Entry.Text = Text;
			return Entry;
		}
	};

	/** Menu order: Flow Control and Variables first, then the rest by name; inside a category, by title. */
	static int32 CategoryRank(const FString& Category)
	{
		if (Category == TEXT("Flow Control")) return 0;
		if (Category == TEXT("Variables"))    return 1;
		if (Category == TEXT("Globals"))      return 2;
		if (Category == TEXT("This Script"))  return 4;
		return 3;
	}
}

namespace Bango
{
	void GetVariableNodeEntries(const FString& Name, const FString& Type, const TSet<FString>& Used, TArray<FBangoNodeMenuEntry>& OutEntries)
	{
		OutEntries.Reset();
		auto Add = [&OutEntries](const TCHAR* Title, EBangoGraphNodeKind Kind, const FString& Text, const FString& Description)
		{
			FBangoNodeMenuEntry& Entry = OutEntries.AddDefaulted_GetRef();
			Entry.Title = Title;
			Entry.Kind = Kind;
			Entry.Text = Text;
			Entry.Description = Description;
		};
		const bool bArray = Type.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive);
		const bool bInt = Type == TEXT("int");
		const bool bNumber = bInt || Type == TEXT("float");
		const bool bVector = Type == TEXT("vector2") || Type == TEXT("vector3");
		const bool bString = Type == TEXT("string");

		Add(TEXT("Set"), EBangoGraphNodeKind::Assign, Name + TEXT(" ="), FString::Printf(TEXT("Gives %s a new value."), *Name));
		if (bNumber || bVector || bString)
		{
			Add(TEXT("Add To"), EBangoGraphNodeKind::Assign, Name + TEXT(" +="),
				bString ? FString::Printf(TEXT("Adds a string to the end of %s."), *Name) : FString::Printf(TEXT("Adds a value to %s."), *Name));
		}
		if (bNumber || bVector)
		{
			Add(TEXT("Subtract From"), EBangoGraphNodeKind::Assign, Name + TEXT(" -="), FString::Printf(TEXT("Subtracts a value from %s."), *Name));
		}
		if (bInt)
		{
			Add(TEXT("Increment"), EBangoGraphNodeKind::IncDec, Name + TEXT("++"), FString::Printf(TEXT("Adds 1 to %s."), *Name));
			Add(TEXT("Decrement"), EBangoGraphNodeKind::IncDec, Name + TEXT("--"), FString::Printf(TEXT("Subtracts 1 from %s."), *Name));
		}
		if (bNumber || bVector)
		{
			Add(TEXT("Multiply"), EBangoGraphNodeKind::Assign, Name + TEXT(" *="),
				bVector ? FString::Printf(TEXT("Multiplies %s by a number."), *Name) : FString::Printf(TEXT("Multiplies %s by a value."), *Name));
			Add(TEXT("Divide"), EBangoGraphNodeKind::Assign, Name + TEXT(" /="),
				bVector ? FString::Printf(TEXT("Divides %s by a number."), *Name) : FString::Printf(TEXT("Divides %s by a value."), *Name));
		}
		if (bArray)
		{
			Add(TEXT("Push"), EBangoGraphNodeKind::Call, Name + TEXT(" Push()"), FString::Printf(TEXT("Adds a value to the end of %s."), *Name));
			Add(TEXT("Insert At"), EBangoGraphNodeKind::Call, Name + TEXT(" InsertAt()"), FString::Printf(TEXT("Puts a value into %s at an index, moving the later elements up."), *Name));
			Add(TEXT("Pop"), EBangoGraphNodeKind::Call, Name + TEXT(" Pop()"), FString::Printf(TEXT("Removes the last element of %s."), *Name));
			Add(TEXT("Remove At"), EBangoGraphNodeKind::Call, Name + TEXT(" RemoveAt()"), FString::Printf(TEXT("Removes the element at an index of %s, moving the later elements down."), *Name));
			Add(TEXT("For Each"), EBangoGraphNodeKind::ForEach, MakeUniqueName(TEXT("_item"), Used) + TEXT(" in ") + Name,
				FString::Printf(TEXT("Runs Do once for each element of %s."), *Name));
		}
	}

	FString MakeUniqueName(const FString& Name, const TSet<FString>& Used)
	{
		if (!Used.Contains(Name))
		{
			return Name;
		}
		for (int32 Suffix = 2; ; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s%d"), *Name, Suffix);
			if (!Used.Contains(Candidate))
			{
				return Candidate;
			}
		}
	}

	void GetNodeMenuEntries(const FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context, TArray<FBangoNodeMenuEntry>& OutEntries)
	{
		using namespace BangoGraphMenuPrivate;

		OutEntries.Reset();
		TSet<FString> Used;
		for (const FBangoGraphDecl& Global : Document.Globals)
		{
			Used.Add(Global.Name);
		}
		AddNamesInGraph(Document.Main, Used);
		for (const FBangoGraph& Function : Document.Functions)
		{
			AddNamesInGraph(Function, Used);
		}
		for (const TPair<int64, TMap<FString, FString>>& Visible : Context.VisibleVariables)
		{
			for (const TPair<FString, FString>& Variable : Visible.Value)
			{
				Used.Add(Variable.Key);
			}
		}

		FNodeMenuBuilder Menu{ Context, OutEntries };

		// ---- Flow Control ----
		const FString Flow = TEXT("Flow Control");
		Menu.Add(Flow, EBangoGraphNodeKind::If, FString(), TEXT("branch condition else"))
			.Description = TEXT("Runs Do if the condition is true, otherwise Else; then carries on from Then.");
		Menu.Add(Flow, EBangoGraphNodeKind::While, FString(), TEXT("loop"))
			.Description = TEXT("Runs Do again and again while the condition is true; then carries on from Then.");
		const FString Counter = MakeUniqueName(TEXT("_i"), Used);
		Menu.Add(Flow, EBangoGraphNodeKind::For, FString::Printf(TEXT("int %s = ; %s <= ; %s++"), *Counter, *Counter, *Counter), TEXT("loop count"))
			.Description = TEXT("Runs Do once for each number from First to Last; then carries on from Then.");
		Menu.Add(Flow, EBangoGraphNodeKind::ForEach, FString::Printf(TEXT("%s in"), *MakeUniqueName(TEXT("_item"), Used)), TEXT("loop array each"))
			.Description = TEXT("Runs Do once for each element of an array; then carries on from Then.");
		Menu.Add(Flow, EBangoGraphNodeKind::Break, FString(), TEXT("loop exit"))
			.Description = TEXT("Leaves the loop it's in, carrying on from the loop's Then.");
		Menu.Add(Flow, EBangoGraphNodeKind::Continue, FString(), TEXT("loop next"))
			.Description = TEXT("Skips the rest of this pass through the loop it's in.");
		Menu.Add(Flow, EBangoGraphNodeKind::Return, FString(), TEXT("end exit"))
			.Description = TEXT("Ends the script, or the function it's in.");
		Menu.Add(Flow, EBangoGraphNodeKind::Label, FString(), TEXT("jump"))
			.Description = TEXT("A named place that a Go To node can jump to.");
		Menu.Add(Flow, EBangoGraphNodeKind::Goto, FString(), TEXT("jump label"))
			.Description = TEXT("Jumps to a Label node.");
		Menu.Add(Flow, EBangoGraphNodeKind::Reroute, FString(), TEXT("knot wire"))
			.Description = TEXT("A bend in a wire, to tidy the graph. It does nothing when the script runs.");

		// ---- Variables (declared in the editor's Globals and Locals lists, not by nodes) ----
		const FString Vars = TEXT("Variables");
		Menu.Add(Vars, EBangoGraphNodeKind::Assign, TEXT("="), TEXT("set assign =")).Description = TEXT("Gives a variable a new value.");
		Menu.Add(Vars, EBangoGraphNodeKind::Assign, TEXT("+="), TEXT("add plus +=")).Description = TEXT("Adds a value to a variable.");
		Menu.Add(Vars, EBangoGraphNodeKind::Assign, TEXT("-="), TEXT("subtract minus -=")).Description = TEXT("Subtracts a value from a variable.");
		Menu.Add(Vars, EBangoGraphNodeKind::Assign, TEXT("*="), TEXT("multiply times scale *=")).Description = TEXT("Multiplies a variable by a value.");
		Menu.Add(Vars, EBangoGraphNodeKind::Assign, TEXT("/="), TEXT("divide /=")).Description = TEXT("Divides a variable by a value.");
		Menu.Add(Vars, EBangoGraphNodeKind::IncDec, TEXT("++"), TEXT("increment add one ++")).Description = TEXT("Adds 1 to an int variable.");
		Menu.Add(Vars, EBangoGraphNodeKind::IncDec, TEXT("--"), TEXT("decrement subtract one --")).Description = TEXT("Subtracts 1 from an int variable.");

		// ---- Array commands (built into the compiler); Count gives a value that must be used ----
		struct FArrayEntry { const TCHAR* Name; const TCHAR* Description; };
		static const FArrayEntry ArrayEntries[] =
		{
			{ TEXT("Push"),     TEXT("Adds a value to the end of an array.") },
			{ TEXT("InsertAt"), TEXT("Puts a value into an array at an index, moving the later elements up.") },
			{ TEXT("Pop"),      TEXT("Removes the last element of an array.") },
			{ TEXT("RemoveAt"), TEXT("Removes the element at an index of an array, moving the later elements down.") },
		};
		for (const FArrayEntry& Array : ArrayEntries)
		{
			Menu.Add(TEXT("Arrays"), EBangoGraphNodeKind::Call, FString::Printf(TEXT("%s()"), Array.Name), TEXT("array list")).Description = Array.Description;
		}

		// ---- Commands that can be a line of their own ----
		if (Context.Commands)
		{
			TArray<FString> Names;
			Context.Commands->GetCommandNames(Names);
			for (const FString& Name : Names)
			{
				const FBangoCommandSignature* Signature = Context.Commands->FindCommand(Name);
				if (!Signature || Signature->MustUseResult() || Context.FindFunction(Name))
				{
					continue;
				}
				Menu.Add(Signature->CategoryName.IsEmpty() ? FString(TEXT("Commands")) : Signature->CategoryName,
					EBangoGraphNodeKind::Call, Name + TEXT("()"), TEXT("command"));
			}
		}

		// ---- The script's own functions ----
		for (const FBangoCommandSignature& Function : Context.Functions)
		{
			FBangoNodeMenuEntry& Entry = Menu.Add(TEXT("This Script"), EBangoGraphNodeKind::Call, Function.Name + TEXT("()"), TEXT("function call"));
			if (Entry.Description.IsEmpty())
			{
				Entry.Description = FString::Printf(TEXT("Calls this script's function %s."), *Function.ToString());
			}
		}

		OutEntries.StableSort([](const FBangoNodeMenuEntry& A, const FBangoNodeMenuEntry& B)
		{
			const int32 RankA = CategoryRank(A.Category);
			const int32 RankB = CategoryRank(B.Category);
			if (RankA != RankB)
			{
				return RankA < RankB;
			}
			const int32 ByCategory = A.Category.Compare(B.Category, ESearchCase::IgnoreCase);
			if (ByCategory != 0)
			{
				return ByCategory < 0;
			}
			return A.Title.Compare(B.Title, ESearchCase::IgnoreCase) < 0;
		});
	}
}
