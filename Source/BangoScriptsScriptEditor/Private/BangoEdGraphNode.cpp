#include "BangoEdGraphNode.h"

#include "BangoEdGraphSchema.h"
#include "BangoGraphCommands.h"
#include "EdGraph/EdGraphSchema.h"
#include "GraphEditor.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "EdGraph/EdGraphPin.h"
#include "BangoGraphEdit.h"
#include "EdGraph/EdGraph.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"
#include "BangoNodeDecorator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BangoEdGraphNode)

#define LOCTEXT_NAMESPACE "BangoEdGraphNode"

const FName UBangoEdGraphNode::InPinName(TEXT("In"));
const FName UBangoEdGraphNode::NextPinName(TEXT("Next"));
const FName UBangoEdGraphNode::DefaultPinName(TEXT("Default"));
FName UBangoEdGraphNode::GetBranchPinName(int32 Branch)
{
	return FName(*FString::Printf(TEXT("Branch%d"), Branch));
}

int32 UBangoEdGraphNode::GetBranchCount() const
{
	if (IsSwitch())
	{
		return CaseValues.Num() + 1;
	}
	if (IsEntries())
	{
		return CaseValues.Num();
	}
	return IsSequence() ? FMath::Max(SequenceOutputs, 1) : Bango::GetGraphNodeBranchCount(GetKind());
}

UEdGraphPin* UBangoEdGraphNode::GetBranchPin(int32 Branch) const
{
	if (IsSwitch() && Branch == CaseValues.Num())
	{
		return FindPin(DefaultPinName, EGPD_Output);
	}
	return FindPin(GetBranchPinName(Branch), EGPD_Output);
}

int32 UBangoEdGraphNode::GetCaseOfPin(const UEdGraphPin* Pin) const
{
	for (int32 Case = 0; HasCaseRows() && Pin && Case < CaseValues.Num(); ++Case)
	{
		if (Pin->PinName == GetBranchPinName(Case) && Pin->Direction == EGPD_Output)
		{
			return Case;
		}
	}
	return INDEX_NONE;
}

