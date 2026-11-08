#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DTHAnimScripting.generated.h"

/**
 * The Animation Wizard's buttons for Python and Blueprints (unreal.DTHAnimScripting). Each returns text lines; failures
 * start with "ERROR:", cautions with "WARNING:".
 */
UCLASS()
class UDTHAnimScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Opens the wizard tab on a page: Settings, Clips or Scenes. */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static FString OpenWizard(const FString& Page);

	/** The sampled clips whose label or pack contains Filter (all for ""): "label | pack | frames | fps | keyed morphs | key | source" (source: "Genesis 9", or "from Genesis 8 Female" for a G8 clip carried onto G9). */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> ListClips(const FString& Filter);

	/**
	 * Writes the clips (by key "<pack>/<label>", or by label where it is unique) as sequences for the character (a DTH Genesis 9 skeletal mesh path) under the
	 * settings' Sequence Folder, or Folder if given. bReplace replaces sequences already there. The run's report.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> WriteClips(const FString& MeshPath, const TArray<FString>& Labels, bool bReplace, const FString& Folder = TEXT(""));

	/** The multi-actor scenes whose name contains Filter (all for ""): "scene | pack | role (frames), role (frames) | key". */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> ListScenes(const FString& Filter);

	/**
	 * Builds a scene (by key "<pack>/<name>", or by name where it is unique): each role's sequence for its character (Roles[i] played by Meshes[i], skeletal mesh paths) and a level
	 * sequence playing them together, under the settings' Sequence Folder, or Folder if given. The run's report.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> BuildScene(const FString& Scene, const TArray<FString>& Roles, const TArray<FString>& Meshes, bool bReplace,
		const FString& Folder = TEXT(""));
};
