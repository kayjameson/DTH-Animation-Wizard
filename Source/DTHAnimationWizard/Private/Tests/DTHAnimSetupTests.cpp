#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTHAnimSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

// Test setup, run once: imports the DTH export named on the command line (-DTHAnimImportExport="<export folder>") with
// the DTH Import Wizard, into Test Folder/Characters/<name>, so the checks run on a character imported exactly as the
// wizard imports one, from the export the ground truth was sampled for. The Import Wizard is called through its
// scripting class (UDTHImportScripting::ImportCharacter, what its Import button runs) by reflection, so this plugin does
// not depend on it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHAnimSetupImport, "DTH.Anim.Setup.ImportTestCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTHAnimSetupImport::RunTest(const FString&)
{
	FString Export;
	if (!FParse::Value(FCommandLine::Get(), TEXT("DTHAnimImportExport="), Export))
	{
		AddInfo(TEXT("No -DTHAnimImportExport=<export folder> on the command line: nothing to import."));
		return true;
	}
	const FString TestFolder = GetDefault<UDTHAnimSettings>()->TestFolder.Path;
	if (!TestFolder.StartsWith(TEXT("/Game/")) || !TestFolder.Contains(TEXT("_Test")))
	{
		AddError(TEXT("Set Test Folder to a _Test content folder."));
		return false;
	}
	FString Name;
	if (!FParse::Value(FCommandLine::Get(), TEXT("DTHAnimImportName="), Name)) Name = FPaths::GetCleanFilename(Export);
	const FString Destination = TestFolder / TEXT("Characters") / Name;

	UClass* Scripting = FindObject<UClass>(nullptr, TEXT("/Script/DTHImportWizard.DTHImportScripting"));
	UFunction* Import = Scripting ? Scripting->FindFunctionByName(TEXT("ImportCharacter")) : nullptr;
	if (!Import)
	{
		AddError(TEXT("The DTH Import Wizard is not loaded (UDTHImportScripting::ImportCharacter not found)."));
		return false;
	}
	// The parameters by name, as the function declares them; the rest keep their defaults (false / empty).
	uint8* Params = static_cast<uint8*>(FMemory_Alloca(Import->ParmsSize));
	FMemory::Memzero(Params, Import->ParmsSize);
	for (TFieldIterator<FProperty> It(Import); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->InitializeValue_InContainer(Params);
	auto SetString = [&](const TCHAR* Parm, const FString& Value)
	{
		if (FStrProperty* P = CastField<FStrProperty>(Import->FindPropertyByName(Parm))) P->SetPropertyValue_InContainer(Params, Value);
	};
	SetString(TEXT("ExportFolder"), Export);
	SetString(TEXT("DestinationFolder"), Destination);
	Scripting->GetDefaultObject()->ProcessEvent(Import, Params);

	int32 Errors = 0;
	if (FArrayProperty* Ret = CastField<FArrayProperty>(Import->GetReturnProperty()))
	{
		FScriptArrayHelper Lines(Ret, Ret->ContainerPtrToValuePtr<void>(Params));
		for (int32 I = 0; I < Lines.Num(); ++I)
		{
			const FString& Line = *reinterpret_cast<const FString*>(Lines.GetRawPtr(I));
			if (Line.StartsWith(TEXT("ERROR:"))) { AddError(Line); ++Errors; }
			else if (Line.StartsWith(TEXT("WARNING:"))) AddWarning(Line);
			else AddInfo(Line);
		}
	}
	for (TFieldIterator<FProperty> It(Import); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->DestroyValue_InContainer(Params);
	AddInfo(FString::Printf(TEXT("Imported %s into %s."), *Export, *Destination));
	return Errors == 0;
}

#endif