void UBangoEdGraphNode::AddSwitchCase()
{
	if (!HasCaseRows())
	{
		return;
	}
	const FScopedTransaction Transaction(IsEntries() ? LOCTEXT("AddEntry", "Add Entry") : LOCTEXT("AddSwitchCase", "Add Switch Case"));
	Modify();
	AddCaseRow();
	if (HasDefaultEntry() && CaseValues.Num() >= 2)
	{
		// The default stays last: the new entry goes just above it (drawn in its row if it would be, there).
		SwapEntryRows(CaseValues.Num() - 2, CaseValues.Num() - 1);
		EntryRows[CaseValues.Num() - 2].bInRow = WouldDrawEntryInRow(CaseValues.Num() - 2);
	}
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

bool UBangoEdGraphNode::CanAddDefaultEntry() const
{
	if (!IsEntries() || bEntryDefault)
	{
		return false;
	}
	for (int32 Entry = 0; Entry < CaseValues.Num(); ++Entry)
	{
		if (CaseValues[Entry].IsEmpty() && !(EntryDisabled.IsValidIndex(Entry) && EntryDisabled[Entry]))
		{
			return false; // an `entry:` always passes, so a default would never run
		}
	}
	return true;
}

void UBangoEdGraphNode::AddDefaultEntry()
{
	if (!CanAddDefaultEntry())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddDefaultEntry", "Add Default"));
	Modify();
	AddCaseRow();
	bEntryDefault = true;
	EntryRows.Last().bInRow = WouldDrawEntryInRow(CaseValues.Num() - 1); // (a default that may hold anything is nodes)
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::AddCaseRow()
{
	const int32 Case = CaseValues.Num();
	CaseValues.Add(FString());
	if (IsEntries())
	{
		EntryDisabled.SetNum(Case);
		EntryDisabled.Add(false);
		EntryRows.SetNum(Case);
		FBangoEdEntryRow& Row = EntryRows.AddDefaulted_GetRef();
		Row.bInRow = WouldDrawEntryInRow(Case);
	}
	UEdGraphPin* Pin = CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, GetBranchPinName(Case));
	// After the other cases, above Default (an entry: after the other entries, above Then).
	Pins.Remove(Pin);
	const int32 DefaultIndex = Pins.IndexOfByKey(FindPin(IsEntries() ? NextPinName : DefaultPinName, EGPD_Output));
	Pins.Insert(Pin, DefaultIndex == INDEX_NONE ? Pins.Num() : DefaultIndex);
}

void UBangoEdGraphNode::RemoveSwitchCase(int32 Case)
{
	const int32 Count = CaseValues.Num();
	if (!HasCaseRows() || Case < 0 || Case >= Count)
	{
		return;
	}
	const FScopedTransaction Transaction(IsEntries() ? LOCTEXT("RemoveEntry", "Remove Entry") : LOCTEXT("RemoveSwitchCase", "Remove Switch Case"));
	Modify();
	// Each case's wire, without the removed one's: the later cases move up one, wires and all.
	TArray<TArray<UEdGraphPin*>> Links;
	for (int32 i = 0; i < Count; ++i)
	{
		UEdGraphPin* Pin = FindPin(GetBranchPinName(i), EGPD_Output);
		TArray<UEdGraphPin*> Linked = Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>();
		for (UEdGraphPin* Other : Linked)
		{
			if (Other && Other->GetOwningNode())
			{
				Other->GetOwningNode()->Modify();
			}
		}
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
		Links.Add(Linked);
	}
	Links.RemoveAt(Case);
	if (UEdGraphPin* Last = FindPin(GetBranchPinName(Count - 1), EGPD_Output))
	{
		RemovePin(Last);
	}
	if (EntryRows.IsValidIndex(Case))
	{
		// Its statements' pins go too (their wires' nodes stay, unwired).
		for (const FBangoEdStatement& Statement : EntryRows[Case].Statements)
		{
			if (Statement.HasBlock())
			{
				RemoveStatementPin(Statement.PinKey);
			}
		}
		EntryRows.RemoveAt(Case);
	}
	if (IsDefaultEntry(Case))
	{
		bEntryDefault = false;
	}
	CaseValues.RemoveAt(Case);
	if (EntryDisabled.IsValidIndex(Case))
	{
		EntryDisabled.RemoveAt(Case);
	}
	for (int32 i = 0; i < Links.Num(); ++i)
	{
		if (UEdGraphPin* Pin = FindPin(GetBranchPinName(i), EGPD_Output))
		{
			for (UEdGraphPin* Other : Links[i])
			{
				Pin->MakeLinkTo(Other);
			}
		}
	}
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

bool UBangoEdGraphNode::SetCaseValues(int32 Case, const FString& Values, FString& OutError)
{
	if (!HasCaseRows() || !CaseValues.IsValidIndex(Case))
	{
		return false;
	}
	const FString Trimmed = Values.TrimStartAndEnd();
	if (IsDefaultEntry(Case) && !Trimmed.IsEmpty())
	{
		OutError = TEXT("The default has no condition: it passes when none of the other entries does.");
		return false;
	}
	if (IsEntries() ? !Bango::ValidateEntryCondition(Trimmed, OutError) : (!Trimmed.IsEmpty() && !Bango::ValidateCaseValues(Trimmed, OutError)))
	{
		return false;
	}
	if (Trimmed.Equals(CaseValues[Case], ESearchCase::CaseSensitive))
	{
		return true;
	}
	const FScopedTransaction Transaction(IsEntries() ? LOCTEXT("EditEntryCondition", "Edit Entry Condition") : LOCTEXT("EditSwitchCase", "Edit Switch Case"));
	Modify();
	CaseValues[Case] = Trimmed;
	UpdateLayout();
	return true;
}

bool UBangoEdGraphNode::TakesEntries() const
{
	const FBangoCommandSignature* Signature = GetKind() == EBangoGraphNodeKind::BlockCall && LayoutContext.IsValid() && LayoutContext->Commands
		? LayoutContext->Commands->FindCommand(Layout.CallName) : nullptr;
	return Signature && Signature->bEntries;
}

bool UBangoEdGraphNode::TakesOptionalEntries() const
{
	const FBangoCommandSignature* Signature = GetKind() == EBangoGraphNodeKind::BlockCall && LayoutContext.IsValid() && LayoutContext->Commands
		? LayoutContext->Commands->FindCommand(Layout.CallName) : nullptr;
	return Signature && Signature->bEntries && Signature->bEntriesOptional;
}

void UBangoEdGraphNode::MoveEntry(int32 Entry, int32 Delta)
{
	const int32 Other = Entry + Delta;
	if (!IsEntries() || !CaseValues.IsValidIndex(Entry) || !CaseValues.IsValidIndex(Other) || Entry == Other || IsDefaultEntry(Entry) || IsDefaultEntry(Other))
	{
		return; // the default stays last
	}
	const FScopedTransaction Transaction(Delta < 0 ? LOCTEXT("MoveEntryUp", "Move Entry Up") : LOCTEXT("MoveEntryDown", "Move Entry Down"));
	Modify();
	SwapEntryRows(Entry, Other);
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::SwapEntryRows(int32 Entry, int32 Other)
{
	UEdGraphPin* PinA = FindPin(GetBranchPinName(Entry), EGPD_Output);
	UEdGraphPin* PinB = FindPin(GetBranchPinName(Other), EGPD_Output);
	if (!PinA || !PinB)
	{
		return;
	}
	// The two rows swap, wires and all.
	TArray<UEdGraphPin*> LinksA = PinA->LinkedTo;
	TArray<UEdGraphPin*> LinksB = PinB->LinkedTo;
	for (UEdGraphPin* Linked : LinksA)
	{
		if (Linked && Linked->GetOwningNode())
		{
			Linked->GetOwningNode()->Modify();
		}
	}
	for (UEdGraphPin* Linked : LinksB)
	{
		if (Linked && Linked->GetOwningNode())
		{
			Linked->GetOwningNode()->Modify();
		}
	}
	PinA->BreakAllPinLinks();
	PinB->BreakAllPinLinks();
	for (UEdGraphPin* Linked : LinksB)
	{
		PinA->MakeLinkTo(Linked);
	}
	for (UEdGraphPin* Linked : LinksA)
	{
		PinB->MakeLinkTo(Linked);
	}
	CaseValues.Swap(Entry, Other);
	EntryDisabled.SetNum(CaseValues.Num());
	EntryDisabled.Swap(Entry, Other);
	EntryRows.SetNum(CaseValues.Num());
	EntryRows.Swap(Entry, Other);
}

void UBangoEdGraphNode::SetEntryDisabled(int32 Entry, bool bDisabled)
{
	if (!IsEntries() || !CaseValues.IsValidIndex(Entry))
	{
		return;
	}
	EntryDisabled.SetNum(CaseValues.Num());
	if (EntryDisabled[Entry] == bDisabled)
	{
		return;
	}
	const FScopedTransaction Transaction(bDisabled ? LOCTEXT("DisableEntry", "Disable Entry") : LOCTEXT("EnableEntry", "Enable Entry"));
	Modify();
	EntryDisabled[Entry] = bDisabled;
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::UseEntries()
{
	if (GetKind() != EBangoGraphNodeKind::BlockCall || bEntries || !TakesOptionalEntries())
	{
		return;
	}
	// Body and the first entry's output are the same pin (Branch0), so its wire stays: what was in the block is the entry's.
	const FScopedTransaction Transaction(LOCTEXT("UseEntries", "Use Entries"));
	Modify();
	bEntries = true;
	bEntryDefault = false;
	CaseValues = { FString() };
	EntryDisabled = { false };
	EntryRows.SetNum(1); // its nodes stay nodes, wired from the entry's output
	EntryRows[0] = FBangoEdEntryRow();
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::UsePlainBlock()
{
	if (!CanUsePlainBlock())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("UsePlainBlock", "Use a Plain Block"));
	Modify();
	bEntries = false;
	bEntryDefault = false;
	CaseValues.Reset();
	EntryDisabled.Reset();
	EntryRows.Reset();
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::AddSequenceOutput()
{
	if (!IsSequence())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddSequenceOutput", "Add Sequence Output"));
	Modify();
	const int32 Branch = GetBranchCount();
	SequenceOutputs = Branch + 1;
	UEdGraphPin* Pin = CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, GetBranchPinName(Branch));
	// Above Then, after the other outputs.
	Pins.Remove(Pin);
	const int32 NextIndex = Pins.IndexOfByKey(GetNextPin());
	Pins.Insert(Pin, NextIndex == INDEX_NONE ? Pins.Num() : NextIndex);
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::RemoveSequenceOutput(int32 Branch)
{
	const int32 Count = GetBranchCount();
	if (!CanRemoveSequenceOutput() || Branch < 0 || Branch >= Count)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("RemoveSequenceOutput", "Remove Sequence Output"));
	Modify();
	// Each output's wire, without the removed one's: the later outputs move up one, wires and all.
	TArray<TArray<UEdGraphPin*>> Links;
	for (int32 i = 0; i < Count; ++i)
	{
		UEdGraphPin* Pin = GetBranchPin(i);
		TArray<UEdGraphPin*> Linked = Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>();
		for (UEdGraphPin* Other : Linked)
		{
			if (Other && Other->GetOwningNode())
			{
				Other->GetOwningNode()->Modify();
			}
		}
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
		Links.Add(Linked);
	}
	Links.RemoveAt(Branch);
	if (UEdGraphPin* Last = GetBranchPin(Count - 1))
	{
		RemovePin(Last);
	}
	SequenceOutputs = Count - 1;
	for (int32 i = 0; i < Links.Num(); ++i)
	{
		if (UEdGraphPin* Pin = GetBranchPin(i))
		{
			for (UEdGraphPin* Other : Links[i])
			{
				Pin->MakeLinkTo(Other);
			}
		}
	}
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UBangoEdGraphNode::AllocateDefaultPins()
{
	if (!IsEntry())
	{
		CreatePin(EGPD_Input, UBangoEdGraphSchema::PC_BangoExec, InPinName);
	}
	// The outputs of statements drawn in rows come first, so the node has their pins ready when it draws the rows.
	for (int32 Entry = 0; IsEntries() && Entry < EntryRows.Num(); ++Entry)
	{
		for (const FBangoEdStatement& Statement : EntryRows[Entry].bInRow ? EntryRows[Entry].Statements : TArray<FBangoEdStatement>())
		{
			if (Statement.HasBlock())
			{
				CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, GetStatementPinName(Statement.PinKey));
			}
		}
	}
	for (int32 Branch = 0; Branch < GetBranchCount(); ++Branch)
	{
		const bool bDefault = IsSwitch() && Branch == CaseValues.Num();
		CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, bDefault ? DefaultPinName : GetBranchPinName(Branch));
	}
	CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, NextPinName);
}

void UBangoEdGraphNode::UpdateLayout()
{
	FBangoGraphNode Source;
	Source.Kind = GetKind();
	Source.Text = Text;
	Source.NodeID = IsEntry() ? 0 : NodeID; // the layout context's variable types are kept by NodeID
	Layout = LayoutContext.IsValid() ? Bango::GetNodeLayout(Source, bInFunctionGraph, *LayoutContext, bShowExpression) : Bango::GetNodeLayout(Source, bInFunctionGraph, FBangoNodeLayoutContext(), bShowExpression);
	ErrorField = INDEX_NONE;
	ErrorCase = INDEX_NONE;
	FieldWarnings.Reset();
	for (FBangoEdEntryRow& Row : EntryRows)
	{
		for (FBangoEdStatement& Statement : Row.Statements)
		{
			FBangoGraphNode StatementSource;
			StatementSource.Kind = Statement.GetKind();
			StatementSource.Text = Statement.Text;
			StatementSource.NodeID = Statement.NodeID;
			Statement.Layout = Bango::GetNodeLayout(StatementSource, bInFunctionGraph, LayoutContext.IsValid() ? *LayoutContext : FBangoNodeLayoutContext(), false);
			Statement.ErrorMessage = Statement.Layout.Error;
		}
	}
	bHasCompilerMessage = !Layout.Error.IsEmpty();
	ErrorType = EMessageSeverity::Error;
	ErrorMsg = Layout.Error;
}

bool UBangoEdGraphNode::SetLocNamespace(const FString& Typed)
{
	const FString NewNamespace = Typed.TrimStartAndEnd();
	const bool bHas = !NewNamespace.IsEmpty();
	if (bHas == bHasLocNamespace && NewNamespace.Equals(LocNamespace, ESearchCase::CaseSensitive))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditLocNamespace", "Edit Localization Namespace"));
	Modify();
	bHasLocNamespace = bHas;
	LocNamespace = NewNamespace;
	return true;
}

bool UBangoEdGraphNode::SetFieldValue(int32 FieldIndex, const FString& Value, FString& OutError)
{
	FBangoGraphNode Edited;
	Edited.Kind = GetKind();
	Edited.Text = Text;
	Edited.NodeID = IsEntry() ? 0 : NodeID;
	const FBangoNodeLayoutContext NoContext;
	if (!Bango::SetNodeField(Edited, bInFunctionGraph, LayoutContext.IsValid() ? *LayoutContext : NoContext, FieldIndex, Value, OutError, bShowExpression))
	{
		return false;
	}
	if (Edited.Text.Equals(Text, ESearchCase::CaseSensitive) && Edited.Kind == GetKind())
	{
		return true;
	}

	const FScopedTransaction Transaction(LOCTEXT("EditField", "Edit Bango Node"));
	Modify();
	Text = Edited.Text;
	KindValue = (uint8)Edited.Kind;
	// Only this node's widget needs rebuilding, and it does that itself after an edit (SBangoGraphNode::RefreshSoon).
	// Other nodes whose look depends on this one (a Set node showing a variable's type) are updated by the editor
	// when it sees what they can see has changed.
	UpdateLayout();
	return true;
}

void UBangoEdGraphNode::SetCompileError(const FString& Message, int32 FieldIndex, int32 CaseIndex)
{
	bHasCompilerMessage = true;
	ErrorType = EMessageSeverity::Error;
	ErrorMsg = Message;
	ErrorField = FieldIndex;
	ErrorCase = CaseIndex;
}

int32 UBangoEdGraphNode::UpdateCompileWarnings()
{
	Bango::GetNodeWarnings(Layout, FieldWarnings);
	if (FieldWarnings.Num() > 0 && !bHasCompilerMessage)
	{
		TArray<FString> Messages;
		for (const Bango::FBangoFieldWarning& Warning : FieldWarnings)
		{
			Messages.Add(Warning.Message);
		}
		bHasCompilerMessage = true;
		ErrorType = EMessageSeverity::Warning;
		ErrorMsg = FString::Join(Messages, TEXT("\n"));
	}
	return FieldWarnings.Num();
}

bool UBangoEdGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema && Schema->IsA<UBangoEdGraphSchema>();
}

bool UBangoEdGraphNode::CanPasteHere(const UEdGraph* TargetGraph) const
{
	return !IsEntry() && TargetGraph && CanCreateUnderSpecifiedSchema(TargetGraph->GetSchema());
}

void UBangoEdGraphNode::PostEditUndo()
{
	Super::PostEditUndo();
	UpdateLayout(); // Text may have changed back
}

void UBangoEdGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	if (!Menu || !Context)
	{
		return;
	}
	if (IsSequence())
	{
		// A sequence's outputs: add one at the end; right-clicking an output removes it.
		UBangoEdGraphNode* MutableThis = const_cast<UBangoEdGraphNode*>(this);
		FToolMenuSection& Section = Menu->AddSection(TEXT("BangoSequence"), LOCTEXT("SequenceSection", "Sequence"));
		Section.AddMenuEntry(TEXT("AddSequenceOutput"), LOCTEXT("AddOutput", "Add Output"),
			LOCTEXT("AddOutputTooltip", "Adds a scope after the others: another block, run after them and before Then."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->AddSequenceOutput(); })));
		int32 Branch = INDEX_NONE;
		for (int32 i = 0; Context->Pin && i < GetBranchCount(); ++i)
		{
			if (Context->Pin->PinName == GetBranchPinName(i))
			{
				Branch = i;
			}
		}
		if (Branch != INDEX_NONE)
		{
			Section.AddMenuEntry(TEXT("RemoveSequenceOutput"), FText::Format(LOCTEXT("RemoveOutput", "Remove Scope {0}"), FText::AsNumber(Branch)),
				LOCTEXT("RemoveOutputTooltip", "Removes this scope and its wire (the nodes on it stay, unwired). The scopes after it move up."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([MutableThis, Branch]() { MutableThis->RemoveSequenceOutput(Branch); }),
					FCanExecuteAction::CreateLambda([MutableThis]() { return MutableThis->CanRemoveSequenceOutput(); })));
		}
	}
	if (IsSwitch())
	{
		// A switch's cases: add one before Default; right-clicking a case's output removes it.
		UBangoEdGraphNode* MutableThis = const_cast<UBangoEdGraphNode*>(this);
		FToolMenuSection& Section = Menu->AddSection(TEXT("BangoSwitch"), LOCTEXT("SwitchSection", "Switch"));
		Section.AddMenuEntry(TEXT("AddSwitchCase"), LOCTEXT("AddCase", "Add Case"),
			LOCTEXT("AddCaseTooltip", "Adds a case after the others, before Default. Type its value (or values, separated by commas) in its box."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->AddSwitchCase(); })));
		const int32 Case = GetCaseOfPin(Context->Pin);
		if (Case != INDEX_NONE)
		{
			const FString Values = CaseValues[Case];
			Section.AddMenuEntry(TEXT("RemoveSwitchCase"), Values.IsEmpty() ? LOCTEXT("RemoveEmptyCase", "Remove Case") : FText::Format(LOCTEXT("RemoveCase", "Remove Case {0}"), FText::FromString(Values)),
				LOCTEXT("RemoveCaseTooltip", "Removes this case and its wire (the nodes on it stay, unwired). The cases after it move up."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([MutableThis, Case]() { MutableThis->RemoveSwitchCase(Case); })));
		}
	}
	if (GetKind() == EBangoGraphNodeKind::BlockCall && (IsEntries() || TakesOptionalEntries()))
	{
		// A command's entries: add one at the end; right-clicking an entry's output removes, moves or disables it.
		UBangoEdGraphNode* MutableThis = const_cast<UBangoEdGraphNode*>(this);
		FToolMenuSection& Section = Menu->AddSection(TEXT("BangoEntries"), LOCTEXT("EntriesSection", "Entries"));
		if (IsEntries())
		{
			Section.AddMenuEntry(TEXT("AddEntry"), LOCTEXT("AddEntryItem", "Add Entry"),
				LOCTEXT("AddEntryTooltip", "Adds an entry after the others (above the default, if there's one). Type its condition in its box, or leave it empty for one that always passes."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->AddSwitchCase(); })));
			if (!bEntryDefault)
			{
				Section.AddMenuEntry(TEXT("AddDefaultEntry"), LOCTEXT("AddDefaultEntryItem", "Add Default"),
					LOCTEXT("AddDefaultEntryTooltip", "Adds default: at the end, which passes when none of the other entries does. Not with an entry that has no condition: that one always passes, so the default would never run."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->AddDefaultEntry(); }),
						FCanExecuteAction::CreateLambda([MutableThis]() { return MutableThis->CanAddDefaultEntry(); })));
			}
			// Remove, move or disable one entry (right-clicking its output; or, for an entry drawn in its row, whose
			// output is hidden, from the node's menu).
			const auto AddEntryItems = [this, MutableThis](FToolMenuSection& Into, int32 Entry)
			{
				const bool bEntryDisabled = EntryDisabled.IsValidIndex(Entry) && EntryDisabled[Entry];
				const bool bIsDefault = IsDefaultEntry(Entry);
				Into.AddMenuEntry(TEXT("RemoveEntry"), bIsDefault ? LOCTEXT("RemoveDefaultEntryItem", "Remove Default") : FText::Format(LOCTEXT("RemoveEntryItem", "Remove Entry {0}"), FText::AsNumber(Entry)),
					LOCTEXT("RemoveEntryTooltip", "Removes this entry and its wire (the nodes on it stay, unwired). The entries after it move up."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([MutableThis, Entry]() { MutableThis->RemoveSwitchCase(Entry); })));
				const int32 Count = CaseValues.Num();
				const int32 Movable = HasDefaultEntry() ? Count - 1 : Count; // the default stays last
				if (!bIsDefault)
				{
					Into.AddMenuEntry(TEXT("MoveEntryUp"), LOCTEXT("MoveEntryUpItem", "Move Entry Up"),
						LOCTEXT("MoveEntryUpTooltip", "Swaps this entry with the one above it, wires and all. The order matters to a command that runs the first that passes, or each in turn."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([MutableThis, Entry]() { MutableThis->MoveEntry(Entry, -1); }),
							FCanExecuteAction::CreateLambda([Entry]() { return Entry > 0; })));
					Into.AddMenuEntry(TEXT("MoveEntryDown"), LOCTEXT("MoveEntryDownItem", "Move Entry Down"),
						LOCTEXT("MoveEntryDownTooltip", "Swaps this entry with the one below it, wires and all. (The default stays last.)"), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([MutableThis, Entry]() { MutableThis->MoveEntry(Entry, 1); }),
							FCanExecuteAction::CreateLambda([Entry, Movable]() { return Entry + 1 < Movable; })));
				}
				Into.AddMenuEntry(TEXT("ToggleEntryDisabled"), bEntryDisabled ? LOCTEXT("EnableEntryItem", "Enable Entry") : LOCTEXT("DisableEntryItem", "Disable Entry"),
					LOCTEXT("ToggleEntryDisabledTooltip", "A disabled entry is left out: its condition isn't worked out, and the command isn't told of it."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([MutableThis, Entry, bEntryDisabled]() { MutableThis->SetEntryDisabled(Entry, !bEntryDisabled); })));
			};
			const int32 PinEntry = GetCaseOfPin(Context->Pin);
			if (PinEntry != INDEX_NONE)
			{
				AddEntryItems(Section, PinEntry);
			}
			for (int32 RowEntry = 0; !Context->Pin && RowEntry < CaseValues.Num(); ++RowEntry)
			{
				if (!IsEntryInRow(RowEntry))
				{
					continue;
				}
				Section.AddSubMenu(FName(*FString::Printf(TEXT("Entry%d"), RowEntry)),
					IsDefaultEntry(RowEntry) ? LOCTEXT("DefaultEntrySubMenu", "Default") : FText::Format(LOCTEXT("EntrySubMenu", "Entry {0}"), FText::AsNumber(RowEntry)),
					LOCTEXT("EntrySubMenuTooltip", "Remove, move or disable this entry."),
					FNewToolMenuDelegate::CreateLambda([AddEntryItems, RowEntry](UToolMenu* SubMenu)
					{
						FToolMenuSection& SubSection = SubMenu->AddSection(TEXT("BangoEntry"));
						AddEntryItems(SubSection, RowEntry);
					}));
			}
			if (TakesOptionalEntries())
			{
				Section.AddMenuEntry(TEXT("UsePlainBlock"), LOCTEXT("UsePlainBlockItem", "Use a Plain Block"),
					LOCTEXT("UsePlainBlockTooltip", "Its one entry, which has no condition, becomes an ordinary block holding what was in it."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->UsePlainBlock(); }),
						FCanExecuteAction::CreateLambda([MutableThis]() { return MutableThis->CanUsePlainBlock(); })));
			}
		}
		else
		{
			Section.AddMenuEntry(TEXT("UseEntries"), LOCTEXT("UseEntriesItem", "Use Entries"),
				LOCTEXT("UseEntriesTooltip", "Its block becomes one entry, with no condition, holding what was in it. Add more entries after it."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([MutableThis]() { MutableThis->UseEntries(); })));
		}
	}
	if (Context->Pin || !CanBeDisabled())
	{
		return;
	}
	// The command works on the selected nodes: it turns them off unless they're all off already. The label says
	// which it will do (the right-clicked node is selected by then; if not, it's the only one counted).
	int32 Count = 0;
	bool bAnyEnabled = false;
	if (const TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Context->Graph))
	{
		for (UObject* Selected : GraphEditor->GetSelectedNodes())
		{
			const UBangoEdGraphNode* Node = Cast<UBangoEdGraphNode>(Selected);
			if (Node && Node->CanBeDisabled())
			{
				++Count;
				bAnyEnabled |= !Node->IsDisabledNode();
			}
		}
	}
	if (Count == 0)
	{
		Count = 1;
		bAnyEnabled = !IsDisabledNode();
	}
	const FText Label = bAnyEnabled
		? (Count == 1 ? LOCTEXT("DisableNode", "Disable Node") : LOCTEXT("DisableNodes", "Disable Nodes"))
		: (Count == 1 ? LOCTEXT("EnableNode", "Enable Node") : LOCTEXT("EnableNodes", "Enable Nodes"));
	FToolMenuSection& Section = Menu->AddSection(TEXT("BangoNode"), LOCTEXT("BangoNodeSection", "Bango Node"));
	Section.AddMenuEntry(FBangoGraphCommands::Get().ToggleNodesDisabled, Label);
}

