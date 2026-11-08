#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class FAdvancedPreviewScene;
class FEditorViewportClient;
class SDTHAnimViewport;
class UDebugSkelMeshComponent;
class USkeletalMesh;

/**
 * The wizard's preview: clips playing on their characters in the wizard's own preview scene (never the level), each
 * frame recreated by the same solve the written sequence carries, through the character's own post-process blueprint.
 * Nothing is written. One actor per role, all at the origin, as the level sequence places them.
 */
class SDTHAnimPreview : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDTHAnimPreview) {}
	SLATE_END_ARGS()

	/** One actor: a character and the clip samples file it plays (role 0 of that file). */
	struct FActor
	{
		USkeletalMesh* Mesh = nullptr;
		FString Samples;
		FString Label;
	};

	void Construct(const FArguments& InArgs);
	virtual ~SDTHAnimPreview() override;

	/** Shows these actors from frame 0 (none clears the preview). Actors whose samples cannot be read are left out and reported. */
	void SetActors(const TArray<FActor>& Actors);
	/** Why the preview shows nothing, or the last load's problems; empty when all is well. */
	const FString& GetProblem() const { return Problem; }

	void SetPlaying(bool bPlay) { bPlaying = bPlay; }
	bool IsPlaying() const { return bPlaying; }
	/** Seconds from the clip's start; the actors are posed at once. */
	void SetTime(double Seconds);
	double GetTime() const { return Time; }
	/** The longest actor's play length, in seconds. */
	double GetLength() const;
	/** The frame shown (at the first actor's rate). */
	int32 GetFrame() const;
	int32 NumFrames() const;
	int32 NumActors() const { return Actors.Num(); }
	/** Play speed (0.1 to 4). bNotify: tell OnSpeedChanged, as when the user changes it. */
	void SetSpeed(float InSpeed, bool bNotify = false);
	float GetSpeed() const { return Speed; }
	/** Called when the user changes the speed. */
	TFunction<void(float)> OnSpeedChanged;
	/** Evaluates every actor at once (the viewport otherwise does so as it draws), for scripting and tests. */
	void EvaluateNow(float DeltaSeconds = 1.0f / 30.0f);
	/** Moves the clip on by this much real time, at the speed set, if playing (what each tick does). */
	void Advance(double Seconds);
	UDebugSkelMeshComponent* GetComponent(int32 Actor) const;

	/** Frames a view of every actor: front, side or threequarter. */
	bool SetView(const FString& View);
	/** Renders the preview as it now stands into a PNG. */
	bool Capture(const FString& File, FString& OutError);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDTHAnimPreview"); }

private:
	struct FPlaying
	{
		TObjectPtr<UDebugSkelMeshComponent> Component = nullptr;
		FString Label;
		double Fps = 30.0;
		/** The mesh-space local pose of each frame, as the written sequence plays back (DTHPoseCharacter::LocalPose). */
		TArray<TMap<FName, FTransform>> Frames;
	};

	void Clear();
	/** Poses every actor at Time (between two frames: their blend, as a sequence player does) and evaluates at once. */
	void Pose();
	TSharedRef<SWidget> MakeTransport();
	FText GetTimeText() const;

	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<SDTHAnimViewport> Viewport;
	TArray<FPlaying> Actors;
	FString Problem;
	FString View = TEXT("threequarter");
	double Time = 0.0;
	bool bPlaying = true;
	float Speed = 1.f;
	bool bLoop = true;
	/** Ticks to wait before framing (the bones are posed after the first ticks). */
	int32 FrameAfterTicks = 0;
};
