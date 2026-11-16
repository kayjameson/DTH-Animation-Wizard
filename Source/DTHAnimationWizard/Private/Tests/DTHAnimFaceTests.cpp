// A clip's face (eyes, jaw, lips, tongue) recreated frame by frame: fitted on the character as DTH's face rig plays it,
// measured region by region against the face Daz shows, written into the clip's sequence and played back.
// Inputs: <Project>/Saved/DTHAnimationWizard/AnimFaceTest.json
//   clip       a clip samples file with its face (the sampler's face test: args.testFace, or any clip that keys a face)
//   character  the DTH Genesis 9 skeletal mesh (a base Genesis 9 export is 1:1 with the samples)
//   frames     the frames to measure (default 0, 8, 15, 22, 30)
//   folder     where the test sequence goes (a _Test folder; it is deleted again)
//   regionMm   pass mark: the largest error of any region on a measured frame, mm (default 1.0)
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Dom/JsonObject.h"
#include "DTHAnimClip.h"
#include "DTHAnimSettings.h"
#include "DTHBodyHarness.h"
#include "DTHFaceFitter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "ShaderCompiler.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimFaceTest, "DTH.Anim.Face.Clip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHAnimFaceTest::RunTest(const FString& Parameters)
{
	FString Text;
	TSharedPtr<FJsonObject> Config;
	const FString ConfigFile = UDTHAnimSettings::SavedDir() / TEXT("AnimFaceTest.json");
	if (!FFileHelper::LoadFileToString(Text, *ConfigFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config)
	{
		AddError(TEXT("No test inputs: ") + ConfigFile);
		return false;
	}
	FDTHAnimClip Clip;
	FString Error;
	if (!Clip.Load(Config->GetStringField(TEXT("clip")), Error) || !Clip.HasFace())
	{
		AddError(Error.IsEmpty() ? TEXT("The clip has no face.") : Error);
		return false;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	if (!Mesh) { AddError(TEXT("No character.")); return false; }
	double RegionMm = 1.0;
	Config->TryGetNumberField(TEXT("regionMm"), RegionMm);
	TArray<int32> Frames = {0, 8, 15, 22, 30};
	const TArray<TSharedPtr<FJsonValue>>* FrameArray = nullptr;
	if (Config->TryGetArrayField(TEXT("frames"), FrameArray)) { Frames.Reset(); for (const TSharedPtr<FJsonValue>& V : *FrameArray) Frames.Add(int32(V->AsNumber())); }

	// The measured frames, fitted alone (the full fit), region by region.
	FDTHFaceFitter Fitter;
	TArray<FString> Report;
	if (!Fitter.Init(Mesh, Clip.FaceRegion, Report, Error)) { AddError(Error); return false; }
	const FDTHAnimClip::FRole& Role = Clip.Roles[0];
	int32 Passed = 0, Checked = 0;
	for (const int32 F : Frames)
	{
		if (!Role.Face.IsValidIndex(F)) continue;
		FDTHFaceFitter::FResult R;
		if (!Fitter.Fit(Role.Face[F], FDTHShapeFit::FOptions(), R)) { AddError(FString::Printf(TEXT("frame %d: no face target"), F)); continue; }
		double WorstMm = 0.0;
		FString WorstRegion;
		const FString Regions = Fitter.RegionReport(R, WorstMm, WorstRegion);
		FString Turns;
		for (const TPair<FName, FQuat>& B : R.Bones) Turns += FString::Printf(TEXT(" %s %.1f deg"), *B.Key.ToString(), FMath::RadiansToDegrees(B.Value.GetAngle()));
		const bool bPass = WorstMm <= RegionMm;
		Passed += bPass ? 1 : 0;
		++Checked;
		Report.Add(FString::Printf(TEXT("%s frame %d: worst %s %.2f mm (the face moves %.1f mm at most) | %d curves, bones:%s |%s"), bPass ? TEXT("PASS") : TEXT("FAIL"),
			F, WorstRegion.IsEmpty() ? TEXT("-") : *WorstRegion, WorstMm, 10.0 * R.Largest, R.Curves.Num(), Turns.IsEmpty() ? TEXT(" none") : *Turns, *Regions));
	}
	Report.Add(FString::Printf(TEXT("%d of %d measured frames within %.1f mm in every region."), Passed, Checked, RegionMm));

	// The whole clip written as the batch writes it (frame by frame, each starting from the frame before), then checked.
	FString Folder = TEXT("/Game/DTH_Animations/_Test/Face");
	Config->TryGetStringField(TEXT("folder"), Folder);
	FString FaceReport;
	const double T0 = FPlatformTime::Seconds();
	UAnimSequence* Sequence = DTHAnimBuild::Write(*Mesh, Clip, 0, Folder, TEXT("A_FaceTest"), Error, &FaceReport);
	if (!TestNotNull(TEXT("sequence written"), Sequence)) { AddError(Error); return false; }
	Report.Add(FString::Printf(TEXT("Written in %.1f s: %s; its %s."), FPlatformTime::Seconds() - T0, *Sequence->GetPathName(), *FaceReport));
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	int32 FaceCurves = 0;
	for (const FFloatCurve& C : Model->GetFloatCurves()) FaceCurves += C.GetName().ToString().StartsWith(TEXT("CTRL_")) || C.GetName().ToString().StartsWith(TEXT("fac")) ? 1 : 0;
	Report.Add(FString::Printf(TEXT("The sequence has %d face curves over %d keys."), FaceCurves, Model->GetNumberOfKeys()));
	TestTrue(TEXT("face curves in the sequence"), FaceCurves > 0);

	// Played back through the character's post-process blueprint: the eye on the peak frame, against the Daz eye.
	FDTHBodyHarness Harness;
	if (Harness.Init(Mesh, Error))
	{
		FDTHBoneReadback Rest, Peak;
		Harness.EvalSequence(Sequence, 0.0, Rest);
		Harness.EvalSequence(Sequence, 15.0 / 30.0, Peak);
		const int32 Eye = Peak.Find(TEXT("eye_l"));
		if (Eye != INDEX_NONE)
		{
			const double Turned = FMath::RadiansToDegrees(Peak.Local[Eye].GetRotation().AngularDistance(Rest.Local[Eye].GetRotation()));
			const FDTHDazBone* DazEye = Role.Frames.IsValidIndex(15) ? Role.Frames[15].Bones.Find(TEXT("l_eye")) : nullptr;
			const FDTHDazBone* DazRest = Role.Frames.IsValidIndex(0) ? Role.Frames[0].Bones.Find(TEXT("l_eye")) : nullptr;
			const double DazTurned = DazEye && DazRest ? FMath::RadiansToDegrees(DazEye->Rot.AngularDistance(DazRest->Rot)) : -1.0;
			Report.Add(FString::Printf(TEXT("Played back: the left eye turns %.2f deg on frame 15 (Daz: %.2f deg, its head included)."), Turned, DazTurned));
			// which way: the eye's front (Daz +Z, Unreal +Y) turned by each side's turn, in Unreal's axes (Daz x, z, y)
			if (DazEye && DazRest)
			{
				const FQuat DazTurn = DazEye->Rot * DazRest->Rot.Inverse();
				const FVector DazFront = DazTurn.RotateVector(FVector(0, 0, 1));
				const FVector DazFrontUE(DazFront.X, DazFront.Z, DazFront.Y);
				const FQuat UETurn = Peak.Component[Eye].GetRotation() * Rest.Component[Eye].GetRotation().Inverse();
				const FVector UEFront = UETurn.RotateVector(FVector(0, 1, 0));
				Report.Add(FString::Printf(TEXT("Gaze on frame 15: Daz %s, Unreal %s (Unreal axes, +X the character's left, +Z up); %.2f deg apart."),
					*DazFrontUE.ToString(), *UEFront.ToString(), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DazFrontUE | UEFront, -1.0, 1.0)))));
			}
		}
		// the frame's face from the Daz shot's camera (tools/daz/dth_face_shots.dsa): the head from 45 cm in front, FOV 30
		FString Shots;
		if (Config->TryGetStringField(TEXT("shots"), Shots))
		{
			int32 Width = 768, Height = 768;
			Config->TryGetNumberField(TEXT("width"), Width);
			Config->TryGetNumberField(TEXT("height"), Height);
			Harness.SetLightDirection(FRotator(-30.0, -100.0, 0.0));
	// The eyes' wet layers (moisture, tear) are translucent shells over the iris that the off-screen capture draws opaque:
	// hidden for the shots, so the gaze shows (the fit never uses them).
	if (USkeletalMeshComponent* C = Harness.GetComponent())
	{
		const FSkeletalMeshRenderData* RD = C->GetSkeletalMeshAsset()->GetResourceForRendering();
		for (int32 S = 0; RD && S < RD->LODRenderData[0].RenderSections.Num(); ++S)
		{
			const int32 M = RD->LODRenderData[0].RenderSections[S].MaterialIndex;
			const FString Slot = C->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(M) ? C->GetSkeletalMeshAsset()->GetMaterials()[M].MaterialSlotName.ToString() : FString();
			if (Slot.Contains(TEXT("Moisture")) || Slot.Contains(TEXT("Tear"))) C->ShowMaterialSection(M, S, false, 0);
		}
	}

			const int32 Head = Peak.Find(TEXT("head"));
			const FVector Target = (Head != INDEX_NONE ? Peak.Component[Head].GetLocation() : FVector(0, 0, 160)) + FVector(0.0, 0.0, 6.0);
			// shaders for the character's materials first (the first capture of a fresh scene can come out before they are)
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			Harness.EvalSequence(Sequence, 15.0 / 30.0, Peak);
			Harness.Capture(Shots / TEXT("clip_facetest_f15_ue.png"), Target + FVector(0.0, 45.0, 0.0), Target, 25.0f, Width, Height, Error);
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			if (!Harness.Capture(Shots / TEXT("clip_facetest_f15_ue.png"), Target + FVector(0.0, 45.0, 0.0), Target, float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(15.0)) * Width / FMath::Max(Height, 1)))), Width, Height, Error)) AddError(Error);
		}
	}

	FString ReportFile = UDTHAnimSettings::SavedDir() / TEXT("AnimFaceReport.txt");
	FFileHelper::SaveStringArrayToFile(Report, *ReportFile);
	for (const FString& Line : Report) AddInfo(Line);
	bool bKeep = false;
	Config->TryGetBoolField(TEXT("keep"), bKeep);
	if (!bKeep) ObjectTools::ForceDeleteObjects({Sequence}, false);
	return true;
}

#endif
