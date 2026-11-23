#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class SWindow;
class FProperty;
struct FBangoInlineScriptBase;
struct FBangoInputsSignature;

/**
 * One inline script property (FBangoInlineScript or FBangoInlineCondition) of one object (spec section 15), for the graph editor to read and write: the
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

	/**
	 * The object the script is on. Unreal replaces some objects while they're being edited: a component an actor's
	 * Blueprint gave it is made again every time the actor's construction script reruns, which is after any change
	 * to it, saving this script included. The one it was is then gone, so this finds the one that has taken its
	 * place (same path), and carries on with that.
	 */
	UObject* GetOwner() const;

	/**
	 * The property is a condition (an FBangoInlineCondition): its script answers true or false, and the editor
	 * compiles it as one (Bango::CompileCondition), so it may be a single expression, like `_count > 3`.
	 */
	bool IsCondition() const { return bCondition; }

	/** Whether StructHandle is an FBangoInlineCondition property. */
	static bool IsConditionProperty(const TSharedPtr<IPropertyHandle>& StructHandle);

	/**
	 * The script's inputs signature (FBangoInlineScriptBase::UseInputsSignature), or null if it has none, or, with
	 * OutError, if it isn't registered.
	 */
	TSharedPtr<const FBangoInputsSignature> GetInputsSignature(FString& OutError) const;

	/**
	 * The same for what a details panel row shows: null, with no error, if the objects it shows have different
	 * signatures (several selected).
	 */
	static TSharedPtr<const FBangoInputsSignature> GetInputsSignature(const TSharedRef<IPropertyHandle>& StructHandle, FString& OutError);

	/**
	 * Finds the inline script or condition at Path in Owner: property names separated by '.', an array's element as
	 * `Name[Index]` (after the array's own name, as the details panel writes it), into structs and objects held by
	 * the object. Null if anything along the way isn't there. OutTopProperty: the object's own property it's in.
	 */
	static FBangoInlineScriptBase* Resolve(UObject* Owner, const FString& Path, FProperty** OutTopProperty = nullptr);

private:
	/** The owner as last found (GetOwner): replaced by the object now at OwnerPath when this one is gone. */
	mutable TWeakObjectPtr<UObject> Owner;
	/** The owner's full path when the target was made, to find its replacement by, and for the window's key. */
	FString OwnerPath;
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
