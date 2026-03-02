# UnrealOpenCode

> AI-assisted Unreal Engine 5 development via oh-my-opencode

UnrealOpenCode is a UE5 editor plugin that connects your project to [oh-my-opencode](https://opencode.ai) through a local MCP server. Ask questions about your C++ hierarchy, inspect Blueprint assets, generate code, and read build logs — all from the chat panel without leaving the editor.

## Architecture

```
oh-my-opencode (CLI)
      |
    stdio
      |
MCP Server (Node.js / TypeScript)   <-- runs as a local MCP process
      |
    TCP :3000
      |
UE5 Editor Plugin (C++)             <-- UnrealOpenCodeCore + UnrealOpenCodeEditor
      |
  UE Reflection / Asset Registry / Output Log
```

The MCP server speaks the [Model Context Protocol](https://modelcontextprotocol.io) over stdio to oh-my-opencode, and forwards tool calls to the UE editor over a local TCP socket (default port 3000). The editor plugin handles each request and returns structured JSON.

## Features

- **Slate chat panel** built into the UE editor — no browser tab required
- **22 MCP tools** covering project structure, C++ classes, Blueprints, scene actors, assets, build logs, and code generation
- **Code generation with confirmation** — the editor shows a diff before writing any file
- **Session history** persisted across editor restarts
- **Auto-reconnect** — the MCP server reconnects to the editor if the TCP connection drops
- **Filesystem tools** that work without a live editor connection (project structure, module dependencies, plugin list)

## Requirements

| Dependency | Minimum version |
|---|---|
| Unreal Engine | 5.7 |
| oh-my-opencode | latest |
| Node.js | 20 LTS |
| npm | 10+ |

Windows and macOS are supported. Linux is untested.

## Quick Start

### 1. Copy the plugin

```
YourProject/
  Plugins/
    UnrealOpenCode/   <-- copy here
```

### 2. Add the MCP config

Add the following to your `opencode.json` (in the project root, next to the `.uproject` file):

```json
{
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

### 3. Launch UE, enable the plugin, open the chat panel

1. Open your project in the UE editor.
2. Go to **Edit > Plugins**, search for `UnrealOpenCode`, and enable it. Restart when prompted.
3. Open the chat panel from **Window > UnrealOpenCode** (or the toolbar button).
4. Run `opencode` in your terminal from the project root. The MCP server starts automatically.

That's it. Type a question in the chat panel or the opencode CLI and the AI can now inspect your project.

## Modules

The plugin ships two C++ modules:

| Module | Type | Purpose |
|---|---|---|
| `UnrealOpenCodeCore` | Runtime | TCP server, request dispatch, tool handlers |
| `UnrealOpenCodeEditor` | Editor | Slate chat panel, toolbar integration, session history |

## Contributing

See [SETUP.md](SETUP.md) for the full development setup, including how to build the MCP server TypeScript source and run the plugin from source.

## License

MIT
