#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "BangoGraph.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoEdGraphNode.generated.h"

/** How the script instance being debugged is using a node (SBangoScriptEditor's Debug dropdown). Drawn as an outline. */
enum class EBangoNodeDebugState : uint8
{
	None,
	/** It's running or paused on this node. */
	Paused,
	/** A call under way on the way there (a call to the function it's in, or further out). */
	CallStack,
	/** It stopped with an error here. */
	Failed,
};

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
	UPROPERTY()
	bool bPersistent = false;

	static FBangoEdDecl From(const FBangoGraphDecl& Decl)
	{
		FBangoEdDecl Out;
		Out.Type = Decl.Type;
		Out.Name = Decl.Name;
		Out.bHasValue = Decl.bHasValue;
		Out.Value = Decl.Value;
		Out.Comment = Decl.Comment;
		Out.bPersistent = Decl.bPersistent;
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
		Out.bPersistent = bPersistent;
		return Out;
	}
};

/**
 * One statement of an entry drawn in its row (IBangoNodeDecorator::DrawsEntryStatements): it isn't a node of its own
 * in the graph editor, but it's still a node in the text, with its NodeID, kept here so undo, copy and paste carry it.
 */
USTRUCT()
struct FBangoEdStatement
{
	GENERATED_BODY()

	/** EBangoGraphNodeKind: Call, or BlockCall for one with a block of its own. */
	UPROPERTY()
	uint8 KindValue = (uint8)EBangoGraphNodeKind::Call;
	/** Its NodeID in the text; 0 until the editor gives it one (BangoScriptEditor::BuildDocument). */
	UPROPERTY()
	int64 NodeID = 0;
	UPROPERTY()
	FString Text;
	UPROPERTY()
	FString Comment;
	UPROPERTY()
	bool bDisabled = false;
	/** Its position in the text's metadata, in grid units (not shown: it's drawn in its row). */
	UPROPERTY()
	int32 X = 0;
	UPROPERTY()
	int32 Y = 0;
	/** For one with a block: its output pin on the node is UBangoEdGraphNode::GetStatementPinName(PinKey). */
	UPROPERTY()
	int32 PinKey = 0;

	/** How it's laid out (its fields), from Text; and its compile error. Not saved. */
	FBangoNodeLayout Layout;
	FString ErrorMessage;

	EBangoGraphNodeKind GetKind() const { return (EBangoGraphNodeKind)KindValue; }
	bool HasBlock() const { return GetKind() == EBangoGraphNodeKind::BlockCall; }
};

/** An entry of a command node: whether its statements are drawn in its row, and if so, the statements. */
USTRUCT()
struct FBangoEdEntryRow
{
	GENERATED_BODY()

	UPROPERTY()
	bool bInRow = false;
	UPROPERTY()
	TArray<FBangoEdStatement> Statements;
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
	/** A switch's Default output (its last branch). Named apart from the cases, so adding a case doesn't rename it. */
	static const FName DefaultPinName;
	/**
	 * The name of an output that leads into a block: Branch0, Branch1, ... (If's Do and Else, a loop's Do, a sequence's
	 * Scope 0, Scope 1, ..., a switch's cases). A switch's last branch is DefaultPinName instead: use GetBranchPin.
	 */
	static FName GetBranchPinName(int32 Branch);

	/** EBangoGraphNodeKind, stored as a byte for reflection. */
	UPROPERTY()
	uint8 KindValue = (uint8)EBangoGraphNodeKind::Call;

	UPROPERTY()
	int64 NodeID = 0;

	/** The node's editable text (see FBangoGraphNode::Text). */
	UPROPERTY()
	FString Text;

	/** A sequence's number of outputs (blocks): at least 1. Other kinds' outputs come from their kind. */
	UPROPERTY()
	int32 SequenceOutputs = 2;

	/**
	 * A switch's cases: each one's values as written, like `1, 2` (see FBangoGraphNode::CaseValues). One output each.
	 * A command node with entries (bEntries): each entry's condition, empty for `entry:`. One output each.
	 */
	UPROPERTY()
	TArray<FString> CaseValues;

	/** A command node whose block is an entry list (see FBangoGraphNode::bEntries): a row per entry, no Body. */
	UPROPERTY()
	bool bEntries = false;

