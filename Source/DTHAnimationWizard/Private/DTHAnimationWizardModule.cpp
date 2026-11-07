#include "DTHAnimationWizardModule.h"

#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SDTHAnimationWizard.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "DTHAnimationWizard"

const FName FDTHAnimationWizardModule::WizardTabName(TEXT("DTHAnimationWizard"));

void FDTHAnimationWizardModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WizardTabName,
		FOnSpawnTab::CreateRaw(this, &FDTHAnimationWizardModule::SpawnWizardTab))
		.SetDisplayName(LOCTEXT("TabTitle", "DTH Animation Wizard"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Daz Studio animations on DazToHue Genesis 9 characters, written as animation sequences."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDTHAnimationWizardModule::RegisterMenus));
}

void FDTHAnimationWizardModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WizardTabName);
	}
}

FString FDTHAnimationWizardModule::ResourcesDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTHAnimationWizard"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources")) : FString();
}

void FDTHAnimationWizardModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	// The same section as the other DTH wizards, so they share one DazToHue group.
	FToolMenuSection& Section = Menu->FindOrAddSection("DTH", LOCTEXT("MenuSection", "DazToHue"));
	Section.AddMenuEntry(
		"OpenDTHAnimationWizard",
		LOCTEXT("MenuEntry", "DTH Animation Wizard"),
		LOCTEXT("MenuEntryTooltip", "Daz Studio animations on DazToHue Genesis 9 characters, written as animation sequences."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FDTHAnimationWizardModule::WizardTabName);
		})));
}

TSharedRef<SDockTab> FDTHAnimationWizardModule::SpawnWizardTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDTHAnimationWizard)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDTHAnimationWizardModule, DTHAnimationWizard)
