using UnrealBuildTool;

public class BlueprintMCPServer : ModuleRules
{
    public BlueprintMCPServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "ToolMenus",
            "LevelEditor",
            "AssetRegistry",
            "AssetTools",
            "Kismet",
            "KismetCompiler",
            "BlueprintGraph",
            "EditorStyle",
            "HttpServer",
            "HTTPServer",
            "Projects"
        });
    }
}
