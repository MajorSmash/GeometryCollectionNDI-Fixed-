#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "GeoCollectionNDIFix"

class FGeoCollectionNDIFixModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GeoCollectionNDIFix"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/GeoCollectionNDIFix"), PluginShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGeoCollectionNDIFixModule, GeoCollectionNDIFix)
