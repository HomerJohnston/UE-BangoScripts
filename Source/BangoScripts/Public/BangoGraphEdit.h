#pragma once

#include "CoreMinimal.h"
#include "BangoGraph.h"
#include "BangoGraphLayout.h"

struct FBangoCompileError;

/** One parameter of an entry node, as written: `Type Name` or `Type Name = Default`. */
struct BANGOSCRIPTS_API FBangoEntryParam
{
	FString Type;
	FString Name;
	/** Only the main graph's parameters (#define params) can have defaults. */
	bool bHasDefault = false;
	FString Default;

	bool operator==(const FBangoEntryParam& Other) const
	{
		return Type == Other.Type && Name == Other.Name && bHasDefault == Other.bHasDefault && (!bHasDefault || Default == Other.Default);
	}
	bool operator!=(const FBangoEntryParam& Other) const { return !(*this == Other); }
};

/**
 * Editing helpers for the graph editor. Every edit is checked so a node's text always stays valid
 * code for its kind: that's what keeps a saved script readable, by the compiler and by the graph editor.
 */
namespace Bango
{
	/**
	 * Checks that Text is valid for a node of this kind: one line, and it reads as code (like `if (<Text>)` for an
	 * If node). Types and names aren't checked here; Compile does that. On failure, OutError says what's wrong.
	 */
	BANGOSCRIPTS_API bool ValidateNodeText(EBangoGraphNodeKind Kind, bool bInFunctionGraph, const FString& Text, FString& OutError);

	/**
	 * For statement text (a declaration, assignment, ++/--, or call): which of those kinds it is. Text that
	 * stores a call's result (`double _t = Random(...)`, `_t = Random(...)`) is a declaration or assignment.
	 */
	BANGOSCRIPTS_API bool GetStatementKind(const FString& Text, EBangoGraphNodeKind& OutKind);

	/** True if the editor lets the user type into this field (it has a value and isn't locked). */
	BANGOSCRIPTS_API bool CanEditField(const FBangoNodeLayout& Layout, int32 FieldIndex);

	/**
	 * Sets one field of Node (as laid out by GetNodeLayout with the same context and bShowExpression) to Value; a
	 * field with MirrorBegins changes in every place it's written. The value is trimmed;
	 * an empty value clears an optional field, with its separators. The new text is validated: on success
	 * Node.Text (and, for statements, Node.Kind) change; on failure Node is unchanged and OutError says why.
	 */
	BANGOSCRIPTS_API bool SetNodeField(FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context, int32 FieldIndex, const FString& Value, FString& OutError, bool bShowExpression = false);

	/** The field whose text contains character Offset of the node's text (or touches it), or INDEX_NONE. */
	BANGOSCRIPTS_API int32 FindFieldAt(const FBangoNodeLayout& Layout, int32 Offset);

	/** Where a compile error is in a document: a graph (INDEX_NONE for the main graph), a node, and an offset in its text. */
	struct FBangoErrorPlace
	{
		int32 Graph = INDEX_NONE;
		int32 Node = INDEX_NONE;
		/** Character offset in the node's text, or INDEX_NONE if the error isn't inside the node's text. */
		int32 Offset = INDEX_NONE;
	};

	/**
	 * Finds the node a compile error belongs to, using its NodeID (node 0 is the main graph's entry).
	 * WrittenText is the text the error came from (as written by WriteGraphText), used to find the offset.
	 * Returns false if the error has no node (like an error in a function line).
	 */
	BANGOSCRIPTS_API bool FindErrorPlace(const FBangoGraphDocument& Document, const FString& WrittenText, const FBangoCompileError& Error, FBangoErrorPlace& OutPlace);

	/**
	 * Renames a variable (a local, a parameter or a global) in a node's text: every identifier OldName becomes
	 * NewName, except a vector part after a `.` and a call (followed by `(`). Returns true if anything changed.
	 * Text that doesn't lex is left alone.
	 */
	BANGOSCRIPTS_API bool RenameVariableInText(FString& Text, const FString& OldName, const FString& NewName);

	/**
	 * Checks a local's starting value, as the Locals list writes it after `Type _name = `: it must read, and be a
	 * fixed value (a literal, a negated number, `nil`, a text literal, or an array of those). Its type is checked
	 * when compiling. False, with OutError saying why, if it isn't.
	 */
	BANGOSCRIPTS_API bool CheckDeclValue(const FString& Type, const FString& Value, FString& OutError);

	/**
	 * Reads the parameters of an entry node's text: `void Name(int _a, string _b)` in a function graph, or the
	 * main graph's `#define params` list without its parentheses (`entity _who, int _times = 1`). Each part is
	 * kept as written. False if the text doesn't read as a parameter list.
	 */
	BANGOSCRIPTS_API bool ReadEntryParams(const FString& EntryText, bool bInFunctionGraph, TArray<FBangoEntryParam>& OutParams);

