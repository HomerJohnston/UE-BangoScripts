#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "BangoProgram.h"
#include "K2Node_SpawnBangoScript.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UK2Node_CallFunction;
class UBangoScriptAsset;

/**
 * Blueprint node: Spawn Bango Script.
 *
 * Pick a Bango Script asset on the node and it shows one input pin per parameter in the
 * script's params line, named after the parameter, with the script's defaults filled in.
 *  - Required parameters always pass the pin's value.
 *  - Optional parameters count as "not passed" (so the script uses its own default) while their pin is
 *    unconnected and still shows the script's default.
 *  - Entities are Actor pins; an unconnected or null actor passes nil.
 * When the script's params line changes, right-click the node and choose Refresh Node. Wires on pins
 * whose name and type didn't change are kept.
 *
 * When the Blueprint compiles, the node becomes a chain of UBangoBlueprintLibrary calls that collect
 * the arguments and spawn the script.
 */
UCLASS()
class UK2Node_SpawnBangoScript : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	// UK2Node
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostReconstructNode() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsNodePure() const override { return false; }

private:
	static const FName ScriptPinName;
	static const FName HandlePinName;

	bool LoadParams(TArray<FBangoScriptParamInfo>& OutParams, FString& OutError) const;
	bool IsParamPin(const UEdGraphPin* Pin) const;
	/** Adds a pin for each of the script's parameters (or records why it can't). */
	void CreateParamPins();
	/** Replaces the parameter pins after the script path changes, keeping matching wires. */
	void RefreshParamPins();
	/** After a refresh: pins still showing the old script default are updated to the new default. */
	void ApplyChangedDefaults();
	void UpdateErrorDisplay();

	/** The script picked on the node (a hard reference: the Blueprint loads it, and packages it). */
	UPROPERTY()
	TObjectPtr<UBangoScriptAsset> Script;

	/** Legacy: a script path typed on nodes made before scripts were assets. Turned into Script when the node is next rebuilt. */
	UPROPERTY()
	FString ScriptPath;

	/** A legacy ScriptPath becomes Script, if there's a script asset at that path (relative to /Game, or to the old /Game/Bango). */
	void MigrateScriptPath();

	/** The default text each parameter pin was given at the last refresh. */
	UPROPERTY()
	TMap<FName, FString> ParamDefaults;

	TMap<FName, FString> PreviousParamDefaults;
	FString LoadError;
};
