using UnrealBuildTool;

// Editor-only validation module. Deliberately depends on nothing that pulls in
// the Kismet / BlueprintEditor graph (BlueprintEditorModule.cpp:214 does check(GEditor),
// which aborts standalone runs).
public class PICOValidate : ModuleRules
{
	public PICOValidate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// RenderCore: FShaderType / FGlobalShaderMap used by the shader check.
		// (PICOOpenXRHMD is not a dependency: PICO_Shaders.h lives in its Private folder,
		//  so the plugin's shader types are resolved by registered name instead.)
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
	}
}
