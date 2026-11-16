// Bango script references: rings in the level editor's viewports around the actors the script being edited refers
// to (its @name entities), and a list of the names no actor in the level has. Drawn through UDebugDrawService, under
// Show > Bango Script References.

#pragma once

#include "CoreMinimal.h"

class SWidget;

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
	 * reference under the cursor gets a thicker ring. Its rings are only drawn while it keeps doing this, so they go
	 * away when its tab is hidden or it's closed.
	 */
	void TouchActiveScript(const SWidget* Editor, int32 CursorLine, int32 CursorColumn);
}
