#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class SWidget;
struct FBangoNodeLayout;

/**
 * Lets another editor module change how the graph editor draws the nodes of its commands (spec section 15, Node
 * decorators): their title, its colour and icon, the widget for any of their fields' values (a speaker list, a big
 * box for a line of dialogue), the whole middle of the node in place of its fields (MakeFieldsArea), rows under it
 * the node's whole width (MakeRowsArea: a row per entry, with its lines or options), and where any of its pins go
 * (FBangoNodeDecoratorContext::MakePinWidget, by name: Bango::NodePins). Everything else stays the graph editor's:
 * the pins' wiring, editing by typing, undo, errors and the debugger's outline.
 *
 * A node is drawn as its title, then the band (the input pin column, the fields, the output pin column), then the
 * decorator's rows, if it gives any:
 *
 *     | Title                                 |
 *     | (In) | fields area           | (Then) |
 *     | rows area                             |
 *     |   entry 1: condition, line, line  (o) |
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
	/** Its comment (the `//` lines above it, without the slashes; lines separated by '\n'), or empty. */
	FString Comment;
	/** Its compile error, from the last Compile, or empty (a row can outline its line in red). */
	FString ErrorMessage;

	// ---- The node's areas, and its pins ----

	/**
	 * In MakeFieldsArea only: the fields the node would show (MakeFieldValueWidget and HidesField still apply to
	 * them; clicking, typing, Tab and the autocomplete work as usual), to put in the decorator's area. Call it once.
	 * With no field shown (none, or HidesField leaves them all out) it's the null widget, taking no room: the gap
	 * above the first field, which keeps it in line with the pins, is there only when there's a field.
	 */
	TFunction<TSharedRef<SWidget>()> MakeDefaultFieldsArea;
	/** The decorator's names of the node's pins (Bango::NodePins), in the order the node would draw them. */
	TArray<FName> PinNames;
	/**
	 * In MakeFieldsArea and MakeRowsArea only: the pin named Name (Bango::NodePins: Then, Entry(2), ...), to place
	 * there; a wire is drawn to its circle wherever it's put. The null widget if the node has no such pin, or it's
	 * placed already. Call it once per pin; don't keep it. A pin that isn't placed is drawn where it would be (the
	 * input on the left, the outputs in the right-hand column, in their order). A placed pin's label (Then, Body)
	 * isn't drawn: the decorator draws one if it wants.
	 *
	 * The rows area reaches half a pin past the node's body on each side, so a pin at the right end of a row has its
	 * circle's centre on the node's right edge, as the output column's have, and one at a row's left start is on the
	 * left edge. Keep a row's other content half a pin (Bango::NodePins::EdgeInset) in from its ends.
	 */
	TFunction<TSharedRef<SWidget>(FName Name)> MakePinWidget;

	// ---- A command with entries (its block is an entry list: a row per entry, each with its condition) ----

	/** True if the node's block is an entry list. */
	bool bEntries = false;
	/** Each entry's condition, as written; empty for `entry:`, which always passes. */
	TArray<FString> EntryConditions;
	/** Whether each entry is disabled. */
	TArray<bool> EntryDisabled;
	/** The `default:` entry (the last; its condition is empty), which passes when none of the others does, or -1. */
	int32 DefaultEntry = -1;
	/**
	 * Sets entry Entry's condition to Condition, written as it would be in the script (empty for `entry:`), as one
	 * undoable change, as SetFieldValue does. False, with why, if it can't be set.
	 */
	TFunction<bool(int32 Entry, const FString& Condition, FString& OutError)> SetEntryCondition;
	/**
	 * In MakeFieldsArea and MakeRowsArea only: the box the node would show for entry Entry's condition (typing in it,
	 * Tab and Shift+Tab to the next and last, the hint "Always" when it's empty, the red outline for an error), to put
	 * wherever the row wants it; for the default, the box saying Default. Call it once per entry; don't keep it.
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
	/** Remove entry Entry (its wire goes; the nodes on it stay), move it up (Delta -1) or down (+1), disable or enable it: each one undoable change, as the entry's menu does. */
	TFunction<void(int32 Entry)> RemoveEntry;
	TFunction<void(int32 Entry, int32 Delta)> MoveEntry;
	TFunction<void(int32 Entry, bool bDisabled)> SetEntryDisabled;
	/**
	 * Add an entry (after the others, above the default; its condition box opens for typing), or the default, as
	 * the Add pin button and the node's menu do: each one undoable change. A node whose decorator gives a rows area
	 * has no Add pin button: its rows give their own.
	 */
	TFunction<void()> AddEntry;
	TFunction<void()> AddDefaultEntry;
	/** Whether a default can be added (there's none yet, and every enabled entry has a condition). */
	bool bCanAddDefaultEntry = false;

	// ---- Entries drawn in their rows (IBangoNodeDecorator::DrawsEntryStatements) ----

	/** Whether each entry's statements are drawn in its row (the decorator asked, and the entry holds only what it may). */
	TArray<bool> EntryDrawsStatements;
	/**
	 * For an entry drawn in its row: its statements, in order (an empty list for the others), each a context as a
	 * node's is (CommandName, Layout, NodeText, bBlock, bDisabled, Comment, ErrorMessage, SetFieldValue), so the row
	 * can draw and edit each one as the command's own node would be.
	 */
	TArray<TArray<TSharedPtr<FBangoNodeDecoratorContext>>> EntryStatements;
	/**
	 * Editing the statements of an entry drawn in its row, each one undoable change; the node is drawn again after it.
	 * Insert: one statement as written in the script, with or without its ';' (`hojo Say(TEXT("XMS700", "Hi"))`, or
	 * `hojo AddPrompt(TEXT_INV(""), nil) { }` for one with a block, which must be empty), before Index (Index = the
	 * count adds it last). Move: in an entry, or to another drawn in its row (ToIndex counted without the one moved).
	 * False, with why, if it can't be done.
	 */
	TFunction<bool(int32 Entry, int32 Index, const FString& Statement, FString& OutError)> InsertEntryStatement;
	TFunction<bool(int32 Entry, int32 Index, FString& OutError)> RemoveEntryStatement;
	TFunction<bool(int32 FromEntry, int32 FromIndex, int32 ToEntry, int32 ToIndex, FString& OutError)> MoveEntryStatement;
	TFunction<bool(int32 Entry, int32 Index, bool bDisabled, FString& OutError)> SetEntryStatementDisabled;
	/** While the debugger follows a copy at one of the statements drawn in a row: that statement, else -1 for both. */
	int32 DebugStatementEntry = -1;
	int32 DebugStatementIndex = -1;
	/** The statement just inserted or moved, for the row to give keyboard focus to (once), else -1 for both. */
	int32 FocusStatementEntry = -1;
	int32 FocusStatementIndex = -1;
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
	/**
	 * The middle of the node, between its input pins and its output pins, in place of its fields; null for the usual
	 * fields. Context.MakeDefaultFieldsArea makes those, to put in it (with something under them, say); edits go
	 * through Context.SetFieldValue. Pins can be placed in it (Context.MakePinWidget). Made again whenever the node is
	 * drawn again (after every change).
	 */
	virtual TSharedPtr<SWidget> MakeFieldsArea(const FBangoNodeDecoratorContext& Context) { return nullptr; }
	/**
	 * Rows under the band, the node's whole width; null for none. For a command with entries, the entries are drawn
	 * here only (a row per entry, with Context.MakeEntryConditionWidget for its condition, its statements if it's drawn
	 * in its row, and its pin: Context.MakePinWidget(Bango::NodePins::Entry(Entry)), or Default), and the output
	 * column has only Then and the pins not placed. Without a rows area, the entries are drawn in the output column,
	 * a condition box beside each pin. Made again whenever the node is drawn again.
	 */
	virtual TSharedPtr<SWidget> MakeRowsArea(const FBangoNodeDecoratorContext& Context) { return nullptr; }
	/**
	 * For a command whose entries hold only certain commands (FBangoCommandSignature::EntriesHold): true to draw entry
	 * Entry's statements in its row (Context.EntryStatements, for MakeRowsArea) rather than as nodes wired from
	 * its output. Asked when the graph is made, and for a new entry. An entry holding anything else (or the default,
	 * when it may hold anything) is drawn as nodes all the same. Context has the node's text and entries, but not
	 * yet its statements.
	 */
	virtual bool DrawsEntryStatements(const FBangoNodeDecoratorContext& Context, int32 Entry) const { return false; }
};

