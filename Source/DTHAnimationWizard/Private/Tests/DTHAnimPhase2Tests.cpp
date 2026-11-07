#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "DTHAnimBatch.h"
#include "DTHAnimScripting.h"
#include "DTHAnimSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UI/SDTHAnimationWizard.h"
#include "DTHAnimationWizardModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	/** How many clips the batch check writes: enough for the batch path, short enough to run often. */
	constexpr int32 BatchClips = 4;
}

// Phase 2, the batch: the first clips of the sampler's index written for the character into Test Folder/Batch, one key
// per sampled frame at the clip's rate, at the path the wizard promises. Run again with replace: no duplicates, same
// keys. Run again without: every clip left as it was.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimP2Batch, "DTH.Anim.P2.Batch", Flags)
bool FDTHAnimP2Batch::RunTest(const FString&)
{
	const UDTHAnimSettings* S = GetDefault<UDTHAnimSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(S->Character.TryLoad());
	if (!Mesh) { AddError(TEXT("Set Character.")); return false; }
	const FString Root = S->TestFolder.Path / TEXT("Batch");
	if (!Root.StartsWith(TEXT("/Game/")) || !Root.Contains(TEXT("_Test"))) { AddError(TEXT("Set Test Folder to a _Test folder.")); return false; }
	FDTHAnimIndex Index;
	FString Error;
	if (!Index.Load(S->SamplesIndex.FilePath, Error)) { AddError(Error); return false; }
	TArray<const FDTHAnimIndexEntry*> Clips;
	// -DTHAnimBatchAll: the whole library, to check the batch at full size.
	const int32 Limit = FParse::Param(FCommandLine::Get(), TEXT("DTHAnimBatchAll")) ? MAX_int32 : BatchClips;
	for (const FDTHAnimIndexEntry& C : Index.Clips) if (Clips.Num() < Limit && !C.Samples.IsEmpty()) Clips.Add(&C);
	AddInfo(FString::Printf(TEXT("Index %s: %d clips, %d not sampled. Writing the first %d."), *Index.File, Index.Clips.Num(), Index.Errors.Num(), Clips.Num()));

	auto Run = [&](bool bReplace, TArray<UAnimSequence*>& Out)
	{
		TArray<FDTHAnimMessage> Report;
		const bool bOk = DTHAnimBatch::WriteClips(*Mesh, Clips, Root, bReplace, Report, &Out);
		for (const FDTHAnimMessage& M : Report)
		{
			if (M.Severity == FDTHAnimMessage::ESeverity::Error) AddError(M.Text); else AddInfo(M.ToLine());
		}
		return bOk;
	};
	TArray<UAnimSequence*> First, Second, Third;
	if (!Run(true, First)) return false;
	TestEqual(TEXT("Sequences written"), First.Num(), Clips.Num());
	for (int32 I = 0; I < First.Num() && I < Clips.Num(); ++I)
	{
		const FString Expected = DTHAnimBatch::PackagePath(*Mesh, *Clips[I], Root);
		TestEqual(*FString::Printf(TEXT("%s: path"), *Clips[I]->Label), First[I]->GetOutermost()->GetName(), Expected);
		// A one-frame clip spans frame 0 to 1 (the shared writer: two equal keys), as a single pose does.
		TestEqual(*FString::Printf(TEXT("%s: one key per sampled frame"), *Clips[I]->Label), First[I]->GetNumberOfSampledKeys(), FMath::Max(Clips[I]->Frames, 2));
		TestEqual(*FString::Printf(TEXT("%s: frame rate"), *Clips[I]->Label), First[I]->GetSamplingFrameRate().AsDecimal(), Clips[I]->Fps);
	}
	if (!Run(true, Second)) return false;
	for (int32 I = 0; I < Second.Num() && I < Clips.Num(); ++I)
	{
		const FString Path = DTHAnimBatch::PackagePath(*Mesh, *Clips[I], Root);
		TestFalse(*FString::Printf(TEXT("%s: replaced, not numbered"), *Clips[I]->Label), FPackageName::DoesPackageExist(Path + TEXT("_2")));
		TestEqual(*FString::Printf(TEXT("%s: same keys after replacing"), *Clips[I]->Label), Second[I]->GetNumberOfSampledKeys(), FMath::Max(Clips[I]->Frames, 2));
	}
	Run(false, Third);
	TestEqual(TEXT("Without replace, nothing is written again"), Third.Num(), 0);
	return true;
}

