#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DTHAnimSettings.generated.h"

/**
 * Where the Animation Wizard reads and writes. Project Settings > Plugins > DTH Animation Wizard, stored per user, per
 * project. The Phase 1 checks (DTH.Anim.P1.*) read their inputs from here.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "DTH Animation Wizard"))
class UDTHAnimSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDTHAnimSettings();

	/** A DTH Genesis 9 character: a skeletal mesh with DTH's post-process blueprint, as the DTH import sets it up. */
	UPROPERTY(config, EditAnywhere, Category = "Character", meta = (DisplayName = "Character", AllowedClasses = "/Script/Engine.SkeletalMesh"))
	FSoftObjectPath Character;

	/** The Daz sampler's index of sampled clips (DTH_AnimationSamples.json): the clips the wizard lists and writes. */
	UPROPERTY(config, EditAnywhere, Category = "Clips", meta = (DisplayName = "Daz Samples Index", FilePathFilter = "json"))
	FFilePath SamplesIndex;

	/** Testing only: one clip's samples on base Genesis 9 (the sampler's clips/*.json). */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (DisplayName = "Clip Samples File", FilePathFilter = "json"))
	FFilePath ClipSamplesFile;

	/** Testing only: the same clip sampled on the character itself in Daz (the sampler's "figure" argument), the ground truth. */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (DisplayName = "Character Clip Samples File", FilePathFilter = "json"))
	FFilePath CharacterClipSamplesFile;

	/** Testing only: a multi-actor scene's clips sampled on the character, one file per role (DTH.Anim.P3.*). */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (DisplayName = "Character Scene Samples Files", FilePathFilter = "json"))
	TArray<FFilePath> CharacterSceneSamples;

	/** The Pose Wizard's DTH_PoseSamples.json, to check the clip sampler's UnrealPose against the Pose Wizard's. */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (DisplayName = "Pose Samples File", FilePathFilter = "json"))
	FFilePath PoseSamplesFile;

	/** New animation sequences are created in this folder. */
	UPROPERTY(config, EditAnywhere, Category = "Output", meta = (ContentDir, DisplayName = "Sequence Folder"))
	FDirectoryPath SequenceFolder;

	/** Measurement and test assets go here, and nowhere else. */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (ContentDir, DisplayName = "Test Folder"))
	FDirectoryPath TestFolder;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Saved/DTHAnimationWizard, where reports and screenshots are written. */
	static FString SavedDir();
};
