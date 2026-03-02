// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCSceneAssetSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealOpenCodeSceneAssetSmokeTest,
	"UnrealOpenCode.Subsystems.SceneAssetSubsystem.SmokeTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FUnrealOpenCodeSceneAssetSmokeTest::RunTest(const FString& Parameters)
{
	// 1. Get subsystem
	UUEOCSceneAssetSubsystem* Subsystem = GEditor
		? GEditor->GetEditorSubsystem<UUEOCSceneAssetSubsystem>()
		: nullptr;

	// 2. Verify subsystem exists (not null)
	TestNotNull(TEXT("SceneAssetSubsystem should exist"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	// 3. Smoke test: subsystem is initialized without crash
	// Scene hierarchy queries require a loaded world/level — graceful empty return expected
	AddInfo(TEXT("SceneAssetSubsystem smoke test passed — subsystem initialized successfully"));

	return true;
}
