#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FSelectOutlineModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Must run in PostConfigInit, before the global shader map is compiled.
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SelectOutline"));
		check(Plugin.IsValid());
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/SelectOutline"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));

		// The project's own Shaders folder (used by /Game/StableMaterials) used to be mapped by the old
		// version of this plugin. Keep that mapping alive so those materials keep compiling.
		const FString ProjectShaders = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
		if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Project")) && FPaths::DirectoryExists(ProjectShaders))
		{
			AddShaderSourceDirectoryMapping(TEXT("/Project"), ProjectShaders);
		}
	}
};

IMPLEMENT_MODULE(FSelectOutlineModule, SelectOutline)