// ---- Entries drawn in their rows ----

FName UBangoEdGraphNode::GetStatementPinName(int32 PinKey)
{
	return FName(*FString::Printf(TEXT("Statement%d"), PinKey));
}

bool UBangoEdGraphNode::GetEntryHold(FBangoEntryHold& OutHold) const
{
	const FBangoCommandSignature* Signature = GetKind() == EBangoGraphNodeKind::BlockCall && LayoutContext.IsValid() && LayoutContext->Commands
		? LayoutContext->Commands->FindCommand(Layout.CallName) : nullptr;
	if (!Signature || !Signature->bEntries || Signature->EntriesHoldCommands.IsEmpty())
	{
		return false;
	}
	OutHold = FBangoEntryHold::FromSignature(*Signature);
	return true;
}

bool UBangoEdGraphNode::WouldDrawEntryInRow(int32 Entry) const
{
	FBangoEntryHold Hold;
	const TSharedPtr<IBangoNodeDecorator> Decorator = IsEntries() ? GetNodeDecorator() : nullptr;
	if (!Decorator.IsValid() || !GetEntryHold(Hold) || (Hold.bDefaultHoldsAnything && IsDefaultEntry(Entry)))
	{
		return false;
	}
	return Decorator->DrawsEntryStatements(MakeDecoratorContext(), Entry);
}

