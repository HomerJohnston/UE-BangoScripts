#include "Modules/ModuleManager.h"

#include "BangoIDLabels.h"

// Editor-only support for Bango: the Spawn Bango Script Blueprint node, and the Bango ID labels in the level viewport.
class FBangoScriptsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		BangoIDLabels::Register();
	}

	virtual void ShutdownModule() override
	{
		BangoIDLabels::Unregister();
	}
};

IMPLEMENT_MODULE(FBangoScriptsEditorModule, BangoScriptsEditor)
