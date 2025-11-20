# Blueprint MCP Server Plugin

Editor-only Unreal Engine 5.7+ plugin that runs a local MCP-compatible HTTP endpoint to expose Blueprint structure, references, and basic introspection over HTTP.

## Contents
- `BlueprintMCPServer/` – the plugin folder (drop into `YourProject/Plugins/`).
- `.gitignore` – ignore build/IDE artifacts for this plugin repo.

## Installation
1. Copy `BlueprintMCPServer` into `YourProject/Plugins/BlueprintMCPServer/`.
2. Regenerate project files if needed.
3. Enable the plugin in **Edit → Plugins** (Editor section).

## Usage
- Open **Tools → Blueprint MCP Server** to launch the control panel.
- Set a port (default `9000`), click **Start Server**.
- The log panel shows server lifecycle and incoming MCP actions.
- Optional: provide a Blueprint asset path (e.g., `/Game/Blueprints/BP_MyAsset.BP_MyAsset`) and click **Export JSON** to preview inspector output.

## MCP HTTP API (local-only)
POST `http://127.0.0.1:PORT/mcp` with JSON body `{ "action": "...", "params": { ... } }`.

Actions:
- `list_blueprints` – params: optional `paths: ["/Game", "/Game/Blueprints"]`
- `get_blueprint_structure` – params: `asset_path: "/Game/Blueprints/BP_X.BP_X"`
- `get_references` – params: `asset_path: "/Game/Blueprints/BP_X.BP_X"`

Errors return HTTP 400 with `{ "error": "reason" }`.

## Build (example, Windows, UE 5.7)
```powershell
$env:UE5_ROOT="C:/Program Files/Epic Games/UE_5.7"
"$env:UE5_ROOT/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" `
  BlueprintLLMEditor Win64 Development `
  -project="D:/Path/To/YourProject.uproject" `
  -TargetType=Editor
```

## Notes
- Uses the built-in `HttpServer` module; no third-party dependencies.
- Inspector covers variables, graphs (uber, functions, delegates), nodes, pins, and incoming/outgoing references via the Asset Registry.
- UI log panel now streams MCP server activity (start/stop, requests, successes/failures).
