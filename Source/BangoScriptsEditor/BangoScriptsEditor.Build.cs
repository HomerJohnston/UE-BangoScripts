using UnrealBuildTool;

public class BangoScriptsEditor : ModuleRules
{
	public BangoScriptsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"BangoScripts",
				"BlueprintGraph",
				"KismetCompiler",
				"UnrealEd",
			}
		);
	}
}
