// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEOCBuildLogSubsystem.h"
#include "UEOCTCPServerSubsystem.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "Misc/DateTime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogUEOCBuildLog);

// ---------------------------------------------------------------------------
// FUEOCOutputDeviceCapture — ring-buffer log capture (thread-safe)
// ---------------------------------------------------------------------------

class FUEOCOutputDeviceCapture : public FOutputDevice
{
public:
	static constexpr int32 MaxEntries = 1000;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		FScopeLock ScopeLock(&Lock);

		FUEOCLogEntry Entry;
		Entry.Timestamp = FDateTime::UtcNow().ToIso8601();
		Entry.Category = Category.ToString();
		Entry.Verbosity = Verbosity;
		Entry.Message = FString(V);

		if (Entries.Num() >= MaxEntries)
		{
			Entries.RemoveAt(0);
		}
		Entries.Add(MoveTemp(Entry));
	}

	virtual bool CanBeUsedOnAnyThread() const override { return true; }

	/**
	 * Returns a copy of all captured entries (thread-safe).
	 */
	TArray<FUEOCLogEntry> GetEntriesCopy() const
	{
		FScopeLock ScopeLock(&Lock);
		return Entries;
	}

	/**
	 * Returns a copy of entries matching the given category filter.
	 * If CategoryFilter is empty, returns all entries.
	 * Respects the limit parameter (most recent N entries).
	 */
	TArray<FUEOCLogEntry> GetFilteredEntries(const FString& CategoryFilter, int32 Limit) const
	{
		FScopeLock ScopeLock(&Lock);

		TArray<FUEOCLogEntry> Result;
		// Iterate backwards to get most recent first, then reverse
		for (int32 i = Entries.Num() - 1; i >= 0 && Result.Num() < Limit; --i)
		{
			if (CategoryFilter.IsEmpty() || Entries[i].Category.Contains(CategoryFilter))
			{
				Result.Add(Entries[i]);
			}
		}
		// Reverse so oldest is first (chronological order)
		Algo::Reverse(Result);
		return Result;
	}

private:
	TArray<FUEOCLogEntry> Entries;
	mutable FCriticalSection Lock;
};

// ---------------------------------------------------------------------------
// Helper: verbosity enum to string
// ---------------------------------------------------------------------------

static FString VerbosityToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal:			return TEXT("Fatal");
	case ELogVerbosity::Error:			return TEXT("Error");
	case ELogVerbosity::Warning:		return TEXT("Warning");
	case ELogVerbosity::Display:		return TEXT("Display");
	case ELogVerbosity::Log:			return TEXT("Log");
	case ELogVerbosity::Verbose:		return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose:	return TEXT("VeryVerbose");
	default:							return TEXT("Unknown");
	}
}

// ---------------------------------------------------------------------------
// UUEOCBuildLogSubsystem
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Create and register the output device capture
	OutputCapture = MakeUnique<FUEOCOutputDeviceCapture>();
	if (GLog)
	{
		GLog->AddOutputDevice(OutputCapture.Get());
	}

	// Subscribe to compilation/module change events
	ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddUObject(
		this, &UUEOCBuildLogSubsystem::OnModulesChanged);

	// Bind to the TCP server's request delegate
	Collection.InitializeDependency<UUEOCTCPServerSubsystem>();
	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		RequestDelegateHandle = TCPSubsystem->OnJsonRequestReceived.AddUObject(
			this, &UUEOCBuildLogSubsystem::HandleRequest);
	}

	UE_LOG(LogUEOCBuildLog, Log, TEXT("Build log subsystem initialized (ring buffer: %d entries)"),
		FUEOCOutputDeviceCapture::MaxEntries);
}

void UUEOCBuildLogSubsystem::Deinitialize()
{
	// Unbind from TCP server
	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
	}

	// Unsubscribe from module change events
	FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);

	// Remove output device from GLog before destroying it
	if (GLog && OutputCapture.IsValid())
	{
		GLog->RemoveOutputDevice(OutputCapture.Get());
	}
	OutputCapture.Reset();

	UE_LOG(LogUEOCBuildLog, Log, TEXT("Build log subsystem deinitialized"));

	Super::Deinitialize();
}

