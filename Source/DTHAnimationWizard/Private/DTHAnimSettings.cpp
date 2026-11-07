#include "DTHAnimSettings.h"

#include "Misc/Paths.h"

UDTHAnimSettings::UDTHAnimSettings()
{
	SequenceFolder.Path = TEXT("/Game/DTH_Animations");
	TestFolder.Path = TEXT("/Game/DTH_Animations/_Test");
}

FString UDTHAnimSettings::SavedDir()
{
	return FPaths::ProjectSavedDir() / TEXT("DTHAnimationWizard");
}
