using UnrealBuildTool;

public class PixelArt3D : ModuleRules
{
	public PixelArt3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "PixelArtRender"
		});
	}
}