bool UBangoEdGraphNode::FindStatementOfPin(const UEdGraphPin* Pin, int32& OutEntry, int32& OutIndex) const
{
	if (!Pin || Pin->Direction != EGPD_Output || !IsEntries())
	{
		return false;
	}
	for (int32 Entry = 0; Entry < EntryRows.Num(); ++Entry)
	{
		for (int32 Index = 0; EntryRows[Entry].bInRow && Index < EntryRows[Entry].Statements.Num(); ++Index)
		{
			const FBangoEdStatement& Statement = EntryRows[Entry].Statements[Index];
			if (Statement.HasBlock() && Pin->PinName == GetStatementPinName(Statement.PinKey))
			{
				OutEntry = Entry;
				OutIndex = Index;
				return true;
			}
		}
	}
	return false;
}

UEdGraphPin* UBangoEdGraphNode::GetStatementPin(int32 Entry, int32 Index) const
{
	if (!IsEntryInRow(Entry) || !EntryRows[Entry].Statements.IsValidIndex(Index) || !EntryRows[Entry].Statements[Index].HasBlock())
	{
		return nullptr;
	}
	return FindPin(GetStatementPinName(EntryRows[Entry].Statements[Index].PinKey), EGPD_Output);
}

bool UBangoEdGraphNode::FindStatement(int64 InNodeID, int32& OutEntry, int32& OutIndex) const
{
	for (int32 Entry = 0; InNodeID > 0 && Entry < EntryRows.Num(); ++Entry)
	{
		for (int32 Index = 0; EntryRows[Entry].bInRow && Index < EntryRows[Entry].Statements.Num(); ++Index)
		{
			if (EntryRows[Entry].Statements[Index].NodeID == InNodeID)
			{
				OutEntry = Entry;
				OutIndex = Index;
				return true;
			}
		}
	}
	return false;
}

