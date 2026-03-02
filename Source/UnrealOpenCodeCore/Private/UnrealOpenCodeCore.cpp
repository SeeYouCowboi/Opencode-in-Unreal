// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealOpenCodeCore.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "FUnrealOpenCodeCoreModule"

void FUnrealOpenCodeCoreModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("UnrealOpenCode Core module loaded"));
}

void FUnrealOpenCodeCoreModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("UnrealOpenCode Core module unloaded"));
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealOpenCodeCoreModule, UnrealOpenCodeCore)
