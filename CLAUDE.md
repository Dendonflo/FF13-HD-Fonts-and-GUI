# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**FF13 Series HD Mod** — A Direct3D9 injection mod for the Final Fantasy XIII trilogy (FF13, FF13-2, and eventually LR:FFXIII) that replaces low-resolution textures, fonts, GUI elements, and map tiles with higher-resolution versions via runtime texture replacement. Also replaces pixel shaders (depth-of-field, shadows) and supports automatic costume artwork swaps. Ships as `version.dll` (a proxy for the Windows system DLL).

The same codebase targets all three games. Per-game differences are handled through:
- A different `hd_textures/` asset folder and `hash_database.txt` for each game
- A different `hd_textures_shaders/hash_table.txt` and `.cso` set for each game's shader hashes
- Preprocessor flags to enable or disable features not applicable to a given game (e.g. costume tracking is FF13-2 specific)

---

## Build & Development

### Building

- **Visual Studio 2019+** required (uses MSVC toolset v145)
- Open `version.vcxproj` and build Release configuration
- Output: `Release/version.dll`
- Platform: Win32 only (32-bit game)
- Output directory: `Release/` — Intermediate objects: `Release/obj/`

### Dependencies

- **MinHook** (`Deps/MinHook/`) — Function hooking library, pre-built as libMinHook.vcxproj (VC16). Used to intercept D3D9 CreateDevice/CreateDeviceEx vtable slots and `KernelBase.CreateFileW` for costume tracking.
- **spdlog** (`Deps/spdlog/`) — Header-only logging. Writes to `HDTextures.log` at runtime. Configured for Unicode wide-character support.

### Preprocessor Defines

All feature flags, diagnostic toggles, and hot-reload options are documented in **[PREPROCESSOR.md](PREPROCESSOR.md)**.

---

## Architecture

### Injection & Hooking Strategy

1. **Version.dll Proxy** — `DllMain()` loads the real system `version.dll` on process attach. Exports standard version info functions by forwarding to the real DLL, so the game operates normally while our code hooks.

2. **MinHook Setup** — On `DLL_PROCESS_ATTACH`, hook `Direct3DCreate9` and `Direct3DCreate9Ex` at the function-body level. These are entry points only — they read the vtable and install the next level of hooks. Never return proxied `IDirect3D9` objects (Steam overlay and d3dx9.dll raw-cast and expect real objects).

