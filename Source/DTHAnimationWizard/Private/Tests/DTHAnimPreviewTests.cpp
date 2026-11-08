#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "DTHAnimBatch.h"
#include "DTHAnimClip.h"
#include "DTHAnimScene.h"
#include "DTHAnimScripting.h"
#include "DTHAnimSettings.h"
#include "DTHBodyHarness.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "UI/SDTHAnimPreview.h"
#include "UI/SDTHAnimationWizard.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	void TickSlate(int32 N)
	{
		for (int32 I = 0; I < N; ++I) FSlateApplication::Get().Tick();
	}

	void Shot(FAutomationTestBase& Test, SDTHAnimPreview& Preview, const FString& Name)
	{
		const FString File = UDTHAnimSettings::SavedDir() / TEXT("preview") / Name;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
		FString Error;
		if (Preview.Capture(File, Error)) Test.AddInfo(TEXT("Screenshot: ") + FPaths::ConvertRelativePathToFull(File));
		else Test.AddWarning(Error);
	}
}

// The Clips page's preview plays what the written sequence plays: the clip on the character, at chosen times on and
// between frames, against the clip's sequence played on the same character by a sequence player. Every bone, after the
// character's post-process blueprint, within 0.05 cm and 0.05 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimPreviewMatchesSequence, "DTH.Anim.Preview.MatchesSequence", Flags)
bool FDTHAnimPreviewMatchesSequence::RunTest(const FString&)
{
	const UDTHAnimSettings* S = GetDefault<UDTHAnimSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(S->Character.TryLoad());
	if (!Mesh) { AddError(TEXT("Set Character.")); return false; }
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(S->SamplesIndex.FilePath, Error)) { AddError(Error); return false; }
	const FDTHAnimIndexEntry* Entry = nullptr;
	for (const FDTHAnimIndexEntry& C : Index.Clips) if (!C.Samples.IsEmpty() && C.Frames > 100) { Entry = &C; break; }
	if (!Entry) { AddError(TEXT("No sampled clip of over 100 frames.")); return false; }

	if (UDTHAnimScripting::OpenWizard(TEXT("Clips")) != TEXT("Wizard open.")) { AddError(TEXT("The wizard did not open.")); return false; }
	const TSharedPtr<SDTHAnimationWizard> Wizard = SDTHAnimationWizard::Get();
	Wizard->Reload();
	Wizard->SetCharacter(Mesh);
	if (!Wizard->PreviewClip(Entry->Key())) { AddError(TEXT("The wizard does not list ") + Entry->Key()); return false; }
	const TSharedPtr<SDTHAnimPreview> Preview = Wizard->GetClipPreview();
	TestEqual(TEXT("One actor"), Preview->NumActors(), 1);
	TestEqual(TEXT("Every frame"), Preview->NumFrames(), Entry->Frames);
	if (!Preview->GetProblem().IsEmpty()) AddWarning(Preview->GetProblem());
	Preview->SetPlaying(false);
	TickSlate(6);

	// The sequence the wizard would write, under the test folder.
	FDTHAnimClip Clip;
	if (!Clip.Load(Entry->Samples, Error)) { AddError(Error); return false; }
	const FString Folder = S->TestFolder.Path / TEXT("Preview");
	TArray<UAnimSequence*> Written;
	TArray<FDTHAnimMessage> Report;
	DTHAnimBatch::WriteClips(*Mesh, {Entry}, Folder, true, Report, &Written);
	if (Written.IsEmpty()) { for (const FDTHAnimMessage& M : Report) AddError(M.Text); return false; }
	FDTHBodyHarness Harness;
	if (!Harness.Init(Mesh, Error)) { AddError(Error); return false; }

	UDebugSkelMeshComponent* Component = Preview->GetComponent(0);
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	double WorstCm = 0.0, WorstDeg = 0.0;
	FName CmBone, DegBone;
	double CmAt = 0.0, DegAt = 0.0;
	for (const double Frame : {0.0, 1.0, 37.0, 37.5, 100.25, double(Entry->Frames - 1)})
	{
		const double Seconds = Frame / Clip.Fps;
		Preview->SetTime(Seconds);
		Preview->EvaluateNow();
		Preview->EvaluateNow();
		FDTHBoneReadback Played;
		Harness.EvalSequence(Written[0], Seconds, Played);
		Harness.EvalSequence(Written[0], Seconds, Played);
		const TArray<FTransform>& Shown = Component->GetComponentSpaceTransforms();
		for (int32 B = 0; B < Ref.GetNum() && B < Shown.Num() && B < Played.Component.Num(); ++B)
		{
			const double Cm = FVector::Dist(Shown[B].GetTranslation(), Played.Component[B].GetTranslation());
			const double Deg = FMath::RadiansToDegrees(Shown[B].GetRotation().AngularDistance(Played.Component[B].GetRotation()));
			if (Cm > WorstCm) { WorstCm = Cm; CmBone = Ref.GetBoneName(B); CmAt = Frame; }
			if (Deg > WorstDeg) { WorstDeg = Deg; DegBone = Ref.GetBoneName(B); DegAt = Frame; }
		}
	}
	AddInfo(FString::Printf(TEXT("%s on %s: preview against the written sequence, worst %.4f cm (%s, frame %g), %.4f deg (%s, frame %g)."),
		*Entry->Label, *Mesh->GetName(), WorstCm, *CmBone.ToString(), CmAt, WorstDeg, *DegBone.ToString(), DegAt));
	TestTrue(TEXT("Preview within 0.05 cm of the sequence"), WorstCm <= 0.05);
	TestTrue(TEXT("Preview within 0.05 deg of the sequence"), WorstDeg <= 0.05);

	// Playing: time moves at the speed set.
	Preview->SetTime(0.0);
	Preview->SetSpeed(0.5f, true);
	Preview->SetPlaying(true);
	Preview->Advance(1.0);
	Preview->SetPlaying(false);
	AddInfo(FString::Printf(TEXT("At 0.5x: %.3f s of clip in 1 s."), Preview->GetTime()));
	TestTrue(TEXT("Plays at half speed"), FMath::IsNearlyEqual(Preview->GetTime(), 0.5, 1e-6));
	for (const TCHAR* View : {TEXT("front"), TEXT("side"), TEXT("threequarter")})
	{
		Preview->SetTime(37.0 / Clip.Fps);
		Preview->SetView(View);
		TickSlate(4);
		Shot(*this, *Preview, FString::Printf(TEXT("clip_%s.png"), View));
	}
	return true;
}

