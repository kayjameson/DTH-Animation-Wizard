#include "DTHAnimScripting.h"

#include "DTHAnimBatch.h"
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
	if (!Page.IsEmpty() && !Wizard->ShowPage(Page)) return FString::Printf(TEXT("ERROR: No page %s (Settings or Clips)."), *Page);
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
		Out.Add(FString::Printf(TEXT("%s | %s | %d | %g | %d"), *C->Label, *C->Pack(), C->Frames, C->Fps, C->KeyedMorphs));
	}
	return Out;
}

TArray<FString> UDTHAnimScripting::WriteClips(const FString& MeshPath, const TArray<FString>& Labels, bool bReplace, const FString& Folder)
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
		if (const FDTHAnimIndexEntry* C = Index.FindLabel(L)) Clips.Add(C);
		else Out.Add(FString::Printf(TEXT("ERROR: No sampled clip %s."), *L));
	}
	TArray<FDTHAnimMessage> Report;
	DTHAnimBatch::WriteClips(*Mesh, Clips, Folder.IsEmpty() ? GetDefault<UDTHAnimSettings>()->SequenceFolder.Path : Folder, bReplace, Report);
	for (const FDTHAnimMessage& M : Report) Out.Add(M.ToLine());
	return Out;
}
