#include "BangoConnectionDrawingPolicy.h"
#include "BangoEditorStyle.h"
#include "BangoEdGraphSchema.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SBangoGraphNode.h"
#include "SBangoGraphPin.h"
#include "SBangoScriptBrowser.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "BangoScriptEditor"

/** Draws Bango exec pins as circles on the node's edge (SBangoGraphPin). */
struct FBangoGraphPinFactory : public FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
	{
		if (Pin && Pin->PinType.PinCategory == UBangoEdGraphSchema::PC_BangoExec)
		{
			return SNew(SBangoGraphPin, Pin);
		}
		return nullptr;
	}
};

/** Editor module for the Bango graph editor ("Bango Scripts" tab). */
class FBangoScriptEditorModule : public IModuleInterface
{
public:
	static const FName TabName;

	virtual void StartupModule() override
	{
		FBangoEditorStyle::Initialize();

		PinFactory = MakeShared<FBangoGraphPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
		ConnectionFactory = MakeShared<FBangoConnectionDrawingPolicyFactory>();
		FEdGraphUtilities::RegisterVisualPinConnectionFactory(ConnectionFactory);
		NodeFactory = MakeShared<FBangoGraphNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FBangoScriptEditorModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Bango Scripts"))
			.SetTooltipText(LOCTEXT("TabTooltip", "Browse Bango scripts and view them as node graphs."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));
	}

	virtual void ShutdownModule() override
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
		}
		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}
		if (ConnectionFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinConnectionFactory(ConnectionFactory);
			ConnectionFactory.Reset();
		}
		if (NodeFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
			NodeFactory.Reset();
		}
		FBangoEditorStyle::Shutdown();
	}

private:
	TSharedPtr<FBangoGraphPinFactory> PinFactory;
	TSharedPtr<FBangoConnectionDrawingPolicyFactory> ConnectionFactory;
	TSharedPtr<FBangoGraphNodeFactory> NodeFactory;

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBangoScriptBrowser)
			];
	}
};

const FName FBangoScriptEditorModule::TabName(TEXT("BangoScripts"));

IMPLEMENT_MODULE(FBangoScriptEditorModule, BangoScriptsScriptEditor)

#undef LOCTEXT_NAMESPACE
