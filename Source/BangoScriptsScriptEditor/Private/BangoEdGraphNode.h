#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "BangoGraph.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoEdGraphNode.generated.h"

/** A declaration (FBangoGraphDecl), kept on an entry node so that editing it can be undone. */
USTRUCT()
struct FBangoEdDecl
{
	GENERATED_BODY()

	UPROPERTY()
	FString Type;
	UPROPERTY()
	FString Name;
	UPROPERTY()
	bool bHasValue = false;
	UPROPERTY()
	FString Value;
	UPROPERTY()
	FString Comment;

	static FBangoEdDecl From(const FBangoGraphDecl& Decl)
	{
		FBangoEdDecl Out;
		Out.Type = Decl.Type;
		Out.Name = Decl.Name;
		Out.bHasValue = Decl.bHasValue;
		Out.Value = Decl.Value;
		Out.Comment = Decl.Comment;
		return Out;
	}

	FBangoGraphDecl ToGraphDecl() const
	{
		FBangoGraphDecl Out;
		Out.Type = Type;
		Out.Name = Name;
		Out.bHasValue = bHasValue;
		Out.Value = Value;
		Out.Comment = Comment;
		return Out;
	}
};

/**
 * One Bango statement in the graph editor. Mirrors an FBangoGraphNode: its kind, NodeID, text and comment.
 * Pins: an exec input (except on entry nodes), then the True/False or Body outputs, then the main output
 * (Then/Completed, or an unnamed exec out).
 */
UCLASS()
class UBangoEdGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	static const FName InPinName;
	static const FName NextPinName;
	static const FName BranchPinNames[2];

	/** EBangoGraphNodeKind, stored as a byte for reflection. */
	UPROPERTY()
	uint8 KindValue = (uint8)EBangoGraphNodeKind::Call;

	UPROPERTY()
	int64 NodeID = 0;

	/** The node's editable text (see FBangoGraphNode::Text). */
	UPROPERTY()
	FString Text;

	/** True for nodes in a function's graph (a function graph's entry node's Text is the signature). */
	UPROPERTY()
	bool bInFunctionGraph = false;

	/** How the node is shown: title and fields (see BangoGraphLayout.h). Rebuilt whenever Text changes. */
	FBangoNodeLayout Layout;

	/** Commands and this script's functions, for layouts. Shared by all nodes of the open script. */
	TSharedPtr<const FBangoNodeLayoutContext> LayoutContext;

	/**
	 * The main Script Start node only: the script's `#define LOCTEXT_NAMESPACE "..."` line, shown as its Loc Namespace row
	 * (see Bango::AddLocNamespaceField). Kept here, not in Text, since it isn't part of the params line.
	 */
	UPROPERTY()
	bool bHasLocNamespace = false;
	UPROPERTY()
	FString LocNamespace;

	/** Entry nodes only: the locals declared at the top of this graph (FBangoGraph::Locals). Edited in the Locals list. */
	UPROPERTY()
	TArray<FBangoEdDecl> Locals;

	/** The main Script Start node only: the file's globals (FBangoGraphDocument::Globals). Edited in the Globals list. */
	UPROPERTY()
	TArray<FBangoEdDecl> Globals;

	/**
	 * The main Script Start node only: the script's description, the comment lines the file starts with
	 * (FBangoGraphDocument::Description). Not shown on the node; edited in the Script Description box.
	 */
	UPROPERTY()
	FString Description;

	/** Text fields whose Key, Namespace and Notes rows are shown (see SBangoGraphNode). Not saved. */
	TSet<int32> ExpandedTextFields;

	/**
	 * For nodes: show the whole header as one Expression row even when it's a simple counting loop (the node's
	 * Expression toggle). Not saved.
	 */
	bool bShowExpression = false;

	/** True for the main graph's Script Start node, which shows the Loc Namespace row. */
	bool IsScriptEntry() const { return IsEntry() && !bInFunctionGraph; }

	/** The field the last compile error points at, or INDEX_NONE. */
	int32 ErrorField = INDEX_NONE;

	/** Warnings from the last compile (like a placeholder with no value), each on a field. Shown in yellow. */
	TArray<Bango::FBangoFieldWarning> FieldWarnings;

	/** The warning on a field, or null. */
	const Bango::FBangoFieldWarning* FindFieldWarning(int32 FieldIndex) const
	{
		return FieldWarnings.FindByPredicate([FieldIndex](const Bango::FBangoFieldWarning& Warning) { return Warning.Field == FieldIndex; });
	}

	bool IsFunctionEntry() const { return IsEntry() && bInFunctionGraph; }

	/**
	 * Rebuilds Layout from Text. Clears any compile error, and shows a layout problem (like an unknown command)
	 * as an error on the node instead.
	 */
	void UpdateLayout();

	/**
	 * Sets one field (see Bango::SetNodeField) as an undoable edit. On failure nothing changes and OutError
	 * says why.
	 */
	bool SetFieldValue(int32 FieldIndex, const FString& Value, FString& OutError);

	/** Shows a compile error on the node, pointing at a field if FieldIndex isn't INDEX_NONE. Replaces a warning banner. */
	void SetCompileError(const FString& Message, int32 FieldIndex);

	/**
	 * Finds this node's compile warnings (Bango::GetNodeWarnings) and shows them: a yellow outline on each field,
	 * and a WARNING! banner unless the node already shows an error. Returns how many there are.
	 */
	int32 UpdateCompileWarnings();

	EBangoGraphNodeKind GetKind() const { return (EBangoGraphNodeKind)KindValue; }
	bool IsEntry() const { return GetKind() == EBangoGraphNodeKind::Entry; }
	/** A bend in a wire: shown as a small dot with an input and an output, no title and no fields. */
	bool IsReroute() const { return GetKind() == EBangoGraphNodeKind::Reroute; }

	UEdGraphPin* GetInputPin() const { return FindPin(InPinName, EGPD_Input); }
	UEdGraphPin* GetNextPin() const { return FindPin(NextPinName, EGPD_Output); }
	UEdGraphPin* GetBranchPin(int32 Branch) const { return FindPin(BranchPinNames[Branch], EGPD_Output); }

	//~ Begin UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	/** A reroute is drawn like Blueprint's: one point that is both its input (pin 0) and its output (pin 1). */
	virtual bool ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const override;
	virtual bool CanUserDeleteNode() const override { return !IsEntry(); }
	virtual bool CanDuplicateNode() const override { return !IsEntry(); }
	/** Bango nodes only go in Bango graphs (pasting them into a Blueprint, or Blueprint nodes into a Bango graph, is refused). */
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	/** An entry node is never pasted: each graph has exactly one. */
	virtual bool CanPasteHere(const UEdGraph* TargetGraph) const override;
	virtual void PostEditUndo() override;
	//~ End UEdGraphNode
};