void UBangoEdGraphNode::SetStatementError(int32 Entry, int32 Index, const FString& Message)
{
	if (IsEntryInRow(Entry) && EntryRows[Entry].Statements.IsValidIndex(Index))
	{
		EntryRows[Entry].Statements[Index].ErrorMessage = Message;
	}
	SetCompileError(Message, INDEX_NONE);
}

UEdGraphPin* UBangoEdGraphNode::CreateStatementPin(int32 PinKey)
{
	UEdGraphPin* Pin = CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, GetStatementPinName(PinKey));
	// Statement outputs go just after the input (see AllocateDefaultPins).
	Pins.Remove(Pin);
	int32 At = 0;
	while (Pins.IsValidIndex(At) && (Pins[At]->Direction == EGPD_Input || Pins[At]->PinName.ToString().StartsWith(TEXT("Statement"))))
	{
		++At;
	}
	Pins.Insert(Pin, At);
	return Pin;
}

void UBangoEdGraphNode::RemoveStatementPin(int32 PinKey)
{
	if (UEdGraphPin* Pin = FindPin(GetStatementPinName(PinKey), EGPD_Output))
	{
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNode())
			{
				Linked->GetOwningNode()->Modify();
			}
		}
		Pin->BreakAllPinLinks();
		RemovePin(Pin);
	}
}

