#include "UI/SDTHAnimPreview.h"

#include "AdvancedPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "DTHAnimBatch.h"
#include "DTHAnimClip.h"
#include "DTHBonePoseDriverInstance.h"
#include "DTHPoseCharacter.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "ImageUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "RenderingThread.h"
#include "SEditorViewport.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDTHAnimPreview"

namespace
{
	/** Ticks the preview world so the poses and the post-process blueprints run. */
	class FAnimPreviewClient : public FEditorViewportClient
	{
	public:
		FAnimPreviewClient(FPreviewScene* InScene, const TWeakPtr<SEditorViewport>& InViewport)
			: FEditorViewportClient(nullptr, InScene, InViewport)
		{
			SetRealtime(true);
			EngineShowFlags.SetGrid(false);
			ViewFOV = 30.0f;
		}

		virtual void Tick(float DeltaSeconds) override
		{
			FEditorViewportClient::Tick(DeltaSeconds);
			if (PreviewScene)
			{
				PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}
	};
}

/** The viewport inside the preview. */
class SDTHAnimViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SDTHAnimViewport) {}
		SLATE_ARGUMENT(FPreviewScene*, Scene)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Scene = InArgs._Scene;
		SEditorViewport::Construct(SEditorViewport::FArguments());
	}

	virtual ~SDTHAnimViewport() override
	{
		if (Client.IsValid()) Client->Viewport = nullptr;
	}

	FEditorViewportClient* GetClient() const { return Client.Get(); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		Client = MakeShared<FAnimPreviewClient>(Scene, SharedThis(this));
		Client->SetViewLocation(FVector(300.0, 300.0, 100.0));
		Client->SetViewRotation(FRotator(0.0, -135.0, 0.0));
		return Client.ToSharedRef();
	}

private:
	FPreviewScene* Scene = nullptr;
	TSharedPtr<FEditorViewportClient> Client;
};

void SDTHAnimPreview::Construct(const FArguments& InArgs)
{
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	Scene->SetFloorVisibility(true);
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Viewport, SDTHAnimViewport).Scene(Scene.Get())
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return Actors.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.Text_Lambda([this]() { return FText::FromString(Problem.IsEmpty() ? TEXT("Pick a clip and a character to preview it.") : Problem); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			MakeTransport()
		]
	];
}

SDTHAnimPreview::~SDTHAnimPreview()
{
	Clear();
}

