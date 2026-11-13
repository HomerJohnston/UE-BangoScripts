using UnrealBuildTool;

/** The Bango graph editor: the "Bango Scripts" tab under the Tools menu. */
public class BangoScriptsScriptEditor : ModuleRules
{
	public BangoScriptsScriptEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"ApplicationCore",
				"Slate",
				"SlateCore",
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
				"BangoScripts",
			}
		);
	}
}