void UUEOCBuildLogSubsystem::OnModulesChanged(FName ModuleName, EModuleChangeReason Reason)
{
	switch (Reason)
	{
	case EModuleChangeReason::ModuleLoaded:
		// A module was hot-reloaded successfully
		BuildStatus = EBuildStatus::Succeeded;
		LastBuildTime = FDateTime::UtcNow().ToIso8601();
		UE_LOG(LogUEOCBuildLog, Log, TEXT("Module loaded (hot-reload succeeded): %s"), *ModuleName.ToString());
		break;

	case EModuleChangeReason::ModuleUnloaded:
		// Module unloading during hot-reload — compilation in progress
		if (BuildStatus != EBuildStatus::Compiling)
		{
			BuildStatus = EBuildStatus::Compiling;
			UE_LOG(LogUEOCBuildLog, Verbose, TEXT("Module unloaded (compilation likely in progress): %s"), *ModuleName.ToString());
		}
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Request Dispatch
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> RequestObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);

	if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
	{
		return;
	}

	const FString Type = RequestObject->GetStringField(TEXT("type"));
	const FString RequestId = RequestObject->GetStringField(TEXT("id"));
	TSharedPtr<FJsonObject> Params = RequestObject->GetObjectField(TEXT("params"));

	if (Type == TEXT("get_build_logs"))
	{
		HandleGetBuildLogs(RequestId, Params);
	}
	else if (Type == TEXT("get_output_log"))
	{
		HandleGetOutputLog(RequestId, Params);
	}
	else if (Type == TEXT("get_compilation_status"))
	{
		HandleGetCompilationStatus(RequestId, Params);
	}
}

// ---------------------------------------------------------------------------
// get_output_log
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::HandleGetOutputLog(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	int32 Limit = 100;
	FString CategoryFilter;

	if (Params.IsValid())
	{
		if (Params->HasField(TEXT("limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("limit"))), 1, FUEOCOutputDeviceCapture::MaxEntries);
		}
		if (Params->HasField(TEXT("category")))
		{
			CategoryFilter = Params->GetStringField(TEXT("category"));
		}
	}

	const TArray<FUEOCLogEntry> FilteredEntries = OutputCapture->GetFilteredEntries(CategoryFilter, Limit);

	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	EntriesArray.Reserve(FilteredEntries.Num());

	for (const FUEOCLogEntry& Entry : FilteredEntries)
	{
		TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetStringField(TEXT("timestamp"), Entry.Timestamp);
		EntryObj->SetStringField(TEXT("category"), Entry.Category);
		EntryObj->SetStringField(TEXT("verbosity"), VerbosityToString(Entry.Verbosity));
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		EntriesArray.Add(MakeShared<FJsonValueObject>(EntryObj));
	}

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetArrayField(TEXT("entries"), EntriesArray);
	DataJson->SetNumberField(TEXT("total"), FilteredEntries.Num());

	SendResponse(RequestId, TEXT("get_output_log"), DataJson);
}

// ---------------------------------------------------------------------------
// get_build_logs
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::HandleGetBuildLogs(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	// Compiler-related log categories
	static const TArray<FString> CompilerCategories = {
		TEXT("LogCompile"),
		TEXT("LogHotReload"),
		TEXT("LogLinker"),
		TEXT("LogModuleManager"),
		TEXT("LogPackageName"),
	};

	int32 Limit = 200;
	if (Params.IsValid() && Params->HasField(TEXT("limit")))
	{
		Limit = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("limit"))), 1, FUEOCOutputDeviceCapture::MaxEntries);
	}

	const TArray<FUEOCLogEntry> AllEntries = OutputCapture->GetEntriesCopy();

	// Regex to parse compiler output: file(line): error/warning: message
	const FRegexPattern CompilerPattern(TEXT("(.+?)\\((\\d+)\\):\\s*(error|warning|note)\\s*(?:C\\d+)?:\\s*(.*)"));

	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	for (const FUEOCLogEntry& Entry : AllEntries)
	{
		// Check if this entry belongs to a compiler category
		bool bIsCompilerLog = false;
		for (const FString& Cat : CompilerCategories)
		{
			if (Entry.Category.Contains(Cat))
			{
				bIsCompilerLog = true;
				break;
			}
		}

		if (!bIsCompilerLog && Entry.Verbosity != ELogVerbosity::Error && Entry.Verbosity != ELogVerbosity::Warning)
		{
			continue;
		}

		TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();

		// Try to parse structured compiler output
		FRegexMatcher Matcher(CompilerPattern, Entry.Message);
		if (Matcher.FindNext())
		{
			EntryObj->SetStringField(TEXT("file"), Matcher.GetCaptureGroup(1));
			EntryObj->SetNumberField(TEXT("line"), FCString::Atoi(*Matcher.GetCaptureGroup(2)));
			const FString Severity = Matcher.GetCaptureGroup(3);
			EntryObj->SetStringField(TEXT("severity"), Severity);
			EntryObj->SetStringField(TEXT("message"), Matcher.GetCaptureGroup(4));

			if (Severity == TEXT("error"))
			{
				ErrorCount++;
			}
			else if (Severity == TEXT("warning"))
			{
				WarningCount++;
			}
		}
		else
		{
			// Unstructured compiler log entry
			EntryObj->SetStringField(TEXT("file"), TEXT(""));
			EntryObj->SetNumberField(TEXT("line"), 0);
			EntryObj->SetStringField(TEXT("severity"), Entry.Verbosity == ELogVerbosity::Error ? TEXT("error") : TEXT("info"));
			EntryObj->SetStringField(TEXT("message"), Entry.Message);

			if (Entry.Verbosity == ELogVerbosity::Error)
			{
				ErrorCount++;
			}
			else if (Entry.Verbosity == ELogVerbosity::Warning)
			{
				WarningCount++;
			}
		}

		EntryObj->SetStringField(TEXT("timestamp"), Entry.Timestamp);
		EntryObj->SetStringField(TEXT("category"), Entry.Category);
		EntriesArray.Add(MakeShared<FJsonValueObject>(EntryObj));

		if (EntriesArray.Num() >= Limit)
		{
			break;
		}
	}

	// Update tracked error/warning counts
	LastErrorCount = ErrorCount;
	LastWarningCount = WarningCount;

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetArrayField(TEXT("entries"), EntriesArray);
	DataJson->SetNumberField(TEXT("errorCount"), ErrorCount);
	DataJson->SetNumberField(TEXT("warningCount"), WarningCount);
	DataJson->SetNumberField(TEXT("total"), EntriesArray.Num());

	SendResponse(RequestId, TEXT("get_build_logs"), DataJson);
}

