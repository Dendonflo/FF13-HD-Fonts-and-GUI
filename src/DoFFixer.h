#pragma once
#include <d3d9.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <string>
#include "spdlog/spdlog.h"

// DoFFixer — intercepts pixel shaders at SetPixelShader and substitutes
// user-supplied replacements loaded from disk.
//
// Configuration directory: hd_textures_shaders\
//   hash_table.txt  — one hex hash per line; # comments; 0x prefix optional.
//                     Absent = feature silently disabled.
//   <hash>.bin      — raw D3D9 pixel shader bytecode to substitute when the
//                     game sets a shader whose bytecode matches <hash>.
//                     If a .bin is missing for a listed hash, that hash is
//                     skipped with a warning.
//
// Confirmed FF13-2 shader hashes:
//   af31d3405062ee68  185t  gameplay DoF gather+composite
//   f206fbc4baadd122  144t  Valhalla fight DoF composite
//   67f7f77335ff7a82  240t  cutscene DoF (in-game + map cutscenes)
//
// Confirmed NON-DoF shaders (tested and ruled out):
//   86222c7146665502  110t  god ray additive composite
//   b9335d1fb80b0be1  169t  bloom composite
//   3382665720d48f76  228t  bloom blur ping-pong (x4)
//   b8df44865fe0815d  152t  bloom extraction
//   3790c2aadc3acdcc   73t  CoC / depth pre-pass
//   fae7dab622acff45  135t  pre-rendered cutscene pass
//   8b0d63a81922561b  211t  spatial reconstruction pass
//
// FF13-2 uses two D3D9 devices. Shaders may be created on device 2 but bound
// via device 1's SetPixelShader. DofPtrMap is therefore global (shared across
// all device proxies). Per-proxy m_replacements handle the fact that a shader
// object belongs to one device but the replacement must be created per-device.

// ---------------------------------------------------------------------------
// Replacement data: bytecode loaded from hd_textures_shaders\<hash>.bin
// ---------------------------------------------------------------------------
struct ShaderReplacement {
    std::vector<DWORD> bytecode;
};

// hash -> replacement bytecode (populated at startup, read-only after)
inline std::unordered_map<uint64_t, ShaderReplacement>& DofReplacementMap()
{
    static std::unordered_map<uint64_t, ShaderReplacement> s;
    return s;
}

// original shader ptr -> hash (populated at CreatePixelShader time)
inline std::unordered_map<IDirect3DPixelShader9*, uint64_t>& DofPtrMap()
{
    static std::unordered_map<IDirect3DPixelShader9*, uint64_t> s;
    return s;
}

// Forward declaration — full definition follows later in this file.
class DoFFixer;

// Registry of all live DoFFixer instances (registered in IDirect3DDevice9Proxy ctor/dtor).
// Used by ReloadShaderHashTable to clear per-device replacement caches on hot reload.
inline std::vector<DoFFixer*>& DoFFixerRegistry()
{
    static std::vector<DoFFixer*> s;
    return s;
}

// ---------------------------------------------------------------------------
// LoadShaderHashTable
// Pass the full path to the hd_textures_shaders directory.
// Returns number of shaders successfully loaded, or -1 if hash_table.txt
// is not found (feature stays disabled).
// ---------------------------------------------------------------------------
inline int LoadShaderHashTable(const std::wstring& dir)
{
    std::wstring tablePath = dir + L"\\hash_table.txt";
    std::ifstream f(tablePath.c_str());
    if (!f.is_open()) {
        spdlog::info("DoFFixer: hd_textures_shaders\\hash_table.txt not found — shader replacement disabled");
        return -1;
    }

    int count = 0;
    std::string line;
    while (std::getline(f, line)) {
        // Strip inline comments and whitespace.
        auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        size_t lo = line.find_first_not_of(" \t\r\n");
        if (lo == std::string::npos) continue;
        size_t hi = line.find_last_not_of(" \t\r\n");
        line = line.substr(lo, hi - lo + 1);
        if (line.empty()) continue;

        uint64_t hash = 0;
        try {
            hash = std::stoull(line, nullptr, 16);
        } catch (...) {
            spdlog::warn("DoFFixer: skipping unreadable line in hash_table.txt: {}", line);
            continue;
        }

        // Build path to replacement .bin
        wchar_t binName[32];
        swprintf_s(binName, L"%016llx.bin", static_cast<unsigned long long>(hash));
        std::wstring binPath = dir + L"\\" + binName;

        // Load bytecode
        std::ifstream bin(binPath, std::ios::binary | std::ios::ate);
        if (!bin.is_open()) {
            spdlog::warn("DoFFixer: no replacement .bin found for {:016x} — skipping", hash);
            continue;
        }
        auto size = bin.tellg();
        if (size <= 0 || size % sizeof(DWORD) != 0) {
            spdlog::warn("DoFFixer: invalid .bin size {} for {:016x} — skipping", (long long)size, hash);
            continue;
        }
        bin.seekg(0);
        ShaderReplacement rep;
        rep.bytecode.resize(static_cast<size_t>(size) / sizeof(DWORD));
        bin.read(reinterpret_cast<char*>(rep.bytecode.data()), size);
        if (!bin) {
            spdlog::warn("DoFFixer: read error for {:016x} .bin — skipping", hash);
            continue;
        }

        DofReplacementMap()[hash] = std::move(rep);
        spdlog::info("DoFFixer: registered {:016x} ({} bytes)", hash, (size_t)size);
        ++count;
    }

    spdlog::info("DoFFixer: {} shader replacement(s) loaded — shader replacement {}",
                 count, count > 0 ? "enabled" : "disabled (no valid .bin files)");
    return count;
}

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash over D3D9 pixel shader bytecode.
// ---------------------------------------------------------------------------
static inline uint64_t DoF_HashBytecode(const DWORD* pFunc)
{
    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t h = kOffset;
    const DWORD* p = pFunc;
    for (size_t guard = 0; guard < 16384; ++guard) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
        h ^= b[0]; h *= kPrime;
        h ^= b[1]; h *= kPrime;
        h ^= b[2]; h *= kPrime;
        h ^= b[3]; h *= kPrime;
        if (*p == 0x0000FFFFu) break;
        ++p;
    }
    return h;
}