// Speed: written into the sequence as its Rate Scale (the keys unchanged), and a scene built at a speed plays every role
// at it, with its tracks and playback range stretched to match. The Scenes page previews every cast role.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimPreviewSpeed, "DTH.Anim.Preview.Speed", Flags)
bool FDTHAnimPreviewSpeed::RunTest(const FString&)
{
	const UDTHAnimSettings* S = GetDefault<UDTHAnimSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(S->Character.TryLoad());
	if (!Mesh) { AddError(TEXT("Set Character.")); return false; }
	const FString Root = S->TestFolder.Path / TEXT("Speed");
	if (!Root.StartsWith(TEXT("/Game/")) || !Root.Contains(TEXT("_Test"))) { AddError(TEXT("Set Test Folder to a _Test folder.")); return false; }
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(S->SamplesIndex.FilePath, Error)) { AddError(Error); return false; }
	const TArray<FDTHAnimScene> Scenes = DTHAnimScenes::FromIndex(Index);
	if (Scenes.IsEmpty()) { AddError(TEXT("No scenes.")); return false; }
	const FDTHAnimScene& Scene = Scenes[0];

	// One clip at 0.75x.
	const FDTHAnimIndexEntry* Entry = Scene.Roles[0].Clip;
	TMap<FString, float> Speeds{{Entry->Key(), 0.75f}};
	TArray<UAnimSequence*> Written;
	TArray<FDTHAnimMessage> Report;
	DTHAnimBatch::WriteClips(*Mesh, {Entry}, Root, true, Report, &Written, &Speeds);
	for (const FDTHAnimMessage& M : Report) AddInfo(M.ToLine());
	if (Written.IsEmpty()) { AddError(TEXT("Not written.")); return false; }
	TestEqual(TEXT("Rate Scale"), Written[0]->RateScale, 0.75f);
	TestEqual(TEXT("Keys unchanged"), Written[0]->GetNumberOfSampledKeys(), FMath::Max(Entry->Frames, 2));
	TestTrue(TEXT("Saved at that speed"), !Written[0]->GetOutermost()->IsDirty());

	// The scene at 0.8x.
	TMap<FString, USkeletalMesh*> Assigned;
	for (const FDTHAnimScene::FRole& R : Scene.Roles) Assigned.Add(R.Label, Mesh);
	TMap<FString, UAnimSequence*> Parts;
	Report.Reset();
	ULevelSequence* Level = DTHAnimScenes::Build(Scene, Assigned, Root, true, Report, &Parts, 0.8f);
	for (const FDTHAnimMessage& M : Report) AddInfo(M.ToLine());
	if (!Level) { AddError(TEXT("The scene was not built.")); return false; }
	const UMovieScene* Movie = Level->GetMovieScene();
	const FFrameRate Tick = Movie->GetTickResolution();
	double Longest = 0.0;
	for (const TPair<FString, UAnimSequence*>& P : Parts)
	{
		TestEqual(*FString::Printf(TEXT("%s plays at the scene's speed"), *P.Key), P.Value->RateScale, 0.8f);
		Longest = FMath::Max(Longest, P.Value->GetPlayLength() / 0.8);
	}
	const double Range = Tick.AsSeconds(Movie->GetPlaybackRange().GetUpperBoundValue());
	AddInfo(FString::Printf(TEXT("Playback range %.3f s for %.3f s of clip at 0.8x."), Range, Longest));
	TestTrue(TEXT("Playback range stretched"), FMath::Abs(Range - Longest) < 0.05);
	for (const FMovieSceneBinding& Binding : Movie->GetBindings())
	{
		for (const UMovieSceneTrack* Track : Binding.GetTracks())
		{
			for (const UMovieSceneSection* Section : Track->GetAllSections())
			{
				const UMovieSceneSkeletalAnimationSection* Anim = Cast<UMovieSceneSkeletalAnimationSection>(Section);
				if (!Anim) continue;
				const double Length = Tick.AsSeconds(Anim->GetExclusiveEndFrame() - Anim->GetInclusiveStartFrame());
				const double Expected = Anim->Params.Animation->GetPlayLength() / 0.8;
				TestTrue(*FString::Printf(TEXT("%s: track as long as the clip at 0.8x (%.3f s, expected %.3f s)"), *Anim->Params.Animation->GetName(), Length, Expected),
					FMath::Abs(Length - Expected) < 0.05);
			}
		}
	}

	// The Scenes page previews every cast role.
	UDTHAnimScripting::OpenWizard(TEXT("Scenes"));
	const TSharedPtr<SDTHAnimationWizard> Wizard = SDTHAnimationWizard::Get();
	Wizard->Reload();
	Wizard->PreviewScene(0, Mesh);
	const TSharedPtr<SDTHAnimPreview> Preview = Wizard->GetScenePreview();
	TestEqual(TEXT("One actor per role"), Preview->NumActors(), Scene.Roles.Num());
	Preview->SetPlaying(false);
	Preview->SetTime(2.0);
	TickSlate(6);
	for (const TCHAR* View : {TEXT("front"), TEXT("side")})
	{
		Preview->SetView(View);
		TickSlate(4);
		Shot(*this, *Preview, FString::Printf(TEXT("scene_%s.png"), View));
	}
	return true;
}

#endif
