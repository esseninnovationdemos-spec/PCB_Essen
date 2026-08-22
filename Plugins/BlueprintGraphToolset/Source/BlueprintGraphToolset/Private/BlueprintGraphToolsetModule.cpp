#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "BlueprintGraphToolset.h"

class FBlueprintGraphToolsetModule : public IModuleInterface
{
	void StartupModule()
	{
		UToolsetRegistry::RegisterToolsetClass(UBlueprintGraphToolset::StaticClass());
	}

	void ShutdownModule()
	{
		UToolsetRegistry::UnregisterToolsetClass(UBlueprintGraphToolset::StaticClass());
	}
};

IMPLEMENT_MODULE(FBlueprintGraphToolsetModule, BlueprintGraphToolset)