bool UBangoEdGraphNode::InsertEntryStatement(int32 Entry, int32 Index, const FString& Statement, FString& OutError)
{
	FBangoEntryHold Hold;
	if (!IsEntryInRow(Entry) || !GetEntryHold(Hold))
	{
		OutError = TEXT("That entry isn't drawn in its row.");
		return false;
	}
	FBangoGraphNode Node;
	if (!Bango::ValidateEntryStatement(Statement, Hold, Node, OutError, LayoutContext.IsValid() ? LayoutContext->Commands : nullptr))
	{
		return false;
	}
	TArray<FBangoEdStatement>& Statements = EntryRows[Entry].Statements;
	if (!Hold.bMany && Statements.Num() > 0)
	{
		OutError = FString::Printf(TEXT("A %s's entries each hold one %s: remove the one there first."), *Layout.CallName, *Bango::GetNodeCommandName(Node));
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("InsertEntryStatement", "Add to Entry"));
	Modify();
	FBangoEdStatement Added;
	Added.KindValue = (uint8)Node.Kind;
	Added.Text = Node.Text;
	// Where BuildGraph would place it: a step right of the node, a step down per entry, and a little per line.
	Index = FMath::Clamp(Index, 0, Statements.Num());
	Added.X = FMath::RoundToInt((float)NodePosX / BangoGraphGridSize) + 20;
	Added.Y = FMath::RoundToInt((float)NodePosY / BangoGraphGridSize) + 10 * (Entry + 1) + 2 * Index;
	if (Added.HasBlock())
	{
		Added.PinKey = NextStatementPinKey++;
		CreateStatementPin(Added.PinKey);
	}
	Statements.Insert(Added, Index);
	FocusStatementEntry = Entry;
	FocusStatementIndex = Index;
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	return true;
}

bool UBangoEdGraphNode::RemoveEntryStatement(int32 Entry, int32 Index, FString& OutError)
{
	if (!IsEntryInRow(Entry) || !EntryRows[Entry].Statements.IsValidIndex(Index))
	{
		OutError = TEXT("That entry has no such statement.");
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("RemoveEntryStatement", "Remove from Entry"));
	Modify();
	const FBangoEdStatement& Statement = EntryRows[Entry].Statements[Index];
	if (Statement.HasBlock())
	{
		RemoveStatementPin(Statement.PinKey);
	}
	EntryRows[Entry].Statements.RemoveAt(Index);
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	return true;
}

bool UBangoEdGraphNode::MoveEntryStatement(int32 FromEntry, int32 FromIndex, int32 ToEntry, int32 ToIndex, FString& OutError)
{
	FBangoEntryHold Hold;
	if (!IsEntryInRow(FromEntry) || !IsEntryInRow(ToEntry) || !EntryRows[FromEntry].Statements.IsValidIndex(FromIndex) || !GetEntryHold(Hold))
	{
		OutError = TEXT("Statements can only be moved between entries drawn in their rows.");
		return false;
	}
	if (FromEntry != ToEntry && !Hold.bMany && EntryRows[ToEntry].Statements.Num() > 0)
	{
		OutError = FString::Printf(TEXT("A %s's entries each hold one: that entry has its one already."), *Layout.CallName);
		return false;
	}
	if (FromEntry == ToEntry && FromIndex == FMath::Clamp(ToIndex, 0, EntryRows[ToEntry].Statements.Num() - 1))
	{
		return true; // already there
	}
	const FScopedTransaction Transaction(LOCTEXT("MoveEntryStatement", "Move in Entries"));
	Modify();
	const FBangoEdStatement Moved = EntryRows[FromEntry].Statements[FromIndex];
	EntryRows[FromEntry].Statements.RemoveAt(FromIndex);
	TArray<FBangoEdStatement>& Target = EntryRows[ToEntry].Statements;
	ToIndex = FMath::Clamp(ToIndex, 0, Target.Num());
	Target.Insert(Moved, ToIndex); // its pin (by its key) and wire go with it
	FocusStatementEntry = ToEntry;
	FocusStatementIndex = ToIndex;
	UpdateLayout();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	return true;
}

bool UBangoEdGraphNode::SetEntryStatementDisabled(int32 Entry, int32 Index, bool bDisabled, FString& OutError)
{
	if (!IsEntryInRow(Entry) || !EntryRows[Entry].Statements.IsValidIndex(Index))
	{
		OutError = TEXT("That entry has no such statement.");
		return false;
	}
	if (EntryRows[Entry].Statements[Index].bDisabled == bDisabled)
	{
		return true;
	}
	const FScopedTransaction Transaction(bDisabled ? LOCTEXT("DisableEntryStatement", "Disable in Entry") : LOCTEXT("EnableEntryStatement", "Enable in Entry"));
	Modify();
	EntryRows[Entry].Statements[Index].bDisabled = bDisabled;
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	return true;
}