	/** With bEntries: whether each entry is disabled. One per entry. */
	UPROPERTY()
	TArray<bool> EntryDisabled;

	/** With bEntries: the last entry is `default:` (no condition), which passes when none of the others does. */
	UPROPERTY()
	bool bEntryDefault = false;

	/**
	 * With bEntries: one per entry. An entry drawn in its row has its statements here (they aren't nodes), its own
	 * output is hidden and unwired, and each of its statements with a block has an output pin of its own.
	 */
	UPROPERTY()
	TArray<FBangoEdEntryRow> EntryRows;

	/** The next free key for a statement's output pin (FBangoEdStatement::PinKey). */
	UPROPERTY()
	int32 NextStatementPinKey = 1;

	/** True for nodes in a function's graph (a function graph's entry node's Text is the signature). */
	UPROPERTY()
	bool bInFunctionGraph = false;

	/** How the node is shown: title and fields (see BangoGraphLayout.h). Rebuilt whenever Text changes. */
	FBangoNodeLayout Layout;

	/** Commands and this script's functions, for layouts. Shared by all nodes of the open script. */
	TSharedPtr<const FBangoNodeLayoutContext> LayoutContext;

	/**
	 * The main Script Start node only: the script's `#TEXT_NAMESPACE "..."` line, edited in the SCRIPT CONFIG panel
	 * (SetLocNamespace). Kept here, not in Text, since it isn't part of the inputs line.
	 */
	UPROPERTY()
	bool bHasLocNamespace = false;
	UPROPERTY()
	FString LocNamespace;

	/**
	 * The main Script Start node only: the script's `#USES_GRAPH_EDITOR` line, so it's saved with its node metadata.
	 * Turned on once nodes are moved in a script with at least the project's Large Script Threshold of nodes.
	 */
	UPROPERTY()
	bool bUsesGraphEditor = false;

	/** Dragged in the graph editor since the script editor last looked (SBangoGraphNode::MoveTo). Not saved. */
	UPROPERTY(Transient)
	bool bMovedByUser = false;

	/** Entry nodes only: the script variables declared at the top of this graph (FBangoGraph::ScriptVars). Edited in the Script Variables list. */
	UPROPERTY()
	TArray<FBangoEdDecl> ScriptVars;

	/** The main Script Start node only: the file's globals (FBangoGraphDocument::Globals). Edited in the Globals list. */
	UPROPERTY()
	TArray<FBangoEdDecl> Globals;

	/**
	 * The main Script Start node only: the script's description, the comment lines the file starts with
	 * (FBangoGraphDocument::Description). Not shown on the node; edited in the Script Description box.
	 */
	UPROPERTY()
	FString Description;

	/** For the debugger: how the script instance being debugged is using this node. Set by the script editor; not saved. */
	EBangoNodeDebugState DebugState = EBangoNodeDebugState::None;
	/**
	 * A command with entries, while the debugger follows a copy of the script with its block open: which entries
	 * passed when last worked out, and the one running (-1 none). Not saved.
	 */
	bool bHasEntryDebug = false;
	TArray<bool> DebugEntryPassed;
	int32 DebugRunningEntry = -1;
	/** The statement drawn in a row the debugged copy is at, else -1 for both. Not saved. */
	int32 DebugStatementEntry = -1;
	int32 DebugStatementIndex = -1;
	/** The statement drawn in a row that was just inserted or moved, for its row to focus once, else -1 for both. Not saved. */
	int32 FocusStatementEntry = -1;
	int32 FocusStatementIndex = -1;
	/** For the debugger's trail: when the script being debugged last ran this node (FPlatformTime::Seconds), or 0. Fades out. */
	double DebugPingTime = 0.0;

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
	/** A switch's case the last compile error is on, or INDEX_NONE. */
	int32 ErrorCase = INDEX_NONE;

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

	/**
	 * The main Script Start node only: sets the script's namespace (the #TEXT_NAMESPACE line) as an undoable
	 * edit, from what was typed in SCRIPT CONFIG (a name, without quotes). Spaces around it are dropped; empty removes the
	 * line. False if nothing changed.
	 */
	bool SetLocNamespace(const FString& Typed);