	/**
	 * EntryText with its parameter list replaced by Params (the rest, like a function's return type and name, is
	 * kept). The result is validated as an entry node's text; on failure OutError says why.
	 */
	BANGOSCRIPTS_API bool WriteEntryParams(const FString& EntryText, bool bInFunctionGraph, const TArray<FBangoEntryParam>& Params, FString& OutText, FString& OutError);

	/**
	 * Renames a function where Text calls it, and in a signature naming it: every `OldName(` becomes `NewName(`
	 * (not inside strings or comments, and not a longer name that starts the same). Returns true if Text changed.
	 */
	BANGOSCRIPTS_API bool RenameFunctionInText(FString& Text, const FString& OldName, const FString& NewName);

	/** The next free NodeID in a document (one more than the largest in use). */
	BANGOSCRIPTS_API int64 GetNextNodeID(const FBangoGraphDocument& Document);

	// ---- Text fields ----
	//
	// A field whose type is string is edited as plain text: the designer types `Hello, "friend"` and the node gets
	// `"Hello, \"friend\""`. Typing `\n` or `\t` gives a line break or tab, and `\\` a backslash; a backslash before
	// anything else is just a backslash. Text that starts with `=` is an expression instead (`=_name`,
	// `=Format("%0", _n)`); to start the text itself with `=`, type `\=`.
	//
	// A field whose type is text (localized) works the same way, but the node gets a text literal: typing `Hello`
	// gives `LOCTEXT("<new key>", "Hello")`, and retyping an existing text keeps its namespace, key and notes. The
	// key, namespace and notes are edited separately (see FBangoTextLiteral).

	/** True if the field holds a string or a text: its value is shown and typed as plain text (see above). */
	BANGOSCRIPTS_API bool IsTextField(const FBangoNodeField& Field);

	/** True if the field holds localized text (its type is text). */
	BANGOSCRIPTS_API bool IsLocalizedTextField(const FBangoNodeField& Field);

	/** If Text is exactly one string literal (`"a\"b"`), its value (`a"b`). */
	BANGOSCRIPTS_API bool TryGetStringLiteral(const FString& Text, FString& OutValue);

	/** Value as a string literal, with quotes and escapes: `a"b` -> `"a\"b"`. */
	BANGOSCRIPTS_API FString MakeStringLiteral(const FString& Value);

	/** A string's value as the designer types it (escapes for line breaks, tabs, and backslashes that need them). */
	BANGOSCRIPTS_API FString StringToTypedText(const FString& Value);
	/** What the designer typed, as a string's value. StringToTypedText's reverse. */
	BANGOSCRIPTS_API FString TypedTextToString(const FString& Typed);

	/**
	 * What a field's edit box starts with. For a text field: the string's value as typed text, nothing if the field
	 * isn't there yet, or `=` and the expression if it holds something other than a string literal. Otherwise the
	 * field's text as written.
	 */
	BANGOSCRIPTS_API FString GetFieldEditText(const FBangoNodeField& Field);

	/**
	 * What a field shows when it isn't being edited: for a text field, the string's value (with real line breaks),
	 * or `=` and the expression. Otherwise the field's text.
	 */
	BANGOSCRIPTS_API FString GetFieldDisplayText(const FBangoNodeField& Field);

	/**
	 * What to pass to SetNodeField for what the designer typed into a field. For a text field: a string literal,
	 * the expression after a leading `=`, or nothing (to clear an optional field) for an empty box.
	 */
	BANGOSCRIPTS_API FString GetFieldValueFromTyped(const FBangoNodeField& Field, const FString& Typed);

	/** A warning about one field of a node: shown in yellow, and doesn't stop the script from compiling. */
	struct FBangoFieldWarning
	{
		int32 Field = INDEX_NONE;
		FString Message;
	};

	/**
	 * Warnings Compile shows for a node: each placeholder (Hint's %2) that has no value, or `nil`.
	 * A missing one is saved as nil (see FillMissingPlaceholders), so the text shows "nil" there.
	 */
	BANGOSCRIPTS_API void GetNodeWarnings(const FBangoNodeLayout& Layout, TArray<FBangoFieldWarning>& OutWarnings);

	/**
	 * Gives every placeholder without a value a `nil`, in order: `Hint("%0 %1 %2", 1)` becomes
	 * `Hint("%0 %1 %2", 1, nil, nil)`. This is how the graph editor saves (and compiles) a node whose
	 * placeholder rows are left missing. Returns true if the text changed.
	 */
	BANGOSCRIPTS_API bool FillMissingPlaceholders(FBangoGraphNode& Node, bool bInFunctionGraph, const FBangoNodeLayoutContext& Context);
	/** FillMissingPlaceholders for every node of a document. Returns true if any text changed. */
	BANGOSCRIPTS_API bool FillMissingPlaceholders(FBangoGraphDocument& Document, const FBangoNodeLayoutContext& Context);
}
