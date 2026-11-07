#include "UI/SDTHAnimationWizard.h"

#include "AssetRegistry/AssetData.h"
#include "DTHAnimSettings.h"
#include "DTHAnimationWizardModule.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "LevelSequence.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SDTHAnimationWizard"

TWeakPtr<SDTHAnimationWizard> SDTHAnimationWizard::Instance;

namespace
{
	FString SamplerScript()
	{
		return FDTHAnimationWizardModule::ResourcesDir() / TEXT("Daz") / TEXT("DTH_SampleAnimations.dsa");
	}
}

bool SDTHAnimationWizard::ShowPage(const FString& Name)
{
	if (Name.Equals(TEXT("Settings"), ESearchCase::IgnoreCase)) { CurrentPage = EPage::Settings; return true; }
	if (Name.Equals(TEXT("Clips"), ESearchCase::IgnoreCase)) { CurrentPage = EPage::Clips; return true; }
	if (Name.Equals(TEXT("Scenes"), ESearchCase::IgnoreCase)) { CurrentPage = EPage::Scenes; return true; }
	return false;
}

void SDTHAnimationWizard::Construct(const FArguments& InArgs)
{
	Instance = SharedThis(this);
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				MakeStepBar()
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(CurrentPage); })
				+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
				+ SWidgetSwitcher::Slot()[MakeClipsPage()]
				+ SWidgetSwitcher::Slot()[MakeScenesPage()]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Thickness(1.f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeActionBar()
			]
		]
	];
	Reload();
	if (!IndexError.IsEmpty()) CurrentPage = EPage::Settings;
}

void SDTHAnimationWizard::Reload()
{
	const UDTHAnimSettings* Settings = GetDefault<UDTHAnimSettings>();
	IndexError.Reset();
	if (!Index.Load(Settings->SamplesIndex.FilePath, IndexError)) Index = FDTHAnimIndex();
	if (!Character.IsValid()) Character = Cast<USkeletalMesh>(Settings->Character.TryLoad());
	RefreshList();
	RefreshScenes();
}

TSharedRef<SWidget> SDTHAnimationWizard::MakeStepBar()
{
	TSharedRef<SSegmentedControl<int32>> Bar = SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]() { return static_cast<int32>(CurrentPage); })
		.OnValueChanged_Lambda([this](int32 NewValue) { CurrentPage = static_cast<EPage>(NewValue); });
	Bar->AddSlot(static_cast<int32>(EPage::Settings)).Text(LOCTEXT("StepSettings", "Settings"));
	Bar->AddSlot(static_cast<int32>(EPage::Clips)).Text(LOCTEXT("StepClips", "Clips"));
	Bar->AddSlot(static_cast<int32>(EPage::Scenes)).Text(LOCTEXT("StepScenes", "Scenes"));
	return Bar;
}

TSharedRef<SWidget> SDTHAnimationWizard::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold")).Text(Title)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(8.f, 6.f))
			[
				Content
			]
		];
}

// --- Settings ---------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHAnimationWizard::MakeSettingsPage()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = false;
	SettingsView = PropertyEditor.CreateDetailView(Args);
	SettingsView->SetObject(GetMutableDefault<UDTHAnimSettings>());
	SettingsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UDTHAnimSettings>()->SaveConfig();
		Character.Reset();
		Reload();
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MakeSection(LOCTEXT("DazSection", "Daz Studio Sampler"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("DazHelp",
						"The clips are measured in Daz Studio. In Daz Studio, open this script (File > Open, or double-click it). It loads "
						"a fresh Genesis 9, records DTH's rest pose, applies every clip in an Animations folder under People/Genesis 9*, "
						"records every frame, and writes DTH_AnimationSamples.json to Documents/DAZ 3D/Studio4/DTH Animation Wizard. "
						"Nothing is saved in Daz. A run that stops carries on where it left off. One set of samples serves every DTH "
						"Genesis 9 character. Then check Daz Samples Index below."))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(SamplerScript()))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CopyScript", "Copy Path"))
						.OnClicked_Lambda([]() { FPlatformApplicationMisc::ClipboardCopy(*SamplerScript()); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ShowScript", "Show in Explorer"))
						.OnClicked_Lambda([]() { FPlatformProcess::ExploreFolder(*SamplerScript()); return FReply::Handled(); })
					]
				])
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(4.f)
			[
				SettingsView.ToSharedRef()
			]
		];
}

