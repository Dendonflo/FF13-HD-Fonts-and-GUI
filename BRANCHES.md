# Branch Overview

## `main` — Stable release

The current shipping version. LRU/group-based memory management driven by
`lazyload_config.txt`. Textures in static namespaces (`gui_resident`, `fonts`,
shops) are preloaded into RAM at startup and kept resident. Numbered namespaces
(`map_scene`, etc.) are lazy-loaded and evicted by an LRU cap when the game
switches scene numbers.

**Status:** Stable, tested in-game.

**To merge from another branch:** This is the target — other branches merge
into this once verified.

---

## `async-hash-perf` — Async hashing experiment (off `main`)

Performance experiment for LR:FFXIII, where the synchronous per-frame hashing
in `SetTexture` was causing frame drops. Adds:
- A dedicated hash worker thread with a work queue
- SRWLock instead of a CRITICAL_SECTION for the texture map fast-path
- Preload threads that warm the texture cache before first use
- Stage/shop number tracking to drive preloading

**Status:** Experimental. The core idea works but the interaction between the
async hash thread and the main render thread needs more testing. Not ready to
merge.

**Before merging to main:** Verify no stutter or corruption when textures are
bound before their hash result arrives. Test device Reset with in-flight hash
requests. Confirm preload threads shut down cleanly on exit.

---

## `release-on-og-free` — All-in-one release-driven model (off `main`)

The intended replacement for `main` and `async-hash-perf` both. A single
codebase that builds one DLL for all three games, with the slow-path strategy
chosen at build time and the D3D9-vs-D3D9Ex differences detected at runtime.

### Core model — release-driven lifetime

Removes the LRU/group/namespace machinery entirely. HD texture lifetime mirrors
the game's own: load on first bind (always from disk — no preloading), release
when the game's own `Release` refcount hits zero, observed via a single vtable
patch on `IDirect3DTexture9::Release`. A per-name refcount (`nameRefs`) lets the
same logical texture reloaded at multiple addresses share one HD texture.

Removed vs `main`: `NumberedGroup`, `FlushGroup`, `EvictOldest`,
`lazyload_config.txt`, `lazyPrefixes`, `lazyLruCaps`, persistent `hdData`.

### Runtime D3D9 / D3D9Ex detection (automatic, no flag)

The proxy records whether the device came from `CreateDeviceEx` and adapts:
- **Ex-QI:** Ex devices (LR) get the proxy back for `IID_IDirect3DDevice9Ex`
  queries so their texture calls stay intercepted; plain devices (FF13-1/2)
  forward to the real device to avoid the d3dx9 raw-cast crash.
- **ResetEx:** Ex devices keep their HD textures (D3D9Ex is resilient — no DEFAULT
  pool loss); plain `Reset` releases and lazily recreates them.
- **Upload:** `CreateHDTextureFromData` uses `D3DPOOL_MANAGED`, falling back to a
  SYSTEMMEM staging + `UpdateTexture` into DEFAULT when MANAGED is rejected (Ex).
- `D3DCREATE_PUREDEVICE` is stripped at device creation so MANAGED is available.

### Async hashing (build-time flag: `HDTEX_ASYNC_HASH`)

For D3D9Ex titles that stream assets continuously (LR), the synchronous
`LockRect` + hash on the render thread causes multi-second stalls (D3D9Ex has no
MANAGED pool, so locking the game's originals can force a GPU→CPU readback). With
the flag defined, a background worker does the lock, hash, DB lookup, **and** the
HD DDS disk read; the render thread only does the GPU upload (`ConsumeHashResults`)
and proactively binds the HD to any stage still showing the original. Originals
are AddRef'd while in flight (keeps them alive for the worker and composes with
the Release hook). Without the flag, hashing is fully synchronous as above.

### Build matrix (one codebase)

| Game | `HDTEX_ASYNC_HASH` | `HDTEX_ASSET_SUBDIR` | `COSTUME_TRACKING` |
|------|:---:|:---:|:---:|
| FF13-1 | off | — | off |
| FF13-2 | off | — | on |
| LR:FFXIII | **on** | `weiss_data` | off |

`HDTEX_ASSET_SUBDIR=<name>` (no quotes) changes the base path for `hd_textures\` from
the DLL directory to `<DLL dir>\<name>\hd_textures\`. Required for LR because its asset
root is `weiss_data\` rather than sitting next to the DLL. The resolved path is logged
at startup (`HDTextures: asset root -> ...`) to make misconfiguration obvious.

**Status:** Compile-verified in both sync and async configurations. Not yet
tested in-game in either.

**Before merging to main:**
- **Sync (FF13-1/2):** full session — menus, combat, map transitions, costume
  changes. Watch `HDTextures.log` for unexpected releases or double-frees.
  Confirm the Release vtable patch doesn't conflict with FF13Fix.
- **Async (LR):** confirm streaming no longer stutters and textures pop in within
  a few frames. Stress map transitions (heavy queue churn). Verify no leak if a
  texture is released mid-hash (the in-flight AddRef + `pendingHash_` guard).
- **Both:** device reset paths (alt-tab, resolution change). Confirm VRAM stays
  bounded across long sessions.
- Worker shutdown is clean on exit (`StopHashThread` in the destructor).

---

## Retiring the other branches

Once `release-on-og-free` is verified in-game, it supersedes both:
- `main` — replaced by the release-driven model (sync build).
- `async-hash-perf` — its async goal is folded in here behind `HDTEX_ASYNC_HASH`,
  without the LRU/preload-thread machinery. Note this branch took a different
  locking route: it used an SRWLock, which is **incompatible** with the Release
  vtable hook (the hook re-enters the lock when freeing HD textures; SRWLock is
  not recursive and would deadlock). `release-on-og-free` keeps the recursive
  `CRITICAL_SECTION` for that reason.
