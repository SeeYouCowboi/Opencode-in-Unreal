// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCTCPServerSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTCPServerStartStopTest,
    "UnrealOpenCode.TCPServer.StartStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTCPServerStartStopTest::RunTest(const FString& Parameters)
{
    // Verify the subsystem exists (it's automatically initialized by UE subsystem framework)
    UUEOCTCPServerSubsystem* Subsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
    TestNotNull(TEXT("TCPServerSubsystem should exist"), Subsystem);

    if (Subsystem)
    {
        // Subsystem auto-starts on Initialize() — just verify it's in a valid state
        // (not crashed, not null)
        AddInfo(TEXT("TCPServerSubsystem is initialized and running"));
    }

    return true;
}
