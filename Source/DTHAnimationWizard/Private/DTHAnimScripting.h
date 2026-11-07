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
	/** Opens the wizard tab on a page: Settings or Clips. */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static FString OpenWizard(const FString& Page);

	/** The sampled clips whose label or pack contains Filter (all for ""): "label | pack | frames | fps | keyed morphs". */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> ListClips(const FString& Filter);

	/**
	 * Writes the clips (by label) as sequences for the character (a DTH Genesis 9 skeletal mesh path) under the
	 * settings' Sequence Folder, or Folder if given. bReplace replaces sequences already there. The run's report.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Animation Wizard")
	static TArray<FString> WriteClips(const FString& MeshPath, const TArray<FString>& Labels, bool bReplace, const FString& Folder = TEXT(""));
};
