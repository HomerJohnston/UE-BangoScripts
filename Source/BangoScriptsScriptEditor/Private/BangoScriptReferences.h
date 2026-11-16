// Bango script references: small filled circles in the level editor's viewports over the actors the script being
// edited refers to (its @name entities), lines to them from self (an inline script's actor), and a list of the names
// no actor in the level has. Drawn through UDebugDrawService, under Show > Bango Script References.

#pragma once

#include "CoreMinimal.h"

class SWidget;
class AActor;
class UObject;

namespace BangoScriptReferences
{
	void Register();
	void Unregister();

	/**
	 * The text editor being typed in reports its lines here (when it gets keyboard focus, and when its text changes):
	 * it becomes the script whose references are drawn, until another one does. Lines are what the editor shows.
	 */
	void SetActiveScript(const TSharedRef<const SWidget>& Editor, const TArray<FString>& Lines);

	/** Whether Editor is the script being drawn. */
	bool IsActiveScript(const SWidget* Editor);

	/**
	 * Called each tick by the active editor while it's on screen, with its cursor (0-based line, column): the
	 * reference under the cursor gets a bigger circle. Its markers are only drawn while it keeps doing this, so they go
	 * away when its tab is hidden or it's closed. Self: the actor `self` would be (an inline script's), or null; lines
	 * are drawn from it to the markers.
	 */
	void TouchActiveScript(const SWidget* Editor, int32 CursorLine, int32 CursorColumn, const AActor* Self);

	/**
	 * The actor `self` would be in an inline script on Owner (the object the script is a property of): Owner if it's
	 * an actor, a component's actor, or null.
	 */
	AActor* GetSelfActor(UObject* Owner);
}
