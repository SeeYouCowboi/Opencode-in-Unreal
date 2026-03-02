# Setup Guide

This guide covers everything from first install to a working development environment.

## Requirements

| Dependency | Minimum | Notes |
|---|---|---|
| Unreal Engine | 5.7 | Earlier versions untested |
| oh-my-opencode | latest | `npm i -g opencode` |
| Node.js | 20 LTS | Required to run the MCP server |
| npm | 10+ | Bundled with Node 20 |

## Installation

### 1. Copy the plugin

Copy the `UnrealOpenCode` folder into your project's `Plugins/` directory:

```
YourProject/
  Plugins/
    UnrealOpenCode/
      Resources/
        MCPServer/
      Source/
      UnrealOpenCode.uplugin
```

If `Plugins/` doesn't exist yet, create it at the project root (same level as the `.uproject` file).

### 2. Build the MCP server

The TypeScript source must be compiled before first use. From the project root:

```bash
cd Plugins/UnrealOpenCode/Resources/MCPServer
npm install
npm run build
```

This produces `dist/index.js`, which is what oh-my-opencode launches.

### 3. Configure oh-my-opencode

Create or edit `opencode.json` in your project root:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "unrealopencode": {
      "type": "local",
      "command": "node",
      "args": ["Plugins/UnrealOpenCode/Resources/MCPServer/dist/index.js"],
      "enabled": true,
      "env": {
        "UEOC_HOST": "localhost",
        "UEOC_PORT": "3000"
      }
    }
  }
}
```

The `args` path is relative to the directory where you run `opencode` (your project root).

### 4. Build the plugin (optional — for source builds)

If you're building from source rather than using a prebuilt binary, use Unreal Build Tool:

```bash
# Windows (from Engine/Build/BatchFiles)
RunUAT.bat BuildPlugin ^
  -Plugin="YourProject/Plugins/UnrealOpenCode/UnrealOpenCode.uplugin" ^
  -Package="YourProject/Plugins/UnrealOpenCode/Binaries" ^
  -Rocket
```

> **Important:** Before running `BuildPlugin`, delete `Plugins/UnrealOpenCode/Resources/MCPServer/nul` if it exists. This file is a Windows reserved-name artifact that causes UAT to fail. See [Troubleshooting](#nul-file-artifact-windows) below.

### 5. Enable the plugin in the editor

1. Open your project in the UE editor.
2. Go to **Edit > Plugins**.
3. Search for `UnrealOpenCode`.
4. Check the **Enabled** box and restart the editor when prompted.

### 6. Open the chat panel

After the editor restarts, open the chat panel from **Window > UnrealOpenCode** or the toolbar button (if visible). The panel connects to oh-my-opencode via the MCP server.

Run `opencode` in a terminal from your project root to start the CLI. The MCP server process starts automatically as a child process.

## Configuration

### Environment variables

These go in the `env` block of your `opencode.json` MCP entry, or in your shell environment.

| Variable | Default | Description |
|---|---|---|
| `UEOC_HOST` | `localhost` | Hostname of the UE editor TCP server |
| `UEOC_PORT` | `3000` | Primary TCP port. Falls back to 3001-3010 if in use |
| `UEOC_RECONNECT_ATTEMPTS` | `3` | How many times to retry a dropped TCP connection |
| `UEOC_RECONNECT_INTERVAL` | `2000` | Milliseconds between reconnect attempts |
| `UEOC_REQUEST_TIMEOUT` | `10000` | Milliseconds before a tool call times out |
| `UEOC_PROJECT_ROOT` | *(auto-detected)* | Absolute path to your UE project root. Set this if auto-detection fails |

### UE editor config

The TCP port the editor listens on is set in `Config/DefaultEngine.ini`:

```ini
[UnrealOpenCode]
TcpPort=3000
```

If you change this, update `UEOC_PORT` in `opencode.json` to match.

## Troubleshooting

### Plugin doesn't load

**Symptom:** The plugin doesn't appear in the Plugins list, or the editor shows a load error.

- Confirm the folder structure is correct: `Plugins/UnrealOpenCode/UnrealOpenCode.uplugin` must exist.
- Check that the plugin binaries are present (`Binaries/` folder). If missing, rebuild with UBT (step 4 above).
- Look at the editor's Output Log (`Window > Output Log`) for lines starting with `LogPluginManager`.

### MCP server not connecting

**Symptom:** oh-my-opencode reports the `unrealopencode` MCP server failed to start, or tools return "UE not connected".

- Confirm `dist/index.js` exists. If not, run `npm run build` in `Resources/MCPServer/`.
- Confirm Node.js 20+ is on your PATH: `node --version`.
- Confirm the `args` path in `opencode.json` is relative to where you run `opencode`.

### TCP connection refused

**Symptom:** Tools return `UE not connected` even though the editor is open.

- Confirm the plugin is enabled and the editor has restarted since enabling it.
- Check `Config/DefaultEngine.ini` for the `TcpPort` value and make sure `UEOC_PORT` matches.
- Check that no firewall rule is blocking `localhost:3000`.
- Try a different port: set `TcpPort=3001` in `DefaultEngine.ini` and `UEOC_PORT=3001` in `opencode.json`.

### Port in use

**Symptom:** The editor logs `Failed to bind TCP port 3000`.

The plugin automatically tries ports 3001 through 3010 as fallbacks. If all are in use, set a free port explicitly in `DefaultEngine.ini` and `opencode.json`.

To find what's using port 3000 on Windows:

```cmd
netstat -ano | findstr :3000
```

### `nul` file artifact (Windows)

**Symptom:** `RunUAT BuildPlugin` fails with an error about an invalid filename or path.

**Cause:** The file `Resources/MCPServer/nul` exists in the repository. On Windows, `nul` is a reserved device name. Some tools create this file accidentally (e.g. redirecting output to `nul` on Unix, which is a valid filename there).

**Fix:** Delete the file before building:

```cmd
del "Plugins\UnrealOpenCode\Resources\MCPServer\nul"
```

Or from PowerShell:

```powershell
Remove-Item "Plugins\UnrealOpenCode\Resources\MCPServer\nul"
```

After deleting, `BuildPlugin` should complete normally.

### Auto-detection of project root fails

**Symptom:** `get_project_structure` returns an empty result or wrong paths.

The MCP server walks up five directory levels from its own location to find the project root. If your directory layout differs, set `UEOC_PROJECT_ROOT` explicitly:

```json
"env": {
  "UEOC_HOST": "localhost",
  "UEOC_PORT": "3000",
  "UEOC_PROJECT_ROOT": "C:/Projects/MyGame"
}
```

Use forward slashes or escaped backslashes on Windows.
