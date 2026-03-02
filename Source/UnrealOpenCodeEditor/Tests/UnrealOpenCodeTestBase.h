// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

/**
 * Base class for UnrealOpenCode automation tests.
 * Provides common setup and utilities for plugin tests.
 */
class FUnrealOpenCodeTestBase : public FAutomationTestBase
{
public:
    FUnrealOpenCodeTestBase(const FString& InName, const bool bInComplexTask)
        : FAutomationTestBase(InName, bInComplexTask)
    {}

protected:
    /** Waits up to TimeoutSeconds for Condition to return true. Returns false on timeout. */
    static bool WaitForCondition(TFunction<bool()> Condition, float TimeoutSeconds = 5.0f)
    {
        const float StartTime = FPlatformTime::Seconds();
        while (!Condition())
        {
            FPlatformProcess::Sleep(0.05f);
            if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
            {
                return false;
            }
        }
        return true;
    }
};
