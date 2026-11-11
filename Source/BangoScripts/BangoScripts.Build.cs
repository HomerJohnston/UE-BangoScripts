using UnrealBuildTool;

public class BangoScripts : ModuleRules
{
	public BangoScripts(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public: game modules include Bango headers that use these (subsystems, settings, commands).
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
			}
		);
	}
}
