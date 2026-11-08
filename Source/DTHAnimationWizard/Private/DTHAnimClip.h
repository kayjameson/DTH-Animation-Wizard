#pragma once

#include "CoreMinimal.h"
#include "DTHPoseSamples.h"

class UAnimSequence;
class USkeletalMesh;

/**
 * One clip as Resources/Daz/DTH_SampleAnimations.dsa sampled it: every frame of every role (figure) in Daz world space.
 * Phase 1 clips have one role; multi-actor scenes have one per figure, sampled together.
 */
struct FDTHAnimClip
{
	struct FRole
	{
		FString Label, Figure;
		/** UnrealPose (DTH ROM frame 1) on this figure: the rest the solve measures from. */
		FDTHDazState Rest;
		/** The figure as loaded, before the clip. */
		FDTHDazState Zero;
		TArray<FDTHDazState> Frames;
	};

	FString Label, Pack, File, DazVersion, SampledAt;
	double Fps = 30.0;
	int32 LastKey = 0;
	/** Morphs the clip keys: v1 writes bone tracks only and reports these (plan, open question 7). */
	TArray<FString> KeyedMorphs;
	/** A Genesis 8 clip carried onto Genesis 9 in Daz (the sampler's "source" block); Figure is empty for a G9 clip. */
	struct FSource
	{
		FString Figure;
		double HipScale = 1.0;
		/** G9's world rotations against G8's after the rest alignment (only locked G9 channels leave a gap). */
		double RotationGapDeg = 0.0;
		FString RotationGapBone;
		/** The angle between each G9 bone segment and G8's. */
		double SegmentGapDeg = 0.0;
		FString SegmentGapBone;
		/** Ankle height change against G8's, scaled by the hip heights (the two figures' leg proportions differ). */
		double FootHeightGapCm = 0.0;
		/** G9 channels past G9's joint limits to follow G8, worst first: "bone axis" and degrees past. */
		TArray<TPair<FString, double>> BeyondG9Limits;

		/** One line for reports. */
		FString Describe() const;
	} Source;
	TArray<FRole> Roles;

	bool Load(const FString& InFile, FString& OutError);

	/** The clip's frame rate as Unreal keeps it (whole frames per second when Daz's step is one). */
	FFrameRate FrameRate() const;
};

namespace DTHAnimBuild
{
	/**
	 * Writes Role of Clip as a bone-track sequence for Mesh's skeleton at the clip's frame rate, one key per frame, with
	 * the Pose Wizard's measured solve (DTHPoseCore). A name already taken gets a number.
	 */
	UAnimSequence* Write(const USkeletalMesh& Mesh, const FDTHAnimClip& Clip, int32 Role, const FString& Folder, const FString& Name,
		FString& OutError);
}
