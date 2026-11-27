#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class SWidget;
struct FBangoNodeLayout;

/**
 * Lets another editor module change how the graph editor draws the nodes of its commands (spec section 12, Node
 * decorators): their title, its colour and icon, the widget for any of their fields' values (a speaker list, a big
 * box for a line of dialogue), and something more under the fields (a play button, a line's ID). Everything else
 * stays the graph editor's: the pins, the other fields, editing by typing, undo, errors and the debugger's outline.
 *
 * The node's text is still what's saved: a decorator's widget changes a field through the context's SetFieldValue,
 * which edits the text as typing in the field would (one undoable change), and the node is drawn again from it.
 *
 * Register one per command name when the module starts, and unregister it when it shuts down (before the module's
 * code is gone):
 *
 *     Bango::RegisterNodeDecorator(TEXT("SpeechLine"), MakeShared<FMySpeechLineDecorator>());
 *     ...
 *     Bango::UnregisterNodeDecorator(TEXT("SpeechLine"));
 *
 * It's used for nodes that call the command: a command's node or a block command's (the command's name is
 * FBangoNodeLayout::CallName). Nodes already on screen pick a new decorator up when they're next drawn again.
 */

/** What a decorator is told about one node. */
struct FBangoNodeDecoratorContext
{
	/** The command the node calls. */
	FString CommandName;
	/** The node's layout: its fields, with their text as written (BangoGraphLayout.h, in the BangoScripts module). */
	const FBangoNodeLayout* Layout = nullptr;
	/** The node's statement, as written in the script. */
	FString NodeText;
	/** True for a block command's node (it has Body and Then outputs). */
	bool bBlock = false;
	/** True if the node is disabled (commented out). */
	bool bDisabled = false;
	/**
	 * Sets field FieldIndex (an index into Layout->Fields) to Value, written as it would be in the script
	 * (`"Happy"`, `TEXT("UXA010", "Hello")`, `2`; Bango::GetFieldValueFromTyped, in BangoGraphEdit.h, makes that from what a person
	 * typed), as one undoable change, and draws the node again. False, with why, if it can't be set. Empty (unbound)
	 * where the node can't be changed, as for a title.
	 */
	TFunction<bool(int32 FieldIndex, const FString& Value, FString& OutError)> SetFieldValue;

	// ---- A command with entries (its block is an entry list: a row per entry, each with its condition) ----

	/** True if the node's block is an entry list. */
	bool bEntries = false;
	/** Each entry's condition, as written; empty for `entry:`, which always passes. */
	TArray<FString> EntryConditions;
	/** Whether each entry is disabled. */
	TArray<bool> EntryDisabled;
	/**
	 * Sets entry Entry's condition to Condition, written as it would be in the script (empty for `entry:`), as one
	 * undoable change, as SetFieldValue does. False, with why, if it can't be set.
	 */
	TFunction<bool(int32 Entry, const FString& Condition, FString& OutError)> SetEntryCondition;
	/**
	 * In MakeEntryRowWidget only: the box the node would show for entry Entry's condition (typing in it, Tab and
	 * Shift+Tab to the next and last, the hint "Always" when it's empty, the red outline for an error), to put
	 * wherever the row wants it. Call it once per row; don't keep it.
	 */
	TFunction<TSharedRef<SWidget>(int32 Entry)> MakeEntryConditionWidget;
	/**
	 * While a game plays in the editor and the debugger follows a copy of the script with this node's block open:
	 * true, with whether each entry passed when its conditions were last worked out, and the entry running (-1 none).
	 * The node is drawn again when they change.
	 */
	bool bHasEntryDebug = false;
	TArray<bool> DebugEntryPassed;
	int32 DebugRunningEntry = -1;
};

/** Changes how one command's nodes are drawn. Every function is optional: by default the node is drawn as usual. */
class BANGOSCRIPTSSCRIPTEDITOR_API IBangoNodeDecorator
{
public:
	virtual ~IBangoNodeDecorator() = default;

	/** The title, in place of the command's name. */
	virtual TOptional<FText> GetTitle(const FBangoNodeDecoratorContext& Context) const { return TOptional<FText>(); }
	/** The title bar's colour, in place of the command blue. */
	virtual TOptional<FLinearColor> GetTitleColor(const FBangoNodeDecoratorContext& Context) const { return TOptional<FLinearColor>(); }
	/** An icon before the title (and its tint), or an invalid icon for none. */
	virtual FSlateIcon GetTitleIcon(const FBangoNodeDecoratorContext& Context, FLinearColor& OutTint) const { return FSlateIcon(); }
	/** The node's tooltip, in place of the command's description. */
	virtual TOptional<FText> GetTooltip(const FBangoNodeDecoratorContext& Context) const { return TOptional<FText>(); }

	/**
	 * The widget for a field's value, in place of the usual box (the field's label and icon stay). Null for the usual
	 * box. To change the value, it calls Context.SetFieldValue. It's made again whenever the node is drawn again
	 * (after every change), so it can show what Context's layout says.
	 */
	virtual TSharedPtr<SWidget> MakeFieldValueWidget(const FBangoNodeDecoratorContext& Context, int32 FieldIndex) { return nullptr; }
	/** True to leave a field's row out of the node altogether (it's still in the text, and can be set by SetFieldValue). */
	virtual bool HidesField(const FBangoNodeDecoratorContext& Context, int32 FieldIndex) const { return false; }
	/** Something to show under the fields, or null. */
	virtual TSharedPtr<SWidget> MakeExtraContent(const FBangoNodeDecoratorContext& Context) { return nullptr; }
	/**
	 * For a command with entries: the widget for entry Entry's row, beside its output pin, in place of the usual
	 * condition box (Context.MakeEntryConditionWidget makes that box, to put in the row). Null for the usual box.
	 * Made again whenever the node is drawn again.
	 */
	virtual TSharedPtr<SWidget> MakeEntryRowWidget(const FBangoNodeDecoratorContext& Context, int32 Entry) { return nullptr; }
};

namespace Bango
{
	/** Draws CommandName's nodes with Decorator, in place of any decorator it had. */
	BANGOSCRIPTSSCRIPTEDITOR_API void RegisterNodeDecorator(const FString& CommandName, const TSharedRef<IBangoNodeDecorator>& Decorator);
	/** Back to the usual drawing for CommandName's nodes. */
	BANGOSCRIPTSSCRIPTEDITOR_API void UnregisterNodeDecorator(const FString& CommandName);
	/** CommandName's decorator, or null. */
	BANGOSCRIPTSSCRIPTEDITOR_API TSharedPtr<IBangoNodeDecorator> FindNodeDecorator(const FString& CommandName);
}
