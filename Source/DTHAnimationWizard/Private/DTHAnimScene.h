#pragma once

#include "CoreMinimal.h"
#include "DTHAnimBatch.h"

class ULevelSequence;
class USkeletalMesh;
class UAnimSequence;

/**
 * A multi-actor animation: clips made to play together, one per role. Daz's paired clips are applied each to a figure at
 * the scene origin and sampled in world space, so each role's samples already hold its placement relative to the
 * others (measured: DTH.Anim.P3.SameAsOneScene).
 */
struct FDTHAnimScene
{
	struct FRole
	{
		FString Label;
		const FDTHAnimIndexEntry* Clip = nullptr;
	};

	FString Name;
	TArray<FRole> Roles;

	/** "<pack>/<name>": unique, where names alone repeat across packs. */
	FString Key() const { return Roles.IsEmpty() ? Name : Roles[0].Clip->Pack() / Name; }
};

namespace DTHAnimScenes
{
	/**
	 * The index's multi-actor scenes: clips in one pack whose labels differ only in their last word (the role, e.g.
	 * "001_... For G9 Female" and "001_... For G9 Male" make the scene "001_... For G9" with roles Female and Male).
	 */
	TArray<FDTHAnimScene> FromIndex(const FDTHAnimIndex& Index);

	/**
	 * Writes each role's sequence for the character assigned to it (by role label) and builds Root/Scenes/<pack>/LS_<scene>:
	 * one spawnable actor per role with its character, its sequence on an animation track from frame 0, every actor at
	 * the same origin (the samples carry their placement), the playback range the longest role, the display rate the
	 * clips'. bReplace replaces sequences and the level sequence already there. Null and an error line on failure.
	 */
	ULevelSequence* Build(const FDTHAnimScene& Scene, const TMap<FString, USkeletalMesh*>& Assigned, const FString& Root, bool bReplace,
		TArray<FDTHAnimMessage>& OutReport, TMap<FString, UAnimSequence*>* OutSequences = nullptr);
}
