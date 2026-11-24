using UnrealBuildTool;

// The benchmark (the Bango.Benchmark console command): nothing depends on this module, and the plugin leaves it
// out of Shipping builds (BangoScripts.uplugin).
public class BangoScriptsBenchmark : ModuleRules
{
	public BangoScriptsBenchmark(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"BangoScripts",
			}
		);
	}
}