bool UBangoEdGraphNode::SetStatementFieldValue(int32 Entry, int32 Index, int32 FieldIndex, const FString& Value, FString& OutError)
{
	FBangoEntryHold Hold;
	if (!IsEntryInRow(Entry) || !EntryRows[Entry].Statements.IsValidIndex(Index) || !GetEntryHold(Hold))
	{
		OutError = TEXT("That entry has no such statement.");
		return false;
	}
	FBangoEdStatement& Statement = EntryRows[Entry].Statements[Index];
	FBangoGraphNode Edited;
	Edited.Kind = Statement.GetKind();
	Edited.Text = Statement.Text;
	Edited.NodeID = Statement.NodeID;
	const FBangoNodeLayoutContext NoContext;
	if (!Bango::SetNodeField(Edited, bInFunctionGraph, LayoutContext.IsValid() ? *LayoutContext : NoContext, FieldIndex, Value, OutError))
	{
		return false;
	}
	if (Edited.Text.Equals(Statement.Text, ESearchCase::CaseSensitive) && Edited.Kind == Statement.GetKind())
	{
		return true;
	}
	if (Edited.Kind != Statement.GetKind() || !Hold.Commands.Contains(Bango::GetNodeCommandName(Edited)))
	{
		OutError = TEXT("That would make it something its entry can't hold.");
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditEntryStatement", "Edit Bango Node"));
	Modify();
	Statement.Text = Edited.Text;
	UpdateLayout();
	return true;
}

TSharedPtr<IBangoNodeDecorator> UBangoEdGraphNode::GetNodeDecorator() const
{
	const bool bCallsCommand = Layout.Style == EBangoNodeStyle::Command || GetKind() == EBangoGraphNodeKind::BlockCall;
	return bCallsCommand && !IsEntry() ? Bango::FindNodeDecorator(Layout.CallName) : nullptr;
}

FName UBangoEdGraphNode::GetDecoratorPinName(const UEdGraphPin* Pin) const
{
	if (!Pin || Pin->GetOwningNodeUnchecked() != this)
	{
		return NAME_None;
	}
	if (Pin->Direction == EGPD_Input)
	{
		return Pin->PinName == InPinName ? Bango::NodePins::In : NAME_None;
	}
	if (Pin->PinName == NextPinName)
	{
		return Bango::NodePins::Then;
	}
	int32 Entry = INDEX_NONE;
	int32 Index = INDEX_NONE;
	if (FindStatementOfPin(Pin, Entry, Index))
	{
		return Bango::NodePins::Statement(Entry, Index);
	}
	if (IsEntries())
	{
		Entry = GetCaseOfPin(Pin);
		return Entry == INDEX_NONE ? NAME_None : IsDefaultEntry(Entry) ? Bango::NodePins::Default : Bango::NodePins::Entry(Entry);
	}
	if (GetKind() == EBangoGraphNodeKind::BlockCall && Pin->PinName == GetBranchPinName(0))
	{
		return Bango::NodePins::Body;
	}
	return NAME_None;
}

UEdGraphPin* UBangoEdGraphNode::FindDecoratorPin(FName Name) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Name.IsNone() && GetDecoratorPinName(Pin) == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

FBangoNodeDecoratorContext UBangoEdGraphNode::MakeDecoratorContext() const
{
	FBangoNodeDecoratorContext Context;
	Context.CommandName = Layout.CallName;
	Context.Layout = &Layout;
	Context.NodeText = Text;
	Context.bBlock = GetKind() == EBangoGraphNodeKind::BlockCall;
	Context.bDisabled = IsDisabledNode();
	Context.Comment = NodeComment;
	Context.ErrorMessage = bHasCompilerMessage && ErrorType == EMessageSeverity::Error ? ErrorMsg : FString();
	TWeakObjectPtr<UBangoEdGraphNode> WeakNode(const_cast<UBangoEdGraphNode*>(this));
	Context.SetFieldValue = [WeakNode](int32 FieldIndex, const FString& Value, FString& OutError)
	{
		UBangoEdGraphNode* Node = WeakNode.Get();
		if (!Node)
		{
			OutError = TEXT("The node is gone.");
			return false;
		}
		return Node->SetFieldValue(FieldIndex, Value, OutError);
	};
	for (const UEdGraphPin* Pin : Pins)
	{
		const FName PinName = GetDecoratorPinName(Pin);
		if (!PinName.IsNone())
		{
			Context.PinNames.Add(PinName);
		}
	}
	Context.bEntries = IsEntries();
	if (Context.bEntries)
	{
		Context.EntryConditions = CaseValues;
		Context.EntryDisabled = EntryDisabled;
		Context.EntryDisabled.SetNum(CaseValues.Num());
		Context.DefaultEntry = HasDefaultEntry() ? CaseValues.Num() - 1 : INDEX_NONE;
		Context.SetEntryCondition = [WeakNode](int32 Entry, const FString& Condition, FString& OutError)
		{
			UBangoEdGraphNode* Node = WeakNode.Get();
			if (!Node)
			{
				OutError = TEXT("The node is gone.");
				return false;
			}
			if (!Node->CaseValues.IsValidIndex(Entry))
			{
				OutError = TEXT("The node has no such entry.");
				return false;
			}
			if (!Node->SetCaseValues(Entry, Condition, OutError))
			{
				return false;
			}
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyNodeChanged(Node); // drawn again, from the new text
			}
			return true;
		};
		Context.bHasEntryDebug = bHasEntryDebug;
		Context.DebugEntryPassed = DebugEntryPassed;
		Context.DebugRunningEntry = DebugRunningEntry;
		Context.RemoveEntry = [WeakNode](int32 Entry) { if (UBangoEdGraphNode* Node = WeakNode.Get()) { Node->RemoveSwitchCase(Entry); } };
		Context.MoveEntry = [WeakNode](int32 Entry, int32 Delta) { if (UBangoEdGraphNode* Node = WeakNode.Get()) { Node->MoveEntry(Entry, Delta); } };
		Context.SetEntryDisabled = [WeakNode](int32 Entry, bool bOff) { if (UBangoEdGraphNode* Node = WeakNode.Get()) { Node->SetEntryDisabled(Entry, bOff); } };
		Context.AddEntry = [WeakNode]() { if (UBangoEdGraphNode* Node = WeakNode.Get()) { Node->AddSwitchCase(); } };
		Context.AddDefaultEntry = [WeakNode]() { if (UBangoEdGraphNode* Node = WeakNode.Get(); Node && Node->CanAddDefaultEntry()) { Node->AddDefaultEntry(); } };
		Context.bCanAddDefaultEntry = CanAddDefaultEntry();

		// The statements of entries drawn in their rows, each told about as a node of its own would be.
		Context.EntryDrawsStatements.Init(false, CaseValues.Num());
		Context.EntryStatements.SetNum(CaseValues.Num());
		for (int32 Entry = 0; Entry < CaseValues.Num(); ++Entry)
		{
			if (!IsEntryInRow(Entry))
			{
				continue;
			}
			Context.EntryDrawsStatements[Entry] = true;
			for (int32 Index = 0; Index < EntryRows[Entry].Statements.Num(); ++Index)
			{
				const FBangoEdStatement& Statement = EntryRows[Entry].Statements[Index];
				const TSharedPtr<FBangoNodeDecoratorContext> StatementContext = MakeShared<FBangoNodeDecoratorContext>();
				StatementContext->CommandName = Statement.Layout.CallName;
				StatementContext->Layout = &Statement.Layout;
				StatementContext->NodeText = Statement.Text;
				StatementContext->bBlock = Statement.HasBlock();
				StatementContext->bDisabled = Statement.bDisabled;
				StatementContext->Comment = Statement.Comment;
				StatementContext->ErrorMessage = Statement.ErrorMessage;
				StatementContext->SetFieldValue = [WeakNode, Entry, Index](int32 FieldIndex, const FString& Value, FString& OutError)
				{
					UBangoEdGraphNode* Node = WeakNode.Get();
					if (!Node)
					{
						OutError = TEXT("The node is gone.");
						return false;
					}
					if (!Node->SetStatementFieldValue(Entry, Index, FieldIndex, Value, OutError))
					{
						return false;
					}
					if (UEdGraph* Graph = Node->GetGraph())
					{
						Graph->NotifyNodeChanged(Node);
					}
					return true;
				};
				Context.EntryStatements[Entry].Add(StatementContext);
			}
		}
		Context.InsertEntryStatement = [WeakNode](int32 Entry, int32 Index, const FString& Statement, FString& OutError)
		{
			UBangoEdGraphNode* Node = WeakNode.Get();
			return Node ? Node->InsertEntryStatement(Entry, Index, Statement, OutError) : false;
		};
		Context.RemoveEntryStatement = [WeakNode](int32 Entry, int32 Index, FString& OutError)
		{
			UBangoEdGraphNode* Node = WeakNode.Get();
			return Node ? Node->RemoveEntryStatement(Entry, Index, OutError) : false;
		};
		Context.MoveEntryStatement = [WeakNode](int32 FromEntry, int32 FromIndex, int32 ToEntry, int32 ToIndex, FString& OutError)
		{
			UBangoEdGraphNode* Node = WeakNode.Get();
			return Node ? Node->MoveEntryStatement(FromEntry, FromIndex, ToEntry, ToIndex, OutError) : false;
		};
		Context.SetEntryStatementDisabled = [WeakNode](int32 Entry, int32 Index, bool bOff, FString& OutError)
		{
			UBangoEdGraphNode* Node = WeakNode.Get();
			return Node ? Node->SetEntryStatementDisabled(Entry, Index, bOff, OutError) : false;
		};
		Context.DebugStatementEntry = DebugStatementEntry;
		Context.DebugStatementIndex = DebugStatementIndex;
		Context.FocusStatementEntry = FocusStatementEntry;
		Context.FocusStatementIndex = FocusStatementIndex;
	}
	return Context;
}