/**
 * The decorator's names of a node's pins, for FBangoNodeDecoratorContext::MakePinWidget and PinNames. They're kept as
 * they are whatever the node calls its pins; they're worked out each time the node is drawn, so a statement's name
 * follows it when it's moved.
 */
namespace Bango::NodePins
{
	/** The exec input. */
	extern BANGOSCRIPTSSCRIPTEDITOR_API const FName In;
	/** Where the script goes after the node. */
	extern BANGOSCRIPTSSCRIPTEDITOR_API const FName Then;
	/** A block command's block, when it's an ordinary block (not entries). */
	extern BANGOSCRIPTSSCRIPTEDITOR_API const FName Body;
	/** The `default:` entry's output (it isn't also Entry(N)). */
	extern BANGOSCRIPTSSCRIPTEDITOR_API const FName Default;
	/** Entry Entry's output (counted as EntryConditions is): Entry0, Entry1, ... */
	BANGOSCRIPTSSCRIPTEDITOR_API FName Entry(int32 Entry);
	/** The output of statement Index of entry Entry, drawn in its row (one with a block of its own): Entry0.Statement1. */
	BANGOSCRIPTSSCRIPTEDITOR_API FName Statement(int32 Entry, int32 Index);
	/** Half a pin: how far a rows area reaches past the node's body on each side (see MakePinWidget). */
	inline constexpr float EdgeInset = 5.0f;
}

namespace Bango
{
	/** Draws CommandName's nodes with Decorator, in place of any decorator it had. */
	BANGOSCRIPTSSCRIPTEDITOR_API void RegisterNodeDecorator(const FString& CommandName, const TSharedRef<IBangoNodeDecorator>& Decorator);
	/** Back to the usual drawing for CommandName's nodes. */
	BANGOSCRIPTSSCRIPTEDITOR_API void UnregisterNodeDecorator(const FString& CommandName);
	/** CommandName's decorator, or null. */
	BANGOSCRIPTSSCRIPTEDITOR_API TSharedPtr<IBangoNodeDecorator> FindNodeDecorator(const FString& CommandName);
}
