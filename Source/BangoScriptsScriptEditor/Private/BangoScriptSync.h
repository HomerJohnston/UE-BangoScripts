#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IDirectoryWatcher.h"

class UBangoScriptAsset;
class UPackage;
struct FAssetData;

/**
 * Keeps Bango Script assets and their linked .bango files in step (Bango spec section 15). A linked asset's script is
 * the .bango file next to it (same folder, same name), and the file always wins:
 *
 * - New .bango files in Content are imported as linked assets: found by a background scan when the editor starts, and
 *   by watching Content while it runs.
 * - When a linked file changes, its asset's copy of the text is refreshed from it (and saved). A hash of the file,
 *   kept in the asset, tells a real change from the editor's own writes.
 * - A linked asset whose file is deleted becomes embedded. Moving or renaming a linked asset moves its file; deleting
 *   one asks whether to delete its file too.
 */
class FBangoScriptSync
{
public:
	void Startup();
	void Shutdown();

	// ---- Files and assets (used by the factories, the asset's commands and the graph editor) ----

	/** The .bango file a script asset links to: next to it, with its name. Full path. */
	static FString GetLinkedFilePath(const FString& PackageName);
	/** The script asset a .bango file in Content belongs to ("/Game/Quests/Intro"), or false if it isn't in a content folder. */
	static bool GetPackageNameForFile(const FString& FilePath, FString& OutPackageName);
	/** A hash of a file's bytes (empty if it can't be read). */
	static FString HashFile(const FString& FilePath);
	/** Writes a .bango file, checking it out or adding it with source control. False, with OutError, if it can't. */
	static bool WriteScriptFile(const FString& FilePath, const FString& Text, FString& OutError);

	/** Linked: reads the asset's file into its text (and hash), and saves the asset. False if the file can't be read. */
	static bool RefreshFromFile(UBangoScriptAsset& Asset);
	/** Embedded → linked: writes the asset's text to the file next to it (asking first if one is there), and saves the asset. */
	static bool ExportToFile(UBangoScriptAsset& Asset);
	/** Linked → embedded: keeps the text and leaves the file (which is then ignored for this asset). */
	static void Unlink(UBangoScriptAsset& Asset);
	/** Makes a linked asset for a .bango file in Content and saves it. Null if it can't. */
	static UBangoScriptAsset* ImportFile(const FString& FilePath, const FString& PackageName);

	/** Saves script assets (when not playing in the editor; while playing, once it ends). */
	static void SaveAssets(const TArray<UBangoScriptAsset*>& Assets);

private:
	/** The Asset Registry has found every asset: look for new and changed .bango files, in the background. */
	void OnFilesLoaded();
	void StartScan();
	/** On the game thread, with the scan's .bango files and their hashes. */
	void OnScanFinished(TArray<TPair<FString, FString>> Files);

	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);
	bool OnTick(float DeltaTime);
	/** A .bango file was added, changed or deleted (once it's been quiet a moment). */
	void HandleFileChange(const FString& FilePath);

	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void OnInMemoryAssetCreated(UObject* Object);
	void OnAssetsPreDelete(const TArray<UObject*>& Objects);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnEndPIE(bool bIsSimulating);

	void RegisterMenus();

	FDelegateHandle WatcherHandle;
	FString WatchedFolder;
	FTSTicker::FDelegateHandle TickerHandle;
	/** .bango files that changed, and when they last did: handled once they've been quiet a moment (editors save in steps). */
	TMap<FString, double> PendingChanges;
	/** Linked files to delete once their assets are deleted (the user said yes), by package name. */
	TMap<FString, FString> FilesToDeleteWithAssets;
	bool bStarted = false;
};
