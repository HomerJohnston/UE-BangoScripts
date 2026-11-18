#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class SWindow;
class FProperty;
struct FBangoInlineScript;

/**
 * One FBangoInlineScript property of one object (spec section 15), for the graph editor to read and write: the
 * object, and the property's path in it as the details panel names it ("OnOpened", "Steps.Steps[2].Script").
 *
 * Writing goes through the details panel's property handle while it's still showing the property (so a Blueprint's
 * default reaches its instances, as typing in the panel does), and otherwise straight into the object, as one undoable
 * change either way.
 */
class FBangoInlineScriptTarget
{
public:
	FBangoInlineScriptTarget(UObject* InOwner, const FString& InPropertyPath, const TSharedPtr<IPropertyHandle>& InStructHandle, const FText& InPropertyLabel);

	/** Which property of which object: one script window per key. */
	FString GetKey() const;
	/** What errors and the debugger call it: the object's name and the property's path ("BP_Door_C_1.OnOpened"). */
	FString GetScriptName() const;
	/** For the window's title and messages: "Door 1: On Opened". */
	FText GetDisplayName() const;

	/** The script's text now. False if the object is gone, or the property can't be found in it any more. */
	bool Read(FString& OutSource) const;
	/** Sets the script's text, as one undoable change. */
	bool Write(const FString& Source, FString& OutError);

	UObject* GetOwner() const { return Owner.Get(); }

	/**
	 * The property is a condition (`meta = (BangoCondition)` on it): its script answers true or false, and the editor
	 * compiles it as one (Bango::CompileCondition), so it may be a single expression, like `_count > 3`.
	 */
	bool IsCondition() const { return bCondition; }

	/**
	 * Finds the FBangoInlineScript at Path in Owner: property names separated by '.', an array's element as
	 * `Name[Index]` (after the array's own name, as the details panel writes it), into structs and objects held by
	 * the object. Null if anything along the way isn't there. OutTopProperty: the object's own property it's in.
	 */
	static FBangoInlineScript* Resolve(UObject* Owner, const FString& Path, FProperty** OutTopProperty = nullptr);

private:
	TWeakObjectPtr<UObject> Owner;
	FString PropertyPath;
	/** The details panel's handle for the struct, while the panel shows it. */
	TWeakPtr<IPropertyHandle> StructHandle;
	FString OwnerLabel;
	FText PropertyLabel;
	bool bCondition = false;
};

/**
 * The inline scripts open in windows of their own (the details panel's Edit), one window per property. While a
 * property's window is open, its box in the details panel can't be typed in (it shows Show Window instead of Edit).
 */
class FBangoInlineScriptWindows
{
public:
	/** Opens a window with the graph editor on the target, or brings its window to the front if it already has one. */
	static void Open(const TSharedRef<FBangoInlineScriptTarget>& Target);
	static bool IsOpen(const FString& Key);

private:
	static TMap<FString, TWeakPtr<SWindow>>& Windows();
};