3. **Device Creation Hooks** — Two-stage approach to handle proxies (e.g. FF13Fix's `IDirect3D9` wrapper):
   - **Stage 1**: From `Direct3DCreate9/Ex`, try QI for the real `IDirect3D9Ex` and hook `CreateDevice/CreateDeviceEx` from it.
   - **Stage 2**: If QI fails (proxy rejects `IDirect3D9Ex`), defer the `CreateDeviceEx` hook to the first successful `CreateDevice` call, which returns the real device internally.

4. **Device Proxy** — `IDirect3DDevice9Proxy` wraps the real device and intercepts:
   - `CreateTexture()` — marks textures as invalidated (queues for hash check)
   - `SetTexture()` — triggers hash-based texture lookup and swap
   - `SetPixelShader()` — intercepts shader pointer and substitutes replacement if hash matches
   - `Reset()` — releases all cached HD textures on device reset

   Proxy inherits from `IDirect3DDevice9Ex` even though it wraps a 9 pointer, because the real device may report Ex capabilities via QI.

### Two-Device Architecture (FF13-2)

FF13-2 creates two D3D9 devices. Shaders may be created on device 2 but bound via device 1's `SetPixelShader`. `DofPtrMap()` is global (shared across all device proxies) so any proxy can intercept a pointer regardless of which device created it. Replacement shader objects are created per-proxy since a shader object belongs to one device.

---

## Texture Replacement

### Flow

1. **Initialization** (`HDTextureReplacer::Init`)
   - Read `hd_textures/hash_database.txt` → hash (64-bit FNV1a) → texture name mapping
   - Scan `hd_textures/` subdirectories for DDS files
   - Static namespaces (e.g. `gui_resident/`, `fonts/`): load pixel data into RAM immediately
   - Lazy-loaded numbered namespaces (e.g. `map_scene/`, `shop_/`): record disk paths only, load on first use
   - Lazy-load behavior configured in `hd_textures/lazyload_config.txt` (prefix list with optional VRAM caps)

2. **Runtime Identification** (`OnSetTexture`)
   - Lock original texture read-only, compute FNV1a hash of pixel data
   - Look up hash in `hashDB`
   - If match found, check if HD replacement is resident in `nameToHDTex`
   - For lazy-loaded namespaces: load pixel data from disk on first miss
   - Create HD texture on GPU, register in texture cache

3. **Memory Management**
   - `nameToHDTex` — sole owner of all live HD textures (static and numbered)
   - `textureMap` — fast-path non-owning pointer → HD texture cache
   - `checkedTextures` — tracks textures with no match (avoid rehashing)
   - Numbered (lazy) textures managed by per-prefix LRU groups
   - On numbered-namespace switch (e.g. scene number change): flush old group from VRAM, keep pixel data cached

4. **Thread Safety**
   - Single critical section `g_hdTexCS` serializes all `HDTextureReplacer` calls
   - FF13 creates device with `D3DCREATE_MULTITHREADED`, so `SetTexture` and `CreateTexture` arrive concurrently

### Key Data Structures

| Structure | Purpose |
|-----------|---------|
| `hashDB` | Hash → texture name (read once at init, never modified) |
| `hdData` | Texture name → `HDTextureData` (pixel buffer + format + dimensions) |
| `nameToHDTex` | Texture name → live GPU texture (sole owner) |
| `textureMap` | Original D3D pointer → HD texture (fast-path cache) |
| `checkedTextures` | Set of pointers with no match (skip rehashing) |
| `numberedGroups` | Per-prefix LRU tracking for lazy-loaded namespaces |

### Adding New HD Textures

1. Place `.dds` files in `hd_textures/<namespace>/`
2. Compute FNV1a hash of the original (low-res) texture's pixel data
3. Add entry to `hash_database.txt`: `<hash> <namespace>/<texture_name>`
4. For lazy-loaded namespaces, ensure the prefix is in `lazyload_config.txt`
5. Reload game (or use `HDTEX_HOT_RELOAD`) to pick up changes

---

## Costume Tracking

### Overview

When enabled (`COSTUME_TRACKING`), the mod automatically swaps character face artwork textures when the player changes costumes in-game, without requiring separate texture files per outfit — the correct DDS is loaded from disk and hot-swapped at runtime.

### How It Works

1. **Hook** (`CostumeTracker.h` / `dllmain.cpp`) — A `CreateFileW` hook is installed at `KernelBase.dll` level. It intercepts every file open within the game process and checks whether the filename matches a known costume file pattern (e.g. `c120_romXXX_...`).

2. **Costume Map** — `CostumeTracker.h` contains a static map of costume IDs (the `cNNN` number in the filename) to `CostumeInfo`:
   ```cpp
   struct CostumeInfo {
       const char* character;   // "Serah" | "Noel"
       const char* name;        // human-readable costume name
       const char* charPath;    // "serah" | "noel"
       const char* texSuffix;   // "serah" | "knoel"
       const char* folderName;  // subfolder in costumes\ e.g. "default", "n7_armor"
   };
   ```

3. **Dedup** — Per-character last-seen folder pointer (`g_lastSerahFolder`, `g_lastNoelFolder`) prevents repeated swaps on every file open for the same costume.

4. **Callback** — `g_costumeSwapCallback` is called outside `g_hdTexCS`. It reads the DDS from `hd_textures\gui_resident\costumes\<charPath>\<folderName>.dds` via `HDTextureReplacer::ReadDDS` (public static), then enters the CS to call `SwapCostumeTexture`.

5. **SwapCostumeTexture** — Evicts the old GPU texture and all stale pointer-cache entries for that texture name, then installs the new pixel data into `hdData`. GPU upload is deferred to the next `SetTexture` call.

### Texture Layout

```
hd_textures\gui_resident\costumes\
  serah\
    default.dds
    white_mage.dds
    battle_attire.dds
    ... (one DDS per costume folder name)
  noel\
    default.dds
    ... 
```

### Known Limitation

Running the game under a **Windows compatibility mode** (Vista, Windows 8, etc.) replaces system DLLs with shims that break the `KernelBase.CreateFileW` hook chain. Costume tracking will not function in compatibility mode. The workaround is to not use compatibility mode — audio issues (e.g. buzzing) should be addressed by setting the audio device to stereo output instead.

### Future: IAT Hooking

The current implementation hooks `KernelBase.CreateFileW` globally within the process (widest net, catches all file opens). A cleaner alternative is IAT hooking — patching only the game executable's import table so only the game's own `CreateFileW` calls are intercepted. This would reduce the heuristic surface that triggers antivirus false positives. Not yet implemented.

---

## Shader Replacement

### Overview

`DoFFixer` (`src/DoFFixer.h`) intercepts `SetPixelShader` and substitutes user-supplied replacement shaders. The feature is driven entirely by files in `hd_textures_shaders\` — absent directory or missing `hash_table.txt` disables it silently.

### Directory Layout

```
hd_textures_shaders\
  hash_table.txt       — one hex hash per line; # comments; 0x prefix optional
  <hash>.cso           — raw D3D9 pixel shader bytecode to substitute

shader_files\          — source HLSL + compiled .cso, organised by purpose
  <folder>\
    <name>.hlsl        — HLSL source
    <hash>.cso         — compiled output (copy to hd_textures_shaders\ to deploy)
    compile_all.bat    — fxc build script
```

At startup, each hash in `hash_table.txt` is matched to its `<hash>.cso`. Hashes with no `.cso` are skipped with a warning. Replacement shader objects are created per-device-proxy on first use.

**Authoring convention**: one folder per shader in `shader_files/`, named by hash when purpose is unknown or by short description once confirmed. Compile with `fxc /T ps_3_0 /E main /Fo <hash>.cso <hash>.hlsl`, then copy to `hd_textures_shaders\`.

### Confirmed FF13-2 Shader Hashes

| Hash | Tokens | Purpose |
|------|--------|---------|
| `af31d3405062ee68` | 185 | Gameplay DoF gather + composite |
| `f206fbc4baadd122` | 144 | Valhalla fight DoF composite |
| `8b0d63a81922561b` | 211 | Cutscene DoF step 1: near blur gather |
| `b32c3447f7a5418c` | — | Cutscene DoF step 2: near blur spread/dilation |
| `67f7f77335ff7a82` | 240 | Cutscene DoF step 3: final composition |
| `060b9a63a30b8f34` | 9 | Shadow caster (opaque, no alpha) — superseded by e3ffdec5 |
| `e3ffdec520d824b2` | 13 | Shadow caster alpha-clip — replaces 060b9a63 (`shader_files/shadow palm tree and chocobo/`) |
| `69d9822c85b46095` | 7 | Shadow alpha-mask pass — fixed with `clip()` (`shader_files/shadow shader unknown/`) |
| `bd38df932e28c100` | — | Rain particles — opacity fix |
| `b9406b1afa8fee57` | 41 | Lit surface: 1 dir light, no shadow (purpose TBD) |
| `4b82d418ba0f5339` | 58 | Lit surface: 1 dir + 1 point, no shadow (purpose TBD) |
| `a009375f128d43d6` | 96 | Lit surface: 1 dir + 4-cascade shadow (purpose TBD) |
| `a6f4d5a5756cdb25` | 114 | Lit surface: 1 dir + 1 point + shadow (purpose TBD) |
| `e02b3853d5a4fc00` | 130 | Lit surface: 1 dir + 2 point (loop) + shadow (purpose TBD) |
| `5e4c28d7859bc12a` | 319 | Lit surface: 4 lights (bool-gated) + shadow (purpose TBD) |
| `4ee03898b1bf663b` | 8 | VFX vfxVanmMergePercent lerp blend (purpose TBD) |

### Lit Surface Alpha Fix — Root Cause

The 7 lit-surface shaders above are candidates for an alpha transparency bug on alpha-blended geometry. All do `mul_pp r0, r4, modulateColor` across all 4 channels then output `oC0.w = r0.w`, meaning alpha = `tex.w × vert.w × modulateColor.w`. If the game sets `modulateColor.w = 0`, output alpha is zeroed entirely. The fix: save alpha as `tex.w × vert.w` before the modulate step and output it directly, multiplying only RGB by modulateColor. Source in `shader_files/lit surface alpha fix/`. Enable one hash at a time to identify which geometry is affected.

### Confirmed Non-Target Shaders

| Hash | Tokens | Identity |
|------|--------|---------|
| `3790c2aadc3acdcc` | 73 | CoC / depth pre-pass |
| `fae7dab622acff45` | 135 | Pre-rendered cutscene pass |
| `86222c7146665502` | 110 | God ray additive composite |
| `b9335d1fb80b0be1` | 169 | Bloom composite |
| `3382665720d48f76` | 228 | Bloom blur ping-pong (×4) |
| `b8df44865fe0815d` | 152 | Bloom extraction |

### Shader Identification Workflow

1. Add `HDTEX_LOG_SHADERS` and/or `HDTEX_DUMP_SHADERS` to `PreprocessorDefinitions` in `version.vcxproj`
2. Run the game through the area of interest and quit
3. `HDTEX_LOG_SHADERS`: each `PSLogger:` line in `HDTextures.log` shows hash + token count; cross-reference `DrawPrimitive` entries for draw order
4. `HDTEX_DUMP_SHADERS`: bytecode written to `shader_dumps\<hash>.bin`; disassemble with `fxc /dumpbin <file>.bin` to read sampler layout and operations
5. Write replacement HLSL, compile, copy `.cso` to `hd_textures_shaders\`, add hash to `hash_table.txt`
6. Remove diagnostic defines when done

---

## Logging

- Initialized in `DllMain()` using spdlog
- Log file: `HDTextures.log` (same directory as `version.dll`)
- Pattern: `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message`
- Info level by default
- Key events: DLL load, hook install, device proxy wrap, texture hash matches/misses, costume swaps, group flushes, LRU evictions, Init() completion

---

## File Structure

```
src/
  dllmain.cpp              — DLL entry point, hooking setup, costume swap callback
  HDTextureReplacer.h      — Core texture replacement + SwapCostumeTexture
  CostumeTracker.h         — Costume ID map, CreateFileW hook, swap callback registration
  DoFFixer.h               — Shader interception and replacement
  IDirect3DDevice9.h       — Device proxy wrapper
  IDirect3D9.h             — IDirect3D9 forwarding
  version.def              — Module definition (version.dll proxy exports)

Deps/
  MinHook/                 — Function hooking library
  spdlog/                  — Header-only logging

hd_textures/               — HD texture assets (loaded at runtime)
  hash_database.txt        — FNV1a hash -> texture name mapping
  lazyload_config.txt      — Lazy-load namespace prefixes and VRAM caps
  gui_resident/            — GUI and face textures (static, always resident)
    costumes/              — Per-character, per-costume face DDS files
      serah/               — serah/<folderName>.dds
      noel/                — noel/<folderName>.dds
  fonts/                   — Font textures (static)
  map_scene/               — Map textures (lazy-loaded, LRU eviction)
  <other namespaces>/

hd_textures_shaders/       — Deployed shader replacements
  hash_table.txt           — Active hashes to intercept
  <hash>.cso               — Replacement bytecode

shader_files/              — Shader source + compiled .cso, by purpose
  DoF_gameplay/            — af31d3405062ee68
  DoF_valhalla/            — f206fbc4baadd122
  DoF_cutscenes/           — 8b0d63a81922561b (step 1), b32c3447f7a5418c (step 2), 67f7f77335ff7a82 (step 3)
  shadow palm tree and chocobo/  — e3ffdec520d824b2 (replaces 060b9a63)
  shadow shader unknown/   — 69d9822c85b46095
  lit surface alpha fix/   — b9406b1a, 4b82d418, a009375f, a6f4d5a5, e02b3853, 5e4c28d7, 4ee03898
  <hash>/                  — Single-shader folders (hash = unknown purpose at time of creation)

Other/
  XIII-2/
    Release/               — Distribution packages
      version.dll          — Built output (copy here after build)
      hd_textures/         — Full texture set
      hd_textures_shaders/ — Full shader set
      NC_version/          — Nova Chrysalia Launcher package
        Data/              — Empty; wizard populates at install time
        Setup/
          Setup.bat        — Entry point called by NC Launcher
          setup_wizard.ps1 — PowerShell WinForms wizard (4 steps)
          assets/          — Preview PNGs (260x354 portrait / 520x292 16:9)
          source/
            base/          — Full base mod (always installed first)
            colorful/      — Colorful artwork overlay
            fonts_chinese/ — Chinese font overlay
            fonts_korean/  — Korean font overlay
            nodof_gameplay/   — Disable DoF in gameplay overlay
            nodof_cutscenes/  — Disable DoF in cutscenes overlay
      Colorful artwork/
      Chinese fonts/
      Korean fonts/
      No DoF in gameplay/
      No DoF in cutscenes/
      README.txt

README.txt                 — User installation & troubleshooting guide
PREPROCESSOR.md            — All preprocessor define documentation
TOOLS.md                   — fxc compiler usage and shader workflow
```

---

## Installation & Testing

- **FF13-1**: Copy `version.dll` and `hd_textures/` to `<game>/white_data/prog/win/bin/`
- **FF13-2**: Copy `version.dll`, `hd_textures/`, `hd_textures_shaders/` to `<game>/alba_data/prog/win/bin/`
- **LR:FFXIII**: Path TBD — not yet in active development for this game
- **Verify**: Check `HDTextures.log` for "HDTextures mod loaded" and non-zero texture count
- **Compatibility**: Works alongside FF13Fix (install FF13Fix first). FF13Fix PLUS may blur UI — set `LODBias=0.0` in FF13Fix.ini to fix.
- **Nova Chrysalia**: Use the NC_version package. The setup wizard handles file selection; NC Launcher handles game-side install and uninstall tracking.

---

## File Organization

When working on this project, create appropriately-named subfolders for extracted or processed assets (e.g. `saveicons_extracted/`, `fonts_hd/`). Do not scatter files across multiple top-level directories.

---

## Common Tasks

### Debugging Texture Replacement

- Enable `HDTEX_TRACE_DEVICE` to log every device call
- Check `HDTextures.log` for hash matches, misses, and LRU activity
- Use `HDTEX_DIAG_NO_HD_TEXTURES` to verify hooking without replacement
- Use `HDTEX_DIAG_NO_DEV_WRAP` to bypass the proxy entirely

### Handling Proxy Conflicts

If another mod uses `version.dll`, one must target a different DLL (e.g. `winmm.dll`, `dinput8.dll`). Modify `TargetName` in `.vcxproj` and update the forwarded DLL name.

---

## FF13 Asset File Formats

### IMGB / XGR Files
- **IMGB**: Raw pixel data for DDS textures. Usually paired with an XGR metadata file (size, compression, format). Always analyze before processing — never assume format or dimensions.
- **XGR**: Metadata/header describing IMGB texture properties. Extraction tools available in `C:\Users\dendo\Documents\FF 13 mods\Tools`.

### GTEX Metadata (96 bytes)
- Offset `0x4a–0x4b`: Width as big-endian 16-bit
- Offset `0x4c–0x4d`: Height as big-endian 16-bit
- Used for FF13-1 save icons (320×176) and other specific textures

### IMGB Extraction
```
python FF13_IMGB_Extractor.py file.imgb [width] [height]
python FF13_IMGB_Extractor.py C:\path [width] [height]   # batch
```
Location: `C:\Users\dendo\Documents\FF13_IMGB_Extractor.py`. Reads GTEX automatically when present.

---

## Available Tools

| Tool | Location | Purpose |
|------|----------|---------|
| fxc.exe | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe` | Compile HLSL → D3D9 bytecode. Not on PATH — always use full path. |
| FF13_IMGB_Extractor.py | `C:\Users\dendo\Documents\` | Extract DXT1 IMGB → DDS |
| XGR / asset tools | `C:\Users\dendo\Documents\FF 13 mods\Tools` | Metadata extraction, texture analysis |

**fxc usage:**
```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /nologo /T ps_3_0 /E main /Fo <hash>.cso <hash>.hlsl
```

---

## Known Limitations

| Limitation | Notes |
|-----------|-------|
| 32-bit only | Game is 32-bit; no 64-bit build |
| D3DX bypass | `D3DXCreateTexture`-internal `CreateTexture` calls bypass our hooks (not an issue for FF13's direct calls) |
| Lazy-load VRAM cap | LRU is eviction-on-threshold; no intelligent prefetch |
| Hash collisions | FNV1a is fast but not cryptographically strong; if collisions occur, add secondary metadata (dimensions, format) to hash key |
| Device proxy leak | Proxy objects are never freed — by design to avoid over-releasing the real device |
| Costume tracking + compatibility mode | `CreateFileW` hook breaks under Windows compatibility mode shims; costume swap will not work |
| Costume tracking AV heuristic | KernelBase-level `CreateFileW` hook matches ransomware/cryptor patterns in some AV heuristics (e.g. VBA32 TScope.Malware-Cryptor.SB). Future mitigation: switch to IAT hooking. |
| D3D9Ex managed pool | D3D9Ex devices (e.g. LR:FFXIII) do not support `D3DPOOL_MANAGED`. `CreateHDTexture` detects this by catching `D3DERR_INVALIDCALL` from the initial `CreateTexture` call and automatically falls back to a SYSTEMMEM staging texture + `UpdateTexture` into a `D3DPOOL_DEFAULT` texture. DEFAULT pool textures are lost on device Reset — `ReleaseTextures()` in the Reset hook releases them before the real Reset fires, so they are recreated lazily on the next `SetTexture` match. |
