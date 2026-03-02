// Copyright 2024 UnrealOpenCode. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UnrealOpenCodeProtocol.generated.h"

/**
 * Request sent from MCP Server → UE Plugin via TCP.
 * Matches TypeScript interface UERequest in protocol.ts.
 * Message framing: 4-byte big-endian uint32 length prefix + UTF-8 JSON body.
 */
USTRUCT()
struct UNREALOPENCODESCORE_API FUEOCRequest
{
    GENERATED_BODY()

    /** UUID v4 string identifying this request */
    UPROPERTY()
    FString Id;

    /** Tool type string, e.g., "get_project_structure" */
    UPROPERTY()
    FString Type;

    /** JSON-encoded params object */
    UPROPERTY()
    FString ParamsJson;

    /** Unix timestamp in milliseconds */
    UPROPERTY()
    int64 Timestamp = 0;
};

/**
 * Response sent from UE Plugin → MCP Server via TCP.
 * Matches TypeScript interface UEResponse in protocol.ts.
 */
USTRUCT()
struct UNREALOPENCODESCORE_API FUEOCResponse
{
    GENERATED_BODY()

    /** UUID matching the request Id */
    UPROPERTY()
    FString Id;

    /** Tool type string echoed from request */
    UPROPERTY()
    FString Type;

    /** True if the operation succeeded */
    UPROPERTY()
    bool bSuccess = false;

    /** JSON-encoded data payload (when success = true) */
    UPROPERTY()
    FString DataJson;

    /** Error code (when success = false) */
    UPROPERTY()
    int32 ErrorCode = 0;

    /** Error message (when success = false) */
    UPROPERTY()
    FString ErrorMessage;

    /** Unix timestamp in milliseconds */
    UPROPERTY()
    int64 Timestamp = 0;
};

/**
 * Tool type string constants — must exactly match constants.ts UE_TOOL_TYPES values.
 */
namespace UEOCToolTypes
{
    // Project structure tools
    static const FString GetProjectStructure     = TEXT("get_project_structure");
    static const FString GetModuleDependencies   = TEXT("get_module_dependencies");
    static const FString GetPluginList           = TEXT("get_plugin_list");

    // C++ reflection tools
    static const FString GetCppHierarchy         = TEXT("get_cpp_hierarchy");
    static const FString GetClassDetails         = TEXT("get_class_details");
    static const FString SearchClasses           = TEXT("search_classes");

    // Blueprint tools
    static const FString GetBlueprintList        = TEXT("get_blueprint_list");
    static const FString GetBlueprintDetails     = TEXT("get_blueprint_details");
    static const FString SearchBlueprints        = TEXT("search_blueprints");

    // Scene tools
    static const FString GetSceneHierarchy       = TEXT("get_scene_hierarchy");
    static const FString GetActorDetails         = TEXT("get_actor_details");
    static const FString GetSelectedActors       = TEXT("get_selected_actors");

    // Asset tools
    static const FString SearchAssets            = TEXT("search_assets");
    static const FString GetAssetDetails         = TEXT("get_asset_details");
    static const FString GetAssetReferences      = TEXT("get_asset_references");

    // Build/log tools
    static const FString GetBuildLogs            = TEXT("get_build_logs");
    static const FString GetOutputLog            = TEXT("get_output_log");
    static const FString GetCompilationStatus    = TEXT("get_compilation_status");

    // Code generation
    static const FString GenerateCode            = TEXT("generate_code");

    // Utility
    static const FString Ping                    = TEXT("ping");
}
