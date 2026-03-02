// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCBuildLogSubsystem.generated.h"

class FUEOCOutputDeviceCapture;

/**
 * Represents a single captured log entry from the UE output log.
 */
USTRUCT()
struct FUEOCLogEntry
{
	GENERATED_BODY()

	FString Timestamp;
	FString Category;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FString Message;
};

DECLARE_LOG_CATEGORY_EXTERN(LogUEOCBuildLog, Log, All);

/**
 * Subsystem that captures UE output log entries via a custom FOutputDevice,
 * maintains a ring buffer, and handles get_build_logs, get_output_log,
 * and get_compilation_status requests.
 */
UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCBuildLogSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** Main request dispatcher bound to the TCP server's OnJsonRequestReceived delegate. */
	void HandleRequest(const FString& JsonRequest);

	/** Handles get_build_logs: returns compiler-related log entries parsed into structured format. */
	void HandleGetBuildLogs(const FString& RequestId, TSharedPtr<FJsonObject> Params);

	/** Handles get_output_log: returns raw output log entries with optional filtering. */
	void HandleGetOutputLog(const FString& RequestId, TSharedPtr<FJsonObject> Params);

	/** Handles get_compilation_status: returns current build status, timing, and error/warning counts. */
	void HandleGetCompilationStatus(const FString& RequestId, TSharedPtr<FJsonObject> Params);

	/** Sends a successful JSON-RPC style response via the TCP server subsystem. */
	void SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson);

	/** Sends an error JSON-RPC style response via the TCP server subsystem. */
	void SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message);

	/** Callback when module compilation/hot-reload completes. */
	void OnModulesChanged(FName ModuleName, EModuleChangeReason Reason);

	/** Delegate handle for TCP server request subscription. */
	FDelegateHandle RequestDelegateHandle;

	/** Custom output device that captures log entries into a ring buffer. */
	TUniquePtr<FUEOCOutputDeviceCapture> OutputCapture;

	/** Delegate handle for module change notifications (compilation tracking). */
	FDelegateHandle ModulesChangedHandle;

	/** Build status tracking. */
	enum class EBuildStatus : uint8 { Idle, Compiling, Succeeded, Failed };
	EBuildStatus BuildStatus = EBuildStatus::Idle;
	FString LastBuildTime;
	int32 LastErrorCount = 0;
	int32 LastWarningCount = 0;
};