	/**
	 * Shows a compile error on the node, pointing at a field if FieldIndex isn't INDEX_NONE (or at a switch's case, if
	 * CaseIndex isn't). Replaces a warning banner.
	 */
	void SetCompileError(const FString& Message, int32 FieldIndex, int32 CaseIndex = INDEX_NONE);

	/**
	 * Finds this node's compile warnings (Bango::GetNodeWarnings) and shows them: a yellow outline on each field,
	 * and a WARNING! banner unless the node already shows an error. Returns how many there are.
	 */
	int32 UpdateCompileWarnings();

	EBangoGraphNodeKind GetKind() const { return (EBangoGraphNodeKind)KindValue; }
	bool IsEntry() const { return GetKind() == EBangoGraphNodeKind::Entry; }
	/** A bend in a wire: shown as a small dot with an input and an output, no title and no fields. */
	bool IsReroute() const { return GetKind() == EBangoGraphNodeKind::Reroute; }

	/** Every node but an entry or a reroute can be turned off (Disable Node). It's kept in UEdGraphNode's EnabledState. */
	bool CanBeDisabled() const { return !IsEntry() && !IsReroute(); }
	/** True if the node is off: it's written between slash-star and star-slash, and does nothing when the script runs. */
	bool IsDisabledNode() const { return CanBeDisabled() && GetDesiredEnabledState() != ENodeEnabledState::Enabled; }

	UEdGraphPin* GetInputPin() const { return FindPin(InPinName, EGPD_Input); }
	UEdGraphPin* GetNextPin() const { return FindPin(NextPinName, EGPD_Output); }
	UEdGraphPin* GetBranchPin(int32 Branch) const;
	/** How many outputs lead into blocks (see Bango::GetGraphNodeBranchCount): a sequence's or switch's own count. */
	int32 GetBranchCount() const;

	bool IsSequence() const { return GetKind() == EBangoGraphNodeKind::Sequence; }
	/** A sequence's Add pin: a new last output, as an undoable edit. */
	void AddSequenceOutput();
	/** A sequence's Remove pin: takes out one output (its wire goes too), moving the later ones up. Never the last one left. */
	void RemoveSequenceOutput(int32 Branch);
	bool CanRemoveSequenceOutput() const { return IsSequence() && GetBranchCount() > 1; }

	bool IsSwitch() const { return GetKind() == EBangoGraphNodeKind::Switch; }
	/** A switch's Add pin: a new case (with no value yet) after the others, before Default, as an undoable edit. */
	void AddSwitchCase();
	/** Takes out one case and its output (its wire goes too), moving the later ones up. Default stays. */
	void RemoveSwitchCase(int32 Case);
	/** Sets a case's values (see Bango::ValidateCaseValues; empty is allowed while it's being filled in) as an undoable edit. */
	bool SetCaseValues(int32 Case, const FString& Values, FString& OutError);
	/** The case (or entry) an output pin is for, or INDEX_NONE (Default, Then, the input, or neither kind of node). */
	int32 GetCaseOfPin(const UEdGraphPin* Pin) const;

	/** A command node whose block is an entry list: its rows are like a switch's cases (CaseValues), without Default. */
	bool IsEntries() const { return GetKind() == EBangoGraphNodeKind::BlockCall && bEntries; }
	/** A switch, or a command with entries: a box beside each of its outputs (CaseValues). */
	bool HasCaseRows() const { return IsSwitch() || IsEntries(); }
	/** The command takes entries (FBangoCommandSignature::Entries), and whether they're optional (it can also have an ordinary block). */
	bool TakesEntries() const;
	bool TakesOptionalEntries() const;
	/** Moves an entry (its condition, whether it's disabled, and its wire) up (Delta -1) or down (+1), as an undoable edit. */
	void MoveEntry(int32 Entry, int32 Delta);
	/** Disables or enables one entry, as an undoable edit. */
	void SetEntryDisabled(int32 Entry, bool bDisabled);
	/** A command with optional entries and an ordinary block: its block becomes one `entry:` holding what was in it. */
	void UseEntries();
	/** The other way, for one entry with no condition. */
	void UsePlainBlock();
	bool CanUsePlainBlock() const { return IsEntries() && !bEntryDefault && TakesOptionalEntries() && CaseValues.Num() == 1 && CaseValues[0].IsEmpty() && !(EntryDisabled.IsValidIndex(0) && EntryDisabled[0])
		&& !(IsEntryInRow(0) && EntryRows[0].Statements.Num() > 0); }
	/** The entries end with `default:`, and whether Entry is it. */
	bool HasDefaultEntry() const { return IsEntries() && bEntryDefault && CaseValues.Num() > 0; }
	bool IsDefaultEntry(int32 Entry) const { return HasDefaultEntry() && Entry == CaseValues.Num() - 1; }
	/** Adds `default:` at the end, as an undoable edit. Not with an enabled `entry:`, which always passes (the default would never run). */
	void AddDefaultEntry();
	bool CanAddDefaultEntry() const;
	/** (In an edit that's under way:) adds a row at the end, with its output pin above Then (or Default); swaps two entries' rows, wires and all. */
	void AddCaseRow();
	void SwapEntryRows(int32 Entry, int32 Other);

