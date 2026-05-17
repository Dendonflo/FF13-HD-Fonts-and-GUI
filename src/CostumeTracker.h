#pragma once

// ---------------------------------------------------------------------------
// CostumeTracker — FF13-2 only, enable with COSTUME_TRACKING preprocessor define.
//
// Hooks CreateFileW and watches for cXXX.win32.trb model loads. When one of
// the known costume IDs is opened, fires g_costumeSwapCallback so the caller
// can load the matching costume DDS and hot-swap it into HDTextureReplacer.
//
// Folder layout expected by the caller:
//   hd_textures\gui_resident\costumes\serah\<folderName>.dds
//   hd_textures\gui_resident\costumes\noel\<folderName>.dds
//
// Do NOT enable in FF13-1 or LR builds — the model IDs are XIII-2 specific.
// ---------------------------------------------------------------------------

#ifdef COSTUME_TRACKING

#include <windows.h>
#include <unordered_map>
#include "spdlog/spdlog.h"
#include "MinHook.h"

// ---------------------------------------------------------------------------
// Costume ID → character / costume name table
// ---------------------------------------------------------------------------
struct CostumeInfo {
    const char* character;   // "Serah", "Noel"          (for logging)
    const char* name;        // "Default", "N7 Armor", … (for logging)
    const char* charPath;    // subfolder:  "serah" | "noel"
    const char* texSuffix;   // tex filename suffix: "serah" | "knoel"
    const char* folderName;  // DDS filename (sans .dds): "default", "n7_armor", …
};

static const std::unordered_map<int, CostumeInfo> kCostumeMap = {
    // ── Serah ──────────────────────────────────────────────────────────────
    { 108, { "Serah", "Default",         "serah", "serah", "default"          } },
    { 179, { "Serah", "Default",         "serah", "serah", "default"          } },
    { 170, { "Serah", "Style and Steel", "serah", "serah", "style_and_steel"  } },
    { 183, { "Serah", "Style and Steel", "serah", "serah", "style_and_steel"  } },
    { 158, { "Serah", "Summoner's Garb", "serah", "serah", "summoners_garb"   } },
    { 180, { "Serah", "Summoner's Garb", "serah", "serah", "summoners_garb"   } },
    { 159, { "Serah", "Beachwear",       "serah", "serah", "beachwear"        } },
    { 181, { "Serah", "Beachwear",       "serah", "serah", "beachwear"        } },
    { 160, { "Serah", "N7 Armor",        "serah", "serah", "n7_armor"         } },
    { 182, { "Serah", "N7 Armor",        "serah", "serah", "n7_armor"         } },
    { 240, { "Serah", "White Mage",      "serah", "serah", "white_mage"       } },
    // ── Noel ───────────────────────────────────────────────────────────────
    { 110, { "Noel", "Default",            "noel", "knoel", "default"            } },
    { 140, { "Noel", "Default",            "noel", "knoel", "default"            } },
    { 161, { "Noel", "Battle Attire",      "noel", "knoel", "battle_attire"      } },
    { 164, { "Noel", "Battle Attire",      "noel", "knoel", "battle_attire"      } },
    { 162, { "Noel", "Spacetime Guardian", "noel", "knoel", "spacetime_guardian" } },
    { 165, { "Noel", "Spacetime Guardian", "noel", "knoel", "spacetime_guardian" } },
    { 163, { "Noel", "N7 Armor",           "noel", "knoel", "n7_armor"           } },
    { 166, { "Noel", "N7 Armor",           "noel", "knoel", "n7_armor"           } },
    { 171, { "Noel", "Ezio Auditore",      "noel", "knoel", "assassins_creed"    } },
    { 172, { "Noel", "Ezio Auditore",      "noel", "knoel", "assassins_creed"    } },
    { 174, { "Noel", "Black Mage",         "noel", "knoel", "black_mage"         } },
    { 175, { "Noel", "Black Mage",         "noel", "knoel", "black_mage"         } },
};

// ---------------------------------------------------------------------------
// Callback — called outside g_hdTexCS whenever a new costume is detected.
//   charPath   : "serah" | "noel"
//   texSuffix  : "serah" | "knoel"
//   folderName : "default", "n7_armor", "assassins_creed", …
// Set this before InstallCostumeTrackerHook.
// ---------------------------------------------------------------------------
typedef void (*pfnCostumeSwap)(const char* charPath,
                               const char* texSuffix,
                               const char* folderName);

inline pfnCostumeSwap g_costumeSwapCallback = nullptr;

