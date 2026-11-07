#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "DTHAnimClip.h"
#include "DTHAnimSettings.h"
#include "DTHBodyHarness.h"
#include "DTHPoseCharacter.h"
#include "DTHPoseMeasure.h"
#include "DTHPoseSamples.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString WriteReport(const FString& Name, const TArray<FString>& Lines)
	{
		const FString File = UDTHAnimSettings::SavedDir() / TEXT("phase1") / Name;
		FFileHelper::SaveStringArrayToFile(Lines, *File);
		return FPaths::ConvertRelativePathToFull(File);
	}

	struct FContext
	{
		USkeletalMesh* Mesh = nullptr;
		FString TestFolder;
		FDTHAnimClip Clip;

		bool Load(FAutomationTestBase& Test)
		{
			const UDTHAnimSettings* S = GetDefault<UDTHAnimSettings>();
			Mesh = Cast<USkeletalMesh>(S->Character.TryLoad());
			if (!Mesh || !Mesh->GetSkeleton())
			{
				Test.AddError(TEXT("Set Character (Project Settings > Plugins > DTH Animation Wizard) to a DTH character's skeletal mesh."));
				return false;
			}
			TestFolder = S->TestFolder.Path;
			if (!TestFolder.StartsWith(TEXT("/Game/")) || !TestFolder.Contains(TEXT("_Test")))
			{
				Test.AddError(TEXT("Set Test Folder to a _Test content folder."));
				return false;
			}
			FString Error;
			if (!Clip.Load(S->ClipSamplesFile.FilePath, Error))
			{
				Test.AddError(TEXT("Clip Samples File: ") + Error);
				return false;
			}
			return true;
		}

		/** The clip's sequence on the character, written afresh under TestFolder/Clips. */
		UAnimSequence* WriteSequence(FAutomationTestBase& Test) const
		{
			const FString Folder = TestFolder / TEXT("Clips");
			const FString Name = TEXT("A_") + Mesh->GetName() + TEXT("_") + DTHPoseCharacter::AssetName(Clip.Label);
			if (UObject* Old = LoadObject<UObject>(nullptr, *(Folder / Name + TEXT(".") + Name)))
			{
				ObjectTools::ForceDeleteObjects({Old}, false);
			}
			FString Error;
			UAnimSequence* Sequence = DTHAnimBuild::Write(*Mesh, Clip, 0, Folder, Name, Error);
			if (!Sequence) Test.AddError(Error);
			return Sequence;
		}
	};
}

// The clip sampler's rest is the Pose Wizard's: UnrealPose (DTH ROM frame 1) on base Genesis 9. If it differs, every
// frame is measured from another rest than the poses are.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimP1UnrealPose, "DTH.Anim.P1.UnrealPoseMatchesPoseWizard", Flags)
bool FDTHAnimP1UnrealPose::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this)) return false;
	FDTHPoseSamples Poses;
	FString Error;
	if (!Poses.Load(GetDefault<UDTHAnimSettings>()->PoseSamplesFile.FilePath, Error) || Poses.RomFrames.Num() < 2)
	{
		AddError(TEXT("Set Pose Samples File to the Pose Wizard's DTH_PoseSamples.json (with its ROM)."));
		return false;
	}
	const FDTHDazState& Mine = C.Clip.Roles[0].Rest;
	const FDTHDazState& Theirs = Poses.RomFrames[1];
	double MaxCm = 0.0, MaxDeg = 0.0;
	FName CmBone, DegBone;
	int32 Missing = 0;
	for (const TPair<FName, FDTHDazBone>& B : Theirs.Bones)
	{
		const FDTHDazBone* M = Mine.Bones.Find(B.Key);
		if (!M) { ++Missing; continue; }
		const double Cm = FVector::Dist(M->Pos, B.Value.Pos);
		const double Deg = FMath::RadiansToDegrees(M->Rot.AngularDistance(B.Value.Rot));
		if (Cm > MaxCm) { MaxCm = Cm; CmBone = B.Key; }
		if (Deg > MaxDeg) { MaxDeg = Deg; DegBone = B.Key; }
	}
	AddInfo(FString::Printf(TEXT("Clip rest vs Pose Wizard UnrealPose: %d bones, %d missing; max %.6f cm (%s), %.6f deg (%s)."),
		Theirs.Bones.Num(), Missing, MaxCm, *CmBone.ToString(), MaxDeg, *DegBone.ToString()));
	if (Missing > 0 || MaxCm > 0.001 || MaxDeg > 0.001) AddError(TEXT("The clip sampler's UnrealPose is not the Pose Wizard's."));
	return true;
}

