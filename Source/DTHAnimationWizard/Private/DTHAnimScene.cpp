#include "DTHAnimScene.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "DTHPoseCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneSpawnable.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DTHAnimScenes
{
	TArray<FDTHAnimScene> FromIndex(const FDTHAnimIndex& Index)
	{
		TMap<FString, FDTHAnimScene> ByName;
		TArray<FString> Order;
		for (const FDTHAnimIndexEntry& C : Index.Clips)
		{
			int32 Space = INDEX_NONE;
			if (C.Samples.IsEmpty() || !C.Label.FindLastChar(TEXT(' '), Space)) continue;
			const FString Key = C.Pack() + TEXT("|") + C.Label.Left(Space);
			FDTHAnimScene& S = ByName.FindOrAdd(Key);
			if (S.Name.IsEmpty()) { S.Name = C.Label.Left(Space); Order.Add(Key); }
			S.Roles.Add({C.Label.Mid(Space + 1), &C});
		}
		TArray<FDTHAnimScene> Out;
		for (const FString& Key : Order)
		{
			if (ByName[Key].Roles.Num() >= 2) Out.Add(ByName[Key]);
		}
		return Out;
	}

	ULevelSequence* Build(const FDTHAnimScene& Scene, const TMap<FString, USkeletalMesh*>& Assigned, const FString& Root, bool bReplace,
		TArray<FDTHAnimMessage>& OutReport, TMap<FString, UAnimSequence*>* OutSequences, float Speed)
	{
		Speed = DTHAnimBatch::ClampSpeed(Speed);
		// Each role's sequence for its character.
		TArray<TPair<const FDTHAnimScene::FRole*, UAnimSequence*>> Parts;
		FFrameRate Rate(30, 1);
		int32 Frames = 0;
		for (const FDTHAnimScene::FRole& Role : Scene.Roles)
		{
			USkeletalMesh* const* Mesh = Assigned.Find(Role.Label);
			if (!Mesh || !*Mesh)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: no character for the role %s."), *Scene.Name, *Role.Label)));
				return nullptr;
			}
			TArray<UAnimSequence*> Written;
			const FString Path = DTHAnimBatch::PackagePath(**Mesh, *Role.Clip, Root);
			if (!DTHAnimBatch::WriteClips(**Mesh, {Role.Clip}, Root, bReplace, OutReport, &Written))
			{
				return nullptr;
			}
			UAnimSequence* Sequence = Written.IsEmpty() ? LoadObject<UAnimSequence>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path))) : Written[0];
			if (!Sequence)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: no sequence at %s."), *Role.Label, *Path)));
				return nullptr;
			}
			// Every role at the scene's speed, written now or kept from before, so the actors stay in sync.
			DTHAnimBatch::SetSpeed(*Sequence, Speed);
			if (Parts.IsEmpty()) Rate = Sequence->GetSamplingFrameRate();
			else if (Sequence->GetSamplingFrameRate() != Rate)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: the roles' clips have different frame rates."), *Scene.Name)));
				return nullptr;
			}
			Frames = FMath::Max(Frames, Sequence->GetNumberOfSampledKeys());
			Parts.Add({&Role, Sequence});
			if (OutSequences) OutSequences->Add(Role.Label, Sequence);
		}

		// The level sequence.
		const FString Name = TEXT("LS_") + DTHPoseCharacter::AssetName(Scene.Name);
		// Per pack, as the clips' sequences are: scene names repeat across packs.
		FString PackFolder;
		TArray<FString> PackParts;
		Scene.Roles[0].Clip->Pack().ParseIntoArray(PackParts, TEXT("/"));
		for (const FString& P : PackParts) PackFolder /= DTHPoseCharacter::AssetName(P);
		const FString PackageName = Root / TEXT("Scenes") / PackFolder / Name;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UObject* Old = LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + Name));
			if (!bReplace || !Old || ObjectTools::ForceDeleteObjects({Old}, false) == 0)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s is already there%s."), *PackageName, bReplace ? TEXT(" and could not be replaced") : TEXT(""))));
				return nullptr;
			}
		}
		UPackage* Package = CreatePackage(*PackageName);
		ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		Sequence->Initialize();
		UMovieScene* Movie = Sequence->GetMovieScene();
		Movie->SetDisplayRate(Rate);
		const FFrameRate Tick = Movie->GetTickResolution();
		// At a speed other than 1 the scene lasts its frames divided by the speed.
		const FFrameNumber End = (FFrameRate::TransformTime(FFrameTime(FMath::Max(Frames - 1, 1)), Rate, Tick) / Speed).CeilToFrame();
		Movie->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), End));

		for (const TPair<const FDTHAnimScene::FRole*, UAnimSequence*>& P : Parts)
		{
			USkeletalMesh* Mesh = Assigned[P.Key->Label];
			const FString Label = FString::Printf(TEXT("%s (%s)"), *P.Key->Label, *Mesh->GetName());
			UObject* Template = Sequence->MakeSpawnableTemplateFromInstance(*ASkeletalMeshActor::StaticClass()->GetDefaultObject(), FName(*DTHPoseCharacter::AssetName(Label)));
			ASkeletalMeshActor* Actor = Cast<ASkeletalMeshActor>(Template);
			if (!Actor)
			{
				OutReport.Add(FDTHAnimMessage::Error(TEXT("Could not make a spawnable actor for the level sequence.")));
				return nullptr;
			}
			// The character as the DTH import sets it up: its mesh, with the post-process blueprint the mesh carries.
			Actor->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(Mesh);
			Actor->GetSkeletalMeshComponent()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			const FGuid Binding = Movie->AddSpawnable(Label, *Template);

			UMovieSceneSpawnTrack* Spawn = Movie->AddTrack<UMovieSceneSpawnTrack>(Binding);
			UMovieSceneBoolSection* Alive = Cast<UMovieSceneBoolSection>(Spawn->CreateNewSection());
			Alive->GetChannel().SetDefault(true);
			Alive->SetRange(TRange<FFrameNumber>::All());
			Spawn->AddSection(*Alive);

			UMovieSceneSkeletalAnimationTrack* Track = Movie->AddTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
			UMovieSceneSection* Section = Track->AddNewAnimation(FFrameNumber(0), P.Value);
			if (!Section)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: could not add its animation track."), *Label)));
				return nullptr;
			}
			// Sequencer plays the sequence at its Rate Scale; the section is as long as the sequence takes at that speed.
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), (P.Value->GetPlayLength() / Speed * Tick).CeilToFrame()));
		}
		FAssetRegistryModule::AssetCreated(Sequence);
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Sequence, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);

		FString Roles;
		for (const auto& P : Parts) Roles += FString::Printf(TEXT("%s%s as %s"), Roles.IsEmpty() ? TEXT("") : TEXT(", "), *Assigned[P.Key->Label]->GetName(), *P.Key->Label);
		const FString AtSpeed = FMath::IsNearlyEqual(Speed, 1.f) ? FString() : FString::Printf(TEXT(" playing at %gx"), Speed);
		OutReport.Add(FDTHAnimMessage::Info(FString::Printf(TEXT("%s: %s, %d frames at %s%s, built as %s."), *Scene.Name, *Roles, Frames,
			*Rate.ToPrettyText().ToString(), *AtSpeed, *Sequence->GetPathName())));
		OutReport.Add(FDTHAnimMessage::Info(TEXT("Manual steps: drag the level sequence into a level to play it. Every actor starts at the sequence's origin: "
			"to place the group, give the sequence's actors one shared parent, or move them together. If a character's proportions differ "
			"from base Genesis 9, contacts between the actors (hands, embraces) may not meet exactly.")));
		return Sequence;
	}
}
