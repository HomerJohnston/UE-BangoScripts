using UnrealBuildTool;

/** The Bango graph editor: the "Bango Scripts" tab under the Tools menu. */
public class BangoScriptsScriptEditor : ModuleRules
{
	public BangoScriptsScriptEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// What its public headers need (Public/BangoValueWidgets.h: the value widgets other editor modules can use).
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Slate",
				"SlateCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"DeveloperSettings",
				"InputCore",
				"ApplicationCore",
				"UnrealEd",
				"GraphEditor",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"Projects",
				"AssetRegistry",
				"AssetDefinition",
				"ContentBrowser",
				"DirectoryWatcher",
				"SourceControl",
				"PropertyEditor",
				"Settings",
				"ToolWidgets",
				"BangoScripts",
			}
		);
	}
}