TSharedRef<SWidget> SDTHAnimPreview::MakeTransport()
{
	auto ViewButton = [this](const FText& Label, const TCHAR* Name)
	{
		const FString ViewName = Name;
		return SNew(SButton)
			.Text(Label)
			.IsEnabled_Lambda([this]() { return !Actors.IsEmpty(); })
			.OnClicked_Lambda([this, ViewName]() { SetView(ViewName); return FReply::Handled(); });
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.IsEnabled_Lambda([this]() { return !Actors.IsEmpty(); })
				.ToolTipText_Lambda([this]() { return bPlaying ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"); })
				.OnClicked_Lambda([this]() { bPlaying = !bPlaying; return FReply::Handled(); })
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image_Lambda([this]() { return FAppStyle::GetBrush(bPlaying ? "Animation.Pause" : "Animation.Forward"); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(SSlider)
				.IsEnabled_Lambda([this]() { return !Actors.IsEmpty(); })
				.Value_Lambda([this]() { const double L = GetLength(); return L > 0.0 ? float(Time / L) : 0.f; })
				.OnMouseCaptureBegin_Lambda([this]() { bPlaying = false; })
				.OnValueChanged_Lambda([this](float V) { SetTime(V * GetLength()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(SBox).MinDesiredWidth(150.f)
				[
					SNew(STextBlock).Text(this, &SDTHAnimPreview::GetTimeText)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("Speed", "Speed"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(70.f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.1f).MaxValue(4.f).MinSliderValue(0.1f).MaxSliderValue(2.f).Delta(0.05f)
					.ToolTipText(LOCTEXT("SpeedTip", "How fast the clip plays. Written into the sequence (its Rate Scale): the frames are unchanged."))
					.Value_Lambda([this]() { return Speed; })
					.OnValueChanged_Lambda([this](float V) { SetSpeed(V, true); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 12.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Text(LOCTEXT("SpeedReset", "1x"))
				.ToolTipText(LOCTEXT("SpeedResetTip", "Back to the clip's own speed."))
				.OnClicked_Lambda([this]() { SetSpeed(1.f, true); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bLoop ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bLoop = S == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(LOCTEXT("Loop", "Loop"))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[ViewButton(LOCTEXT("Front", "Front"), TEXT("front"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[ViewButton(LOCTEXT("Side", "Side"), TEXT("side"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[ViewButton(LOCTEXT("ThreeQuarter", "3/4"), TEXT("threequarter"))]
		];
}

void SDTHAnimPreview::Clear()
{
	for (FPlaying& A : Actors)
	{
		if (A.Component && Scene.IsValid()) Scene->RemoveComponent(A.Component);
	}
	Actors.Reset();
}

void SDTHAnimPreview::SetActors(const TArray<FActor>& InActors)
{
	Clear();
	Problem.Reset();
	Time = 0.0;
	TArray<FString> Problems;
	FScopedSlowTask Task(float(InActors.Num() * 2), LOCTEXT("Loading", "Loading the preview..."));
	Task.MakeDialogDelayed(0.5f);
	for (const FActor& In : InActors)
	{
		Task.EnterProgressFrame(1.f, FText::FromString(In.Label));
		if (!In.Mesh || !In.Mesh->GetSkeleton()) { Problems.Add(FString::Printf(TEXT("%s: no character."), *In.Label)); continue; }
		FDTHAnimClip Clip;
		FString Error;
		if (In.Samples.IsEmpty() || !Clip.Load(In.Samples, Error) || Clip.Roles.IsEmpty() || Clip.Roles[0].Frames.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s: %s"), *In.Label, Error.IsEmpty() ? TEXT("not sampled.") : *Error));
			continue;
		}
		Task.EnterProgressFrame(1.f);
		FPlaying& P = Actors.AddDefaulted_GetRef();
		P.Label = In.Label;
		P.Fps = Clip.Fps > 0.0 ? Clip.Fps : 30.0;
		// The same solve the written sequence carries, posed as it plays back (DTHPoseCharacter::LocalPose).
		const FDTHAnimClip::FRole& Role = Clip.Roles[0];
		const DTHPoseSolve::FRig Rig = DTHPoseCharacter::Make(*In.Mesh, Role.Rest);
		P.Frames.Reserve(Role.Frames.Num());
		for (const FDTHDazState& State : Role.Frames) P.Frames.Add(DTHPoseCharacter::LocalPose(Rig, *In.Mesh, State));

		P.Component = NewObject<UDebugSkelMeshComponent>(GetTransientPackage());
		P.Component->SetSkeletalMesh(In.Mesh);
		P.Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		P.Component->bEnableUpdateRateOptimizations = false;
		P.Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		P.Component->SetAnimInstanceClass(UDTHBonePoseDriverInstance::StaticClass());
		// Every actor at the origin: the samples carry each role's placement, as the level sequence plays them.
		Scene->AddComponent(P.Component, FTransform::Identity);
		if (!P.Component->GetPostProcessInstance())
		{
			Problems.Add(FString::Printf(TEXT("%s: %s has no post-process blueprint running, so its twist bones and correctives are missing."), *In.Label, *In.Mesh->GetName()));
		}
	}
	Problem = FString::Join(Problems, TEXT("\n"));
	Pose();
	FrameAfterTicks = 3;
}

double SDTHAnimPreview::GetLength() const
{
	double Length = 0.0;
	for (const FPlaying& A : Actors) Length = FMath::Max(Length, (A.Frames.Num() - 1) / A.Fps);
	return Length;
}

int32 SDTHAnimPreview::NumFrames() const
{
	int32 N = 0;
	for (const FPlaying& A : Actors) N = FMath::Max(N, A.Frames.Num());
	return N;
}

int32 SDTHAnimPreview::GetFrame() const
{
	return Actors.IsEmpty() ? 0 : FMath::Min(FMath::FloorToInt32(Time * Actors[0].Fps + 1e-6), Actors[0].Frames.Num() - 1);
}

UDebugSkelMeshComponent* SDTHAnimPreview::GetComponent(int32 Actor) const
{
	return Actors.IsValidIndex(Actor) ? Actors[Actor].Component.Get() : nullptr;
}

void SDTHAnimPreview::SetSpeed(float InSpeed, bool bNotify)
{
	Speed = DTHAnimBatch::ClampSpeed(InSpeed);
	if (bNotify && OnSpeedChanged) OnSpeedChanged(Speed);
}

void SDTHAnimPreview::SetTime(double Seconds)
{
	Time = FMath::Clamp(Seconds, 0.0, GetLength());
	Pose();
}

void SDTHAnimPreview::Pose()
{
	for (FPlaying& A : Actors)
	{
		UDTHBonePoseDriverInstance* Driver = A.Component ? Cast<UDTHBonePoseDriverInstance>(A.Component->GetAnimInstance()) : nullptr;
		if (!Driver || A.Frames.IsEmpty()) continue;
		// Between two frames: their blend (translations lerped, rotations slerped), as a sequence player plays its keys.
		const double At = FMath::Clamp(Time * A.Fps, 0.0, double(A.Frames.Num() - 1));
		const int32 F0 = FMath::FloorToInt32(At);
		const int32 F1 = FMath::Min(F0 + 1, A.Frames.Num() - 1);
		const float Alpha = float(At - F0);
		if (F0 == F1 || Alpha < 1e-5f)
		{
			Driver->SetPose(A.Frames[F0], {});
			continue;
		}
		TMap<FName, FTransform> Blend;
		Blend.Reserve(A.Frames[F0].Num());
		for (const TPair<FName, FTransform>& P : A.Frames[F0])
		{
			const FTransform* Next = A.Frames[F1].Find(P.Key);
			FTransform T = P.Value;
			if (Next) T.Blend(P.Value, *Next, Alpha);
			Blend.Add(P.Key, T);
		}
		Driver->SetPose(Blend, {});
	}
}

void SDTHAnimPreview::EvaluateNow(float DeltaSeconds)
{
	for (FPlaying& A : Actors)
	{
		if (!A.Component) continue;
		A.Component->TickAnimation(DeltaSeconds, false);
		A.Component->RefreshBoneTransforms();
		A.Component->HandleExistingParallelEvaluationTask(true, true);
	}
}

void SDTHAnimPreview::Advance(double Seconds)
{
	if (!bPlaying || Actors.IsEmpty()) return;
	const double Length = GetLength();
	double Next = Time + Seconds * Speed;
	if (Next > Length)
	{
		if (bLoop && Length > 0.0) Next = FMath::Fmod(Next, Length);
		else { Next = Length; bPlaying = false; }
	}
	Time = Next;
	Pose();
}

void SDTHAnimPreview::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Advance(InDeltaTime);
	if (FrameAfterTicks > 0 && --FrameAfterTicks == 0)
	{
		SetView(View);
	}
}

bool SDTHAnimPreview::SetView(const FString& InView)
{
	FEditorViewportClient* Client = Viewport.IsValid() ? Viewport->GetClient() : nullptr;
	if (Actors.IsEmpty() || !Client) return false;
	FBoxSphereBounds Bounds(ForceInit);
	bool bFirst = true;
	for (const FPlaying& A : Actors)
	{
		if (!A.Component) continue;
		A.Component->UpdateBounds();
		Bounds = bFirst ? A.Component->Bounds : Bounds + A.Component->Bounds;
		bFirst = false;
	}
	if (bFirst) return false;
	// DTH characters face +Y; +X is the character's left (DTH.Pose.P0.DazMapping).
	FVector Dir;
	if (InView.Equals(TEXT("front"), ESearchCase::IgnoreCase)) Dir = FVector(0.0, 1.0, 0.0);
	else if (InView.Equals(TEXT("side"), ESearchCase::IgnoreCase)) Dir = FVector(1.0, 0.0, 0.0);
	else if (InView.Equals(TEXT("threequarter"), ESearchCase::IgnoreCase)) Dir = FVector(1.0, 1.0, 0.0).GetSafeNormal();
	else return false;
	// The distance that fits the actors' bounding sphere in the narrower of the two fields of view, a little from above.
	const FIntPoint Size = Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint(1, 1);
	const double Aspect = Size.Y > 0 ? double(Size.X) / Size.Y : 1.0;
	const double HalfH = FMath::DegreesToRadians(Client->ViewFOV * 0.5);
	const double HalfV = FMath::Atan(FMath::Tan(HalfH) / FMath::Max(Aspect, 0.01));
	const double Distance = Bounds.SphereRadius * 0.8 / FMath::Sin(FMath::Min(HalfH, HalfV));
	const FVector Target = Bounds.Origin;
	const FVector Eye = Target + (Dir + FVector(0.0, 0.0, 0.15)).GetSafeNormal() * Distance;
	View = InView;
	Client->SetViewLocation(Eye);
	Client->SetViewRotation((Target - Eye).Rotation());
	Client->Invalidate();
	return true;
}

bool SDTHAnimPreview::Capture(const FString& File, FString& OutError)
{
	FEditorViewportClient* Client = Viewport.IsValid() ? Viewport->GetClient() : nullptr;
	FViewport* Target = Client ? Client->Viewport : nullptr;
	if (!Target || Target->GetSizeXY().X <= 0)
	{
		OutError = TEXT("The preview is not on screen.");
		return false;
	}
	// The pose at the time shown, evaluated now (on screen the viewport's world tick does this each frame).
	EvaluateNow(0.f);
	Target->Draw(false);
	FlushRenderingCommands();
	TArray<FColor> Pixels;
	if (!Target->ReadPixels(Pixels))
	{
		OutError = TEXT("Could not read the preview's pixels.");
		return false;
	}
	for (FColor& P : Pixels) P.A = 255;
	if (!FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Target->GetSizeXY().X, Target->GetSizeXY().Y)))
	{
		OutError = FString::Printf(TEXT("Could not write %s."), *File);
		return false;
	}
	return true;
}

FText SDTHAnimPreview::GetTimeText() const
{
	if (Actors.IsEmpty()) return FText::GetEmpty();
	return FText::FromString(FString::Printf(TEXT("frame %d / %d   %.2f / %.2f s"), GetFrame(), NumFrames() - 1, Time / Speed, GetLength() / Speed));
}

void SDTHAnimPreview::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FPlaying& A : Actors) Collector.AddReferencedObject(A.Component);
}

#undef LOCTEXT_NAMESPACE
