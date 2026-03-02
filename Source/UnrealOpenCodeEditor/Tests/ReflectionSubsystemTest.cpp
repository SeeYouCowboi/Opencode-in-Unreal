// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCReflectionSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealOpenCodeReflectionSmokeTest,
	"UnrealOpenCode.Subsystems.ReflectionSubsystem.SmokeTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FUnrealOpenCodeReflectionSmokeTest::RunTest(const FString& Parameters)
{
	// 1. Get subsystem
	UUEOCReflectionSubsystem* Subsystem = GEditor
		? GEditor->GetEditorSubsystem<UUEOCReflectionSubsystem>()
		: nullptr;

	// 2. Verify subsystem exists (not null)
	TestNotNull(TEXT("ReflectionSubsystem should exist"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	// 3. Smoke test: subsystem is initialized without crash
	// Real data verification requires UE Editor running with a loaded project
	AddInfo(TEXT("ReflectionSubsystem smoke test passed — subsystem initialized successfully"));

	return true;
}
