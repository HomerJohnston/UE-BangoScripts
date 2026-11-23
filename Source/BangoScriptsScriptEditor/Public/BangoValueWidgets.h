#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
struct FSlateBrush;

/**
 * The script editor's widgets for a value's type and a fixed value, as its SCRIPT VARS and GLOBAL VARS rows use
 * them, for other editors to use too (a details panel row for a constant, say). They need no script editor, document
 * or graph around them: just a type, a value's text, and callbacks. Spec section 15, Value widgets.
 *
 * A value here is **Bango text**: what a script would have after `=` (`[0, 0, 100]`, `"hello"`, `@john`,
 * `INVTEXT("Hi")`), or empty for none. Bango::ParseFixedValue (BangoCompiler.h, in the runtime module) checks one and
 * gives its FBangoValue, in the editor and in a packaged game alike.
 *
 * When the type changes, SCRIPT VARS clears the value (a value made for the old type rarely fits the new one),
 * unless it's `nil`, which fits any type: SBangoValueBox::GetValueAfterTypeChange does that for whoever owns both.
 */

/** A type was picked in an SBangoTypePicker: its name ("vector3", "entity[]"). */
DECLARE_DELEGATE_OneParam(FOnBangoTypePicked, const FString& /*TypeName*/);
/** A value was typed into an SBangoValueBox and passed its check: as Bango text, or empty for none. */
DECLARE_DELEGATE_OneParam(FOnBangoValueCommitted, const FString& /*BangoText*/);
/** A value typed into an SBangoValueBox didn't pass its check, and the box went back to the value it had: why. */
DECLARE_DELEGATE_OneParam(FOnBangoValueRejected, const FString& /*Error*/);
/** An actor was picked in an SBangoActorNamePicker: its Bango ID name, without the '@' ("john"). */
DECLARE_DELEGATE_OneParam(FOnBangoActorNamePicked, const FString& /*Name*/);
/** Checks a value (as Bango text, never empty) of a type for an SBangoValueBox: false, with why, if it can't be used. */
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnCheckBangoValue, const FString& /*TypeName*/, const FString& /*BangoText*/, FString& /*OutError*/);

/**
 * The type picker: a button showing a type's icon (as the graph's nodes show it), whose menu lists the types a
 * value can have (Bango::GetValueTypeNames: each type, then an array of each) and reports the one picked. It doesn't
 * change anything itself: OnTypePicked's owner changes what Type gives.
 */
class BANGOSCRIPTSSCRIPTEDITOR_API SBangoTypePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoTypePicker)
		: _ShowTypeName(false)
	{
	}
		/** The type shown: its name, like "int" or "vector3[]". */
		SLATE_ATTRIBUTE(FString, Type)
		/** Also shows the type's name beside its icon (SCRIPT VARS shows it in a column of its own instead). */
		SLATE_ARGUMENT(bool, ShowTypeName)
		SLATE_EVENT(FOnBangoTypePicked, OnTypePicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The menu by itself: the types, then an array of each, with Current ticked. OnPick gets the one chosen. */
	static TSharedRef<SWidget> MakeTypeMenu(const FString& Current, TFunction<void(const FString&)> OnPick);
	/** A type's icon and tint, as the picker and the graph's nodes show it. */
	static const FSlateBrush* GetTypeIcon(const FString& TypeName, FLinearColor& OutTint);

private:
	TAttribute<FString> Type;
	FOnBangoTypePicked OnTypePicked;
};

/**
 * The actor picker for values written as text: a small button whose menu lists the actors in the open level that
 * have a named Bango ID (a Bango ID component with a name), by `@name`, and reports the name picked. For places that
 * hold an entity as `@name` text (a script variable's starting value, a node's field); an actor without a named ID
 * can't be written that way, so it isn't listed. It doesn't change anything itself.
 */
