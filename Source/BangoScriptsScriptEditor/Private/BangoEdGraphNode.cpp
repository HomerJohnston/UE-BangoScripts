#include "BangoEdGraphNode.h"

#include "BangoEdGraphSchema.h"
#include "EdGraph/EdGraphPin.h"
#include "BangoGraphEdit.h"
#include "EdGraph/EdGraph.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BangoEdGraphNode)

#define LOCTEXT_NAMESPACE "BangoEdGraphNode"

const FName UBangoEdGraphNode::InPinName(TEXT("In"));
const FName UBangoEdGraphNode::NextPinName(TEXT("Next"));
const FName UBangoEdGraphNode::BranchPinNames[2] = { FName(TEXT("Branch0")), FName(TEXT("Branch1")) };

void UBangoEdGraphNode::AllocateDefaultPins()
{
	if (!IsEntry())
	{
		CreatePin(EGPD_Input, UBangoEdGraphSchema::PC_BangoExec, InPinName);
	}
	for (int32 Branch = 0; Branch < Bango::GetGraphNodeBranchCount(GetKind()); ++Branch)
	{
		CreatePin(EGPD_Output, UBangoEdGraphSchema::PC_BangoExec, BranchPinNames[Branch]);
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
	if (IsScriptEntry())
	{
		Bango::AddLocNamespaceField(Layout, bHasLocNamespace, LocNamespace);
	}

	ErrorField = INDEX_NONE;
	FieldWarnings.Reset();
	bHasCompilerMessage = !Layout.Error.IsEmpty();
	ErrorType = EMessageSeverity::Error;
	ErrorMsg = Layout.Error;
}

bool UBangoEdGraphNode::SetFieldValue(int32 FieldIndex, const FString& Value, FString& OutError)
{
	// The Loc Namespace row is the script's #define LOCTEXT_NAMESPACE line, not part of this node's text.
	if (Layout.Fields.IsValidIndex(FieldIndex) && Layout.Fields[FieldIndex].bLocNamespace)
	{
		// Typing nothing (which arrives as "") removes the line.
		FString NewNamespace;
		const FString Trimmed = Value.TrimStartAndEnd();
		if (!Trimmed.IsEmpty() && !Bango::TryGetStringLiteral(Trimmed, NewNamespace))
		{
			OutError = TEXT("The Namespace is a name, like Bango.Intro.");
			return false;
		}
		NewNamespace.TrimStartAndEndInline();
		const bool bHas = !NewNamespace.IsEmpty();
		if (bHas == bHasLocNamespace && NewNamespace.Equals(LocNamespace, ESearchCase::CaseSensitive))
		{
			return true;
		}
		const FScopedTransaction Transaction(LOCTEXT("EditLocNamespace", "Edit Localization Namespace"));
		Modify();
		bHasLocNamespace = bHas;
		LocNamespace = NewNamespace;
		UpdateLayout(); // the node's widget rebuilds itself after an edit (SBangoGraphNode::RefreshSoon)
		return true;
	}

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

void UBangoEdGraphNode::SetCompileError(const FString& Message, int32 FieldIndex)
{
	bHasCompilerMessage = true;
	ErrorType = EMessageSeverity::Error;
	ErrorMsg = Message;
	ErrorField = FieldIndex;
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

FText UBangoEdGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!Layout.Title.IsEmpty())
	{
		return FText::FromString(Layout.Title);
	}
	return FText::FromString(IsEntry() ? FString(bInFunctionGraph ? TEXT("Function") : TEXT("Script Start")) : Bango::GetGraphNodeKindName(GetKind()));
}

FLinearColor UBangoEdGraphNode::GetNodeTitleColor() const
{
	switch (Layout.Style)
	{
	case EBangoNodeStyle::ScriptEntry:
	case EBangoNodeStyle::FunctionEntry:  return FLinearColor(0.75f, 0.05f, 0.05f);   // like Blueprint events
	case EBangoNodeStyle::Command:
	case EBangoNodeStyle::ArrayCommand:   return FLinearColor(0.10f, 0.35f, 0.75f);   // like Blueprint function calls
	case EBangoNodeStyle::ScriptFunction: return FLinearColor(0.10f, 0.45f, 0.55f);   // a calmer blue for this script's own functions
	default: break;
	}

	switch (GetKind())
	{
	case EBangoGraphNodeKind::Call:     return FLinearColor(0.10f, 0.35f, 0.75f);
	case EBangoGraphNodeKind::VarDecl:
	case EBangoGraphNodeKind::Assign:
	case EBangoGraphNodeKind::IncDec:   return FLinearColor(0.15f, 0.50f, 0.20f);
	case EBangoGraphNodeKind::If:
	case EBangoGraphNodeKind::While:
	case EBangoGraphNodeKind::For:
	case EBangoGraphNodeKind::ForEach:  return FLinearColor(0.35f, 0.35f, 0.35f);   // like Blueprint flow control
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
	for (int32 Branch = 0; Branch < 2; ++Branch)
	{
		if (Pin->PinName == BranchPinNames[Branch])
		{
			return FText::FromString(Bango::GetGraphNodeBranchName(GetKind(), Branch));
		}
	}
	return FText::GetEmpty(); // the exec input has no label
}

#undef LOCTEXT_NAMESPACE
