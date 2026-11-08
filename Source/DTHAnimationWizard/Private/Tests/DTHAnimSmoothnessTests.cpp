#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "DTHAnimClip.h"
#include "DTHAnimSettings.h"
#include "DTHBodyHarness.h"
#include "DTHPoseCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Animation/AnimInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "ObjectTools.h"
#include "UI/SDTHAnimPreview.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	int32 Sub = 8;

	struct FTrack
	{
		TArray<FVector> Pos;
		TArray<float> Value;
	};

	/** Plays the clip at Sub points per frame (Eval: the character at step J, read back after its post-process blueprint)
	 * and summarizes how each bone and curve moves between frames. */
	void Measure(FAutomationTestBase& Test, int32 Frames, const TCHAR* Mode, TFunctionRef<void(int32, FDTHBoneReadback&)> Eval,
		const FString& Tag, TArray<FString>& Lines)
	{
		const int32 Steps = (Frames - 1) * Sub + 1;
		TMap<FName, FTrack> Bones, Curves;
		for (int32 J = 0; J < Steps; ++J)
		{
			FDTHBoneReadback R;
			Eval(J, R);
			for (int32 B = 0; B < R.Bones.Num(); ++B) Bones.FindOrAdd(R.Bones[B]).Pos.Add(R.Component[B].GetTranslation());
			for (const TPair<FName, float>& C : R.Curves) Curves.FindOrAdd(C.Key).Value.Add(C.Value);
		}
		struct FStat { FName Name; double Speed = 0, Accel = 0, KeyAccel = 0, Held = 0; int32 At = 0; };
		TArray<FStat> Stats;
		auto Analyse = [&](FName Name, int32 N, TFunctionRef<double(int32, int32)> Dist, TFunctionRef<double(int32, int32, int32)> Second)
		{
			if (N < Sub * 3) return;
			FStat S; S.Name = Name;
			TArray<double> V;
			for (int32 J = 0; J + 1 < N; ++J) V.Add(Dist(J, J + 1));
			TArray<double> Sorted = V; Sorted.Sort();
			S.Speed = Sorted[Sorted.Num() / 2];
			int32 Held = 0, Moving = 0;
			for (int32 J = 1; J + 1 < N; ++J)
			{
				const double A = Second(J - 1, J, J + 1);
				if (A > S.Accel) { S.Accel = A; S.At = J; }
				if (J % Sub == 0) S.KeyAccel = FMath::Max(S.KeyAccel, A);
			}
			// Stepping: an in-between that does not move while the frame interval around it does.
			for (int32 F = 0; F + 1 < (N - 1) / Sub; ++F)
			{
				const double Whole = Dist(F * Sub, (F + 1) * Sub);
				if (Whole < 1e-3) continue;
				++Moving;
				for (int32 K = 0; K < Sub; ++K) if (V[F * Sub + K] < Whole * 0.02) { ++Held; break; }
			}
			S.Held = Moving ? double(Held) / Moving : 0.0;
			Stats.Add(S);
		};
		for (const TPair<FName, FTrack>& P : Bones)
		{
			const TArray<FVector>& X = P.Value.Pos;
			Analyse(P.Key, X.Num(), [&](int32 A, int32 B) { return FVector::Dist(X[A], X[B]); },
				[&](int32 A, int32 B, int32 C) { return ((X[C] - X[B]) - (X[B] - X[A])).Size(); });
		}
		const int32 BoneStats = Stats.Num();
		for (const TPair<FName, FTrack>& P : Curves)
		{
			const TArray<float>& X = P.Value.Value;
			Analyse(P.Key, X.Num(), [&](int32 A, int32 B) { return double(FMath::Abs(X[B] - X[A])); },
				[&](int32 A, int32 B, int32 C) { return double(FMath::Abs((X[C] - X[B]) - (X[B] - X[A]))); });
		}
		// Ratio of the worst change of velocity to the typical in-between step: smooth motion keeps it small.
		auto Ratio = [](const FStat& S) { return S.Speed > 1e-6 ? S.Accel / S.Speed : 0.0; };
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			TArray<FStat> Part = Kind == 0 ? TArray<FStat>(Stats.GetData(), BoneStats) : TArray<FStat>(Stats.GetData() + BoneStats, Stats.Num() - BoneStats);
			Part.Sort([&](const FStat& A, const FStat& B) { return Ratio(A) > Ratio(B); });
			int32 Stepped = 0;
			for (const FStat& S : Part) if (S.Held > 0.5) ++Stepped;
			Test.AddInfo(FString::Printf(TEXT("%s %s: %d %s, %d stepping (an in-between holds in over half the frames)."), *Tag,
				Mode, Part.Num(), Kind == 0 ? TEXT("bones") : TEXT("curves"), Stepped));
			for (int32 I = 0; I < Part.Num(); ++I)
			{
				const FStat& S = Part[I];
				const FString Line = FString::Printf(TEXT("%s\t%s\t%s\t%s\t%.5f\t%.5f\t%.5f\t%.3f\t%.2f\t%.3f"), *Tag, Mode,
					Kind == 0 ? TEXT("bone") : TEXT("curve"), *S.Name.ToString(), S.Speed, S.Accel, S.KeyAccel, Ratio(S), S.At / double(Sub), S.Held);
				Lines.Add(Line);
				if (I < 8) Test.AddInfo(Line);
			}
		}
	}
}