// Plan Phase 1 pass: the clip written on the character and played through its post-process blueprint, against the same
// clip Daz evaluated on the character itself, on every frame, by the Pose Wizard's rule (DTHPoseMeasure: every joint
// within 0.5 cm, every non-twist bone within 0.5 deg). Also measures what keys alone cannot show (open question 4): the
// sequence between keys, against the halfway blend of the keys either side, so an Euler flip between frames shows up.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimP1ClipRecreated, "DTH.Anim.P1.ClipRecreated", Flags)
bool FDTHAnimP1ClipRecreated::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this)) return false;
	FDTHAnimClip Own;
	FString Error;
	if (!Own.Load(GetDefault<UDTHAnimSettings>()->CharacterClipSamplesFile.FilePath, Error))
	{
		AddError(TEXT("Character Clip Samples File (the clip sampled on the character in Daz): ") + Error);
		return false;
	}
	const TArray<FDTHDazState>& Frames = C.Clip.Roles[0].Frames;
	if (Own.Roles[0].Frames.Num() != Frames.Num())
	{
		AddError(FString::Printf(TEXT("The two samples have %d and %d frames."), Frames.Num(), Own.Roles[0].Frames.Num()));
		return false;
	}
	const double StartWrite = FPlatformTime::Seconds();
	UAnimSequence* Sequence = C.WriteSequence(*this);
	if (!Sequence) return false;
	const double WriteSeconds = FPlatformTime::Seconds() - StartWrite;
	AddInfo(FString::Printf(TEXT("%s: %d frames at %s written in %.2f s (%d keys, %.3f s long)."), *C.Clip.Label, Frames.Num(),
		*C.Clip.FrameRate().ToPrettyText().ToString(), WriteSeconds, Sequence->GetNumberOfSampledKeys(), Sequence->GetPlayLength()));
	if (Sequence->GetNumberOfSampledKeys() != Frames.Num()) AddError(TEXT("The sequence does not have one key per frame."));

	FDTHBodyHarness Harness;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	const DTHPoseSolve::FRig Rig = DTHPoseCharacter::Make(*C.Mesh, C.Clip.Roles[0].Rest);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
	const double Step = 1.0 / C.Clip.Fps;

	TArray<FString> Lines{TEXT("frame\tmax_cm\tcm_bone\tmax_deg\tdeg_bone\tmax_twist_deg\ttwist_bone\thalfway_deg\thalfway_bone")};
	int32 Failed = 0;
	DTHPoseMeasure::FError Worst;
	int32 WorstFrame = 0;
	double WorstHalfway = 0.0;
	FName WorstHalfwayBone;
	int32 WorstHalfwayFrame = 0;
	FDTHBoneReadback Previous;
	for (int32 F = 0; F < Frames.Num(); ++F)
	{
		FDTHBoneReadback Played;
		Harness.EvalSequence(Sequence, F * Step, Played);
		const DTHPoseMeasure::FError E = DTHPoseMeasure::Compare(Rig, Ref, Played, Own.Roles[0].Rest, Own.Roles[0].Frames[F]);
		// Between keys: the sequence halfway from the last frame, against the halfway blend of the two keys.
		double Halfway = 0.0;
		FName HalfwayBone;
		if (F > 0)
		{
			FDTHBoneReadback Mid;
			Harness.EvalSequence(Sequence, (F - 0.5) * Step, Mid);
			for (int32 I = 0; I < Played.Bones.Num(); ++I)
			{
				if (DTHPoseSolve::IsSkipped(Played.Bones[I])) continue;
				const FQuat Blend = FQuat::Slerp(Previous.Local[I].GetRotation(), Played.Local[I].GetRotation(), 0.5);
				const double Deg = FMath::RadiansToDegrees(Mid.Local[I].GetRotation().AngularDistance(Blend));
				if (Deg > Halfway) { Halfway = Deg; HalfwayBone = Played.Bones[I]; }
			}
		}
		Lines.Add(FString::Printf(TEXT("%d\t%.4f\t%s\t%.4f\t%s\t%.4f\t%s\t%.4f\t%s"), F, E.MaxCm, *E.CmBone.ToString(), E.MaxDeg,
			*E.DegBone.ToString(), E.MaxTwistDeg, *E.TwistBone.ToString(), Halfway, *HalfwayBone.ToString()));
		if (!E.Passes()) ++Failed;
		if (E.MaxCm + E.MaxDeg > Worst.MaxCm + Worst.MaxDeg) { Worst = E; WorstFrame = F; }
		if (Halfway > WorstHalfway) { WorstHalfway = Halfway; WorstHalfwayBone = HalfwayBone; WorstHalfwayFrame = F; }
		Previous = MoveTemp(Played);
	}
	AddInfo(FString::Printf(TEXT("%d of %d frames within 0.5 cm / 0.5 deg. Worst frame %d: %s."), Frames.Num() - Failed, Frames.Num(),
		WorstFrame, *Worst.Describe()));
	AddInfo(FString::Printf(TEXT("Between keys: largest departure from the halfway blend %.4f deg (%s, frames %d-%d)."), WorstHalfway,
		*WorstHalfwayBone.ToString(), WorstHalfwayFrame - 1, WorstHalfwayFrame));
	AddInfo(TEXT("Report: ") + WriteReport(TEXT("clip_recreated.tsv"), Lines));
	if (!C.Clip.KeyedMorphs.IsEmpty())
	{
		AddWarning(FString::Printf(TEXT("The clip keys %d morphs; v1 writes bone tracks only."), C.Clip.KeyedMorphs.Num()));
	}
	if (Failed > 0) AddError(FString::Printf(TEXT("%d frames are not recreated within 0.5 cm / 0.5 deg."), Failed));
	if (WorstHalfway > 1.0) AddError(TEXT("The sequence leaves the blend of its keys between frames by more than 1 deg (a key flip)."));
	return true;
}

