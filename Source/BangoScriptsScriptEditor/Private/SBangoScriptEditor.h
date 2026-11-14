#pragma once

#include "CoreMinimal.h"
#include "BangoGraph.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "BangoScriptSubsystem.h"
#include "BangoEdGraphNode.h" // EBangoNodeDebugState
#include "EditorUndoClient.h"
#include "Types/SlateStructs.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Animation/CurveSequence.h"

class FUICommandList;
class SBox;
class SButton;
class SGraphEditor;
class SBangoLineMarkers;
class SInlineEditableTextBlock;
class SMultiLineEditableTextBox;
class SBangoTextEditor;
class SScrollBox;
class SScrollBar;
class STextBlock;
class SVerticalBox;
class ITableRow;
class STableViewBase;
class UEdGraph;
struct FBangoNodeLayoutContext;
struct FBangoNodeMenuEntry;

/**
 * The editor for one script: its graphs (the main graph and its functions) listed on the left, and the picked graph
 * on the right under Compile, Save and the script's name. Everything about one open script lives here, so the same
 * widget can sit in the Bango Scripts tab (SBangoScriptBrowser, which picks the script) or in a window of its own.
 *
 * It edits a Bango Script asset (spec section 15). For a linked script the .bango file next to it is the source of
 * truth: opening reads it into editor graphs, and Save writes them back (Bango::WriteGraphText), then the asset's copy.
 * For an embedded script, the asset's text is. Unsaved changes show as a * in the title.
 */
class UBangoScriptAsset;

