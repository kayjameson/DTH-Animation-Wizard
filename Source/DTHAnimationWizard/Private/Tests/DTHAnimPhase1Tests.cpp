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

	FString WriteReport(const FString& Name, const TArray<FString>& Lines, const TCHAR* Sub = TEXT("phase1"))
	{
		const FString File = UDTHAnimSettings::SavedDir() / Sub / Name;
		FFileHelper::SaveStringArrayToFile(Lines, *File);
		return FPaths::ConvertRelativePathToFull(File);
	}

	struct FContext
	{
		USkeletalMesh* Mesh = nullptr;
		FString TestFolder;
		FDTHAnimClip Clip;

		/** Samples: the clip's samples file (default: Clip Samples File). */
		bool Load(FAutomationTestBase& Test, const FFilePath* Samples = nullptr)
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
			if (!Clip.Load((Samples ? *Samples : S->ClipSamplesFile).FilePath, Error))
			{
				Test.AddError(TEXT("Clip samples: ") + Error);
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

	/** Writes the clip on the character, plays every frame and compares it with Own (the clip as Daz evaluated it on the
	 * character itself), as DTH.Anim.P1.ClipRecreated describes. */
	void CheckRecreated(FAutomationTestBase& Test, FContext& C, const FDTHAnimClip& Own, const TCHAR* Sub)
	{
		FString Error;
		const TArray<FDTHDazState>& Frames = C.Clip.Roles[0].Frames;
		if (Own.Roles[0].Frames.Num() != Frames.Num())
		{
			Test.AddError(FString::Printf(TEXT("The two samples have %d and %d frames."), Frames.Num(), Own.Roles[0].Frames.Num()));
			return;
		}
		const double StartWrite = FPlatformTime::Seconds();
		UAnimSequence* Sequence = C.WriteSequence(Test);
		if (!Sequence) return;
		const double WriteSeconds = FPlatformTime::Seconds() - StartWrite;
		Test.AddInfo(FString::Printf(TEXT("%s: %d frames at %s written in %.2f s (%d keys, %.3f s long)."), *C.Clip.Label, Frames.Num(),
			*C.Clip.FrameRate().ToPrettyText().ToString(), WriteSeconds, Sequence->GetNumberOfSampledKeys(), Sequence->GetPlayLength()));
		if (Sequence->GetNumberOfSampledKeys() != Frames.Num()) Test.AddError(TEXT("The sequence does not have one key per frame."));

		FDTHBodyHarness Harness;
		if (!Harness.Init(C.Mesh, Error)) { Test.AddError(Error); return; }
		const DTHPoseSolve::FRig Rig = DTHPoseCharacter::Make(*C.Mesh, C.Clip.Roles[0].Rest);
		const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
		const double Step = 1.0 / C.Clip.Fps;

		TArray<FString> Lines{TEXT("frame\tmax_cm\tcm_bone\tmax_deg\tdeg_bone\tmax_twist_deg\ttwist_bone\thalfway_deg\thalfway_bone")};
		int32 Failed = 0, RetargetOnly = 0;
		double WorstRetargetCm = 0.0;
		FName WorstRetargetBone;
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
			if (!E.Passes())
			{
				// The same frame played from the solve itself, without the track's trip through DTH's translation retargeting
				// modes: if it passes, what is left over is a value DTH's retarget mode cannot carry (the pec clavicles' Skeleton
				// mode keeps the bind translation [imp apply_skeleton_retargeting_modes]), which is DTH's and reported.
				FDTHBoneReadback Raw;
				Harness.EvalPose(DTHPoseSolve::Solve(Rig, Ref, Frames[F]), {}, Raw);
				const DTHPoseMeasure::FError R = DTHPoseMeasure::Compare(Rig, Ref, Raw, Own.Roles[0].Rest, Own.Roles[0].Frames[F]);
				if (R.Passes())
				{
					++RetargetOnly;
					if (E.MaxCm > WorstRetargetCm) { WorstRetargetCm = E.MaxCm; WorstRetargetBone = E.CmBone; }
				}
				else
				{
					++Failed;
					Test.AddWarning(FString::Printf(TEXT("Frame %d: %s; from the solve itself: %s."), F, *E.Describe(), *R.Describe()));
				}
			}
			if (E.MaxCm + E.MaxDeg > Worst.MaxCm + Worst.MaxDeg) { Worst = E; WorstFrame = F; }
			if (Halfway > WorstHalfway) { WorstHalfway = Halfway; WorstHalfwayBone = HalfwayBone; WorstHalfwayFrame = F; }
			Previous = MoveTemp(Played);
		}
		Test.AddInfo(FString::Printf(TEXT("%d of %d frames within 0.5 cm / 0.5 deg. Worst frame %d: %s."), Frames.Num() - Failed - RetargetOnly,
			Frames.Num(), WorstFrame, *Worst.Describe()));
		if (RetargetOnly > 0)
		{
			Test.AddWarning(FString::Printf(TEXT("%d frames pass from the solve itself but not as played: DTH's translation retargeting mode cannot carry the value (worst %.4f cm at %s). DTH's, reported."),
				RetargetOnly, WorstRetargetCm, *WorstRetargetBone.ToString()));
		}
		Test.AddInfo(FString::Printf(TEXT("Between keys: largest departure from the halfway blend %.4f deg (%s, frames %d-%d)."), WorstHalfway,
			*WorstHalfwayBone.ToString(), WorstHalfwayFrame - 1, WorstHalfwayFrame));
		Test.AddInfo(TEXT("Report: ") + WriteReport(TEXT("clip_recreated.tsv"), Lines, Sub));
		if (!C.Clip.KeyedMorphs.IsEmpty())
		{
			Test.AddWarning(FString::Printf(TEXT("The clip keys %d morphs; v1 writes bone tracks only."), C.Clip.KeyedMorphs.Num()));
		}
		if (Failed > 0) Test.AddError(FString::Printf(TEXT("%d frames are not recreated within 0.5 cm / 0.5 deg."), Failed));
		if (WorstHalfway > 1.0) Test.AddError(TEXT("The sequence leaves the blend of its keys between frames by more than 1 deg (a key flip)."));
	}

	/** Screenshots of the clip on the character at fixed frames, as DTH.Anim.P1.Screenshots describes, to Saved/.../<Sub>/screens. */
	void TakeScreenshots(FAutomationTestBase& Test, FContext& C, const TCHAR* Sub)
	{
		UAnimSequence* Sequence = C.WriteSequence(Test);
		if (!Sequence) return;
		FDTHBodyHarness Harness;
		FString Error;
		if (!Harness.Init(C.Mesh, Error)) { Test.AddError(Error); return; }
		USkeletalMeshComponent* Body = Harness.GetComponent();
		const FString Dir = UDTHAnimSettings::SavedDir() / Sub / TEXT("screens");
		const int32 Last = C.Clip.Roles[0].Frames.Num() - 1;
		const FString Clip = DTHPoseCharacter::AssetName(C.Clip.Label);
		const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
		const int32 HeadIndex = Ref.FindBoneIndex(TEXT("head"));
		if (HeadIndex == INDEX_NONE) { Test.AddError(TEXT("The character has no head bone.")); return; }
		const FQuat HeadRest = DTHPoseSolve::ComponentSpace(Ref, [&Ref](int32 I) { return Ref.GetRefBonePose()[I]; })[HeadIndex].GetRotation();
		int32 Shots = 0;
		for (const int32 F : {0, Last / 4, Last / 2, 3 * Last / 4, Last})
		{
			FDTHBoneReadback Played;
			Harness.EvalSequence(Sequence, F / C.Clip.Fps, Played);
			Body->UpdateBounds();
			// The skinned bounds do not follow the pose here (they read the same on every frame): centre between pelvis and head.
			const FVector Center = 0.5 * (Body->GetSocketLocation(TEXT("pelvis")) + Body->GetSocketLocation(TEXT("head")));
			const double Distance = Body->Bounds.SphereRadius * 3.9;
			// DTH characters face +Y on the UE5 Mannequin skeleton; +X is the character's left.
			// The face cameras follow the head's facing.
			const FVector HeadPos = Body->GetSocketLocation(TEXT("head"));
			// Ahead: from the head joint through the point between the eyes, flattened against the head's up so the camera
			// looks at the face, not down on it.
			const FVector EyeMid = 0.5 * (Body->GetSocketLocation(TEXT("eye_l")) + Body->GetSocketLocation(TEXT("eye_r")));
			const FQuat HeadTurn = Body->GetBoneQuaternion(TEXT("head"), EBoneSpaces::ComponentSpace) * HeadRest.Inverse();
			const FVector HeadUp = Body->GetComponentQuat().RotateVector(HeadTurn.RotateVector(FVector::UpVector));
			const FVector Ahead = FVector::VectorPlaneProject(EyeMid - HeadPos, HeadUp).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(HeadUp, Ahead).GetSafeNormal();
			struct FShot { const TCHAR* Name; FVector Eye, Target; float Fov; };
			const FVector FaceCenter = EyeMid - HeadUp * 3.0;
			const TArray<FShot> ShotsAt{
				{TEXT("body_front"), Center + FVector(0.0, Distance, 0.0), Center, 30.f},
				{TEXT("body_side"), Center + FVector(Distance, 0.0, 0.0), Center, 30.f},
				{TEXT("body_back"), Center + FVector(0.0, -Distance, 0.0), Center, 30.f},
				{TEXT("face_front"), FaceCenter + Ahead * 45.0, FaceCenter, 30.f},
				{TEXT("face_threequarter"), FaceCenter + (Ahead + Side * 0.7).GetSafeNormal() * 45.0, FaceCenter, 30.f},
				{TEXT("mouth"), FaceCenter - HeadUp * 4.0 + Ahead * 22.0, FaceCenter - HeadUp * 4.0, 30.f},
			};
			Test.AddInfo(FString::Printf(TEXT("Frame %d: component at %s rot %s; head %s, eyes %s, ahead %s, up %s; body bounds %s r %.1f."), F,
				*Body->GetComponentLocation().ToString(), *Body->GetComponentRotation().ToString(), *HeadPos.ToString(), *EyeMid.ToString(),
				*Ahead.ToString(), *HeadUp.ToString(), *Center.ToString(), Body->Bounds.SphereRadius));
			for (const FShot& S : ShotsAt)
			{
				// Lit from over the camera's shoulder, so what the camera sees is lit whichever way the clip turns the body.
				FRotator Light = (S.Target - S.Eye).Rotation();
				Light.Pitch -= 25.0;
				Light.Yaw += 20.0;
				Harness.SetLightDirection(Light);
				const FString File = Dir / FString::Printf(TEXT("%s_f%03d_%s.png"), *Clip, F, S.Name);
				if (!Harness.Capture(File, S.Eye, S.Target, S.Fov, 1024, 1024, Error)) { Test.AddError(Error); return; }
				++Shots;
			}
		}
		Test.AddInfo(FString::Printf(TEXT("%d screenshots in %s."), Shots, *FPaths::ConvertRelativePathToFull(Dir)));
	}
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
	CheckRecreated(*this, C, Own, TEXT("phase1"));
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
	TakeScreenshots(*this, C, TEXT("phase1"));
	return true;
}

