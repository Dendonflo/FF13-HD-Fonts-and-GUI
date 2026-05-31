# Preprocessor Defines Reference

All defines are toggled in `version.vcxproj` under `PreprocessorDefinitions` for the Release configuration. Rebuild after any change.

---

## Build Settings (vcxproj, not preprocessor)

| Setting | Value | Notes |
|---------|-------|-------|
| `LanguageStandard` | C++17 | |
| `RuntimeLibrary` | MultiThreaded (/MT) | Static CRT — must not change |
| `Optimization` | MaxSpeed + function-level linking | |
| `GenerateDebugInfo` | false | Release build |
| `Platform` | Win32 only | 32-bit game |

---

## Feature Flags

These are shipped in production builds. Toggle to include/exclude a feature.

| Define | Default | Effect |
|--------|---------|--------|
| `COSTUME_TRACKING` | **on** | Enables automatic costume artwork swap. Installs a `CreateFileW` hook at `KernelBase.dll` to detect costume ID changes from the game's file access pattern. Calls `OnCostumeChanged` which reads a DDS from `hd_textures\gui_resident\costumes\<char>\<folder>.dds` and hot-swaps the face texture via `SwapCostumeTexture`. Disable if hook causes compatibility issues. |
| `SHADOW_ALPHA_FIX` | **on** | Forces `D3DRS_ALPHATESTENABLE = TRUE` for the duration of the shadow map render pass. Detected by watching `SetRenderTarget`: when the render target switches to a `D3DFMT_R32F` surface (the shadow map), alpha test is enabled and the saved state is restored when the pass ends. Any game attempt to set `D3DRS_ALPHATESTENABLE` during the shadow pass is overridden to keep it `TRUE`. Without this, alpha-tested geometry (palm tree leaves, chocobo head feathers) casts a fully opaque shadow silhouette. No shader replacement involved. |

---

## Diagnostic / Isolation Flags

Never ship these. Use to narrow down issues by selectively disabling systems.

| Define | Effect |
|--------|--------|
| `HDTEX_DISABLE_D3D_HOOKS` | Skip MinHook setup entirely — no texture or shader replacement at all |
| `HDTEX_DIAG_NO_DEV_WRAP` | Return the real unwrapped device instead of the proxy — bypasses all interception |
| `HDTEX_DIAG_NO_HD_TEXTURES` | Wrap the device but skip HD texture replacement logic — hooks active, no swaps |
| `HDTEX_DIAG_NO_CONSTRUCT` | Skip `HDTextureReplacer` construction |
| `HDTEX_DIAG_SKIP_HD_INIT` | Skip `Init()` call — replacer constructed but never loaded |

Typical isolation sequence: start with `HDTEX_DISABLE_D3D_HOOKS` (is the crash in hooking?), then `HDTEX_DIAG_NO_DEV_WRAP` (is it the proxy?), then `HDTEX_DIAG_NO_HD_TEXTURES` (is it the replacement logic?).

---

## Logging Flags

Dev-only. Leave disabled in shipped builds — they write to disk on every frame.

| Define | Effect |
|--------|--------|
| `HDTEX_TRACE_DEVICE` | Log every `IDirect3DDevice9` method call — extremely verbose, use only to trace a specific call sequence |
| `HDTEX_LOG_SHADERS` | Log every `CreatePixelShader` (hash + token count) and every draw call to `HDTextures.log`. Used for shader identification — cross-reference `PSLogger:` lines with `DrawPrimitive` entries to map shaders to geometry |
| `HDTEX_DUMP_SHADERS` | Write each shader's raw bytecode to `shader_dumps\<hash>.bin` (valid D3D9 bytecode, disassemble with `fxc /dumpbin`). Can be used with or without `HDTEX_LOG_SHADERS`. Note: intentionally `.bin` here — dev artifact, never shipped |
| `HDTEX_DUMP_TEXTURES` | Write every texture seen by `OnSetTexture` to `textures_dump\<hash>.dds` (valid DDS, opens directly in paint.net / Photoshop). Each hash is written only once — skipped if the file already exists. Will stutter heavily on the first pass through any scene. Cross-reference filenames against `hash_database.txt` to identify unknowns. |

---

## Per-Game Compatibility Flags

Define only for the game that requires the behaviour. Do not apply globally.

| Define | Game | Effect |
|--------|------|--------|
| `HDTEX_ASSET_SUBDIR` | LR:FFXIII | Looks for `hd_textures/` and `hd_textures_shaders/` in a subdirectory of the DLL location rather than beside it. LR's NC Launcher maps `Data\` into `weiss_data\`, so assets sit at `<gameRoot>\weiss_data\`. Set to `L"weiss_data"`. |

---

## Hot Reload

Dev-only. Never ship.

| Define | Effect |
|--------|--------|
| `HDTEX_HOT_RELOAD` | Spawn a background thread that performs a full reload every N seconds: flushes all texture caches, re-reads every DDS from disk, re-reads `hash_database.txt`, `lazyload_config.txt`, `hash_table.txt`, and all `.cso` shader files. Lets you iterate on textures and shaders without restarting the game. |
| `HDTEX_HOT_RELOAD_INTERVAL_SEC` | Override the flush interval (integer seconds). Default: `5` when `HDTEX_HOT_RELOAD` is defined but this is not set. |
