using UnrealBuildTool;

public class HybridMessageNodes : ModuleRules
{
    public HybridMessageNodes(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "KismetCompiler",
                "PropertyEditor",
                "HybridMessageRuntime",
                "UnrealEd"
            });

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "BlueprintGraph",
            });
    }
}
