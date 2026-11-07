#include "DTHAnimBatch.h"

#include "Animation/AnimSequence.h"
#include "DTHAnimClip.h"
#include "DTHPoseCharacter.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "DTHAnimBatch"

FString FDTHAnimIndexEntry::Pack() const
{
	// ".../People/Genesis 9/Animations/<Pack...>/<clip>.duf"
	FString Path = FPaths::GetPath(File);
	const int32 At = Path.Find(TEXT("/Animations/"));
	return At == INDEX_NONE ? FPaths::GetCleanFilename(Path) : Path.Mid(At + 12);
}

bool FDTHAnimIndex::Load(const FString& InFile, FString& OutError)
{
	*this = FDTHAnimIndex();
	File = InFile;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *InFile))
	{
		OutError = FString::Printf(TEXT("Could not read %s. Run the Daz sampler first (see Settings)."), *InFile);
		return false;
	}
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| Root->GetStringField(TEXT("format")) != TEXT("DTH Animation Samples Index"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH Animation Samples index."), *InFile);
		return false;
	}
	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Root->TryGetArrayField(TEXT("clips"), Array))
	{
		for (const TSharedPtr<FJsonValue>& V : *Array)
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			if (!C.IsValid()) continue;
			FDTHAnimIndexEntry& E = Clips.AddDefaulted_GetRef();
			E.Label = C->GetStringField(TEXT("label"));
			E.File = C->GetStringField(TEXT("file"));
			C->TryGetStringField(TEXT("samples"), E.Samples);
			C->TryGetStringField(TEXT("skipped"), E.Skipped);
			C->TryGetNumberField(TEXT("frames"), E.Frames);
			C->TryGetNumberField(TEXT("fps"), E.Fps);
			C->TryGetNumberField(TEXT("keyedMorphs"), E.KeyedMorphs);
			C->TryGetNumberField(TEXT("restoreDriftCm"), E.RestoreDriftCm);
		}
	}
	if (Root->TryGetArrayField(TEXT("errors"), Array))
	{
		for (const TSharedPtr<FJsonValue>& V : *Array) Errors.Add(V->AsString());
	}
	return true;
}

TArray<const FDTHAnimIndexEntry*> FDTHAnimIndex::Find(const FString& Filter) const
{
	TArray<const FDTHAnimIndexEntry*> Out;
	for (const FDTHAnimIndexEntry& C : Clips)
	{
		if (Filter.IsEmpty() || C.Label.Contains(Filter) || C.Pack().Contains(Filter)) Out.Add(&C);
	}
	return Out;
}

const FDTHAnimIndexEntry* FDTHAnimIndex::FindLabel(const FString& LabelOrKey, TArray<FString>* OutAmbiguous) const
{
	if (const FDTHAnimIndexEntry* ByKey = Clips.FindByPredicate([&LabelOrKey](const FDTHAnimIndexEntry& C) { return C.Key() == LabelOrKey; }))
	{
		return ByKey;
	}
	const FDTHAnimIndexEntry* Found = nullptr;
	int32 Count = 0;
	for (const FDTHAnimIndexEntry& C : Clips)
	{
		if (C.Label != LabelOrKey) continue;
		Found = &C;
		++Count;
		if (OutAmbiguous) OutAmbiguous->Add(C.Key());
	}
	if (Count == 1) { if (OutAmbiguous) OutAmbiguous->Reset(); return Found; }
	return nullptr;
}

FString FDTHAnimMessage::ToLine() const
{
	return Severity == ESeverity::Error ? TEXT("ERROR: ") + Text : Severity == ESeverity::Warning ? TEXT("WARNING: ") + Text : Text;
}

namespace DTHAnimBatch
{
	FString PackagePath(const USkeletalMesh& Mesh, const FDTHAnimIndexEntry& Clip, const FString& Root)
	{
		FString Character = Mesh.GetName();
		Character.RemoveFromStart(TEXT("SKM_"));
		Character.RemoveFromEnd(TEXT("_Body"));
		FString PackFolder;
		TArray<FString> Parts;
		Clip.Pack().ParseIntoArray(Parts, TEXT("/"));
		for (const FString& P : Parts) PackFolder /= DTHPoseCharacter::AssetName(P);
		return Root / DTHPoseCharacter::AssetName(Character) / PackFolder / (TEXT("A_") + DTHPoseCharacter::AssetName(Clip.Label));
	}

