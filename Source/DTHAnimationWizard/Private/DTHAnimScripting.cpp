#include "DTHAnimScripting.h"

#include "DTHAnimBatch.h"
#include "DTHAnimScene.h"
#include "DTHAnimSettings.h"
#include "DTHAnimationWizardModule.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "UI/SDTHAnimationWizard.h"

FString UDTHAnimScripting::OpenWizard(const FString& Page)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDTHAnimationWizardModule::WizardTabName);
	const TSharedPtr<SDTHAnimationWizard> Wizard = SDTHAnimationWizard::Get();
	if (!Wizard.IsValid()) return TEXT("ERROR: The wizard tab did not open.");
	if (!Page.IsEmpty() && !Wizard->ShowPage(Page)) return FString::Printf(TEXT("ERROR: No page %s (Settings, Clips or Scenes)."), *Page);
	return TEXT("Wizard open.");
}

TArray<FString> UDTHAnimScripting::ListClips(const FString& Filter)
{
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(GetDefault<UDTHAnimSettings>()->SamplesIndex.FilePath, Error)) return {TEXT("ERROR: ") + Error};
	TArray<FString> Out;
	for (const FDTHAnimIndexEntry* C : Index.Find(Filter))
	{
		Out.Add(FString::Printf(TEXT("%s | %s | %d | %g | %d | %s | %s"), *C->Label, *C->Pack(), C->Frames, C->Fps, C->KeyedMorphs, *C->Key(),
			C->Source.IsEmpty() ? TEXT("Genesis 9") : *FString::Printf(TEXT("from %s"), *C->SourceName())));
	}
	return Out;
}

TArray<FString> UDTHAnimScripting::WriteClips(const FString& MeshPath, const TArray<FString>& Labels, bool bReplace, const FString& Folder, float Speed)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh) return {FString::Printf(TEXT("ERROR: %s is not a skeletal mesh."), *MeshPath)};
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(GetDefault<UDTHAnimSettings>()->SamplesIndex.FilePath, Error)) return {TEXT("ERROR: ") + Error};
	TArray<FString> Out;
	TArray<const FDTHAnimIndexEntry*> Clips;
	for (const FString& L : Labels)
	{
		TArray<FString> Ambiguous;
		if (const FDTHAnimIndexEntry* C = Index.FindLabel(L, &Ambiguous)) Clips.Add(C);
		else if (Ambiguous.Num() > 1) Out.Add(FString::Printf(TEXT("ERROR: %d clips are called %s; name one by pack: %s."), Ambiguous.Num(), *L, *FString::Join(Ambiguous, TEXT(", "))));
		else Out.Add(FString::Printf(TEXT("ERROR: No sampled clip %s."), *L));
	}
	TMap<FString, float> Speeds;
	for (const FDTHAnimIndexEntry* C : Clips) Speeds.Add(C->Key(), Speed);
	TArray<FDTHAnimMessage> Report;
	DTHAnimBatch::WriteClips(*Mesh, Clips, Folder.IsEmpty() ? GetDefault<UDTHAnimSettings>()->SequenceFolder.Path : Folder, bReplace, Report, nullptr, &Speeds);
	for (const FDTHAnimMessage& M : Report) Out.Add(M.ToLine());
	return Out;
}

TArray<FString> UDTHAnimScripting::ListScenes(const FString& Filter)
{
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(GetDefault<UDTHAnimSettings>()->SamplesIndex.FilePath, Error)) return {TEXT("ERROR: ") + Error};
	TArray<FString> Out;
	for (const FDTHAnimScene& S : DTHAnimScenes::FromIndex(Index))
	{
		if (!Filter.IsEmpty() && !S.Name.Contains(Filter)) continue;
		FString Roles;
		for (const FDTHAnimScene::FRole& R : S.Roles) Roles += FString::Printf(TEXT("%s%s (%d)"), Roles.IsEmpty() ? TEXT("") : TEXT(", "), *R.Label, R.Clip->Frames);
		Out.Add(FString::Printf(TEXT("%s | %s | %s | %s"), *S.Name, *S.Roles[0].Clip->Pack(), *Roles, *S.Key()));
	}
	return Out;
}

TArray<FString> UDTHAnimScripting::BuildScene(const FString& Scene, const TArray<FString>& Roles, const TArray<FString>& Meshes, bool bReplace,
	const FString& Folder, float Speed)
{
	if (Roles.Num() != Meshes.Num()) return {TEXT("ERROR: Give one mesh per role.")};
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(GetDefault<UDTHAnimSettings>()->SamplesIndex.FilePath, Error)) return {TEXT("ERROR: ") + Error};
	const TArray<FDTHAnimScene> Scenes = DTHAnimScenes::FromIndex(Index);
	const FDTHAnimScene* Found = Scenes.FindByPredicate([&Scene](const FDTHAnimScene& S) { return S.Key() == Scene; });
	if (!Found)
	{
		TArray<FString> Named;
		for (const FDTHAnimScene& S : Scenes) if (S.Name == Scene) { Named.Add(S.Key()); Found = &S; }
		if (Named.Num() > 1) return {FString::Printf(TEXT("ERROR: %d scenes are called %s; name one by pack: %s."), Named.Num(), *Scene, *FString::Join(Named, TEXT(", ")))};
		if (!Found) return {FString::Printf(TEXT("ERROR: No scene %s."), *Scene)};
	}
	TMap<FString, USkeletalMesh*> Assigned;
	for (int32 I = 0; I < Roles.Num(); ++I)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Meshes[I]);
		if (!Mesh) return {FString::Printf(TEXT("ERROR: %s is not a skeletal mesh."), *Meshes[I])};
		Assigned.Add(Roles[I], Mesh);
	}
	TArray<FDTHAnimMessage> Report;
	DTHAnimScenes::Build(*Found, Assigned, Folder.IsEmpty() ? GetDefault<UDTHAnimSettings>()->SequenceFolder.Path : Folder, bReplace, Report, nullptr, Speed);
	TArray<FString> Out;
	for (const FDTHAnimMessage& M : Report) Out.Add(M.ToLine());
	return Out;
}