// Measurement, not a pass/fail check: how the written test clips move between their frames, on the character, with and
// without its post-process blueprint. Saved/DTHAnimationWizard/smoothness.tsv.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimMeasureSmoothness, "DTH.Anim.Measure.Smoothness", Flags)
bool FDTHAnimMeasureSmoothness::RunTest(const FString&)
{
	const UDTHAnimSettings* S = GetDefault<UDTHAnimSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(S->Character.TryLoad());
	if (!Mesh) { AddError(TEXT("Set Character.")); return false; }
	TArray<FString> Lines{TEXT("clip\tpost\tkind\tname\tmedian_step\tmax_accel\tmax_accel_at_key\tratio\tat_frame\theld_fraction")};
	for (const FFilePath* File : {&S->ClipSamplesFile, &S->G8ClipSamplesFile})
	{
		FDTHAnimClip Clip;
		FString Error;
		if (!Clip.Load(File->FilePath, Error)) { AddError(Error); continue; }
		const FString Folder = S->TestFolder.Path / TEXT("Smoothness");
		const FString Name = TEXT("A_") + DTHPoseCharacter::AssetName(Clip.Label);
		if (UObject* Old = LoadObject<UObject>(nullptr, *(Folder / Name + TEXT(".") + Name))) ObjectTools::ForceDeleteObjects({Old}, false);
		UAnimSequence* Sequence = DTHAnimBuild::Write(*Mesh, Clip, 0, Folder, Name, Error);
		if (!Sequence) { AddError(Error); continue; }
		AddInfo(FString::Printf(TEXT("%s: %d frames, %d keys, %.3f s, interpolation %s."), *Clip.Label, Clip.Roles[0].Frames.Num(),
			Sequence->GetNumberOfSampledKeys(), Sequence->GetPlayLength(), Sequence->Interpolation == EAnimInterpolationType::Step ? TEXT("step") : TEXT("linear")));
		const int32 Frames = FMath::Min(Clip.Roles[0].Frames.Num(), 120);
		const double Fps = Clip.Fps;
		for (const bool bPost : {true, false})
		{
			FDTHBodyHarness Harness;
			if (!Harness.Init(Mesh, Error)) { AddError(Error); return false; }
			Harness.SetPostProcess(bPost);
			Sub = 8;
			Measure(*this, Frames, bPost ? TEXT("sequence, post-process on") : TEXT("sequence, post-process off"),
				[&](int32 J, FDTHBoneReadback& R) { Harness.EvalSequence(Sequence, double(J) / (Sub * Fps), R); }, Clip.Label, Lines);
		}
		// As seen: the wizard's preview in 60 fps real-time steps, so the post-process blueprint's own state moves as on screen.
		{
			TSharedRef<SDTHAnimPreview> Preview = SNew(SDTHAnimPreview);
			Preview->SetActors({{Mesh, File->FilePath, Clip.Label}});
			Preview->SetPlaying(false);
			UDebugSkelMeshComponent* Component = Preview->GetComponent(0);
			if (!Component) { AddError(TEXT("The preview has no actor.")); return false; }
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			Sub = 2;
			for (int32 Settle = 0; Settle < 4; ++Settle) Preview->EvaluateNow(1.0f / 60.0f);
			Measure(*this, Frames, TEXT("preview at 60 fps"), [&](int32 J, FDTHBoneReadback& R)
			{
				Preview->SetTime(double(J) / (Sub * Fps));
				Preview->EvaluateNow(1.0f / 60.0f);
				const TArray<FTransform>& Space = Component->GetComponentSpaceTransforms();
				for (int32 B = 0; B < Ref.GetNum() && B < Space.Num(); ++B) { R.Bones.Add(Ref.GetBoneName(B)); R.Component.Add(Space[B]); }
				if (const UAnimInstance* Post = Component->GetPostProcessInstance()) Post->AppendAnimationCurveList(EAnimCurveType::AttributeCurve, R.Curves);
			}, Clip.Label, Lines);
		}
	}
	const FString Out = UDTHAnimSettings::SavedDir() / TEXT("smoothness.tsv");
	FFileHelper::SaveStringArrayToFile(Lines, *Out);
	AddInfo(TEXT("Report: ") + FPaths::ConvertRelativePathToFull(Out));
	return true;
}

#endif