class SBangoScriptEditor : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SBangoScriptEditor)
		: _EmptyMessage(NSLOCTEXT("SBangoScriptEditor", "NoScript", "No script is open."))
	{}
		/** What the graph area says while no script is open. */
		SLATE_ARGUMENT(FText, EmptyMessage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBangoScriptEditor() override;

	/**
	 * Reads the open script asset (OpenScriptAsset) again, by its name (its asset path), replacing what's shown.
	 * Doesn't ask about unsaved changes: call ConfirmCloseScript first.
	 */
	void LoadScript(const FString& Script);
	/**
	 * Opens a Bango Script asset (spec section 15), replacing the open script. Its name is its asset path
	 * ("/Game/Quests/Intro"). A linked script's text is its .bango file (the file wins); an embedded one's is the
	 * asset's. Save writes the file and the asset's copy (linked), or the asset (embedded). Doesn't ask about unsaved
	 * changes either.
	 */
	void OpenScriptAsset(UBangoScriptAsset* Asset);
	/** Opens the open script again from its file, to pick up changes made outside the editor. Doesn't ask either. */
	void ReloadScript();
	/** Saves the open script (as the Save button does). False if it wasn't saved. */
	bool SaveScript() { return Save(); }
	/**
	 * If there are unsaved changes, asks whether to save them. Returns false if the user cancelled. Answering No
	 * throws the changes away: they aren't kept as a recovery copy when the editor closes.
	 */
	bool ConfirmCloseScript();

	/** The open script's name, its asset path ("/Game/Quests/Intro"), or empty. */
	const FString& GetOpenScript() const { return OpenScript; }
	/** As of the last check: checked every quarter second while the editor is shown, after undo, and before closing. */
	bool HasUnsavedChanges() const { return bDirty || bTextEdited; }

	//~ Begin SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget

	//~ Begin FEditorUndoClient: however the undo was started (Ctrl+Z, Edit menu, Undo History), the nodes' widgets are rebuilt.
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FEditorUndoClient

private:
	/** One of the open script's graphs. FunctionIndex is INDEX_NONE for the main graph. */
	struct FGraphEntry
	{
		FString Label;
		int32 FunctionIndex = INDEX_NONE;
		/** The name in the FUNCTIONS list, for renaming. */
		TWeakPtr<SInlineEditableTextBlock> NameWidget;
	};

	/** Every graph, in the order of EdGraphs: [0] is the main graph, then the functions. */
	TArray<TSharedPtr<FGraphEntry>> GraphItems;

	// FUNCTIONS: GraphItems without the main graph.
	TArray<TSharedPtr<FGraphEntry>> FunctionItems;
	TSharedPtr<SListView<TSharedPtr<FGraphEntry>>> FunctionList;

	// INPUTS: the shown graph's parameters (the script's #define params, or the function's), from its entry node.
	struct FInputEntry
	{
		int32 Index = INDEX_NONE;
		FBangoEntryParam Param;
		/** The name in the INPUTS list, for renaming. */
		TWeakPtr<SInlineEditableTextBlock> NameWidget;
	};
	TArray<TSharedPtr<FInputEntry>> InputItems;
	TSharedPtr<SListView<TSharedPtr<FInputEntry>>> InputList;
	/** The entry text InputItems were read from (and in which graph), so they're only made again when it changes. */
	FString InputsText;
	int32 InputsGraph = INDEX_NONE;
	/** After + Add: the input to start renaming once its row exists. */
	int32 PendingInputRename = INDEX_NONE;

	// The shown graph's variables (see Bango::GetGraphVariables), for names and types.
	TArray<TSharedPtr<FBangoGraphVariable>> VariableItems;

	// GLOBALS and LOCALS: the file's global declarations, and the shown graph's locals, from the entry nodes.
	struct FDeclEntry
	{
		bool bGlobal = false;
		/** Into the globals or the graph's locals; INDEX_NONE for a read-only row. */
		int32 Index = INDEX_NONE;
		FBangoGraphDecl Decl;
		/** A loop variable (Locals) or a global declared elsewhere (Globals): shown greyed, not edited here. */
		bool bReadOnly = false;
		/** A loop variable: the loop's node, in the shown graph. */
		int32 LoopNode = INDEX_NONE;
		TWeakPtr<SInlineEditableTextBlock> NameWidget;
	};
	TArray<TSharedPtr<FDeclEntry>> GlobalItems;
	TArray<TSharedPtr<FDeclEntry>> LocalItems;
	TSharedPtr<SListView<TSharedPtr<FDeclEntry>>> GlobalList;
	TSharedPtr<SListView<TSharedPtr<FDeclEntry>>> LocalList;

	/** The left pane's foldouts, open or closed. */
	bool bDescriptionExpanded = true;
	bool bFunctionsExpanded = true;
	bool bInputsExpanded = true;
	bool bGlobalsExpanded = true;
	bool bLocalsExpanded = true;
	/** One of the reference pane's tabs (COMMANDS or NODES): a search box, an order, and a list in group foldouts. */
	struct FReferenceList
	{
		/** The list: a foldout per group, one line per name. */
		TSharedPtr<SVerticalBox> ListBox;
		/** What's typed in the tab's search box: only names that contain it are listed. */
		FString Filter;
		/** Listed in groups by category, instead of Native and Game (remembered between sessions). */
		bool bByCategory = false;
		/** Whether each group foldout is open, by "Order:Group" (shared, so the foldouts can point at them). */
		TMap<FString, TSharedPtr<bool>> GroupsExpanded;
	};
	/** One line of a reference list. */
	struct FReferenceEntry
	{
		FString Name;
		FString Category;
		FString Tooltip;
		/** Built into Bango (the Native group), or the project's or script's own (the Game group). */
		bool bBuiltIn = true;
		/** For the NODES tab: the node it places when it's dragged onto the graph. */
		TSharedPtr<FBangoNodeMenuEntry> Node;
	};
	/** The reference pane's tabs: every command a script can call, and every node that can be placed in the graph. */
	FReferenceList CommandReference;
	FReferenceList NodeReference;
	/** Which tab the reference pane shows: 0 COMMANDS, 1 NODES (remembered between sessions). */
	int32 ReferenceTab = 0;
	/** The reference pane on the right (COMMANDS and NODES), shown or hidden with its Command Palette tab, like the text pane on the left. */
	bool bCommandsPaneOpen = false;
	float CommandsPaneOpenness = 0.0f;
	bool bCommandsPaneSliding = false;
	/** Its width in pixels when it's open (the splitter beside it sets this; resizing the window doesn't). */
	float CommandsPaneWidth = 260.0f;
	TSharedPtr<SButton> CommandsTabButton;
	/** What the lists show, so they're only made again when it changes. */
	FString DeclsShown;
	/** After + Add: the declaration to start renaming once its row exists. */
	bool bPendingDeclGlobal = false;
	int32 PendingDeclRename = INDEX_NONE;

	TSharedPtr<STextBlock> TitleText;

	/** SCRIPT DESCRIPTION: the box for the script's description (kept on the main Script Start node). */
	TSharedPtr<SMultiLineEditableTextBox> DescriptionBox;

	// The script text pane, left of the graph: the script as it would be saved, which can be edited and applied.
	TSharedPtr<SBangoTextEditor> TextBox;
	/** The text box's vertical scrollbar, which says how far it's scrolled, for the line numbers beside it. */
	TSharedPtr<SScrollBar> TextVScrollBar;
	TSharedPtr<SScrollBox> LineNumberScroll;
	TSharedPtr<STextBlock> LineNumbers;
	/** The debugger's marks beside the line numbers: where the followed copy is, its calls, where it failed, its trail. */
	TSharedPtr<SBangoLineMarkers> LineMarkers;
	/** How many line numbers LineNumbers shows. */
	int32 LineNumberCount = 0;
	/** Shown or hidden with the Source Editor tab on the graph's left edge (SetTextPaneOpen). Hidden to start with. */
	bool bTextPaneOpen = false;
	/** How far the pane is slid out, 0 (hidden) to 1 (open): it moves toward bTextPaneOpen when the tab is clicked. */
	float TextPaneOpenness = 0.0f;
	/** True while the pane is sliding (AnimateTextPane is running). */
	bool bTextPaneSliding = false;
	/** The text pane's width in pixels when it's open (the splitter beside it sets this; resizing the window doesn't). */
	float TextPaneWidth = 480.0f;
	/** The left pane's width (SCRIPT DESCRIPTION ... FUNCTIONS): kept when the window is resized; the splitter sets it. */
	float ScriptDataWidth = 280.0f;
	/** The side panes can't be dragged narrower than this. */
	static constexpr float MinSidePaneWidth = 120.0f;
	TSharedPtr<SButton> TextTabButton;
	/** The text as last put in the box (from the graphs, or the file if it didn't read), so typing in it can be told apart. */
	FString TextShown;
	/** The box has been typed in since TextShown: the graphs don't change it until the typing is applied or reverted. */
	bool bTextEdited = false;
	/** Why the text in the box couldn't be shown as a graph (from Apply, or opening a file that doesn't read). Empty if it can. */
	FString TextError;
	/** Flashes the error under the text once each time it's set (SetTextError), so a new (or the same) error is noticed. */
	FCurveSequence TextErrorFlash;
	/** Sets TextError, and flashes it if there is one. */
	void SetTextError(const FString& Error);
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<FUICommandList> GraphCommands;

	/** The open script's name, its asset path ("/Game/Quests/Intro"). Empty if none. */
	FString OpenScript;
	/** The file the open script is read from and saved to, watched for changes made outside the editor. Empty for an embedded script asset. */
	FString OpenPath;
	FDateTime OpenFileTime;
	/** The open Bango Script asset (OpenScriptAsset). */
	TWeakObjectPtr<UBangoScriptAsset> OpenAsset;

	/** Editor graphs for the open script: [0] is the main graph, then one per function. */
	TArray<TStrongObjectPtr<UEdGraph>> EdGraphs;
	/** Which of EdGraphs is shown, or INDEX_NONE. */
	int32 ShownGraph = INDEX_NONE;

	/** Commands and this script's functions, shared by every node's layout. Updated when a function's signature changes. */
	TSharedPtr<FBangoNodeLayoutContext> LayoutContext;

	/** The document as last opened or saved; the script has unsaved changes when the graphs differ from it. */
	FBangoGraphDocument SavedDocument;
	bool bDirty = false;
	/** What the toolbar's Compile Test icon shows, as Blueprint's does: compiled when opened (CompileQuietly), Unknown once changed after. */
	enum class ECompileStatus : uint8 { Unknown, Good, Warning, Error };
	ECompileStatus CompileStatus = ECompileStatus::Unknown;
	/** The script as it was last compiled, so a change after it puts CompileStatus back to Unknown (OnPoll). */
	FBangoGraphDocument CompiledDocument;

	/**
	 * The file was changed outside the editor while the editor had unsaved changes: a bar above the graph offers to
	 * reload (losing them) or keep them. Without unsaved changes, an outside change is reloaded straight away.
	 */
	bool bChangedOnDisk = false;
	/** The file's time when the user chose to keep their changes over an outside change, so it isn't offered again. */
	FDateTime KeptFileTime;

	/** Shown in the graph area while no script is open. */
	FText EmptyMessage;

	// FUNCTIONS
	/** Makes FunctionItems from GraphItems again, after functions are added or removed. */
	void RebuildFunctionItems();
	TSharedRef<ITableRow> OnGenerateFunctionRow(TSharedPtr<FGraphEntry> Item, const TSharedRef<STableViewBase>& Owner);
	void OnFunctionSelected(TSharedPtr<FGraphEntry> Item, ESelectInfo::Type SelectInfo);
	/** A click (not a drag) on a function shows its graph. */
	void OnFunctionClicked(TSharedPtr<FGraphEntry> Item);
	/** Selects the shown function's row (none for the main graph). */
	void SelectShownFunction();
	/** Adds `function void NewFunction()` (with a name no other function or command has), shows it, and starts renaming it. */
	FReply OnAddFunctionClicked();
	/** Deletes a function after asking. Calls to it are left alone, so they fail to compile until removed or renamed. */
	void DeleteFunction(TSharedPtr<FGraphEntry> Item);
	/** Why NewName can't be the name of the function now called OldName, or empty if it can. */
	FString CheckFunctionName(const FString& OldName, const FString& NewName) const;
	/** Renames a function, and every call to it in the script, as one undoable edit. */
	void RenameFunction(TSharedPtr<FGraphEntry> Item, const FString& NewName);

	/** How tall a FUNCTIONS, INPUTS, GLOBALS or LOCALS list is with NumRows rows: as tall as its rows, since the pane around them scrolls. */
	FOptionalSize VariableListHeight(int32 NumRows) const;

	// INPUTS
	/** The names of the shown graph's parameters and variables. */
	TSet<FString> GetShownGraphNames() const;
	/** The shown graph's entry node, or null. */
	class UBangoEdGraphNode* GetShownEntryNode() const;
	/** Reads InputItems again from the shown graph's entry node, if its text changed. */
	void UpdateInputs();
	TSharedRef<ITableRow> OnGenerateInputRow(TSharedPtr<FInputEntry> Item, const TSharedRef<STableViewBase>& Owner);
	/** The type menu of an input's icon: every type, and arrays of them. */
	TSharedRef<SWidget> MakeInputTypeMenu(int32 Index);
	/** A type menu: every type, and arrays of them, with Current checked. Picking one calls OnPick. */
	TSharedRef<SWidget> MakeTypeMenu(const FString& Current, TFunction<void(const FString&)> OnPick);
	/** Adds `bool _newParam` (numbered if the name is taken) at the end, and starts renaming it. */
	FReply OnAddInputClicked();
	void DeleteInput(int32 Index);
	void SetInputType(int32 Index, const FString& Type);
	/** Moves input From to just before input To (To == the number of inputs: to the end). */
	void MoveInput(int32 From, int32 To);
	/** Sets a script parameter's default from what was typed in its box (empty: required). */
	void SetInputDefault(int32 Index, const FString& Typed);
	/** Why NewName can't be the name of input Index, or empty if it can. */
	FString CheckInputName(int32 Index, const FString& NewName) const;
	/** Renames an input, and its uses in the graph, as one undoable edit. */
	void RenameInput(int32 Index, const FString& NewName);
	/**
	 * Writes Params into the shown entry node, as one undoable edit called Description. With a RenamedFrom, that
	 * variable is renamed to RenamedTo in every node of the graph too. False (with a notification) if they don't
	 * make valid parameters.
	 */
	bool ApplyInputs(const TArray<FBangoEntryParam>& Params, const FText& Description, const FString& RenamedFrom = FString(), const FString& RenamedTo = FString());

	// GLOBALS and LOCALS
	/** Reads the shown graph's variables, and the Globals and Locals lists, again if they changed. Document and Context are the script as it is now. */
	void UpdateVariables(const FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context);
	/** UpdateVariables, reading the graphs first. */
	void UpdateVariablesNow();
	TSharedRef<ITableRow> OnGenerateDeclRow(TSharedPtr<FDeclEntry> Item, const TSharedRef<STableViewBase>& Owner);
	/** Clicking a loop variable shows and selects its loop. */
	void OnDeclClicked(TSharedPtr<FDeclEntry> Item);
	/** The file's globals (from the Script Start node), or the shown graph's locals (from its entry node). */
	TArray<FBangoGraphDecl> GetDecls(bool bGlobal) const;
	/** Adds `global bool newGlobal;` or `bool _newLocal;` (numbered if taken) at the end, and starts renaming it. */
	FReply OnAddDeclClicked(bool bGlobal);
	/** Declares a global the graph uses but the file doesn't declare. */
	void DeclareGlobal(const FString& Name, const FString& Type);
	void DeleteDecl(bool bGlobal, int32 Index);
	void SetDeclType(bool bGlobal, int32 Index, const FString& Type);
	/** Sets a local's starting value from what was typed in its box (empty: none, so nil). */
	void SetDeclValue(int32 Index, const FString& Typed);
	void MoveDecl(bool bGlobal, int32 From, int32 To);
	/** Why NewName can't be the name of declaration Index, or empty if it can. */
	FString CheckDeclName(bool bGlobal, int32 Index, const FString& NewName) const;
	/** Renames a declaration and its uses (in the shown graph for a local, everywhere for a global), as one undoable edit. */
	void RenameDecl(bool bGlobal, int32 Index, const FString& NewName);
	/** True if a node anywhere in the script uses the global. */
	bool IsGlobalUsed(const FString& Name) const;
	/**
	 * Writes Decls as the file's globals or the shown graph's locals, as one undoable edit called Description. With
	 * a RenamedFrom, that variable is renamed to RenamedTo in the nodes that can see it too.
	 */
	bool ApplyDecls(bool bGlobal, const TArray<FBangoGraphDecl>& Decls, const FText& Description, const FString& RenamedFrom = FString(), const FString& RenamedTo = FString());

	void CloseScript();
	/** Shows one of EdGraphs. With bZoomToFit, the view fits the whole graph; otherwise it's left for the caller to set. */
	void ShowGraph(int32 GraphIndex, bool bZoomToFit = true);

	/** Whether Node's top-left corner is in the part of the graph that's shown (safe for reroute nodes, unlike IsNodeTitleVisible). */
	bool IsNodeInView(const UEdGraphNode* Node) const;
	/** Sync Graph (SBangoTextEditor): the text's cursor moved to these nodes, so select them in their graph. */
	void OnTextCursorNodes(const FString& FunctionName, const TArray<int64>& NodeIDs);

	/** Sync Graph: nodes were selected in the graph, so move the text's cursor to them. */
	void OnGraphSelectionChanged(const TSet<UObject*>& Selection);

	/** Set while the text's cursor is selecting nodes, so that selection isn't sent back to the text. */
	bool bSyncingFromText = false;
	void ShowMessage(const FText& Message);
	FText GetTitle() const;

	/**
	 * Reads the editor graphs back into a document, as it would be saved: placeholders left without a value
	 * get a nil (Bango::FillMissingPlaceholders).
	 */
	bool BuildCurrentDocument(FBangoGraphDocument& OutDocument, TArray<TArray<class UBangoEdGraphNode*>>* OutNodes, FString& OutError) const;

	/**
	 * Checks whether the file was changed outside the editor (by a text editor, source control, ...). If so, reloads
	 * it when there are no unsaved changes, or shows the changed-on-disk bar when there are.
	 */
	void CheckFileOnDisk();
	/** Reads the script again from its file, keeping the shown graph and its view where they can be. */
	void ReloadKeepingView();
	/** Runs Rebuild (which makes the editor graphs again), then shows the same graph from the same place, where it still exists. */
	void KeepingView(TFunctionRef<void()> Rebuild);
	/** Makes the editor graphs, the graph list and the variable lists for a document, and shows the main graph. */
	void ShowDocument(const FBangoGraphDocument& Document);

	// SCRIPT DESCRIPTION
	/** The script's description, from the main Script Start node (empty if no script is open). */
	FString GetScriptDescription() const;
	/** Sets the description from what was typed (trailing spaces and empty lines at the end dropped), as one undoable edit. */
	void SetScriptDescription(const FString& Typed);
	/** Puts the description in its box again if it changed (after an undo, a reload, Apply), unless the box is being typed in. */
	void UpdateDescriptionBox();

	// Script text pane
	TSharedRef<SWidget> MakeTextPane();
	/** The Source Editor tab: a folder tab on the graph's left edge that pulls the pane out, or pushes it back. */
	TSharedRef<SWidget> MakeTextTab();
	/** Opens or closes the text pane, sliding it (bAnimate) or at once. */
	void SetTextPaneOpen(bool bOpen, bool bAnimate);
	/** The Command Palette tab on the graph's right edge, and the reference pane (COMMANDS and NODES tabs) it pulls out. */
	TSharedRef<SWidget> MakeCommandsTab();
	TSharedRef<SWidget> MakeCommandsPane();
	void SetCommandsPaneOpen(bool bOpen, bool bAnimate);
	EActiveTimerReturnType AnimateCommandsPane(double InCurrentTime, float InDeltaTime);
	/** Every frame while the pane is sliding: moves TextPaneOpenness toward bTextPaneOpen. */
	EActiveTimerReturnType AnimateTextPane(double InCurrentTime, float InDeltaTime);
	/** Shows a number for each line of Text beside the script text. */
	void UpdateLineNumbers(const FString& Text);
	/** Every frame while the pane is shown: scrolls the line numbers to where the text is scrolled. */
	EActiveTimerReturnType SyncLineNumbers(double InCurrentTime, float InDeltaTime);
	/** Puts Text in the box, as the new TextShown: not typed in. */
	void SetScriptText(const FString& Text);
	/** Unless the box has been typed in: shows Document (the graphs as they are now; null to read them) as text. */
	void UpdateScriptText(const FBangoGraphDocument* Document);
	/**
	 * Makes the graphs again from the text in the box, as an edit that can't be undone with Undo (like Reload). If it
	 * doesn't read, sets TextError, leaves the graphs alone and returns false.
	 */
	bool ApplyScriptText();
	/** Throws away what was typed in the box: it shows the graphs again. */
	void RevertScriptText();

	/** Compares the graphs with the file as last opened or saved, and updates bDirty. */
	void RefreshDirty();

	/** Every quarter second: updates the * for unsaved changes, and the layouts if a function's signature changed. */
	EActiveTimerReturnType OnPoll(double CurrentTime, float DeltaTime);
	/**
	 * Lays out every node again (which also clears compile errors and warnings), and rebuilds the widgets of only
	 * the nodes that now look different. Used when what nodes can see changes (a function's signature, a variable).
	 */
	void RefreshAllNodes();
	/**
	 * Lays out every node again and rebuilds every node widget from scratch. Only for undo and redo, which can bring
	 * pins back as new objects that the old pin widgets don't know about. Anything else uses RefreshAllNodes, since
	 * rebuilding the whole panel makes the graph flash.
	 */
	void RebuildAllNodes();

	/** Reload: asks about unsaved changes, then reads the script again from its file. */
	FReply OnReloadClicked();
	/** The toolbar: Reload, Compile (with its status icon) and Save, then the Debug dropdown, styled like an asset editor's. */
	TSharedRef<SWidget> MakeToolbar();
	FReply OnCompileClicked();
	/** Compiles the script only to set CompileStatus (the Compile Test icon): no notification, no node errors, no view change. */
	void CompileQuietly();
	FReply OnSaveClicked();
	/** Writes the open script to its file. Returns false if it couldn't (and says why). */
	bool Save();
	/** If the editor goes away with unsaved changes, keeps a copy in Saved/Bango/Unsaved. */
	void WriteRecoveryCopy() const;

	/**
	 * A foldout for the left pane: a header (arrow and title) that shows or hides Body when clicked, with Extra (like a
	 * + Add button) at its right if given. bExpanded is the member that says whether it's open.
	 */
	TSharedRef<SWidget> MakeFoldout(const FText& Title, bool* bExpanded, TSharedRef<SWidget> Body, TSharedPtr<SWidget> Extra = nullptr);
	/** Fills the COMMANDS tab: every command a script can call whose name contains its filter. */
	void RefreshCommandList();
	/** Fills the NODES tab: every node the graph's add-node menu offers whose title contains its filter. */
	void RefreshNodeList();
	/**
	 * Fills List's box with Entries that contain its filter, a foldout per group: Native and Game, or (by category) a
	 * group per category, by name with Trailing's categories last. NoMatch is shown when none do ({0} is the filter).
	 */
	void FillReferenceList(FReferenceList& List, TArray<FReferenceEntry>& Entries, const TArray<FString>& Trailing, const FText& NoMatch);
	/** A NODES line: a card that can be dragged onto the graph to place Entry's node. */
	TSharedRef<SWidget> MakeNodeReferenceRow(const FReferenceEntry& Entry);
	/** A reference tab's contents: its search box and order dropdown, and its list. ConfigKey remembers the order. */
	TSharedRef<SWidget> MakeReferenceTab(FReferenceList& List, const TCHAR* ConfigKey, const FText& SearchHint, const FText& CategoryTooltip, TFunction<void()> Refresh);

	void DeleteSelectedNodes();
	bool CanDeleteSelectedNodes() const;
	/** Disable Node / Enable Node (Ctrl+/): turns the selected nodes off, or back on if they're all off already. */
	void ToggleSelectedNodesDisabled();
	bool CanToggleSelectedNodesDisabled() const;

	/** Copy, Cut, Paste and Duplicate (Ctrl+C, X, V, W). The clipboard holds Unreal's text form of the nodes, as in Blueprint. */
	void CopySelectedNodes();
	bool CanCopySelectedNodes() const;
	void CutSelectedNodes();
	bool CanCutSelectedNodes() const;
	void PasteNodes();
	/** Pastes the clipboard's nodes, centred on Location in the shown graph. */
	void PasteNodesHere(const FVector2f& Location);
	bool CanPasteNodes() const;
	void DuplicateSelectedNodes();
	void Undo();
	void Redo();

	static void Notify(const FText& Message, bool bSuccess);

	// ---- Debugging (while playing in the editor) ----

	/** A running copy of this script, in one of the Play in Editor worlds. */
	struct FDebugEntry
	{
		/** Which Play in Editor world (FWorldContext::PIEInstance). */
		int32 PIEInstance = INDEX_NONE;
		FBangoScriptDebugInfo Info;
	};
	/** The copies of the open script running in every Play in Editor world (and those that failed), as of the last check. */
	TArray<FDebugEntry> DebugEntries;
	/** The copy being debugged: its world and Id. DebugId 0 means none. */
	int32 DebugPIEInstance = INDEX_NONE;
	int64 DebugId = 0;
	/** True if "No debugging" was picked, so a copy isn't picked automatically until playing stops. */
	bool bDebugOff = false;

	/** Ten times a second: lists the running copies, keeps one selected (the only one, automatically), and outlines its nodes. */
	EActiveTimerReturnType UpdateDebugging(double CurrentTime, float DeltaTime);
	void GatherDebugEntries(TArray<FDebugEntry>& OutEntries) const;
	const FDebugEntry* FindDebugEntry() const;
	/** Sets every node's EBangoNodeDebugState from the selected copy. */
	void ApplyDebugStates();
	/** The trail: pings the nodes the followed copy has run since the last check (UBangoEdGraphNode::DebugPingTime). */
	void UpdateDebugTrail();
	/** The script subsystem of a Play in Editor world, or null. */
	static class UBangoScriptSubsystem* FindDebugSubsystem(int32 PIEInstance);
	/** Which copy the trail is from, and how many nodes it had run at the last check. */
	int32 TrailPIEInstance = INDEX_NONE;
	int64 TrailId = 0;
	uint64 TrailSeen = 0;
	/** The followed copy's node states (ApplyDebugStates) and trail pings (UpdateDebugTrail), by NodeID, for the text pane's gutter. */
	TMap<int64, EBangoNodeDebugState> DebugNodeStates;
	TMap<int64, double> DebugNodePings;
	/** The script text the gutter's line-to-node map was made from, and the NodeIDs on each of its lines. */
	FString MarkerText;
	TArray<TArray<int64>> MarkerLineNodes;
	/** Updates the gutter's marks from DebugNodeStates and DebugNodePings. */
	void UpdateLineMarkers();
	/** The line (from 0) of the text pane whose //@ metadata has this NodeID, or INDEX_NONE. */
	int32 FindTextLineOfNode(int64 NodeID);
	/** Picks a copy to debug (Id 0: none) and, with bShowNode, shows the node it's at. */
	void SelectDebugEntry(int32 PIEInstance, int64 Id, bool bShowNode);
	/** The node it's at, or failed at, or INDEX_NONE. */
	static int64 GetDebugNodeID(const FDebugEntry& Entry);
	/** Shows the graph that has the node with this ID, and centres it. */
	void ShowNodeByID(int64 NodeID);
	TSharedRef<SWidget> MakeDebugMenu();
	FText GetDebugLabel() const;
	FText DescribeDebugEntry(const FDebugEntry& Entry) const;
};