// The look at fixed frames (first, a quarter, half, three quarters, last): the whole body front, side and back, and the
// face (skin, eyes, lids, teeth, tongue) front and three-quarter, through DTH's deformer and materials in the harness's
// preview scene (the level and the editor camera are never touched). Written to Saved/DTHAnimationWizard/phase1/screens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimP1Screenshots, "DTH.Anim.P1.Screenshots", Flags)
bool FDTHAnimP1Screenshots::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this)) return false;
	UAnimSequence* Sequence = C.WriteSequence(*this);
	if (!Sequence) return false;
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	USkeletalMeshComponent* Body = Harness.GetComponent();
	const FString Dir = UDTHAnimSettings::SavedDir() / TEXT("phase1") / TEXT("screens");
	const int32 Last = C.Clip.Roles[0].Frames.Num() - 1;
	const FString Clip = DTHPoseCharacter::AssetName(C.Clip.Label);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
	const int32 HeadIndex = Ref.FindBoneIndex(TEXT("head"));
	if (HeadIndex == INDEX_NONE) { AddError(TEXT("The character has no head bone.")); return false; }
	const FQuat HeadRest = DTHPoseSolve::ComponentSpace(Ref, [&Ref](int32 I) { return Ref.GetRefBonePose()[I]; })[HeadIndex].GetRotation();
	int32 Shots = 0;
	for (const int32 F : {0, Last / 4, Last / 2, 3 * Last / 4, Last})
	{
		FDTHBoneReadback Played;
		Harness.EvalSequence(Sequence, F / C.Clip.Fps, Played);
		Body->UpdateBounds();
		const FVector Center = Body->Bounds.Origin;
		const double Distance = Body->Bounds.SphereRadius * 3.9;
		// DTH characters face +Y on the UE5 Mannequin skeleton; +X is the character's left.
		// The face cameras follow the head's facing.
		const FVector HeadPos = Body->GetSocketLocation(TEXT("head"));
		const FQuat HeadTurn = Body->GetBoneQuaternion(TEXT("head"), EBoneSpaces::ComponentSpace) * HeadRest.Inverse();
		const FVector Ahead = Body->GetComponentQuat().RotateVector(HeadTurn.RotateVector(FVector(0.0, 1.0, 0.0)));
		struct FShot { const TCHAR* Name; FVector Eye, Target; float Fov; };
		const FVector FaceCenter = HeadPos + FVector(0.0, 0.0, 6.0);
		const TArray<FShot> ShotsAt{
			{TEXT("body_front"), Center + FVector(0.0, Distance, 0.0), Center, 30.f},
			{TEXT("body_side"), Center + FVector(Distance, 0.0, 0.0), Center, 30.f},
			{TEXT("body_back"), Center + FVector(0.0, -Distance, 0.0), Center, 30.f},
			{TEXT("face_front"), FaceCenter + Ahead * 45.0, FaceCenter, 30.f},
			{TEXT("face_threequarter"), FaceCenter + (Ahead + FVector(0.7, 0.0, 0.0)).GetSafeNormal() * 45.0, FaceCenter, 30.f},
		};
		for (const FShot& S : ShotsAt)
		{
			const FString File = Dir / FString::Printf(TEXT("%s_f%03d_%s.png"), *Clip, F, S.Name);
			if (!Harness.Capture(File, S.Eye, S.Target, S.Fov, 1024, 1024, Error)) { AddError(Error); return false; }
			++Shots;
		}
	}
	AddInfo(FString::Printf(TEXT("%d screenshots in %s."), Shots, *FPaths::ConvertRelativePathToFull(Dir)));
	return true;
}

#endif