// --- Clips --------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHAnimationWizard::MakeClipsPage()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.55f)
		[
			MakeSection(LOCTEXT("ClipsSection", "Sampled Clips"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).AutoWrapText(true).Text(this, &SDTHAnimationWizard::GetIndexSummary)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SSearchBox)
						.OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); RefreshList(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("TickAll", "Tick Listed"))
						.OnClicked_Lambda([this]()
						{
							for (const TSharedPtr<FRow>& R : Rows) { R->bChecked = true; Checked.Add(R->Clip->Label); }
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearTicks", "Clear"))
						.OnClicked_Lambda([this]()
						{
							Checked.Reset();
							for (const TSharedPtr<FRow>& R : Rows) R->bChecked = false;
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(ClipList, SListView<TSharedPtr<FRow>>)
					.ListItemsSource(&Rows)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SDTHAnimationWizard::MakeClipRow)
				])
		]
		+ SSplitter::Slot().Value(0.45f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("WriteSection", "Write"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("CharacterLabel", "Character"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(USkeletalMesh::StaticClass())
							.ObjectPath_Lambda([this]() { return Character.IsValid() ? Character->GetPathName() : FString(); })
							.OnObjectChanged_Lambda([this](const FAssetData& Asset) { Character = Cast<USkeletalMesh>(Asset.GetAsset()); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bReplace ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bReplace = S == ECheckBoxState::Checked; })
						[
							SNew(STextBlock).Text(LOCTEXT("Replace", "Replace sequences already written"))
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Writes to %s/<character>/<pack>/A_<clip>."),
								*GetDefault<UDTHAnimSettings>()->SequenceFolder.Path));
						})
					])
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				MakeSection(LOCTEXT("ReportSection", "Report"),
					SAssignNew(MessageList, SListView<TSharedPtr<FDTHAnimMessage>>)
					.ListItemsSource(&Messages)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow(this, &SDTHAnimationWizard::MakeMessageRow))
			]
		];
}

TSharedRef<ITableRow> SDTHAnimationWizard::MakeClipRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	const FDTHAnimIndexEntry& C = *Row->Clip;
	const FString Details = C.Samples.IsEmpty() ? FString::Printf(TEXT("not sampled: %s"), *C.Skipped)
		: FString::Printf(TEXT("%s  |  %d frames at %g fps%s"), *C.Pack(), C.Frames, C.Fps,
			C.KeyedMorphs > 0 ? *FString::Printf(TEXT("  |  %d keyed morphs (not written)"), C.KeyedMorphs) : TEXT(""));
	return SNew(STableRow<TSharedPtr<FRow>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SCheckBox)
				.IsEnabled(!C.Samples.IsEmpty())
				.IsChecked_Lambda([Row]() { return Row->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Row](ECheckBoxState S)
				{
					Row->bChecked = S == ECheckBoxState::Checked;
					if (Row->bChecked) Checked.Add(Row->Clip->Label); else Checked.Remove(Row->Clip->Label);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(C.Label))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text(FText::FromString(Details))
				]
			]
		];
}

TSharedRef<ITableRow> SDTHAnimationWizard::MakeMessageRow(TSharedPtr<FDTHAnimMessage> Message, const TSharedRef<STableViewBase>& Owner)
{
	const TCHAR* IconName = TEXT("Icons.Info");
	if (Message->Severity == FDTHAnimMessage::ESeverity::Warning) IconName = TEXT("Icons.WarningWithColor");
	else if (Message->Severity == FDTHAnimMessage::ESeverity::Error) IconName = TEXT("Icons.ErrorWithColor");
	return SNew(STableRow<TSharedPtr<FDTHAnimMessage>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 0.f)
			[
				SNew(SImage).Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Message->Text))
			]
		];
}