// ---------------------------------------------------------------------------
// get_compilation_status
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::HandleGetCompilationStatus(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString StatusStr;
	switch (BuildStatus)
	{
	case EBuildStatus::Idle:		StatusStr = TEXT("idle"); break;
	case EBuildStatus::Compiling:	StatusStr = TEXT("compiling"); break;
	case EBuildStatus::Succeeded:	StatusStr = TEXT("succeeded"); break;
	case EBuildStatus::Failed:		StatusStr = TEXT("failed"); break;
	}

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetStringField(TEXT("status"), StatusStr);
	DataJson->SetStringField(TEXT("lastBuildTime"), LastBuildTime);
	DataJson->SetNumberField(TEXT("errorCount"), LastErrorCount);
	DataJson->SetNumberField(TEXT("warningCount"), LastWarningCount);

	SendResponse(RequestId, TEXT("get_compilation_status"), DataJson);
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void UUEOCBuildLogSubsystem::SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson)
{
	TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
	ResponseObject->SetStringField(TEXT("id"), RequestId);
	ResponseObject->SetStringField(TEXT("type"), Type);
	ResponseObject->SetBoolField(TEXT("success"), true);
	ResponseObject->SetObjectField(TEXT("data"), DataJson);

	FString ResponseString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem->SendJsonResponse(ResponseString);
	}
}

void UUEOCBuildLogSubsystem::SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetNumberField(TEXT("code"), Code);
	ErrorObj->SetStringField(TEXT("message"), Message);

	TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
	ResponseObject->SetStringField(TEXT("id"), RequestId);
	ResponseObject->SetStringField(TEXT("type"), Type);
	ResponseObject->SetBoolField(TEXT("success"), false);
	ResponseObject->SetObjectField(TEXT("error"), ErrorObj);

	FString ResponseString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem->SendJsonResponse(ResponseString);
	}
}