// ---- Genesis 8 clips (stretch goal) ----

// In Daz: the sampler carried the G8 clip onto base Genesis 9 (G8 on base Genesis 8 beside it; each G9 bone turned to its
// G8 bone, after a rest alignment). Its "source" block measures how closely G9 followed G8 over every frame: G9's world
// rotations against G8's (only G9's locked channels can leave a gap: the toes), the angle between each G9 bone segment and
// G8's (what the eye sees; the hand's includes a fixed 2-2.4 deg from where the two figures' middle fingers start), and the
// ankles' height change against G8's scaled by the hip heights (floor contact; the figures' leg proportions differ).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimG8Retargeted, "DTH.Anim.G8.CarriedOntoGenesis9", Flags)
bool FDTHAnimG8Retargeted::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this, &GetDefault<UDTHAnimSettings>()->G8ClipSamplesFile)) return false;
	const FDTHAnimClip::FSource& S = C.Clip.Source;
	if (S.Figure.IsEmpty())
	{
		AddError(TEXT("G8 Clip Samples File is not a Genesis 8 clip carried onto Genesis 9 (no source block)."));
		return false;
	}
	AddInfo(FString::Printf(TEXT("%s (%d frames): %s"), *C.Clip.Label, C.Clip.Roles[0].Frames.Num(), *S.Describe()));
	for (int32 I = 0; I < FMath::Min(8, S.BeyondG9Limits.Num()); ++I)
	{
		AddInfo(FString::Printf(TEXT("Past G9's limits: %s by %.2f deg."), *S.BeyondG9Limits[I].Key, S.BeyondG9Limits[I].Value));
	}
	if (S.RotationGapDeg > 5.0) AddError(FString::Printf(TEXT("A G9 bone is %.2f deg off its G8 bone (%s)."), S.RotationGapDeg, *S.RotationGapBone));
	if (S.SegmentGapDeg > 3.0) AddError(FString::Printf(TEXT("A G9 bone points %.2f deg away from its G8 bone (%s)."), S.SegmentGapDeg, *S.SegmentGapBone));
	if (S.FootHeightGapCm > 3.0) AddError(FString::Printf(TEXT("An ankle's height change is %.2f cm off G8's."), S.FootHeightGapCm));
	return true;
}