// Phase 2, the tab: it opens from scripting on both pages, lists the sampler's clips, and the scripting buttons give
// the same lists.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimP2Wizard, "DTH.Anim.P2.Wizard", Flags)
bool FDTHAnimP2Wizard::RunTest(const FString&)
{
	TestEqual(TEXT("Opens on Settings"), UDTHAnimScripting::OpenWizard(TEXT("Settings")), FString(TEXT("Wizard open.")));
	TestEqual(TEXT("Opens on Clips"), UDTHAnimScripting::OpenWizard(TEXT("Clips")), FString(TEXT("Wizard open.")));
	TestTrue(TEXT("Unknown page refused"), UDTHAnimScripting::OpenWizard(TEXT("Nope")).StartsWith(TEXT("ERROR:")));
	const TSharedPtr<SDTHAnimationWizard> Wizard = SDTHAnimationWizard::Get();
	if (!Wizard.IsValid()) { AddError(TEXT("No wizard.")); return false; }
	Wizard->Reload();
	const TArray<FString> Listed = UDTHAnimScripting::ListClips(TEXT(""));
	TestTrue(TEXT("Clips listed"), Listed.Num() > 0 && !Listed[0].StartsWith(TEXT("ERROR:")));
	TestEqual(TEXT("The tab lists what scripting lists"), Wizard->NumListed(), Listed.Num());
	AddInfo(FString::Printf(TEXT("%d clips listed; first: %s"), Listed.Num(), Listed.IsEmpty() ? TEXT("") : *Listed[0]));
	TestEqual(TEXT("Opens on Scenes"), UDTHAnimScripting::OpenWizard(TEXT("Scenes")), FString(TEXT("Wizard open.")));
	const TArray<FString> SceneLines = UDTHAnimScripting::ListScenes(TEXT(""));
	TestTrue(TEXT("Scenes listed"), SceneLines.Num() > 0 && !SceneLines[0].StartsWith(TEXT("ERROR:")));
	TestEqual(TEXT("The tab lists what scripting lists (scenes)"), Wizard->NumScenes(), SceneLines.Num());
	AddInfo(FString::Printf(TEXT("%d scenes listed; first: %s"), SceneLines.Num(), SceneLines.IsEmpty() ? TEXT("") : *SceneLines[0]));
	// The look: each page as drawn, to Saved/DTHAnimationWizard/phase2.
	if (TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FDTHAnimationWizardModule::WizardTabName))
	{
		for (const TCHAR* Page : {TEXT("Settings"), TEXT("Clips"), TEXT("Scenes")})
		{
			Wizard->ShowPage(Page);
			if (FCString::Stricmp(Page, TEXT("Scenes")) == 0) Wizard->PreviewScene(0, Cast<USkeletalMesh>(GetDefault<UDTHAnimSettings>()->Character.TryLoad()));
			for (int32 Tick = 0; Tick < 6; ++Tick) FSlateApplication::Get().Tick();
			TArray<FColor> Pixels;
			FIntVector Size;
			if (FSlateApplication::Get().TakeScreenshot(Tab->GetContent(), Pixels, Size) && Size.X > 0)
			{
				for (FColor& P : Pixels) P.A = 255;
				const FString File = UDTHAnimSettings::SavedDir() / TEXT("phase2") / FString::Printf(TEXT("tab_%s.png"), Page);
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
				FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Size.X, Size.Y));
				AddInfo(FString::Printf(TEXT("%s page: %s (%dx%d)."), Page, *FPaths::ConvertRelativePathToFull(File), Size.X, Size.Y));
			}
			else AddWarning(FString::Printf(TEXT("Could not capture the %s page."), Page));
		}
		Tab->RequestCloseTab();
	}
	return true;
}

#endif
