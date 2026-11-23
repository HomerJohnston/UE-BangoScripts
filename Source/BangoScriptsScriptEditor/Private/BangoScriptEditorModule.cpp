#include "BangoConnectionDrawingPolicy.h"
#include "BangoConstant.h"
#include "BangoConstantCustomization.h"
#include "BangoInlineScript.h"
#include "BangoInlineScriptCustomization.h"
#include "BangoLevelStartScriptMenu.h"
#include "BangoIDName.h"
#include "BangoIDNameCustomization.h"
#include "PropertyEditorModule.h"
#include "BangoEditorStyle.h"
#include "BangoGraphCommands.h"
#include "BangoScriptSync.h"
#include "BangoScriptReferences.h"
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
		FBangoGraphCommands::Register();

		// Rings in the level viewports around the actors the script being edited refers to.
		BangoScriptReferences::Register();

		// Bango Script assets and their linked .bango files (spec section 15).
		ScriptSync = MakeUnique<FBangoScriptSync>();
		ScriptSync->Startup();

		// The level toolbar's Bango dropdown: the level's start script (spec section 15).
		LevelStartScriptMenu = MakeUnique<FBangoLevelStartScriptMenu>();
		LevelStartScriptMenu->Startup();

		// Inline scripts and conditions (FBangoInlineScript, FBangoInlineCondition properties): a box to type in, and Edit for a window of their own.
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditor.RegisterCustomPropertyTypeLayout(FBangoInlineScript::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBangoInlineScriptCustomization::MakeInstance));
		PropertyEditor.RegisterCustomPropertyTypeLayout(FBangoInlineCondition::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBangoInlineScriptCustomization::MakeInstance));
		// Bango ID names (FBangoIDName properties): their text box after an '@'.
		PropertyEditor.RegisterCustomPropertyTypeLayout(FBangoIDName::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBangoIDNameCustomization::MakeInstance));
		// Constants (FBangoConstant properties): a name, the type picker and the value box on one row; actors for an entity.
		PropertyEditor.RegisterCustomPropertyTypeLayout(FBangoConstant::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FBangoConstantCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();

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
			.SetIcon(FSlateIcon(FBangoEditorStyle::GetStyleSetName(), FBangoEditorStyle::GetScriptIconName())); // the script icon (the style is set up above)
	}

	virtual void ShutdownModule() override
	{
		BangoScriptReferences::Unregister();
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
		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FBangoInlineScript::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FBangoInlineCondition::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FBangoIDName::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FBangoConstant::StaticStruct()->GetFName());
		}
		if (LevelStartScriptMenu.IsValid())
		{
			LevelStartScriptMenu->Shutdown();
			LevelStartScriptMenu.Reset();
		}
		if (ScriptSync.IsValid())
		{
			ScriptSync->Shutdown();
			ScriptSync.Reset();
		}
		FBangoGraphCommands::Unregister();
		FBangoEditorStyle::Shutdown();
	}

private:
	TUniquePtr<FBangoScriptSync> ScriptSync;
	TUniquePtr<FBangoLevelStartScriptMenu> LevelStartScriptMenu;
	TSharedPtr<FBangoGraphPinFactory> PinFactory;
	TSharedPtr<FBangoConnectionDrawingPolicyFactory> ConnectionFactory;
	TSharedPtr<FBangoGraphNodeFactory> NodeFactory;

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		// The browser's script tabs are document tabs of a tab manager this tab owns, so it's made first.
		const TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab);
		Tab->SetContent(
			SNew(SBangoScriptBrowser)
			.OwnerTab(Tab)
			.OwnerWindow(Args.GetOwnerWindow()));
		return Tab;
	}
};

const FName FBangoScriptEditorModule::TabName(TEXT("BangoScripts"));

IMPLEMENT_MODULE(FBangoScriptEditorModule, BangoScriptsScriptEditor)

#undef LOCTEXT_NAMESPACE
