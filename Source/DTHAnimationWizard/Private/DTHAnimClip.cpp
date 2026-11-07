#include "DTHAnimClip.h"

#include "DTHPoseCharacter.h"
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
	const TArray<TSharedPtr<FJsonValue>>* RoleArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("roles"), RoleArray) || RoleArray->IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s has no roles."), *InFile);
		return false;
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
	UAnimSequence* Write(const USkeletalMesh& Mesh, const FDTHAnimClip& Clip, int32 Role, const FString& Folder, const FString& Name,
		FString& OutError)
	{
		if (!Clip.Roles.IsValidIndex(Role))
		{
			OutError = FString::Printf(TEXT("%s has no role %d."), *Clip.Label, Role);
			return nullptr;
		}
		const FDTHAnimClip::FRole& R = Clip.Roles[Role];
		return DTHPoseCharacter::Write(Mesh, R.Rest, R.Frames, Folder, Name, OutError, Clip.FrameRate());
	}
}
