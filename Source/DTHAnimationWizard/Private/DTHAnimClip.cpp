#include "DTHAnimClip.h"

#include "DTHPoseCharacter.h"
#include "DTHFaceFitter.h"
#include "DTHFaceRegion.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FDTHAnimClip::Load(const FString& InFile, FString& OutError)
{
	*this = FDTHAnimClip();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *InFile))
	{
		OutError = FString::Printf(TEXT("Could not read %s."), *InFile);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| Root->GetStringField(TEXT("format")) != TEXT("DTH Animation Samples"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH Animation Samples file."), *InFile);
		return false;
	}
	DazVersion = Root->GetStringField(TEXT("dazVersion"));
	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	Fps = Root->GetNumberField(TEXT("fps"));
	LastKey = int32(Root->GetNumberField(TEXT("lastKey")));
	const TSharedPtr<FJsonObject>* ClipInfo = nullptr;
	if (Root->TryGetObjectField(TEXT("clip"), ClipInfo))
	{
		Label = (*ClipInfo)->GetStringField(TEXT("label"));
		Pack = (*ClipInfo)->GetStringField(TEXT("pack"));
		File = (*ClipInfo)->GetStringField(TEXT("file"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Morphs = nullptr;
	if (Root->TryGetArrayField(TEXT("keyedMorphs"), Morphs))
	{
		for (const TSharedPtr<FJsonValue>& M : *Morphs) KeyedMorphs.Add(M->AsString());
	}
	const TSharedPtr<FJsonObject>* SourceInfo = nullptr;
	if (Root->TryGetObjectField(TEXT("source"), SourceInfo) && SourceInfo->IsValid())
	{
		const FJsonObject& S = **SourceInfo;
		Source.Figure = S.GetStringField(TEXT("figure"));
		S.TryGetNumberField(TEXT("hipScale"), Source.HipScale);
		S.TryGetNumberField(TEXT("maxRotationGapDeg"), Source.RotationGapDeg);
		S.TryGetStringField(TEXT("maxRotationGapBone"), Source.RotationGapBone);
		S.TryGetNumberField(TEXT("maxSegmentGapDeg"), Source.SegmentGapDeg);
		S.TryGetStringField(TEXT("maxSegmentGapBone"), Source.SegmentGapBone);
		S.TryGetNumberField(TEXT("maxFootHeightGapCm"), Source.FootHeightGapCm);
		const TArray<TSharedPtr<FJsonValue>>* Beyond = nullptr;
		if (S.TryGetArrayField(TEXT("beyondG9Limits"), Beyond))
		{
			for (const TSharedPtr<FJsonValue>& V : *Beyond)
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = V->AsArray();
				if (Pair.Num() == 2) Source.BeyondG9Limits.Add({Pair[0]->AsString(), Pair[1]->AsNumber()});
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* RoleArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("roles"), RoleArray) || RoleArray->IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s has no roles."), *InFile);
		return false;
	}
	// The face: the region, and per frame the face figures' displacements (the first role's; the sampler keys one figure).
	FString FaceFile, FaceGeometry;
	double FaceUnit = 1e-4;
	if (Root->TryGetStringField(TEXT("face"), FaceFile) && Root->TryGetStringField(TEXT("faceGeometry"), FaceGeometry) && !FaceGeometry.IsEmpty())
	{
		const FString Dir = FPaths::GetPath(InFile);
		if (!DTHFaceRegion::LoadFace(FPaths::ConvertRelativePathToFull(Dir / FaceFile), FaceRegion, FaceUnit, OutError)) return false;
	}
	for (const TSharedPtr<FJsonValue>& V : *RoleArray)
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		FRole& Role = Roles.AddDefaulted_GetRef();
		const TSharedPtr<FJsonObject>* Rest = nullptr;
		const TSharedPtr<FJsonObject>* Zero = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		if (!R.IsValid() || !R->TryGetObjectField(TEXT("rest"), Rest) || !DTHDazSamples::ReadState(*Rest, Role.Rest)
			|| !R->TryGetArrayField(TEXT("frames"), Frames))
		{
			OutError = FString::Printf(TEXT("%s: role %d has no rest or no frames."), *InFile, Roles.Num() - 1);
			return false;
		}
		R->TryGetStringField(TEXT("label"), Role.Label);
		R->TryGetStringField(TEXT("figure"), Role.Figure);
		if (R->TryGetObjectField(TEXT("zero"), Zero)) DTHDazSamples::ReadState(*Zero, Role.Zero);
		Role.Frames.Reserve(Frames->Num());
		for (int32 F = 0; F < Frames->Num(); ++F)
		{
			if (!DTHDazSamples::ReadState((*Frames)[F]->AsObject(), Role.Frames.AddDefaulted_GetRef()))
			{
				OutError = FString::Printf(TEXT("%s: frame %d of role %d is malformed."), *InFile, F, Roles.Num() - 1);
				return false;
			}
		}
		if (HasFace() && Roles.Num() == 1)
		{
			Role.Face.SetNum(Role.Frames.Num());
			if (!DTHFaceRegion::ReadGeometry(FPaths::GetPath(InFile) / FaceGeometry, FaceRegion, FaceUnit, 0, Role.Face, OutError)) return false;
		}
	}
	return true;
}

FFrameRate FDTHAnimClip::FrameRate() const
{
	const int32 Whole = FMath::RoundToInt(Fps);
	return FMath::IsNearlyEqual(Fps, double(Whole), 1e-6) ? FFrameRate(Whole, 1) : FFrameRate(FMath::RoundToInt(Fps * 1000.0), 1000);
}

namespace DTHAnimBuild
{
	namespace
	{
		/** One face fitter per character and face region, kept while a batch writes clip after clip. */
		struct FCachedFitter
		{
			TWeakObjectPtr<const USkeletalMesh> Mesh;
			int32 RegionPoints = 0;
			TUniquePtr<FDTHFaceFitter> Fitter;
		};
		FCachedFitter& Cache()
		{
			static FCachedFitter Instance;
			return Instance;
		}
	}

	bool FitFace(const USkeletalMesh& Mesh, const FDTHAnimClip& Clip, int32 Role, TArray<FDTHDazState>& OutFaces, FString& OutReport, FString& OutError)
	{
		OutFaces.Reset();
		if (!Clip.Roles.IsValidIndex(Role)) return false;
		const FDTHAnimClip::FRole& R = Clip.Roles[Role];
		OutFaces.SetNum(R.Frames.Num());
		bool bAny = false;
		for (const TArray<TArray<FVector3f>>& F : R.Face) bAny |= !F.IsEmpty();
		if (!Clip.HasFace() || !bAny) return true;

		int32 Points = 0;
		for (const FDTHFaceFigure& F : Clip.FaceRegion) Points += F.Vertices.Num();
		FCachedFitter& C = Cache();
		if (!C.Fitter || C.Mesh.Get() != &Mesh || C.RegionPoints != Points)
		{
			C.Fitter = MakeUnique<FDTHFaceFitter>();
			TArray<FString> Lines;
			if (!C.Fitter->Init(const_cast<USkeletalMesh*>(&Mesh), Clip.FaceRegion, Lines, OutError))
			{
				C.Fitter.Reset();
				return false;
			}
			C.Mesh = &Mesh;
			C.RegionPoints = Points;
		}
		// Frame by frame: a frame like the last takes its curves; the others start from the last frame's, so the face
		// moves smoothly and the fit is short (the first face takes the full fit).
		const double Start = FPlatformTime::Seconds();
		int32 Fitted = 0, Reused = 0;
		double MatchSum = 0.0, Worst = 1.0;
		int32 WorstFrame = 0;
		const TArray<TArray<FVector3f>>* Last = nullptr;
		TMap<FName, float> LastCurves;
		for (int32 F = 0; F < R.Face.Num() && F < OutFaces.Num(); ++F)
		{
			const TArray<TArray<FVector3f>>& Face = R.Face[F];
			if (Face.IsEmpty()) { Last = nullptr; LastCurves.Reset(); continue; }
			if (Last && *Last == Face) { OutFaces[F] = OutFaces[F - 1]; ++Reused; continue; }
			FDTHShapeFit::FOptions Options;
			Options.TimeBudget = LastCurves.IsEmpty() ? 3.0 : 1.0;
			FDTHFaceFitter::FResult Result;
			if (!C.Fitter->Fit(Face, Options, Result, LastCurves.IsEmpty() ? nullptr : &LastCurves)) continue;
			OutFaces[F].FaceCurves = Result.Curves;
			OutFaces[F].FaceBones = Result.Bones;
			LastCurves = Result.Values;
			Last = &Face;
			++Fitted;
			if (Result.Largest >= FDTHFaceFitter::RestTolerance)
			{
				MatchSum += Result.Match;
				if (Result.Match < Worst) { Worst = Result.Match; WorstFrame = F; }
			}
		}
		OutReport = FString::Printf(TEXT("face fitted on %d frames (%d more the same as the frame before) in %.1f s; frame %d reproduces the least (%.1f%% of the Daz face's movement)"),
			Fitted, Reused, FPlatformTime::Seconds() - Start, WorstFrame, 100.0 * Worst);
		return true;
	}

	UAnimSequence* Write(const USkeletalMesh& Mesh, const FDTHAnimClip& Clip, int32 Role, const FString& Folder, const FString& Name,
		FString& OutError, FString* FaceReport)
	{
		if (!Clip.Roles.IsValidIndex(Role))
		{
			OutError = FString::Printf(TEXT("%s has no role %d."), *Clip.Label, Role);
			return nullptr;
		}
		const FDTHAnimClip::FRole& R = Clip.Roles[Role];
		if (!Clip.HasFace() || R.Face.IsEmpty())
		{
			return DTHPoseCharacter::Write(Mesh, R.Rest, R.Frames, Folder, Name, OutError, Clip.FrameRate());
		}
		TArray<FDTHDazState> Faces;
		FString Report;
		if (!FitFace(Mesh, Clip, Role, Faces, Report, OutError)) return nullptr;
		if (FaceReport) *FaceReport = Report;
		TArray<FDTHDazState> Frames = R.Frames;
		for (int32 F = 0; F < Frames.Num() && F < Faces.Num(); ++F)
		{
			Frames[F].FaceCurves = MoveTemp(Faces[F].FaceCurves);
			Frames[F].FaceBones = MoveTemp(Faces[F].FaceBones);
		}
		return DTHPoseCharacter::Write(Mesh, R.Rest, Frames, Folder, Name, OutError, Clip.FrameRate());
	}
}

FString FDTHAnimClip::FSource::Describe() const
{
	if (Figure.IsEmpty()) return FString();
	FString Line = FString::Printf(TEXT("carried onto Genesis 9 from %s in Daz: bones within %.2f deg of G8's (worst %s), bone directions within %.2f deg (%s), ankle heights within %.2f cm"),
		*Figure, RotationGapDeg, *RotationGapBone, SegmentGapDeg, *SegmentGapBone, FootHeightGapCm);
	if (!BeyondG9Limits.IsEmpty())
	{
		Line += FString::Printf(TEXT("; %d G9 channels go past G9's joint limits to follow G8 (worst %s by %.1f deg)"), BeyondG9Limits.Num(),
			*BeyondG9Limits[0].Key, BeyondG9Limits[0].Value);
	}
	return Line + TEXT(".");
}
