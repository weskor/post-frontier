# Unreal MCP

Epic's Unreal MCP lets one omp agent drive the running Unreal editor (UE 5.8.3, experimental).

## What is enabled

- `CoopRTS.uproject` enables the engine plugins `ModelContextProtocol` and `AllToolsets` (which pulls in `ToolsetRegistry`). Both live under `Engine/Plugins/Experimental/`.
- `Config/DefaultEditorPerProjectUserSettings.ini` sets `bAutoStartServer=True` under `[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]`. That is `UModelContextProtocolSettings::bAutoStartServer` (`config=EditorPerProjectUserSettings`, Editor Preferences > General > Model Context Protocol > Auto Start Server), so the editor serves MCP as soon as it opens this project.
- The server listens on `http://127.0.0.1:8000/mcp` (streamable HTTP). `tools/list` returns three meta-tools (`list_toolsets`, `describe_toolset`, `call_tool`); the toolset tools are discovered on demand through them (`bEnableToolSearch` defaults to true).
- `.omp/mcp.json` defines the `unreal-mcp` server with `"enabled": "${UNREAL_MCP:-false}"`. Only an omp launched with `UNREAL_MCP=1` connects to it. There is deliberately no root `.mcp.json`, which every omp session in the repo would load.

## Start the editor and the MCP agent

Open the editor with `./x editor` ([`./x help editor`](../x)).

Wait for `LogModelContextProtocol: Starting MCP server on port 8000` in the editor log, then in a separate terminal at the repo root:

```bash
UNREAL_MCP=1 omp --model claude-sonnet-5-5
```

The agent sees the server as `unreal_mcp` (tools `mcp__unreal_mcp_list_toolsets`, `..._describe_toolset`, `..._call_tool`). If the editor was started after omp, run `/mcp reload`.

## Rules

- **One agent only.** Tool calls must not overlap. Never launch a second omp with `UNREAL_MCP=1`; every other agent runs without it.
- **Headless generators must not save a map the open editor is editing.** `./x gen` ([`./x help gen`](../x)) and `./x editor` share the exclusive lock, so close the editor before generator work. MCP asset-editing ownership remains an owner decision.
- **Rebuild and re-cook after the `.uproject` change.** Adding plugins changes build/package inputs. Editor compilation uses `./x build` ([`./x help build`](../x)); cooked artifacts use `./x package` ([`./x help package`](../x)).
- Port 8000 is used by one editor at a time. Config-driven auto-start is skipped for commandlets (cook, generator scripts), so they do not collide with the open editor.