// The G8 clip carried onto G9, written on the character and played through its post-process blueprint, against the same
// G8 clip carried onto the character itself in Daz: the Phase 1 rule on every frame (DTH.Anim.P1.ClipRecreated).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimG8ClipRecreated, "DTH.Anim.G8.ClipRecreated", Flags)
bool FDTHAnimG8ClipRecreated::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this, &GetDefault<UDTHAnimSettings>()->G8ClipSamplesFile)) return false;
	FDTHAnimClip Own;
	FString Error;
	if (!Own.Load(GetDefault<UDTHAnimSettings>()->G8CharacterClipSamplesFile.FilePath, Error))
	{
		AddError(TEXT("G8 Character Clip Samples File (the G8 clip carried onto the character in Daz): ") + Error);
		return false;
	}
	CheckRecreated(*this, C, Own, TEXT("g8"));
	return true;
}

// The G8 clip on the character at fixed frames, as DTH.Anim.P1.Screenshots. Written to Saved/DTHAnimationWizard/g8/screens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimG8Screenshots, "DTH.Anim.G8.Screenshots", Flags)
bool FDTHAnimG8Screenshots::RunTest(const FString&)
{
	FContext C;
	if (!C.Load(*this, &GetDefault<UDTHAnimSettings>()->G8ClipSamplesFile)) return false;
	TakeScreenshots(*this, C, TEXT("g8"));
	return true;
}

#endif