	// ---- Entries drawn in their rows (IBangoNodeDecorator::DrawsEntryStatements) ----

	/** The output pin name of a statement drawn in a row. */
	static FName GetStatementPinName(int32 PinKey);
	/** Whether entry Entry's statements are drawn in its row. */
	bool IsEntryInRow(int32 Entry) const { return IsEntries() && EntryRows.IsValidIndex(Entry) && EntryRows[Entry].bInRow; }
	/** What the command's entries may hold (its signature's EntriesHold); false if it doesn't say. */
	bool GetEntryHold(FBangoEntryHold& OutHold) const;
	/** Whether a new, empty entry Entry would be drawn in its row: the command says what its entries hold, and its decorator asks. */
	bool WouldDrawEntryInRow(int32 Entry) const;
	/** The statement whose output Pin is, or false. */
	bool FindStatementOfPin(const UEdGraphPin* Pin, int32& OutEntry, int32& OutIndex) const;
	/** The output pin of a statement drawn in a row (one with a block), or null. */
	UEdGraphPin* GetStatementPin(int32 Entry, int32 Index) const;
	/** The statement drawn in a row whose NodeID is NodeID, or false. */
	bool FindStatement(int64 NodeID, int32& OutEntry, int32& OutIndex) const;
	/** Shows a compile error on a statement drawn in a row (and on the node, as for any error). */
	void SetStatementError(int32 Entry, int32 Index, const FString& Message);
	/** The statement edits a row makes (FBangoNodeDecoratorContext): each an undoable change. False, with why, if refused. */
	bool InsertEntryStatement(int32 Entry, int32 Index, const FString& Statement, FString& OutError);
	bool RemoveEntryStatement(int32 Entry, int32 Index, FString& OutError);
	bool MoveEntryStatement(int32 FromEntry, int32 FromIndex, int32 ToEntry, int32 ToIndex, FString& OutError);
	bool SetEntryStatementDisabled(int32 Entry, int32 Index, bool bDisabled, FString& OutError);
	bool SetStatementFieldValue(int32 Entry, int32 Index, int32 FieldIndex, const FString& Value, FString& OutError);
	/** (In an edit:) puts a new output pin for a statement just after the input, where statement pins go. */
	UEdGraphPin* CreateStatementPin(int32 PinKey);
	/** (In an edit:) takes out a statement's output pin, its wire too (the nodes on it stay). */
	void RemoveStatementPin(int32 PinKey);

	/** The decorator another module registered for the command this node calls (Bango::RegisterNodeDecorator), or null. */
	TSharedPtr<class IBangoNodeDecorator> GetNodeDecorator() const;
	/** What a decorator is told about this node; SetFieldValue sets a field as an undoable edit (SetFieldValue, below). */
	struct FBangoNodeDecoratorContext MakeDecoratorContext() const;

	//~ Begin UEdGraphNode
	virtual void AllocateDefaultPins() override;
	/** A decorated command's icon, if its decorator gives one. */
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool ShowPaletteIconOnNode() const override;
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
	/** Disable Node / Enable Node (FBangoGraphCommands), for the node's right-click menu. */
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	//~ End UEdGraphNode
};