TSharedRef<SWidget> SDTHAnimationWizard::MakeActionBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 6.f)
		[
			SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 6.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("Reload", "Reload Samples"))
			.OnClicked_Lambda([this]() { Reload(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 6.f, 8.f, 6.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.IsEnabled_Lambda([this]() { return Character.IsValid() && NumChecked() > 0; })
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("WriteN", "Write {0} Sequences"), NumChecked()); })
			.OnClicked(this, &SDTHAnimationWizard::OnWrite)
		];
}

void SDTHAnimationWizard::RefreshList()
{
	Rows.Reset();
	for (const FDTHAnimIndexEntry* C : Index.Find(Search))
	{
		TSharedPtr<FRow> R = MakeShared<FRow>();
		R->Clip = C;
		R->bChecked = Checked.Contains(C->Label);
		Rows.Add(R);
	}
	if (ClipList.IsValid()) ClipList->RequestListRefresh();
}

void SDTHAnimationWizard::SetMessages(const TArray<FDTHAnimMessage>& Report)
{
	Messages.Reset();
	for (const FDTHAnimMessage& M : Report) Messages.Add(MakeShared<FDTHAnimMessage>(M));
	if (MessageList.IsValid()) MessageList->RequestListRefresh();
}

int32 SDTHAnimationWizard::NumChecked() const
{
	int32 N = 0;
	for (const FDTHAnimIndexEntry& C : Index.Clips) if (Checked.Contains(C.Label) && !C.Samples.IsEmpty()) ++N;
	return N;
}

FReply SDTHAnimationWizard::OnWrite()
{
	if (!Character.IsValid()) return FReply::Handled();
	TArray<const FDTHAnimIndexEntry*> Clips;
	for (const FDTHAnimIndexEntry& C : Index.Clips) if (Checked.Contains(C.Label) && !C.Samples.IsEmpty()) Clips.Add(&C);
	TArray<FDTHAnimMessage> Report;
	const bool bOk = DTHAnimBatch::WriteClips(*Character, Clips, GetDefault<UDTHAnimSettings>()->SequenceFolder.Path, bReplace, Report);
	SetMessages(Report);
	Status = bOk ? FString::Printf(TEXT("Wrote %d clips for %s."), Clips.Num(), *Character->GetName()) : TEXT("Some clips failed: see the report.");
	return FReply::Handled();
}

FText SDTHAnimationWizard::GetIndexSummary() const
{
	if (!IndexError.IsEmpty()) return FText::FromString(IndexError);
	int32 WithMorphs = 0;
	for (const FDTHAnimIndexEntry& C : Index.Clips) if (C.KeyedMorphs > 0) ++WithMorphs;
	return FText::FromString(FString::Printf(TEXT("%d clips sampled %s%s. %d key morphs, which sequences do not carry. %d ticked."),
		Index.Clips.Num(), *Index.SampledAt, Index.Errors.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %d could not be sampled"), Index.Errors.Num()),
		WithMorphs, NumChecked()));
}

// --- Scenes -------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHAnimationWizard::MakeScenesPage()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.5f)
		[
			MakeSection(LOCTEXT("ScenesSection", "Multi-Actor Scenes"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("ScenesHelp",
						"Clips made to play together: those in one pack whose names differ only in the last word (the role, "
						"e.g. Female and Male). Pick a scene, give each role a character, and build: each role's sequence is "
						"written for its character, with a level sequence that plays them together, in sync and in place."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SSearchBox)
					.OnTextChanged_Lambda([this](const FText& Text) { SceneSearch = Text.ToString(); RefreshScenes(); })
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(SceneList, SListView<TSharedPtr<int32>>)
					.ListItemsSource(&SceneRows)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SDTHAnimationWizard::MakeSceneRow)
					.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Row, ESelectInfo::Type)
					{
						SelectedScene = Row.IsValid() ? *Row : INDEX_NONE;
						MakeRolesPanel();
					})
				])
		]
		+ SSplitter::Slot().Value(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("RolesSection", "Roles"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(RolesBox, SVerticalBox)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([this]() { return bReplace ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bReplace = S == ECheckBoxState::Checked; })
							[
								SNew(STextBlock).Text(LOCTEXT("ReplaceScene", "Replace what is already written"))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
							.Text(LOCTEXT("BuildScene", "Build Scene"))
							.IsEnabled_Lambda([this]()
							{
								if (!Scenes.IsValidIndex(SelectedScene)) return false;
								for (const FDTHAnimScene::FRole& R : Scenes[SelectedScene].Roles)
								{
									const TWeakObjectPtr<USkeletalMesh>* M = RoleCharacters.Find(R.Label);
									if (!M || !M->IsValid()) return false;
								}
								return true;
							})
							.OnClicked(this, &SDTHAnimationWizard::OnBuildScene)
						]
					])
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				MakeSection(LOCTEXT("SceneReportSection", "Report"),
					SAssignNew(SceneMessageList, SListView<TSharedPtr<FDTHAnimMessage>>)
					.ListItemsSource(&Messages)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow(this, &SDTHAnimationWizard::MakeMessageRow))
			]
		];
}

