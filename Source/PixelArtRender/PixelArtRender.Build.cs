using System.IO;
using UnrealBuildTool;

// Render-side half of the pixel pipeline: the scene view extension that drives the internal
// resolution and the custom spatial upscaler (integer nearest upscale + sub-texel shift).
// Loaded at PostConfigInit so the /PixelArt3D shader mapping exists before global shaders compile.
public class PixelArtRender : ModuleRules
{
	public PixelArtRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// ISpatialUpscaler lives in the renderer's private headers; re-check on every engine upgrade.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"));
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Renderer", "Projects" });
	}
}
