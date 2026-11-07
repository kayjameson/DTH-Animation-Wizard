#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeletalMesh;

/** One clip in the sampler's index (DTH_AnimationSamples.json). */
struct FDTHAnimIndexEntry
{
	FString Label, File, Samples, Skipped;
	int32 Frames = 0;
	double Fps = 0.0;
	/** Morphs the clip moves off their rest value: v1 does not recreate them. */
	int32 KeyedMorphs = 0;
	double RestoreDriftCm = 0.0;

	/** The clip's pack folder below People/<figure>/Animations, e.g. "AdultDazAnimation". */
	FString Pack() const;
	/** "<pack>/<label>": unique, where labels alone repeat across packs. */
	FString Key() const { return Pack() / Label; }
};

/** The sampler's index of sampled clips. */
struct FDTHAnimIndex
{
	FString File, SampledAt;
	TArray<FDTHAnimIndexEntry> Clips;
	/** Clips the sampler could not sample, with the reason. */
	TArray<FString> Errors;

	bool Load(const FString& InFile, FString& OutError);
	/** The clips whose label or pack contains Filter (all for ""). */
	TArray<const FDTHAnimIndexEntry*> Find(const FString& Filter) const;
	/** A clip by key ("<pack>/<label>"), or by label when only one clip has it. Null (and the candidates in OutAmbiguous) if not. */
	const FDTHAnimIndexEntry* FindLabel(const FString& LabelOrKey, TArray<FString>* OutAmbiguous = nullptr) const;
};

/** A line of a run's report. */
struct FDTHAnimMessage
{
	enum class ESeverity : uint8 { Info, Warning, Error };
	ESeverity Severity = ESeverity::Info;
	FString Text;

	static FDTHAnimMessage Info(const FString& T) { return {ESeverity::Info, T}; }
	static FDTHAnimMessage Warning(const FString& T) { return {ESeverity::Warning, T}; }
	static FDTHAnimMessage Error(const FString& T) { return {ESeverity::Error, T}; }
	/** "WARNING: ..." / "ERROR: ..." / text, for scripting. */
	FString ToLine() const;
};

namespace DTHAnimBatch
{
	/** Where a clip's sequence for this character goes: Root/<character>/<pack>/A_<clip>. */
	FString PackagePath(const USkeletalMesh& Mesh, const FDTHAnimIndexEntry& Clip, const FString& Root);

	/**
	 * Writes each clip as a bone-track sequence for Mesh (DTHAnimBuild::Write: the Pose Wizard's solve, one key per frame
	 * at the clip's rate). bReplace: a sequence already at a clip's path is replaced; otherwise the clip is left as it is
	 * and reported. Ends with the summary and the manual steps. False if any clip failed.
	 */
	bool WriteClips(USkeletalMesh& Mesh, TConstArrayView<const FDTHAnimIndexEntry*> Clips, const FString& Root, bool bReplace,
		TArray<FDTHAnimMessage>& OutReport, TArray<UAnimSequence*>* OutSequences = nullptr);
}
