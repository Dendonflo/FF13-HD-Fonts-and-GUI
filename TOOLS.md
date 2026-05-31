# Tools & Shader Workflow

## fxc — DirectX Shader Compiler

**Location:**
```
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe
```
Comes with the Windows SDK. The `x86` version is used because the game is 32-bit, though for offline compilation any architecture works.

### Disassembling a shader binary

Dumps the bytecode of a `.cso` file as readable HLSL assembly, including parameter names, register assignments, and instruction listing.

```
fxc /dumpbin <file>.cso
```

Example:
```
fxc /dumpbin af31d3405062ee68.cso
```

### Compiling a shader

Compiles HLSL source to a raw D3D9 bytecode `.cso` file ready to use as a replacement.

```
fxc /T ps_3_0 /E main /Fo <output>.cso <source>.hlsl
```

- `/T ps_3_0` — target profile (PS 3.0, what FF13-2 uses)
- `/E main` — entry point function name
- `/Fo` — output file path

Example:
```
fxc /T ps_3_0 /E main /Fo dof_bokeh.cso dof_bokeh.hlsl
```

> **PowerShell note:** `/` is interpreted as a division operator in PowerShell.
> Always prefix with `&` when calling fxc from PowerShell:
> ```powershell
> & "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe" /T ps_3_0 /E main /Fo dof_bokeh.cso dof_bokeh.hlsl
> ```
> Or run from a standard `cmd` prompt where `/` works normally.

---

## Shader Replacement Workflow

### Finding shaders

1. Add `HDTEX_DUMP_SHADERS` to `PreprocessorDefinitions` in `version.vcxproj`
2. Rebuild and run the game through the area of interest, then quit
3. Raw bytecode files appear in `<game bin>\shader_dumps\<hash>.cso`
4. Disassemble with `/dumpbin` to read sampler layout, register assignments, and logic
5. Remove `HDTEX_DUMP_SHADERS` from the project when done

Optionally add `HDTEX_LOG_SHADERS` alongside it to get draw-order logging in `HDTextures.log`, useful for identifying which shader fires at which point in the frame.

### Replacing a shader

1. Write an HLSL replacement in `shader_files/<subfolder>/`
2. Preserve the original sampler and register layout exactly (check with `/dumpbin`)
3. Compile:
   ```
   fxc /T ps_3_0 /E main /Fo <name>.cso <name>.hlsl
   ```
4. Copy the resulting `.cso` to `hd_textures_shaders\<hash>.cso`
5. Add the hash to `hd_textures_shaders\hash_table.txt` if not already present
6. Copy the updated `hd_textures_shaders\` folder to the game bin directory and test

### Deploying (PowerShell one-liner)

```powershell
$fxc = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
& $fxc /T ps_3_0 /E main /Fo shader.cso shader.hlsl
Copy-Item shader.cso "C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY XIII-2\alba_data\prog\win\bin\hd_textures_shaders\<hash>.cso" -Force
```

---

## Project Shader Files

| Folder | File | Replaces | Notes |
|--------|------|----------|-------|
| `shader_files/DoF/` | `dof_bokeh.hlsl` | `af31d3405062ee68` | Gameplay DoF — 37-tap circular bokeh |
| `shader_files/DoF_valhalla/` | `dof_valhalla.hlsl` | `f206fbc4baadd122` | Valhalla fight DoF — 37-tap circular bokeh, different register layout, no color matrix |
| `shader_files/DoF_cutscenes/` | `dof_cutscene_near.hlsl` | `8b0d63a81922561b` | Cutscene near-blur pass — 19-tap circular gather, preserves CoC alpha output |

### Recompiling all shaders

```powershell
$fxc = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
$dst = "C:\Users\dendo\Documents\FF13HDMod\hd_textures_shaders"
$base = "C:\Users\dendo\Documents\FF13HDMod\shader_files"

& $fxc /T ps_3_0 /E main /Fo "$base\DoF\dof_bokeh.cso"                   "$base\DoF\dof_bokeh.hlsl"
& $fxc /T ps_3_0 /E main /Fo "$base\DoF_valhalla\dof_valhalla.cso"        "$base\DoF_valhalla\dof_valhalla.hlsl"
& $fxc /T ps_3_0 /E main /Fo "$base\DoF_cutscenes\dof_cutscene_near.cso"  "$base\DoF_cutscenes\dof_cutscene_near.hlsl"

Copy-Item "$base\DoF\dof_bokeh.cso"                  "$dst\af31d3405062ee68.cso" -Force
Copy-Item "$base\DoF_valhalla\dof_valhalla.cso"       "$dst\f206fbc4baadd122.cso" -Force
Copy-Item "$base\DoF_cutscenes\dof_cutscene_near.cso" "$dst\8b0d63a81922561b.cso" -Force
```
