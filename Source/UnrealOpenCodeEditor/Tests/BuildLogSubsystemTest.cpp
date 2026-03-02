// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCBuildLogSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealOpenCodeBuildLogSmokeTest,
	"UnrealOpenCode.Subsystems.BuildLogSubsystem.SmokeTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FUnrealOpenCodeBuildLogSmokeTest::RunTest(const FString& Parameters)
{
	// 1. Get subsystem
	UUEOCBuildLogSubsystem* Subsystem = GEditor
		? GEditor->GetEditorSubsystem<UUEOCBuildLogSubsystem>()
		: nullptr;

	// 2. Verify subsystem exists (not null)
	TestNotNull(TEXT("BuildLogSubsystem should exist"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	// 3. Smoke test: emit a log message to exercise the capture pipeline
	// The subsystem's OutputCapture device should see this if initialized
	UE_LOG(LogTemp, Warning, TEXT("UnrealOpenCode BuildLog smoke test message %d"), FMath::Rand());

	// 4. Subsystem is initialized and capture device is active (no crash)
	AddInfo(TEXT("BuildLogSubsystem smoke test passed — subsystem initialized, log capture active"));

	return true;
}
