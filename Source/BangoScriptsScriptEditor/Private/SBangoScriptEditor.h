#pragma once

#include "CoreMinimal.h"
#include "BangoGraph.h"
#include "BangoGraphEdit.h"
#include "BangoGraphLayout.h"
#include "EditorUndoClient.h"
#include "Types/SlateStructs.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FUICommandList;
class SBox;
class SGraphEditor;
class SInlineEditableTextBlock;
class SMultiLineEditableTextBox;
class SBangoTextEditor;
class SScrollBox;
class SScrollBar;
class STextBlock;
class ITableRow;
class STableViewBase;
class UEdGraph;
struct FBangoNodeLayoutContext;

/**
 * The editor for one script: its graphs (the main graph and its functions) listed on the left, and the picked graph
 * on the right under Compile, Save and the script's name. Everything about one open script lives here, so the same
 * widget can sit in the Bango Scripts tab (SBangoScriptBrowser, which picks the script) or in a window of its own.
 *
 * The .bango file is the source of truth: opening reads it into editor graphs, and Save writes them back
 * (Bango::WriteGraphText). Unsaved changes show as a * in the title.
 */
class SBangoScriptEditor : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SBangoScriptEditor)
		: _EmptyMessage(NSLOCTEXT("SBangoScriptEditor", "NoScript", "No script is open."))
	{}
		/** A script to open straight away, as a path inside the script folder without extension ("Examples/Hello"). */
		SLATE_ARGUMENT(FString, Script)
		/** What the graph area says while no script is open. */
		SLATE_ARGUMENT(FText, EmptyMessage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBangoScriptEditor() override;

	/** The folder scripts are in (Content/ plus the Script Directory setting), as a full path. */
	static FString GetScriptFolder();

	/**
	 * Opens a script (a path inside the script folder without extension), replacing the open one. Doesn't ask about
	 * unsaved changes: call ConfirmCloseScript first.
	 */
	void LoadScript(const FString& Script);
	/** Opens the open script again from its file, to pick up changes made outside the editor. Doesn't ask either. */
	void ReloadScript();
	/**
	 * If there are unsaved changes, asks whether to save them. Returns false if the user cancelled. Answering No
	 * throws the changes away: they aren't kept as a recovery copy when the editor closes.
	 */
	bool ConfirmCloseScript();

	/** The open script ("Examples/Hello"), or empty. */
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
	/** How many line numbers LineNumbers shows. */
	int32 LineNumberCount = 0;
	/** Shown or hidden with the tab on the graph area's left edge. Hidden to start with. */
	bool bTextPaneOpen = false;
	/** The text as last put in the box (from the graphs, or the file if it didn't read), so typing in it can be told apart. */
	FString TextShown;
	/** The box has been typed in since TextShown: the graphs don't change it until the typing is applied or reverted. */
	bool bTextEdited = false;
	/** Why the text in the box couldn't be shown as a graph (from Apply, or opening a file that doesn't read). Empty if it can. */
	FString TextError;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<FUICommandList> GraphCommands;

	/** The open script, as a path inside the script folder without extension ("Examples/Hello"). Empty if none. */
	FString OpenScript;
	FString OpenPath;
	FDateTime OpenFileTime;

	/** Editor graphs for the open script: [0] is the main graph, then one per function. */
	TArray<TStrongObjectPtr<UEdGraph>> EdGraphs;
	/** Which of EdGraphs is shown, or INDEX_NONE. */
	int32 ShownGraph = INDEX_NONE;

	/** Commands and this script's functions, shared by every node's layout. Updated when a function's signature changes. */
	TSharedPtr<FBangoNodeLayoutContext> LayoutContext;

	/** The document as last opened or saved; the script has unsaved changes when the graphs differ from it. */
	FBangoGraphDocument SavedDocument;
	bool bDirty = false;

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
	FReply OnCompileClicked();
	FReply OnSaveClicked();
	/** Writes the open script to its file. Returns false if it couldn't (and says why). */
	bool Save();
	/** If the editor goes away with unsaved changes, keeps a copy in Saved/Bango/Unsaved. */
	void WriteRecoveryCopy() const;

	void DeleteSelectedNodes();
	bool CanDeleteSelectedNodes() const;

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
};