// ---------------------------------------------------------------------------
// DoFFixer — per-device instance.
//
// OnCreate  : called from CreatePixelShader. Records matching pointers in the
//             global DofPtrMap so every proxy can see them.
// Redirect  : called from SetPixelShader. Looks up the replacement bytecode
//             for the matched hash and creates/caches the replacement shader
//             on this device.
// ---------------------------------------------------------------------------
class DoFFixer {
public:
    DoFFixer() = default;
    ~DoFFixer() {
        for (auto& [ptr, rep] : m_replacements)
            if (rep) rep->Release();
    }

    // Release and clear all per-device compiled replacement shaders.
    // Called by ReloadShaderHashTable so fresh bytecode takes effect immediately.
    void ClearCachedReplacements() {
        for (auto& [ptr, rep] : m_replacements)
            if (rep) rep->Release();
        m_replacements.clear();
    }

    void OnCreate(const DWORD* pFunction, IDirect3DPixelShader9* pShader) {
        if (!pFunction || !pShader) return;
        uint64_t h = DoF_HashBytecode(pFunction);
        // Track if it has a replacement OR is the cutscene detection hash.
        // kCutsceneDofHash is hardcoded so it's tracked even without a .bin.
        bool track = (h == kCutsceneDofHash) ||
                     (!DofReplacementMap().empty() && DofReplacementMap().count(h));
        if (!track) return;
        if (DofPtrMap().count(pShader)) return;
        DofPtrMap()[pShader] = h;
        spdlog::info("DoFFixer: identified {:016x} ptr={:p}", h, static_cast<void*>(pShader));
    }

    IDirect3DPixelShader9* Redirect(IDirect3DPixelShader9* pShader,
                                    IDirect3DDevice9Ex*    pDevice) {
        if (!pShader) return pShader;
        auto it = DofPtrMap().find(pShader);
        if (it == DofPtrMap().end()) return pShader;
        auto& rep = m_replacements[pShader];
        if (!rep) rep = CreateFromBytecode(it->second, pDevice);
        return rep ? rep : pShader;
    }

    // Hash of the cutscene DoF compositor (step 3 of 3).
    // This shader is ONLY used during in-game cutscenes, so detecting it in
    // SetPixelShader gives a reliable cutscene-active signal.
    static constexpr uint64_t kCutsceneDofHash = 0x67f7f77335ff7a82ULL;


    // Returns true if pShader's bytecode hash is the cutscene DoF compositor.
    // Requires the hash to be registered in DofPtrMap (i.e. listed in
    // hash_table.txt with a valid .bin).
    static bool IsCutsceneDofShader(IDirect3DPixelShader9* pShader) {
        if (!pShader) return false;
        auto it = DofPtrMap().find(pShader);
        return it != DofPtrMap().end() && it->second == kCutsceneDofHash;
    }

private:
    // Per-proxy: original shader ptr -> replacement shader for this device.
    std::unordered_map<IDirect3DPixelShader9*, IDirect3DPixelShader9*> m_replacements;

    static IDirect3DPixelShader9* CreateFromBytecode(uint64_t hash,
                                                      IDirect3DDevice9Ex* pDevice) {
        auto it = DofReplacementMap().find(hash);
        if (it == DofReplacementMap().end()) return nullptr;
        IDirect3DPixelShader9* pShader = nullptr;
        HRESULT hr = pDevice->CreatePixelShader(it->second.bytecode.data(), &pShader);
        if (FAILED(hr)) {
            spdlog::warn("DoFFixer: CreatePixelShader for replacement {:016x} failed hr=0x{:08x}",
                         hash, (unsigned)hr);
            return nullptr;
        }
        spdlog::info("DoFFixer: replacement created for {:016x}", hash);
        return pShader;
    }
};

// ---------------------------------------------------------------------------
// ReloadShaderHashTable — hot-reload wrapper around LoadShaderHashTable.
//
// Clears DofReplacementMap and the per-device compiled replacement caches on
// every registered DoFFixer, then re-reads hash_table.txt + .bin files from disk.
//
// DofPtrMap is intentionally NOT cleared: original-shader-ptr → hash associations
// are valid for the lifetime of those D3D9 shader objects. The game won't
// re-call CreatePixelShader for shaders it already holds, so clearing the map
// would permanently lose those associations for the current session.
// ---------------------------------------------------------------------------
inline int ReloadShaderHashTable(const std::wstring& dir)
{
    DofReplacementMap().clear();
    for (DoFFixer* fixer : DoFFixerRegistry())
        if (fixer) fixer->ClearCachedReplacements();
    spdlog::info("DoFFixer: hot reloading shaders from disk...");
    return LoadShaderHashTable(dir);
}
