#include "DTHAnimSettings.h"

#include "Misc/Paths.h"

UDTHAnimSettings::UDTHAnimSettings()
{
	SequenceFolder.Path = TEXT("/Game/DTH_Animations");
	TestFolder.Path = TEXT("/Game/DTH_Animations/_Test");
	// Where the Daz sampler writes by default: Daz's documents folder (Documents/DAZ 3D/Studio4).
	SamplesIndex.FilePath = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("DAZ 3D/Studio4/DTH Animation Wizard/DTH_AnimationSamples.json"));
}

FString UDTHAnimSettings::SavedDir()
{
	return FPaths::ProjectSavedDir() / TEXT("DTHAnimationWizard");
}