TSharedRef<SWidget> SDTHAnimationWizard::MakeRolesPanel()
{
	if (!RolesBox.IsValid()) return SNullWidget::NullWidget;
	RolesBox->ClearChildren();
	if (!Scenes.IsValidIndex(SelectedScene))
	{
		RolesBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("PickScene", "Pick a scene on the left."))];
		return RolesBox.ToSharedRef();
	}
	const FDTHAnimScene& Scene = Scenes[SelectedScene];
	RolesBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold")).Text(FText::FromString(Scene.Name))];
	for (const FDTHAnimScene::FRole& Role : Scene.Roles)
	{
		const FString Label = Role.Label;
		RolesBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(140.f)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s (%d frames)"), *Label, Role.Clip->Frames)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USkeletalMesh::StaticClass())
				.ObjectPath_Lambda([this, Label]()
				{
					const TWeakObjectPtr<USkeletalMesh>* M = RoleCharacters.Find(Label);
					return M && M->IsValid() ? (*M)->GetPathName() : FString();
				})
				.OnObjectChanged_Lambda([this, Label](const FAssetData& Asset) { RoleCharacters.Add(Label, Cast<USkeletalMesh>(Asset.GetAsset())); })
			]
		];
	}
	return RolesBox.ToSharedRef();
}

TSharedRef<ITableRow> SDTHAnimationWizard::MakeSceneRow(TSharedPtr<int32> Scene, const TSharedRef<STableViewBase>& Owner)
{
	const FDTHAnimScene& S = Scenes[*Scene];
	FString Roles;
	for (const FDTHAnimScene::FRole& R : S.Roles) Roles += (Roles.IsEmpty() ? TEXT("") : TEXT(", ")) + R.Label;
	return SNew(STableRow<TSharedPtr<int32>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(S.Name))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FString::Printf(TEXT("%s  |  roles: %s"), *S.Roles[0].Clip->Pack(), *Roles)))
			]
		];
}

void SDTHAnimationWizard::RefreshScenes()
{
	Scenes = DTHAnimScenes::FromIndex(Index);
	SceneRows.Reset();
	for (int32 I = 0; I < Scenes.Num(); ++I)
	{
		if (SceneSearch.IsEmpty() || Scenes[I].Name.Contains(SceneSearch)) SceneRows.Add(MakeShared<int32>(I));
	}
	SelectedScene = INDEX_NONE;
	if (SceneList.IsValid()) SceneList->RequestListRefresh();
	MakeRolesPanel();
}

FReply SDTHAnimationWizard::OnBuildScene()
{
	if (!Scenes.IsValidIndex(SelectedScene)) return FReply::Handled();
	TMap<FString, USkeletalMesh*> Assigned;
	for (const TPair<FString, TWeakObjectPtr<USkeletalMesh>>& P : RoleCharacters) if (P.Value.IsValid()) Assigned.Add(P.Key, P.Value.Get());
	TArray<FDTHAnimMessage> Report;
	ULevelSequence* Level = DTHAnimScenes::Build(Scenes[SelectedScene], Assigned, GetDefault<UDTHAnimSettings>()->SequenceFolder.Path, bReplace, Report);
	SetMessages(Report);
	if (SceneMessageList.IsValid()) SceneMessageList->RequestListRefresh();
	Status = Level ? FString::Printf(TEXT("Built %s."), *Level->GetPathName()) : TEXT("The scene was not built: see the report.");
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
