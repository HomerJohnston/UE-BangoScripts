#pragma once

#include "CoreMinimal.h"

struct FBangoGraph;
struct FBangoGraphDecl;
struct FBangoGraphDocument;
struct FBangoNodeLayoutContext;
class UBangoEdGraphNode;
class UEdGraph;

namespace BangoScriptEditor
{
	/**
	 * Makes an editor graph (UBangoEdGraphNode nodes, exec wires) for one Bango graph. The graph lives in the
	 * transient package (never saved as an asset) and is transactional, so edits can be undone.
	 */
	UEdGraph* CreateEdGraph(const FBangoGraph& Graph, const TSharedPtr<const FBangoNodeLayoutContext>& LayoutContext);

	/** Puts the script's #define LOCTEXT_NAMESPACE on the main graph's Script Start node. */
	void SetLocNamespace(UEdGraph* MainGraph, bool bHasLocNamespace, const FString& LocNamespace);

	/** Puts the file's global declarations on the main graph's Script Start node (CreateEdGraph puts each graph's locals on its entry). */
	void SetGlobals(UEdGraph* MainGraph, const TArray<FBangoGraphDecl>& Globals);

	/** Puts the script's description on the main graph's Script Start node. */
	void SetDescription(UEdGraph* MainGraph, const FString& Description);

	/** A graph's entry node, or null. */
	UBangoEdGraphNode* FindEntryNode(const UEdGraph* Graph);

	/**
	 * Reads editor graphs back into a document: EdGraphs[0] is the main graph, the rest are functions in order.
	 * Positions are rounded to whole grid units. If OutNodes isn't null, it gets each graph's nodes in the
	 * document's node order (OutNodes[0] for the main graph). Fails if a graph has no entry node.
	 * A node added in the editor (NodeID 0, like a new reroute) is given the next free NodeID here.
	 */
	bool BuildDocument(const TArray<UEdGraph*>& EdGraphs, FBangoGraphDocument& OutDocument, TArray<TArray<UBangoEdGraphNode*>>* OutNodes, FString& OutError);
}
