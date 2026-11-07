#pragma once

#include "Modules/ModuleInterface.h"

class FDTHAnimationWizardModule : public IModuleInterface
{
public:
	/** The plugin's Resources folder on disk (the Daz clip sampler lives there). */
	static FString ResourcesDir();
};