class BANGOSCRIPTSSCRIPTEDITOR_API SBangoActorNamePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoActorNamePicker)
	{
	}
		SLATE_EVENT(FOnBangoActorNamePicked, OnActorNamePicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** An actor in the open level with a named Bango ID. */
	struct FNamedActor
	{
		/** Its Bango ID name, without the '@'. */
		FString Name;
		/** The actor's label in the level (its name in the Outliner). */
		FString Label;
	};
	/** The open level's actors with a named Bango ID, sorted by name. Empty while there's no editor world. */
	static void GetNamedActors(TArray<FNamedActor>& OutActors);
	/**
	 * What an entity value becomes when an actor is picked for it. An `entity`: `@name`. An `entity[]`: the array
	 * with `@name` added at its end (`[@a]` becomes `[@a, @name]`; none, `nil` or anything that isn't an array
	 * becomes `[@name]`).
	 */
	static FString AddNameToValue(const FString& TypeName, const FString& BangoText, const FString& Name);

private:
	FOnBangoActorNamePicked OnActorNamePicked;

	TSharedRef<SWidget> MakeMenu() const;
};

/**
 * The value box: a one-line box for a fixed value of a type. An `entity` or an `entity[]` has an actor picker
 * beside the box (SBangoActorNamePicker), which writes the picked actor's `@name` (added to an array's end). It's typed as a graph node's field is: a string or a
 * text as plain words, without quotes (`=` first for the code instead, like `=nil`); anything else as code (`5`,
 * `[0, 0, 100]`, `@john`, `true`). It's checked as it's typed, with the error shown on the box, and when it's
 * committed (Enter, or the focus leaving): a value that passes is reported, as Bango text, and one that doesn't is
 * dropped, the box going back to the value it had.
 */
class BANGOSCRIPTSSCRIPTEDITOR_API SBangoValueBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoValueBox)
		: _UseScriptNamespace(false)
	{
	}
		/** The value's type: its name, like "vector3". */
		SLATE_ATTRIBUTE(FString, Type)
		/** The value, as Bango text; empty for none. */
		SLATE_ATTRIBUTE(FString, Value)
		/** Shown, greyed, while the box is empty ("nil", say). */
		SLATE_ATTRIBUTE(FText, HintText)
		/**
		 * For a `text` value typed as plain words: the namespace it's given, as NSLOCTEXT("Namespace", "<a new key>",
		 * "words"). Empty (the default): it's written INVTEXT("words"), a text that's never translated.
		 */
		SLATE_ARGUMENT(FString, TextNamespace)
		/** For a value inside a script (SCRIPT VARS): a `text` typed as plain words is a LOCTEXT, using the script's own namespace. */
		SLATE_ARGUMENT(bool, UseScriptNamespace)
		/** Checks a value. Not bound: Bango::ParseFixedValue, so it's checked just as it will be read. */
		SLATE_EVENT(FOnCheckBangoValue, OnCheckValue)
		SLATE_EVENT(FOnBangoValueCommitted, OnValueCommitted)
		SLATE_EVENT(FOnBangoValueRejected, OnValueRejected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * What a value becomes when its type changes, as SCRIPT VARS does it: empty (none), unless it's `nil`, which fits
	 * any type and stays.
	 */
	static FString GetValueAfterTypeChange(const FString& BangoText);

private:
	TAttribute<FString> Type;
	TAttribute<FString> Value;
	FString TextNamespace;
	bool bUseScriptNamespace = false;
	FOnCheckBangoValue OnCheckValue;
	FOnBangoValueCommitted OnValueCommitted;
	FOnBangoValueRejected OnValueRejected;
	TSharedPtr<SEditableTextBox> TextBox;

	/** What the box shows for the value it has: a string's or text's words, anything else as written. */
	FText GetShownText() const;
	/** What was typed, as Bango text (empty for none). */
	FString ToBangoText(const FString& Typed) const;
	bool Check(const FString& BangoText, FString& OutError) const;
	/** An actor was picked beside the box: its `@name` becomes the value (or is added to an array's end). */
	void HandleActorNamePicked(const FString& Name);
	void HandleTextChanged(const FText& NewText);
	void HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
};
