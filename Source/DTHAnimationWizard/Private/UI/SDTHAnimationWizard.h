#pragma once

#include "DTHAnimBatch.h"
#include "DTHAnimScene.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IDetailsView;
class SDTHAnimPreview;
class ITableRow;
class STableViewBase;
class USkeletalMesh;

/** The Animation Wizard's tab: Settings (and the Daz script), and Clips (pick clips and a character, write sequences). */
class SDTHAnimationWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHAnimationWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The open wizard, if its tab exists (for scripting). */
	static TSharedPtr<SDTHAnimationWizard> Get() { return Instance.Pin(); }
	/** Shows a page by name: Settings, Clips or Scenes. False for an unknown name. */
	bool ShowPage(const FString& Name);
	/** Reads the settings and the sampler's index again. */
	void Reload();
	/** How many clips the list shows (for scripting and tests). */
	int32 NumListed() const { return Rows.Num(); }
	/** How many multi-actor scenes the Scenes page lists. */
	int32 NumScenes() const { return SceneRows.Num(); }
	/** Selects a listed scene and casts Mesh in every role (for scripting, tests and screenshots). */
	void PreviewScene(int32 Row, USkeletalMesh* Mesh);
	/** Selects a clip by key and plays it on the Clips page's character. False if no listed clip has that key. */
	bool PreviewClip(const FString& Key);
	/** Sets the Clips page's character. */
	void SetCharacter(USkeletalMesh* Mesh);
	/** The previews on the Clips and Scenes pages. */
	TSharedPtr<SDTHAnimPreview> GetClipPreview() const { return ClipPreview; }
	TSharedPtr<SDTHAnimPreview> GetScenePreview() const { return ScenePreview; }

private:
	enum class EPage : int32 { Settings, Clips, Scenes, Count };

	struct FRow
	{
		const FDTHAnimIndexEntry* Clip = nullptr;
		bool bChecked = false;
	};

	TSharedRef<SWidget> MakeStepBar();
	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> MakeSettingsPage();
	TSharedRef<SWidget> MakeClipsPage();
	TSharedRef<SWidget> MakeScenesPage();
	TSharedRef<SWidget> MakeRolesPanel();
	TSharedRef<ITableRow> MakeSceneRow(TSharedPtr<int32> Scene, const TSharedRef<STableViewBase>& Owner);
	void RefreshScenes();
	FReply OnBuildScene();
	TSharedRef<SWidget> MakeActionBar();
	TSharedRef<ITableRow> MakeClipRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeMessageRow(TSharedPtr<FDTHAnimMessage> Message, const TSharedRef<STableViewBase>& Owner);

	void RefreshList();
	/** Plays the selected clip on the character, at its speed. */
	void RefreshClipPreview();
	/** Plays the selected scene with every role that has a character, at the scene's speed. */
	void RefreshScenePreview();
	/** The speed set for a clip or scene key (1 if none). */
	float SpeedOf(const FString& Key) const;
	void SetMessages(const TArray<FDTHAnimMessage>& Report);
	FReply OnWrite();
	FText GetIndexSummary() const;
	int32 NumChecked() const;

	EPage CurrentPage = EPage::Clips;
	TSharedPtr<IDetailsView> SettingsView;
	FDTHAnimIndex Index;
	FString IndexError;
	FString Search;
	FString Status;
	bool bReplace = true;
	TWeakObjectPtr<USkeletalMesh> Character;

	TArray<TSharedPtr<FRow>> Rows;
	/** Ticks by clip key (pack/label), kept across searches. */
	TSet<FString> Checked;
	TSharedPtr<SListView<TSharedPtr<FRow>>> ClipList;
	/** The clip the Clips page previews (its key), and the play speed set per clip and per scene key. */
	FString PreviewedClip;
	TMap<FString, float> Speeds;
	TSharedPtr<SDTHAnimPreview> ClipPreview;
	TSharedPtr<SDTHAnimPreview> ScenePreview;
	TArray<TSharedPtr<FDTHAnimMessage>> Messages;
	TSharedPtr<SListView<TSharedPtr<FDTHAnimMessage>>> MessageList;

	TArray<FDTHAnimScene> Scenes;
	TArray<TSharedPtr<int32>> SceneRows;
	TSharedPtr<SListView<TSharedPtr<int32>>> SceneList;
	FString SceneSearch;
	int32 SelectedScene = INDEX_NONE;
	/** The character for each role, by role label (kept across scenes, so "Female" stays cast). */
	TMap<FString, TWeakObjectPtr<USkeletalMesh>> RoleCharacters;
	TSharedPtr<class SVerticalBox> RolesBox;
	TSharedPtr<SListView<TSharedPtr<FDTHAnimMessage>>> SceneMessageList;

	static TWeakPtr<SDTHAnimationWizard> Instance;
};