	bool WriteClips(USkeletalMesh& Mesh, TConstArrayView<const FDTHAnimIndexEntry*> Clips, const FString& Root, bool bReplace,
		TArray<FDTHAnimMessage>& OutReport, TArray<UAnimSequence*>* OutSequences)
	{
		if (!Mesh.GetSkeleton())
		{
			OutReport.Add(FDTHAnimMessage::Error(TEXT("The character has no skeleton.")));
			return false;
		}
		if (!Root.StartsWith(TEXT("/Game/")))
		{
			OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("Sequence Folder %s is not a content folder."), *Root)));
			return false;
		}
		int32 Written = 0, Kept = 0, Failed = 0, WithMorphs = 0, Frames = 0;
		const double Start = FPlatformTime::Seconds();
		FScopedSlowTask Task(float(Clips.Num()), LOCTEXT("Writing", "Writing animation sequences..."));
		Task.MakeDialog(true);
		for (const FDTHAnimIndexEntry* Entry : Clips)
		{
			Task.EnterProgressFrame(1.f, FText::FromString(Entry->Label));
			if (Task.ShouldCancel())
			{
				OutReport.Add(FDTHAnimMessage::Warning(TEXT("Cancelled: the clips after this were not written.")));
				break;
			}
			if (Entry->Samples.IsEmpty())
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: not sampled (%s)."), *Entry->Label, *Entry->Skipped)));
				++Failed;
				continue;
			}
			const FString Path = PackagePath(Mesh, *Entry, Root);
			const FString Folder = FPackageName::GetLongPackagePath(Path);
			const FString Name = FPackageName::GetShortName(Path);
			if (FPackageName::DoesPackageExist(Path))
			{
				if (!bReplace)
				{
					OutReport.Add(FDTHAnimMessage::Info(FString::Printf(TEXT("%s: %s is already there; left as it is."), *Entry->Label, *Path)));
					++Kept;
					continue;
				}
				if (UObject* Old = LoadObject<UObject>(nullptr, *(Path + TEXT(".") + Name)))
				{
					if (ObjectTools::ForceDeleteObjects({Old}, false) == 0)
					{
						OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: could not replace %s (in use?)."), *Entry->Label, *Path)));
						++Failed;
						continue;
					}
				}
			}
			FDTHAnimClip Clip;
			FString Error;
			UAnimSequence* Sequence = Clip.Load(Entry->Samples, Error) ? DTHAnimBuild::Write(Mesh, Clip, 0, Folder, Name, Error) : nullptr;
			if (!Sequence)
			{
				OutReport.Add(FDTHAnimMessage::Error(FString::Printf(TEXT("%s: %s"), *Entry->Label, *Error)));
				++Failed;
				continue;
			}
			if (OutSequences) OutSequences->Add(Sequence);
			++Written;
			Frames += Clip.Roles[0].Frames.Num();
			OutReport.Add(FDTHAnimMessage::Info(FString::Printf(TEXT("%s: %d frames at %s, %.2f s, written to %s."), *Entry->Label,
				Clip.Roles[0].Frames.Num(), *Clip.FrameRate().ToPrettyText().ToString(), Sequence->GetPlayLength(), *Sequence->GetPathName())));
			if (!Clip.KeyedMorphs.IsEmpty())
			{
				++WithMorphs;
				OutReport.Add(FDTHAnimMessage::Warning(FString::Printf(TEXT("%s keys %d morphs (face or body shapes), e.g. %s. Sequences carry bones only, so these are not in it."),
					*Entry->Label, Clip.KeyedMorphs.Num(), *Clip.KeyedMorphs[0])));
			}
		}
		OutReport.Add(FDTHAnimMessage::Info(FString::Printf(TEXT("%d clips written for %s (%d frames in %.1f s), %d left as they were, %d failed."),
			Written, *Mesh.GetName(), Frames, FPlatformTime::Seconds() - Start, Kept, Failed)));
		if (Written > 0)
		{
			OutReport.Add(FDTHAnimMessage::Info(TEXT("Manual steps: play a sequence on the character's body mesh (Animation Mode \"Use Animation Asset\", or an Animation track in Sequencer). "
				"Keep the character's post-process blueprint on: it supplies the twist bones and the joint correctives. The sequences carry no face: "
				"face sequences or MetaHuman performances layer over them.")));
		}
		if (WithMorphs > 0)
		{
			OutReport.Add(FDTHAnimMessage::Warning(FString::Printf(TEXT("%d clips key morphs that are not in their sequences (see above)."), WithMorphs)));
		}
		return Failed == 0;
	}
}

#undef LOCTEXT_NAMESPACE
