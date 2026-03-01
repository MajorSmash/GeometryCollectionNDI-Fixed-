using UnrealBuildTool;

public class GeoCollectionNDIFix : ModuleRules
{
	public GeoCollectionNDIFix(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Niagara",
				"NiagaraCore",
				"NiagaraShader",
				"NiagaraVertexFactories",
				"VectorVM",
				"RHI",
				"RenderCore",
				"Chaos",
				"GeometryCollectionEngine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"Projects",
			}
		);
	}
}
