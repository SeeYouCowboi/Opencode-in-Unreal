# MCP API Reference

This document describes all 21 tools exposed by the UnrealOpenCode MCP server, plus the `ping` utility. Tools are grouped by category.

## Protocol

The MCP server communicates with oh-my-opencode over **stdio** using the [Model Context Protocol](https://modelcontextprotocol.io) (JSON-RPC 2.0).

Internally, the MCP server forwards tool calls to the UE editor over a **TCP socket** (default `localhost:3000`). The framing is a 4-byte big-endian length prefix followed by a UTF-8 JSON payload.

### Request format (MCP server -> UE editor)

```json
{
  "type": "tool_name",
  "data": { ...tool arguments... }
}
```

### Response format (UE editor -> MCP server)

```json
{
  "success": true,
  "data": { ...result... }
}
```

On error:

```json
{
  "success": false,
  "error": {
    "message": "Human-readable error description"
  }
}
```

### Filesystem tools

Three tools (`get_project_structure`, `get_module_dependencies`, `get_plugin_list`) read the filesystem directly in the MCP server process. They do not require a live editor connection and work even when the UE editor is closed.

---

## Tools

### Project Structure

---

#### `get_project_structure`

Scans the UE project root for `.uproject`, `Source/`, `Content/`, `Config/`, and `Plugins/` to build a structural overview.

**Parameters:** none

**Response:**

```json
{
  "projectName": "MyGame",
  "projectRoot": "C:/Projects/MyGame",
  "uprojectPath": "C:/Projects/MyGame/MyGame.uproject",
  "modules": [...],
  "plugins": [...],
  "sourceModules": [
    { "name": "MyGame", "buildCsPath": "Source/MyGame/MyGame.Build.cs" }
  ],
  "contentDirectories": ["Blueprints", "Characters", "Environment"],
  "configFiles": ["DefaultEngine.ini", "DefaultGame.ini"],
  "upluginFiles": ["Plugins/UnrealOpenCode/UnrealOpenCode.uplugin"]
}
```

**Example:**

```
Tool: get_project_structure
Args: {}
```

---

#### `get_module_dependencies`

Parses `Build.cs` files in `Source/` to extract `PublicDependencyModuleNames` and `PrivateDependencyModuleNames`.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `moduleName` | string | No | Filter to a single module by name. Omit to return all modules. |

**Response:**

```json
{
  "modules": [
    {
      "moduleName": "MyGame",
      "buildCsPath": "Source/MyGame/MyGame.Build.cs",
      "publicDeps": ["Core", "CoreUObject", "Engine", "InputCore"],
      "privateDeps": ["Slate", "SlateCore"]
    }
  ]
}
```

**Example:**

```
Tool: get_module_dependencies
Args: { "moduleName": "MyGame" }
```

---

#### `get_plugin_list`

Lists all plugins in the project's `Plugins/` directory by scanning for `.uplugin` files one level deep.

**Parameters:** none

**Response:**

```json
{
  "plugins": [
    {
      "name": "UnrealOpenCode",
      "version": 1,
      "versionName": "0.1.0",
      "category": "AI",
      "description": "AI-assisted game development via oh-my-opencode integration",
      "path": "Plugins/UnrealOpenCode/UnrealOpenCode.uplugin",
      "modules": [
        { "Name": "UnrealOpenCodeCore", "Type": "Runtime" },
        { "Name": "UnrealOpenCodeEditor", "Type": "Editor" }
      ]
    }
  ]
}
```

---

### C++ Hierarchy

These tools require a live editor connection. They query the UE reflection system.

---

#### `get_cpp_hierarchy`

Returns the UClass tree for project classes, with parent relationships, module info, and UCLASS specifiers.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `baseClass` | string | No | Filter by base class name, e.g. `"AActor"`. Omit to return all project classes. |
| `includeEngine` | boolean | No | Include engine classes (default: `false`). |
| `moduleFilter` | string | No | Filter results to a specific module name. |

**Response:**

```json
[
  {
    "className": "AMyCharacter",
    "parentClass": "ACharacter",
    "module": "MyGame",
    "specifiers": ["Blueprintable", "BlueprintType"],
    "children": []
  }
]
```

**Example:**

```
Tool: get_cpp_hierarchy
Args: { "baseClass": "ACharacter", "includeEngine": false }
```

---

#### `get_class_details`

Returns all `UPROPERTY` fields, `UFUNCTION` methods, and metadata for a specific class.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `className` | string | Yes | Class name with or without prefix, e.g. `"AMyCharacter"` or `"MyCharacter"`. |

**Response:**

```json
{
  "className": "AMyCharacter",
  "parentClass": "ACharacter",
  "module": "MyGame",
  "properties": [
    {
      "name": "Health",
      "type": "float",
      "specifiers": ["EditAnywhere", "BlueprintReadWrite"],
      "category": "Stats"
    }
  ],
  "functions": [
    {
      "name": "TakeDamage",
      "specifiers": ["BlueprintCallable"],
      "category": "Combat",
      "params": [
        { "name": "Amount", "type": "float" },
        { "name": "DamageCauser", "type": "AActor*" }
      ]
    }
  ]
}
```

**Example:**

```
Tool: get_class_details
Args: { "className": "AMyCharacter" }
```

---

#### `search_classes`

Searches for UE C++ classes by partial name match. Returns up to 20 results.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `query` | string | Yes | Partial class name to search for. |
| `includeEngine` | boolean | No | Include engine classes (default: `false`). |

**Response:**

```json
[
  {
    "className": "AMyCharacter",
    "parentClass": "ACharacter",
    "module": "MyGame"
  }
]
```

**Example:**

```
Tool: search_classes
Args: { "query": "Character" }
```

---

### Blueprint Assets

These tools require a live editor connection. They query the UE Asset Registry.

---

#### `get_blueprint_list`

Lists all Blueprint assets in the project, optionally filtered by content path prefix.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | No | Content path prefix filter, e.g. `"/Game/Blueprints"`. |

**Response:**

```json
{
  "blueprints": [
    {
      "name": "BP_Player",
      "path": "/Game/Blueprints/BP_Player",
      "parentClass": "AMyCharacter"
    }
  ]
}
```

**Example:**

```
Tool: get_blueprint_list
Args: { "path": "/Game/Characters" }
```

---

#### `get_blueprint_details`

Returns detailed Blueprint information: variables, functions, parent class, implemented interfaces, and a graph summary.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `blueprintPath` | string | Yes | Blueprint asset path, e.g. `"/Game/Blueprints/BP_Player"`. |

**Response:**

```json
{
  "name": "BP_Player",
  "path": "/Game/Blueprints/BP_Player",
  "parentClass": "AMyCharacter",
  "interfaces": ["IInteractable"],
  "variables": [
    { "name": "bIsRunning", "type": "bool", "category": "Movement" }
  ],
  "functions": [
    { "name": "OnJump", "type": "event" }
  ],
  "graphs": ["EventGraph", "OnJump"]
}
```

**Example:**

```
Tool: get_blueprint_details
Args: { "blueprintPath": "/Game/Blueprints/BP_Player" }
```

---

#### `search_blueprints`

Searches Blueprint assets by partial name or class, with optional parent class filtering. Returns up to 20 matches.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `query` | string | Yes | Partial Blueprint name or class query. |
| `parentClass` | string | No | Filter by parent class name. |

**Response:**

```json
{
  "blueprints": [
    {
      "name": "BP_Player",
      "path": "/Game/Blueprints/BP_Player",
      "parentClass": "AMyCharacter"
    }
  ]
}
```

**Example:**

```
Tool: search_blueprints
Args: { "query": "Player", "parentClass": "ACharacter" }
```

---

### Scene Hierarchy

These tools require a live editor connection. They query the current persistent level.

---

#### `get_scene_hierarchy`

Returns all actors in the current persistent level as a hierarchy tree.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `includeComponents` | boolean | No | Include component details for each actor (default: `true`). |

**Response:**

```json
{
  "actors": [
    {
      "name": "BP_Player_0",
      "class": "BP_Player",
      "location": { "x": 0.0, "y": 0.0, "z": 100.0 },
      "components": [
        { "name": "CapsuleComponent", "class": "UCapsuleComponent" }
      ],
      "children": []
    }
  ]
}
```

**Example:**

```
Tool: get_scene_hierarchy
Args: { "includeComponents": false }
```

---

#### `get_actor_details`

Returns detailed information for a specific actor: components, properties, tags, and selection state.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `actorName` | string | Yes | Name of the actor as shown in the World Outliner. |

**Response:**

```json
{
  "name": "BP_Player_0",
  "class": "BP_Player",
  "isSelected": true,
  "location": { "x": 0.0, "y": 0.0, "z": 100.0 },
  "rotation": { "pitch": 0.0, "yaw": 0.0, "roll": 0.0 },
  "scale": { "x": 1.0, "y": 1.0, "z": 1.0 },
  "tags": ["Player"],
  "components": [
    {
      "name": "CapsuleComponent",
      "class": "UCapsuleComponent",
      "properties": {}
    }
  ]
}
```

**Example:**

```
Tool: get_actor_details
Args: { "actorName": "BP_Player_0" }
```

---

#### `get_selected_actors`

Returns the actors currently selected in the editor viewport.

**Parameters:** none

**Response:**

```json
{
  "selectedActors": [
    {
      "name": "BP_Player_0",
      "class": "BP_Player",
      "location": { "x": 0.0, "y": 0.0, "z": 100.0 }
    }
  ]
}
```

---

### Asset Management

These tools require a live editor connection. They query the UE Asset Registry.

---

#### `search_assets`

Searches project assets by name, type, or content path. Supports pagination.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `query` | string | No | Partial asset name to match. |
| `assetType` | string | No | Asset type filter, e.g. `"StaticMesh"`, `"Material"`, `"Texture2D"`, `"SoundWave"`. |
| `pathPrefix` | string | No | Content path prefix filter, e.g. `"/Game/Environment"`. |
| `limit` | number | No | Maximum results to return (default: `50`). |
| `offset` | number | No | Results to skip for pagination (default: `0`). |

**Response:**

```json
{
  "assets": [
    {
      "name": "SM_Chair",
      "path": "/Game/Meshes/SM_Chair",
      "type": "StaticMesh",
      "sizeBytes": 204800,
      "modifiedAt": "2025-11-15T10:30:00Z"
    }
  ],
  "total": 142,
  "offset": 0,
  "limit": 50
}
```

**Example:**

```
Tool: search_assets
Args: { "assetType": "StaticMesh", "pathPrefix": "/Game/Environment", "limit": 20 }
```

---

#### `get_asset_details`

Returns detailed, type-specific metadata for a single asset.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `assetPath` | string | Yes | Full asset path, e.g. `"/Game/Meshes/SM_Chair"`. |

**Response** (varies by asset type):

```json
{
  "name": "SM_Chair",
  "path": "/Game/Meshes/SM_Chair",
  "type": "StaticMesh",
  "triangleCount": 1248,
  "lodCount": 3,
  "materials": ["/Game/Materials/M_Wood"],
  "sizeBytes": 204800
}
```

For a `Texture2D`:

```json
{
  "name": "T_Wood_D",
  "path": "/Game/Textures/T_Wood_D",
  "type": "Texture2D",
  "width": 2048,
  "height": 2048,
  "format": "BC1",
  "sizeBytes": 1398101
}
```

**Example:**

```
Tool: get_asset_details
Args: { "assetPath": "/Game/Meshes/SM_Chair" }
```

---

#### `get_asset_references`

Returns the bidirectional reference graph for an asset.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `assetPath` | string | Yes | Full asset path, e.g. `"/Game/Materials/M_Wood"`. |
| `direction` | string | No | `"referencers"`, `"dependencies"`, or `"both"` (default: `"both"`). |

**Response:**

```json
{
  "assetPath": "/Game/Materials/M_Wood",
  "referencers": [
    "/Game/Meshes/SM_Chair",
    "/Game/Meshes/SM_Table"
  ],
  "dependencies": [
    "/Game/Textures/T_Wood_D",
    "/Game/Textures/T_Wood_N"
  ]
}
```

**Example:**

```
Tool: get_asset_references
Args: { "assetPath": "/Game/Materials/M_Wood", "direction": "dependencies" }
```

---

### Build Logs

These tools require a live editor connection.

---

#### `get_build_logs`

Returns structured compilation errors and warnings from the last UE build.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `severity` | string | No | Filter by severity: `"error"`, `"warning"`, `"note"`, or `"all"` (default: `"all"`). |
| `limit` | number | No | Maximum log entries to return (default: `100`). |

**Response:**

```json
{
  "entries": [
    {
      "severity": "error",
      "file": "Source/MyGame/MyCharacter.cpp",
      "line": 42,
      "column": 5,
      "message": "use of undeclared identifier 'bIsRunning'"
    }
  ],
  "total": 3
}
```

**Example:**

```
Tool: get_build_logs
Args: { "severity": "error" }
```

---

#### `get_output_log`

Returns recent UE Output Log entries, optionally filtered by category.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `category` | string | No | Log category filter, e.g. `"LogTemp"`, `"LogBlueprintUserMessages"`. |
| `limit` | number | No | Maximum entries to return (default: `100`). |
| `since` | string | No | Return only entries after this ISO 8601 timestamp. |

**Response:**

```json
{
  "entries": [
    {
      "timestamp": "2025-11-15T10:30:00.123Z",
      "category": "LogTemp",
      "verbosity": "Warning",
      "message": "Health component not found on actor"
    }
  ]
}
```

**Example:**

```
Tool: get_output_log
Args: { "category": "LogTemp", "limit": 50 }
```

---

#### `get_compilation_status`

Returns the current build/compilation status including error and warning counts.

**Parameters:** none

**Response:**

```json
{
  "status": "succeeded",
  "errorCount": 0,
  "warningCount": 2,
  "lastBuildTime": "2025-11-15T10:28:45Z"
}
```

Possible `status` values: `"succeeded"`, `"failed"`, `"in_progress"`, `"unknown"`.

---

### Code Generation

---

#### `generate_code`

Sends a code generation request to the UE editor. The editor shows a confirmation dialog with the proposed file content before writing anything to disk.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `filePath` | string | Yes | Project-relative file path to generate, e.g. `"Source/MyGame/Public/MyActor.h"`. |
| `content` | string | Yes | Full file content to write. |
| `description` | string | Yes | Human-readable reason shown in the UE confirmation dialog. |

**Response:**

```json
{
  "status": "accepted",
  "filePath": "Source/MyGame/Public/MyActor.h"
}
```

If the user rejects the confirmation:

```json
{
  "status": "rejected",
  "filePath": "Source/MyGame/Public/MyActor.h"
}
```

**Example:**

```
Tool: generate_code
Args: {
  "filePath": "Source/MyGame/Public/MyPickup.h",
  "content": "#pragma once\n\n#include \"CoreMinimal.h\"\n...",
  "description": "Create AMyPickup actor class for collectible items"
}
```

---

#### `get_code_template`

Returns a UE C++ header template for a common class type. Useful as a starting point before calling `generate_code`.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `classType` | string | Yes | One of: `"Actor"`, `"Component"`, `"GameMode"`, `"Widget"`, `"AnimInstance"`, `"Interface"`, `"Struct"`, `"Enum"`. |

**Response:**

The response is the raw template string (a complete `.h` file). Example for `"Actor"`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyActor.generated.h"

UCLASS()
class MYPROJECT_API AMyActor : public AActor
{
    GENERATED_BODY()

public:
    AMyActor();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
};
```

**Example:**

```
Tool: get_code_template
Args: { "classType": "Component" }
```

---

### Utility

---

#### `ping`

Checks connectivity to the UE editor plugin. Safe to call at any time.

**Parameters:** none

**Response (editor connected):**

```
pong: { "status": "ok" }
```

**Response (editor not connected):**

```
pong (UE not connected)
```

---

## Error Codes

All tool errors are returned as plain text in the MCP response content field. Common scenarios:

| Scenario | Message pattern |
|---|---|
| Editor not running | `UE not connected or request failed: ...` |
| Plugin not enabled | `UE not connected: ...` |
| Invalid parameter | `Error: <parameter> is required` |
| UE-side failure | `Error: <message from UE>` |
| Request timeout | `UE not connected or request failed: timeout` |

The `ping` tool never throws. Use it to distinguish "editor not running" from "plugin not enabled".
