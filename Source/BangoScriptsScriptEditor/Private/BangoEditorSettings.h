#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BangoEditorSettings.generated.h"

/**
 * Editor Preferences > Plugins > Bango: your own preferences for editing Bango scripts. Saved for you alone (in
 * EditorPerProjectUserSettings.ini), not with the project.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Bango"))
class UBangoEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * Script text boxes (the script editor's text pane, and inline scripts' boxes) compile the script as you type, and
	 * show its first error: the error underlined in red, and its message under the box.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Text Editing")
	bool bCompileAsYouType = true;

	/** How long after you stop typing the script is compiled, in seconds. Until then, the last error is hidden. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Editing", meta = (EditCondition = "bCompileAsYouType", ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float CompileAsYouTypeDelay = 0.5f;

	/**
	 * Tab puts in the autocomplete list's entry (in script text boxes and graph node fields). Off: Tab closes the list
	 * and does what it would do without it (a tab in the text, the next field on a node).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Autocomplete", meta = (DisplayName = "Accept With Tab"))
	bool bAcceptCompletionWithTab = true;

	/**
	 * Enter puts in the autocomplete list's entry (in script text boxes and graph node fields). Off: Enter closes the
	 * list and does what it would do without it (a new line in the text, applying the field on a node).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Autocomplete", meta = (DisplayName = "Accept With Enter"))
	bool bAcceptCompletionWithEnter = true;
};
