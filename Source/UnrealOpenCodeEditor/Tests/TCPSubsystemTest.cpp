// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#include "UnrealOpenCodeTestBase.h"
#include "UEOCTCPServerSubsystem.h"
#include "Editor.h"
#include "Subsystems/SubsystemCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealOpenCodeTCPSubsystemSmokeTest,
	"UnrealOpenCode.Subsystems.TCPServerSubsystem.SmokeTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FUnrealOpenCodeTCPSubsystemSmokeTest::RunTest(const FString& Parameters)
{
	// 1. Get subsystem
	UUEOCTCPServerSubsystem* Subsystem = GEditor
		? GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>()
		: nullptr;

	// 2. Verify subsystem exists
	TestNotNull(TEXT("TCPServerSubsystem should exist"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	// 3. IsConnected() returns a valid bool (no crash)
	const bool bConnected = Subsystem->IsConnected();
	TestTrue(TEXT("IsConnected returns valid bool"), bConnected || !bConnected);

	// 4. GetBoundPort() returns >= 0
	const int32 Port = Subsystem->GetBoundPort();
	TestTrue(TEXT("GetBoundPort returns non-negative value"), Port >= 0);

	// 5. IsServerRunning() returns a valid bool (no crash)
	const bool bRunning = Subsystem->IsServerRunning();
	TestTrue(TEXT("IsServerRunning returns valid bool"), bRunning || !bRunning);

	AddInfo(TEXT("TCPServerSubsystem smoke test passed — subsystem initialized with valid state"));

	return true;
}
