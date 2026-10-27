#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FTerragenShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Cooked builds carry compiled shaders and don't ship the source folder, so only map it when present.
		const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("ClimbingGameAssets/TerragenShader/Shaders")));
		if (FPaths::DirectoryExists(ShaderDir))
		{
			AddShaderSourceDirectoryMapping(TEXT("/TerragenShaders"), ShaderDir);
		}
	}
};

IMPLEMENT_MODULE(FTerragenShadersModule, TerragenShaders)