FSlateIcon UBangoEdGraphNode::GetIconAndTint(FLinearColor& OutColor) const
{
	if (const TSharedPtr<IBangoNodeDecorator> Decorator = GetNodeDecorator())
	{
		const FSlateIcon Icon = Decorator->GetTitleIcon(MakeDecoratorContext(), OutColor);
		if (Icon.IsSet())
		{
			return Icon;
		}
	}
	return Super::GetIconAndTint(OutColor);
}

bool UBangoEdGraphNode::ShowPaletteIconOnNode() const
{
	if (const TSharedPtr<IBangoNodeDecorator> Decorator = GetNodeDecorator())
	{
		FLinearColor Tint;
		return Decorator->GetTitleIcon(MakeDecoratorContext(), Tint).IsSet();
	}
	return false;
}

FText UBangoEdGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (const TSharedPtr<IBangoNodeDecorator> Decorator = GetNodeDecorator())
	{
		const TOptional<FText> Title = Decorator->GetTitle(MakeDecoratorContext());
		if (Title.IsSet())
		{
			return Title.GetValue();
		}
	}
	if (!Layout.Title.IsEmpty())
	{
		return FText::FromString(Layout.Title);
	}
	return FText::FromString(IsEntry() ? FString(bInFunctionGraph ? TEXT("Function") : TEXT("Script Start")) : Bango::GetGraphNodeKindName(GetKind()));
}

FLinearColor UBangoEdGraphNode::GetNodeTitleColor() const
{
	if (const TSharedPtr<IBangoNodeDecorator> Decorator = GetNodeDecorator())
	{
		const TOptional<FLinearColor> Color = Decorator->GetTitleColor(MakeDecoratorContext());
		if (Color.IsSet())
		{
			return Color.GetValue();
		}
	}
	switch (Layout.Style)
	{
	case EBangoNodeStyle::ScriptEntry:
	case EBangoNodeStyle::FunctionEntry:  return FLinearColor(0.75f, 0.05f, 0.05f);   // like Blueprint events
	case EBangoNodeStyle::Command:        return FLinearColor(0.10f, 0.35f, 0.75f);   // like Blueprint function calls
	case EBangoNodeStyle::ScriptFunction: return FLinearColor(0.10f, 0.45f, 0.55f);   // a calmer blue for this script's own functions
	default: break;
	}

	switch (GetKind())
	{
	case EBangoGraphNodeKind::Call:
	case EBangoGraphNodeKind::BlockCall: return FLinearColor(0.10f, 0.35f, 0.75f);
	case EBangoGraphNodeKind::Assign:
	case EBangoGraphNodeKind::IncDec:
	case EBangoGraphNodeKind::Local:
	case EBangoGraphNodeKind::Global:   return FLinearColor(0.15f, 0.50f, 0.20f);
	case EBangoGraphNodeKind::If:
	case EBangoGraphNodeKind::While:
	case EBangoGraphNodeKind::For:
	case EBangoGraphNodeKind::ForEach:
	case EBangoGraphNodeKind::Sequence:
	case EBangoGraphNodeKind::Switch:
	case EBangoGraphNodeKind::Spawn:
	case EBangoGraphNodeKind::WaitUntil: return FLinearColor(0.35f, 0.35f, 0.35f);  // like Blueprint flow control
	case EBangoGraphNodeKind::Label:
	case EBangoGraphNodeKind::Goto:     return FLinearColor(0.45f, 0.20f, 0.60f);
	case EBangoGraphNodeKind::Break:
	case EBangoGraphNodeKind::Continue:
	case EBangoGraphNodeKind::Return:   return FLinearColor(0.55f, 0.35f, 0.10f);
	default: break;
	}
	return FLinearColor::Gray;
}

bool UBangoEdGraphNode::ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const
{
	if (!IsReroute())
	{
		return false;
	}
	// AllocateDefaultPins makes In first, then Next (a reroute has no branches).
	OutInputPinIndex = 0;
	OutOutputPinIndex = 1;
	return Pins.Num() == 2;
}

FText UBangoEdGraphNode::GetTooltipText() const
{
	if (IsEntry())
	{
		return bInFunctionGraph
			? LOCTEXT("FunctionEntryTooltip", "Where the function starts (node 0).")
			: LOCTEXT("ScriptEntryTooltip", "Where the script starts (node 0). Shows the script's parameters.");
	}
	const FText Id = FText::Format(LOCTEXT("NodeId", "Node {0}"), FText::AsNumber(NodeID));
	if (const TSharedPtr<IBangoNodeDecorator> Decorator = GetNodeDecorator())
	{
		const TOptional<FText> Tooltip = Decorator->GetTooltip(MakeDecoratorContext());
		if (Tooltip.IsSet())
		{
			return FText::Format(LOCTEXT("DecoratedTooltip", "{0}\n\n{1}"), Tooltip.GetValue(), Id);
		}
	}
	if (!Layout.Description.IsEmpty())
	{
		return FText::Format(LOCTEXT("DescribedTooltip", "{0}\n\n{1}"), FText::FromString(Layout.Description), Id);
	}
	return Id;
}

FText UBangoEdGraphNode::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return FText::GetEmpty();
	}
	if (Pin->PinName == NextPinName)
	{
		return FText::FromString(Bango::GetGraphNodeNextName(GetKind()));
	}
	if (HasCaseRows())
	{
		// A case's values (an entry's condition) are in the box beside its pin, and Default has a box saying so, so
		// these pins have no names.
		return FText::GetEmpty();
	}
	for (int32 Branch = 0; Branch < GetBranchCount(); ++Branch)
	{
		if (Pin->PinName == GetBranchPinName(Branch))
		{
			return FText::FromString(Bango::GetGraphNodeBranchName(GetKind(), Branch));
		}
	}
	return FText::GetEmpty(); // the exec input has no label
}

#undef LOCTEXT_NAMESPACE