// Dedup: pointer to the last fired folderName string per character.
// folderName pointers are string literals in kCostumeMap — pointer equality is valid.
inline const char* g_lastSerahFolder = nullptr;
inline const char* g_lastNoelFolder  = nullptr;

// ---------------------------------------------------------------------------
// ParseCostumeTRBPath
// Extracts the numeric ID from a path whose filename matches cXXX.win32.trb.
// Returns the ID on match, -1 otherwise. No allocations, no regex.
// ---------------------------------------------------------------------------
static int ParseCostumeTRBPath(const wchar_t* path)
{
    if (!path) return -1;

    // Advance to the filename component (after the last \ or /).
    const wchar_t* fname = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            fname = p + 1;

    // Must start with 'c'.
    if (fname[0] != L'c') return -1;

    // Parse the numeric ID that follows.
    int    id      = 0;
    int    nDigits = 0;
    size_t i       = 1;
    while (fname[i] >= L'0' && fname[i] <= L'9') {
        id = id * 10 + (fname[i] - L'0');
        ++i; ++nDigits;
    }
    if (nDigits == 0) return -1;

    // Remaining string must be exactly ".win32.trb".
    if (wcscmp(fname + i, L".win32.trb") != 0) return -1;

    return id;
}

// ---------------------------------------------------------------------------
// CreateFileW hook
// ---------------------------------------------------------------------------
typedef HANDLE (WINAPI* pfnCreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static pfnCreateFileW g_fpCreateFileW = nullptr;

static HANDLE WINAPI HookCreateFileW(
    LPCWSTR               lpFileName,
    DWORD                 dwDesiredAccess,
    DWORD                 dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD                 dwCreationDisposition,
    DWORD                 dwFlagsAndAttributes,
    HANDLE                hTemplateFile)
{
    HANDLE h = g_fpCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                                lpSecurityAttributes, dwCreationDisposition,
                                dwFlagsAndAttributes, hTemplateFile);

    // Only inspect successful opens to avoid noise from probe/failed opens.
    if (h != INVALID_HANDLE_VALUE && lpFileName) {
        int id = ParseCostumeTRBPath(lpFileName);
        if (id >= 0) {
            auto it = kCostumeMap.find(id);
            if (it != kCostumeMap.end()) {
                const CostumeInfo& info = it->second;

                // Dedup: skip if this is the same costume we already fired for.
                const char** pLastFolder = (info.charPath[0] == 's')
                                         ? &g_lastSerahFolder
                                         : &g_lastNoelFolder;
                if (info.folderName == *pLastFolder)
                    return h;   // already active, no change

                *pLastFolder = info.folderName;

                spdlog::info("CostumeTracker: {} / {} detected (c{:03d}) — firing swap",
                             info.character, info.name, id);

                if (g_costumeSwapCallback)
                    g_costumeSwapCallback(info.charPath, info.texSuffix, info.folderName);
            }
        }
    }

    return h;
}

// ---------------------------------------------------------------------------
// InstallCostumeTrackerHook — call once after MH_Initialize().
//
// On Windows 8+, kernel32!CreateFileW is a thin stub that forwards to
// KernelBase!CreateFileW. The game may call KernelBase directly, bypassing
// the kernel32 stub entirely. We resolve the real target from KernelBase
// first, falling back to kernel32 if KernelBase isn't present.
// ---------------------------------------------------------------------------
inline void InstallCostumeTrackerHook()
{
    void* pTarget = nullptr;

    // Prefer KernelBase — the actual implementation on modern Windows.
    HMODULE hMod = GetModuleHandleW(L"KernelBase.dll");
    if (hMod) pTarget = reinterpret_cast<void*>(GetProcAddress(hMod, "CreateFileW"));

    // Fallback to kernel32 (older Windows, or if KernelBase wasn't loaded yet).
    if (!pTarget) {
        hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod) pTarget = reinterpret_cast<void*>(GetProcAddress(hMod, "CreateFileW"));
    }

    if (!pTarget) {
        spdlog::error("CostumeTracker: could not resolve CreateFileW address");
        return;
    }

    if (MH_CreateHook(pTarget, &HookCreateFileW,
                      reinterpret_cast<void**>(&g_fpCreateFileW)) == MH_OK) {
        MH_EnableHook(pTarget);
        spdlog::info("CostumeTracker: CreateFileW hook installed ({:p})", pTarget);
    } else {
        spdlog::error("CostumeTracker: failed to hook CreateFileW");
    }
}

#endif // COSTUME_TRACKING
