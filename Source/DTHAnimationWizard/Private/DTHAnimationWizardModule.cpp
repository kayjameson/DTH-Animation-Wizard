#include "DTHAnimationWizardModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDTHAnimation, Log, All);

FString FDTHAnimationWizardModule::ResourcesDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTHAnimationWizard"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources")) : FString();
}

IMPLEMENT_MODULE(FDTHAnimationWizardModule, DTHAnimationWizard)
