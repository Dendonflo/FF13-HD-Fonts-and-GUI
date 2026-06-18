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

## `release-on-og-free` — Simplified release-driven lifetime (off `main`)

Architectural redesign that removes the LRU/group/namespace machinery entirely.
Instead of heuristics, HD texture lifetime mirrors the game's own: load on
first bind (always from disk — no preloading), release when the game's own
`Release` refcount hits zero. Uses a single vtable patch on
`IDirect3DTexture9::Release` to observe frees.

Key removals: `NumberedGroup`, `FlushGroup`, `EvictOldest`, `lazyload_config.txt`,
`lazyPrefixes`, `lazyLruCaps`, `hdData` as a persistent map. Net: ~430 lines vs
~1000 on `main`.

**Status:** Compile-verified, not yet tested in-game. Likely to replace `main`
once confirmed working.

**Before merging to main:** 
- Run through a full FF13-2 session: menus, combat, multiple map transitions,
  costume changes. Check `HDTextures.log` for unexpected releases or double-frees.
- Confirm VRAM stays bounded across long sessions (no leak if `nameRefs` ever
  gets out of sync).
- Test device Reset path (alt-tab, resolution change) — all HD textures should
  be recreated cleanly on next bind.
- Verify the vtable patch doesn't interfere with FF13Fix or any other d3d9 mod.
- If `async-hash-perf` is ever merged, the Release hook and the async hash
  thread need careful coordination — a texture could be freed while its hash
  is still in the worker queue.
