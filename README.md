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
- Toggle **Enable write operations (unsafe)** to allow MCP to create/modify Blueprints.

## MCP HTTP API (local-only)
POST `http://127.0.0.1:PORT/mcp` with JSON body `{ "action": "...", "params": { ... } }`.

Actions:
- `list_blueprints` – params: optional `paths: ["/Game", "/Game/Blueprints"]`
- `get_blueprint_structure` – params: `asset_path: "/Game/Blueprints/BP_X.BP_X"`
- `get_references` – params: `asset_path: "/Game/Blueprints/BP_X.BP_X"`
- Write actions (require UI toggle on):
  - `create_blueprint` – `package_path`, optional `parent_class` (e.g., `/Game/MyFolder/BP_New`, `parent_class: "/Script/Engine.Pawn"`).
  - `add_variable` – `asset_path`, `name`, `type: { category, sub_category?, is_array?, is_set?, is_map? }`.
  - `add_function_graph` – `asset_path`, `name`.
  - `add_call_function_node` – `asset_path`, `graph`, `function_path` (e.g., `/Script/Engine.Character.Jump`), optional `x`,`y`.
  - `add_event_node` – `asset_path`, `graph`, `event_name`, optional `x`,`y` (returns existing guid if already present).
  - `add_input_action_event` – `asset_path`, `graph`, `input_action` (asset path), `trigger_event` (e.g., `Pressed`), optional `x`,`y`.
  - `add_component` – `asset_path`, `component_class` (path), `name` (adds via SimpleConstructionScript).
  - `set_pin_default` – `asset_path`, `graph`, `node_guid`, `pin_name`, `value` (for vectors: `(X=1.0,Y=0.0,Z=0.0)`).
  - `connect_pins` – `asset_path`, `graph`, `from_node`, `from_pin`, `to_node`, `to_pin` (node GUIDs from `get_blueprint_structure`).
  - `compile_blueprint` – `asset_path`.
  - `save_blueprint` – `asset_path`.

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
