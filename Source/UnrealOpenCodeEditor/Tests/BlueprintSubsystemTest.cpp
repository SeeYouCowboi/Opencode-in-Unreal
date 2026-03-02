// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCBlueprintSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealOpenCodeBlueprintSmokeTest,
	"UnrealOpenCode.Subsystems.BlueprintSubsystem.SmokeTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FUnrealOpenCodeBlueprintSmokeTest::RunTest(const FString& Parameters)
{
	// 1. Get subsystem
	UUEOCBlueprintSubsystem* Subsystem = GEditor
		? GEditor->GetEditorSubsystem<UUEOCBlueprintSubsystem>()
		: nullptr;

	// 2. Verify subsystem exists (not null)
	TestNotNull(TEXT("BlueprintSubsystem should exist"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	// 3. Smoke test: subsystem is initialized without crash
	// Blueprint listing requires Asset Registry and loaded project content
	AddInfo(TEXT("BlueprintSubsystem smoke test passed — subsystem initialized successfully"));

	return true;
}
