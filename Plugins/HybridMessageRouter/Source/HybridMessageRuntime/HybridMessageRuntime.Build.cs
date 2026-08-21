using UnrealBuildTool;

public class HybridMessageRuntime : ModuleRules
{
    public HybridMessageRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
                "GameplayTags"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
            });
    }
}
