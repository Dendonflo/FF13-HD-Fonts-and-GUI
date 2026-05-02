#pragma once

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <d3d9.h>

#include "spdlog/spdlog.h"

// Runtime HD texture replacement for FF XIII via content hashing.
// Covers fonts, GUI elements, map tiles, and shop artwork.
//
// This system identifies textures by hashing their pixel data at runtime
// and looking up the hash in a pre-computed database.
//
// Flow:
//   1. At startup, load hash_database.txt (hash -> texture name)
//   2. Scan hd_textures/ folder for HD DDS replacement files
//      - Static namespaces (gui_resident, etc.): pixel data loaded into RAM immediately
//      - Lazy-loaded numbered namespaces (e.g. map_scene): path indexed, pixel data
//        loaded from disk on first access and discarded on flush
//   3. On first SetTexture for each texture, lock it read-only, hash pixels,
//      look up name in database
//   4. If HD replacement exists for that name, create HD texture and swap
//   5. On numbered group change (scene/shop number change): flush old group's
//      HD textures and tracking entries from memory
//
// Ownership:
//   nameToHDTex owns ALL live HD textures (static and numbered alike).
//   textureMap is a non-owning pointer-keyed fast-path cache for all textures.
//   Static textures are in nameToHDTex but never in any group LRU (never evicted).
//   Numbered textures are in nameToHDTex and their group's LRU (evicted normally).
//
// Format-agnostic: works with DXT1, DXT5, etc.
class HDTextureReplacer
{
public:
    // modDir: directory containing d3d9.dll (white_data\prog\win\bin\).
    // Reads hd_textures\lazyload_config.txt for lazy-load prefixes,
    // and hd_textures\hash_database.txt for the hash → name mapping.
    void Init(const std::wstring& modDir);
    // Called from SetTexture hook — identifies texture by hash, swaps if HD available.
    // Needs device pointer to create HD textures on first match.
    IDirect3DBaseTexture9* OnSetTexture(IDirect3DDevice9* pDevice,
                                        IDirect3DBaseTexture9* pTexture);

    void ReleaseTextures();

    // Called from CreateTexture hook — evicts stale cache entries for reused pointers.
    void InvalidateTexture(IDirect3DBaseTexture9* pTexture);

private:
    struct HDTextureData {
        UINT hdW, hdH;
        D3DFORMAT format;
        std::vector<uint8_t> pixelData;
    };

    // Per-numbered-namespace group: owns its LRU state and current active number.
    // All numbered namespaces sharing the same prefix (e.g. "map_scene") form one group;
    // the group tracks which suffix (e.g. "00023") is currently active.
    struct NumberedGroup {
        std::string currentNumber;   // active numeric suffix, e.g. "00023" or "02"
        std::list<std::string>                            lruOrder;
        std::unordered_map<std::string,
            std::list<std::string>::iterator>             lruIndex;
        size_t lruCap = 0;           // 0 = not yet initialised; set on first encounter
    };

    // Pre-computed hash -> texture name
    std::unordered_map<uint64_t, std::string> hashDB;

    // name -> HD replacement data
    // Populated at startup for static + preloaded-numbered namespaces;
    // populated on demand for lazy-loaded namespaces (map scenes).
    std::unordered_map<std::string, HDTextureData> hdData;

    // original texture pointer -> HD texture (non-owning fast-path for ALL textures).
    // nameToHDTex is the sole owner; textureMap is just a pointer-keyed lookup cache.
    std::unordered_map<IDirect3DBaseTexture9*, IDirect3DTexture9*> textureMap;

    // Textures already checked (no match or already mapped)
    std::unordered_set<IDirect3DBaseTexture9*> checkedTextures;

    // game pointer -> texture key ("namespace/name")
    std::unordered_map<IDirect3DBaseTexture9*, std::string> pointerKey;

    // Disk paths for lazy-loaded tiles
    // key -> full DDS path on disk.
    std::unordered_map<std::string, std::wstring> lazyPaths;

    // Set of namespace prefixes (digits stripped) that are lazy-loaded from disk.
    // Populated at Init() from hd_textures/lazyload_config.txt.
    std::unordered_set<std::string> lazyPrefixes;

    // Per-prefix VRAM LRU cap, read from lazyload_config.txt.
    // Falls back to LruCapDefault() if no entry for the prefix.
    std::unordered_map<std::string, size_t> lazyLruCaps;

    // Texture name -> live D3D9 texture (sole owner for ALL textures, static and numbered).
    // Static textures live here forever (until ReleaseTextures); numbered tiles are also
    // managed by their group's LRU and released on eviction or flush.
    std::unordered_map<std::string, IDirect3DTexture9*> nameToHDTex;

    // Active numbered groups, keyed by prefix (e.g. "map_scene", "shop_").
    // Each group owns its LRU and tracks its current active number.
    std::unordered_map<std::string, NumberedGroup> numberedGroups;

    // -----------------------------------------------------------------------
    // Namespace classification helpers
    // -----------------------------------------------------------------------

    // Returns true if ns is a numbered namespace:
    //   "…scene[0-9]+"  e.g. "map_scene00023", "gui_scene00004"
    //   "…_[0-9]+"      e.g. "shop_02", "foo_01"
    static bool IsNumberedNamespace(const std::string& ns);

    // Returns true if the key's namespace is numbered.
    static bool IsNumberedTile(const std::string& key);

    // Split a numbered namespace into (prefix, number).
    //   "map_scene00023" -> {"map_scene", "00023"}
    //   "shop_02"        -> {"shop_",     "02"}
    static std::pair<std::string, std::string>
        SplitNumberedNamespace(const std::string& ns);

    // Returns true if tiles in this namespace should be lazy-loaded from disk.
    // ns may be a full namespace ("map_scene00023") or bare prefix ("map_scene") —
    // trailing digits are stripped before checking against lazyPrefixes.
    bool ShouldLazyLoad(const std::string& ns) const;

    // VRAM LRU cap for a given prefix.
    // Checks lazyLruCaps first; falls back to a heuristic default.
    size_t LruCapForPrefix(const std::string& prefix) const;

    // -----------------------------------------------------------------------
    // Core operations
    // -----------------------------------------------------------------------

    // Upload HD pixel data and return a new D3D9 texture (caller owns it).
    IDirect3DTexture9* CreateHDTexture(IDirect3DDevice9* pDevice,
                                       const std::string& texName);

    // Release all HD textures and tracking entries belonging to the currently
    // active namespace of a group (prefix + group.currentNumber).
    // Frees lazy-loaded pixel data from hdData; keeps preloaded pixel data.
    void FlushGroup(const std::string& prefix, NumberedGroup& group);

    // Evict the least-recently-used tile from a group to reclaim VRAM.
    void EvictOldest(const std::string& prefix, NumberedGroup& group);

    void ScanHDSubdir(const std::wstring& subDirPath, const std::string& prefix);

    static bool ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                        D3DFORMAT& format, std::vector<uint8_t>& pixelData);

    static uint64_t FNV1a64(const uint8_t* data, size_t len,
                             uint64_t h = 14695981039346656037ULL);

    static bool IsBlockCompressed(D3DFORMAT format);
    static UINT GetBytesPerBlock(D3DFORMAT format);
    static UINT GetBytesPerPixel(D3DFORMAT format);
    static UINT ComputeRowPitch(D3DFORMAT format, UINT width);
    static UINT ComputeRowCount(D3DFORMAT format, UINT height);
};


inline uint64_t HDTextureReplacer::FNV1a64(const uint8_t* data, size_t len, uint64_t h)
{
    for (size_t i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}


inline bool HDTextureReplacer::IsBlockCompressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 ||
           format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

inline UINT HDTextureReplacer::GetBytesPerBlock(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_DXT1: return 8;
        case D3DFMT_DXT2: case D3DFMT_DXT3: return 16;
        case D3DFMT_DXT4: case D3DFMT_DXT5: return 16;
        default: return 0;
    }
}

inline UINT HDTextureReplacer::GetBytesPerPixel(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
        case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: return 2;
        case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: return 2;
        case D3DFMT_A8: case D3DFMT_L8: return 1;
        case D3DFMT_A8L8: return 2;
        default: return 0;
    }
}

inline UINT HDTextureReplacer::ComputeRowPitch(D3DFORMAT format, UINT width)
{
    if (IsBlockCompressed(format))
        return ((width + 3) / 4) * GetBytesPerBlock(format);
    UINT bpp = GetBytesPerPixel(format);
    return bpp ? width * bpp : 0;
}

inline UINT HDTextureReplacer::ComputeRowCount(D3DFORMAT format, UINT height)
{
    if (IsBlockCompressed(format))
        return (height + 3) / 4;
    return height;
}


// -----------------------------------------------------------------------
// Namespace classification
// -----------------------------------------------------------------------

inline bool HDTextureReplacer::IsNumberedNamespace(const std::string& ns)
{
    if (ns.empty()) return false;
    size_t i = ns.size();
    while (i > 0 && std::isdigit((unsigned char)ns[i - 1])) --i;
    return i < ns.size(); // true if at least one trailing digit exists
}

inline bool HDTextureReplacer::IsNumberedTile(const std::string& key)
{
    auto slash = key.find('/');
    if (slash == std::string::npos) return false;
    return IsNumberedNamespace(key.substr(0, slash));
}

inline std::pair<std::string, std::string>
HDTextureReplacer::SplitNumberedNamespace(const std::string& ns)
{
    size_t i = ns.size();
    while (i > 0 && std::isdigit((unsigned char)ns[i - 1])) --i;
    return { ns.substr(0, i), ns.substr(i) };
}

inline bool HDTextureReplacer::ShouldLazyLoad(const std::string& ns) const
{
    // ns may be a full namespace ("map_scene00023") or a bare prefix ("map_scene").
    // Strip trailing digits before checking — the set stores prefixes only.
    return lazyPrefixes.count(SplitNumberedNamespace(ns).first) > 0;
}

inline size_t HDTextureReplacer::LruCapForPrefix(const std::string& prefix) const
{
    auto it = lazyLruCaps.find(prefix);
    if (it != lazyLruCaps.end()) return it->second;
    // Heuristic fallback: map scenes have up to 63 tiles, 128 is comfortable headroom.
    if (prefix.size() >= 5 && prefix.substr(prefix.size() - 5) == "scene")
        return 128;
    return 32;
}


// -----------------------------------------------------------------------
// Init — load hash database + scan hd_textures/ subdirectories
// -----------------------------------------------------------------------
static std::string StripDDSExtension(const std::string& fname)
{
    size_t pos = fname.find(".txbh.dds");
    if (pos != std::string::npos) return fname.substr(0, pos);
    pos = fname.rfind(".dds");
    if (pos != std::string::npos) return fname.substr(0, pos);
    return fname;
}

inline void HDTextureReplacer::ScanHDSubdir(const std::wstring& subDirPath,
                                             const std::string& prefix)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((subDirPath + L"\\*.dds").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    const bool isNumbered = IsNumberedNamespace(prefix);
    const bool lazy       = isNumbered && ShouldLazyLoad(prefix);
    int count = 0;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring filePath = subDirPath + L"\\" + fd.cFileName;
        std::wstring fnameW   = fd.cFileName;
        std::string  fname(fnameW.begin(), fnameW.end());
        std::string  key = prefix + "/" + StripDDSExtension(fname);

        if (lazy)
        {
            // Map scene tile: record path only — pixel data loaded on first access.
            lazyPaths[key] = filePath;
        }
        else
        {
            // Static or preloaded-numbered (shops): load pixel data into RAM now.
            UINT hdW, hdH;
            D3DFORMAT format;
            std::vector<uint8_t> pixels;
            if (!ReadDDS(filePath, hdW, hdH, format, pixels)) continue;

            HDTextureData hd;
            hd.hdW = hdW; hd.hdH = hdH;
            hd.format = format;
            hd.pixelData = std::move(pixels);

            spdlog::debug("HDTextures: HD texture '{}' loaded ({}x{}, {} bytes)",
                          key, hdW, hdH, hd.pixelData.size());
            hdData[key] = std::move(hd);
        }
        ++count;

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (lazy)
        spdlog::info("HDTextures: map scene '{}': {} tile(s) indexed for lazy load",
                     prefix, count);
}

inline void HDTextureReplacer::Init(const std::wstring& modDir)
{
    std::wstring hdRoot = modDir + L"\\hd_textures";

    // Read lazy-load prefixes from hd_textures\lazyload_config.txt.
    // Each non-empty, non-comment line is one prefix (e.g. "map_scene").
    {
        std::wstring cfgPath = hdRoot + L"\\lazyload_config.txt";
        std::ifstream cfg(cfgPath);
        if (!cfg.is_open())
        {
            spdlog::info("HDTextures: no lazyload_config.txt found, all namespaces preloaded");
        }
        else
        {
            std::string line;
            while (std::getline(cfg, line))
            {
                // Strip carriage return (Windows line endings)
                if (!line.empty() && line.back() == '\r') line.pop_back();
                // Strip inline comment
                auto hash = line.find('#');
                if (hash != std::string::npos) line = line.substr(0, hash);

                std::istringstream iss(line);
                std::string prefix;
                if (!(iss >> prefix)) continue;

                lazyPrefixes.insert(prefix);

                size_t cap;
                if (iss >> cap)
                    lazyLruCaps[prefix] = cap;
            }
        }
    }

    if (!lazyPrefixes.empty())
    {
        std::string joined;
        for (auto& p : lazyPrefixes)
        {
            auto capIt = lazyLruCaps.find(p);
            std::string entry = p;
            if (capIt != lazyLruCaps.end())
                entry += "(cap=" + std::to_string(capIt->second) + ")";
            joined += (joined.empty() ? "" : ", ") + entry;
        }
        spdlog::info("HDTextures: lazy-load prefixes: {}", joined);
    }

    std::wstring hashDBPath = hdRoot + L"\\hash_database.txt";
    {
        std::ifstream f(hashDBPath);
        if (!f.is_open())
        {
            spdlog::info("HDTextures: no hash_database.txt found, texture replacement disabled");
            return;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string hashStr, name;
            iss >> hashStr >> name;
            if (hashStr.empty() || name.empty()) continue;

            uint64_t hash = std::strtoull(hashStr.c_str(), nullptr, 16);
            hashDB[hash] = name;
        }
        spdlog::info("HDTextures: loaded {} entries from hash database", hashDB.size());
    }

    DWORD attr = GetFileAttributesW(hdRoot.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        spdlog::info("HDTextures: no hd_textures directory found");
        return;
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((hdRoot + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring subDirPath = hdRoot + L"\\" + fd.cFileName;
        std::wstring subNameW   = fd.cFileName;
        std::string  subName(subNameW.begin(), subNameW.end());

        ScanHDSubdir(subDirPath, subName);

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!hdData.empty())
        spdlog::info("HDTextures: {} HD texture(s) available for replacement", hdData.size());
}


// -----------------------------------------------------------------------
// OnSetTexture — identify texture by hash, swap if HD replacement available
// -----------------------------------------------------------------------
inline IDirect3DBaseTexture9* HDTextureReplacer::OnSetTexture(IDirect3DDevice9* pDevice,
                                                              IDirect3DBaseTexture9* pTexture)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    // Fast path: already mapped to an HD texture.
    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end())
        return mapIt->second;

    // Already checked with no match — skip.
    if (checkedTextures.count(pTexture))
        return pTexture;

    checkedTextures.insert(pTexture);

    if (pTexture->GetType() != D3DRTYPE_TEXTURE)
        return pTexture;

    IDirect3DTexture9* tex = static_cast<IDirect3DTexture9*>(pTexture);

    D3DSURFACE_DESC desc;
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return pTexture;

    UINT rowPitch = ComputeRowPitch(desc.Format, desc.Width);
    UINT rowCount = ComputeRowCount(desc.Format, desc.Height);
    if (rowPitch == 0 || rowCount == 0)
        return pTexture;

    D3DLOCKED_RECT locked;
    if (FAILED(tex->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        return pTexture;

    uint64_t h = 14695981039346656037ULL;
    const uint8_t* bits = static_cast<const uint8_t*>(locked.pBits);
    for (UINT row = 0; row < rowCount; row++)
        h = FNV1a64(bits + row * locked.Pitch, rowPitch, h);

    tex->UnlockRect(0);

    auto dbIt = hashDB.find(h);
    if (dbIt == hashDB.end())
        return pTexture;

    const std::string& texName = dbIt->second;

    // Track the prefix so we can manage the right group, without splitting twice.
    std::string numberedPrefix;

    if (IsNumberedTile(texName))
    {
        const std::string ns = texName.substr(0, texName.find('/'));
        auto [pfx, num] = SplitNumberedNamespace(ns);
        numberedPrefix = pfx;

        // Initialise group on first encounter.
        auto& group = numberedGroups[pfx];
        if (group.lruCap == 0)
            group.lruCap = LruCapForPrefix(pfx);

        // Flush the old namespace when the active number changes.
        if (!group.currentNumber.empty() && group.currentNumber != num)
        {
            spdlog::debug("HDTextures: numbered group switching '{}{}' -> '{}{}'",
                          pfx, group.currentNumber, pfx, num);
            FlushGroup(pfx, group);
        }
        group.currentNumber = num;
    }

    // Check nameToHDTex for ALL textures — static and numbered alike.
    // If the HD texture is already resident (e.g. game reloaded at a new address),
    // reuse it — no disk read, no GPU upload.
    {
        auto nameIt = nameToHDTex.find(texName);
        if (nameIt != nameToHDTex.end())
        {
            // Touch LRU for numbered tiles.
            if (!numberedPrefix.empty())
            {
                auto& group = numberedGroups[numberedPrefix];
                auto lruIt = group.lruIndex.find(texName);
                if (lruIt != group.lruIndex.end())
                {
                    group.lruOrder.erase(lruIt->second);
                    group.lruOrder.push_front(texName);
                    group.lruIndex[texName] = group.lruOrder.begin();
                }
            }

            textureMap[pTexture] = nameIt->second;   // non-owning fast-path
            pointerKey[pTexture] = texName;
            return nameIt->second;
        }
    }

    // Not yet resident: lazy-load pixel data from disk for lazy-loaded numbered namespaces.
    // Preloaded namespaces (shops) and static namespaces already have data in hdData.
    if (!numberedPrefix.empty() && hdData.find(texName) == hdData.end())
    {
        auto pathIt = lazyPaths.find(texName);
        if (pathIt != lazyPaths.end())
        {
            UINT hdW, hdH;
            D3DFORMAT format;
            std::vector<uint8_t> pixels;
            if (ReadDDS(pathIt->second, hdW, hdH, format, pixels))
            {
                HDTextureData hd;
                hd.hdW = hdW; hd.hdH = hdH;
                hd.format = format;
                hd.pixelData = std::move(pixels);
                spdlog::debug("HDTextures: lazy-loaded map tile '{}' ({}x{})",
                              texName, hdW, hdH);
                hdData[texName] = std::move(hd);
            }
        }
    }

    IDirect3DTexture9* hdTex = CreateHDTexture(pDevice, texName);
    if (!hdTex)
        return pTexture;

    // nameToHDTex owns ALL HD textures — static and numbered alike.
    nameToHDTex[texName] = hdTex;
    textureMap[pTexture]  = hdTex;   // non-owning fast-path
    pointerKey[pTexture]  = texName;

    // Numbered tiles: register in group LRU and evict if over cap.
    if (!numberedPrefix.empty())
    {
        auto& group = numberedGroups[numberedPrefix];
        group.lruOrder.push_front(texName);
        group.lruIndex[texName] = group.lruOrder.begin();

        while (group.lruOrder.size() > group.lruCap)
            EvictOldest(numberedPrefix, group);
    }

    spdlog::debug("HDTextures: '{}' matched by hash {:016x}, swapped to HD ({}x{} -> {}x{})",
                  texName, h, desc.Width, desc.Height,
                  hdData.at(texName).hdW, hdData.at(texName).hdH);

    return hdTex;
}


// -----------------------------------------------------------------------
// ReleaseTextures — called on device reset; release all D3D9 objects
// -----------------------------------------------------------------------
inline void HDTextureReplacer::ReleaseTextures()
{
    // nameToHDTex owns ALL HD textures (static and numbered) — release all here.
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();

    // textureMap is non-owning — just clear, no Release.
    textureMap.clear();

    // Reset per-group LRU state. Keep groups and their caps registered
    // so they are ready immediately after device reset without re-init.
    for (auto& [pfx, group] : numberedGroups)
    {
        group.lruOrder.clear();
        group.lruIndex.clear();
        group.currentNumber.clear();
    }

    checkedTextures.clear();
    pointerKey.clear();

    // Free lazily-loaded pixel data (map scenes).
    // Preloaded pixel data (shops, gui_resident) is kept — it came from Init().
    for (auto it = hdData.begin(); it != hdData.end(); )
        it = lazyPaths.count(it->first) ? hdData.erase(it) : std::next(it);
}


// -----------------------------------------------------------------------
// InvalidateTexture — evict stale entries when a D3D9 pointer is reused
// -----------------------------------------------------------------------
inline void HDTextureReplacer::InvalidateTexture(IDirect3DBaseTexture9* pTexture)
{
    // textureMap is non-owning for ALL textures — just remove the entry, no Release.
    // nameToHDTex remains the owner; the HD texture stays resident for future reuse.
    textureMap.erase(pTexture);
    checkedTextures.erase(pTexture);
    pointerKey.erase(pTexture);
}


// -----------------------------------------------------------------------
// FlushGroup — release all HD textures for a group's current namespace
// -----------------------------------------------------------------------
inline void HDTextureReplacer::FlushGroup(const std::string& prefix, NumberedGroup& group)
{
    if (group.currentNumber.empty()) return;

    // Build the namespace prefix used to identify tiles belonging to this group.
    // e.g. prefix="map_scene", currentNumber="00023" -> "map_scene00023/"
    const std::string nsPrefix = prefix + group.currentNumber + "/";

    // Release HD textures (nameToHDTex is owner for all numbered tiles).
    std::vector<std::string> toRelease;
    for (auto& [name, tex] : nameToHDTex)
    {
        if (name.size() >= nsPrefix.size() &&
            name.compare(0, nsPrefix.size(), nsPrefix) == 0)
        {
            if (tex) tex->Release();
            toRelease.push_back(name);
        }
    }
    for (auto& name : toRelease)
    {
        nameToHDTex.erase(name);

        auto lruIt = group.lruIndex.find(name);
        if (lruIt != group.lruIndex.end())
        {
            group.lruOrder.erase(lruIt->second);
            group.lruIndex.erase(lruIt);
        }

        // Free pixel data only for lazy-loaded tiles (map scenes).
        // Preloaded pixel data (shops) stays in hdData permanently.
        if (lazyPaths.count(name))
            hdData.erase(name);
    }

    // Clean up pointer tracking entries (non-owning for numbered tiles, no Release).
    std::vector<IDirect3DBaseTexture9*> ptrsToRemove;
    for (auto& [ptr, key] : pointerKey)
    {
        if (key.size() >= nsPrefix.size() &&
            key.compare(0, nsPrefix.size(), nsPrefix) == 0)
            ptrsToRemove.push_back(ptr);
    }
    for (auto ptr : ptrsToRemove)
    {
        textureMap.erase(ptr);
        checkedTextures.erase(ptr);
        pointerKey.erase(ptr);
    }

    spdlog::debug("HDTextures: flushed '{}{}' ({} HD texture(s), {} pointer(s) removed)",
                  prefix, group.currentNumber, toRelease.size(), ptrsToRemove.size());
}


// -----------------------------------------------------------------------
// EvictOldest — evict least-recently-used tile from a group to free VRAM
// -----------------------------------------------------------------------
inline void HDTextureReplacer::EvictOldest(const std::string& prefix, NumberedGroup& group)
{
    if (group.lruOrder.empty()) return;

    const std::string name = group.lruOrder.back();
    group.lruOrder.pop_back();
    group.lruIndex.erase(name);

    // Release the HD texture (nameToHDTex is owner).
    auto hdTexIt = nameToHDTex.find(name);
    if (hdTexIt != nameToHDTex.end())
    {
        if (hdTexIt->second) hdTexIt->second->Release();
        nameToHDTex.erase(hdTexIt);
    }

    // Remove all pointer tracking entries for this tile (non-owning, no Release).
    std::vector<IDirect3DBaseTexture9*> toRemove;
    for (auto& [ptr, key] : pointerKey)
        if (key == name) toRemove.push_back(ptr);

    for (auto ptr : toRemove)
    {
        textureMap.erase(ptr);
        checkedTextures.erase(ptr);
        pointerKey.erase(ptr);
    }

    // Pixel data kept in hdData:
    //   - Lazy tiles (maps): allows fast VRAM recreation without a disk read if revisited.
    //   - Preloaded tiles (shops): already permanently resident, nothing to do.
    spdlog::debug("HDTextures: LRU evicted '{}' from '{}' ({} pointer(s) removed)",
                  name, prefix, toRemove.size());
}


// -----------------------------------------------------------------------
// CreateHDTexture — upload pixel data to VRAM, return new D3D9 texture
// -----------------------------------------------------------------------
inline IDirect3DTexture9* HDTextureReplacer::CreateHDTexture(IDirect3DDevice9* pDevice,
                                                              const std::string& texName)
{
    auto hdIt = hdData.find(texName);
    if (hdIt == hdData.end()) return nullptr;
    const HDTextureData& hd = hdIt->second;

    IDirect3DTexture9* hdTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                        hd.format, D3DPOOL_MANAGED,
                                        &hdTex, nullptr);
    if (FAILED(hr))
    {
        spdlog::error("HDTextures: failed to create HD texture for '{}' (hr=0x{:08X})",
                      texName, (unsigned)hr);
        return nullptr;
    }

    D3DLOCKED_RECT hdLocked;
    hr = hdTex->LockRect(0, &hdLocked, nullptr, 0);
    if (FAILED(hr))
    {
        spdlog::error("HDTextures: failed to lock HD texture for '{}' (hr=0x{:08X})",
                      texName, (unsigned)hr);
        hdTex->Release();
        return nullptr;
    }

    UINT hdRowPitch = ComputeRowPitch(hd.format, hd.hdW);
    UINT hdRowCount = ComputeRowCount(hd.format, hd.hdH);
    const uint8_t* src = hd.pixelData.data();
    for (UINT row = 0; row < hdRowCount; row++)
        memcpy(static_cast<uint8_t*>(hdLocked.pBits) + row * hdLocked.Pitch,
               src + row * hdRowPitch, hdRowPitch);

    hdTex->UnlockRect(0);
    return hdTex;
}


// -----------------------------------------------------------------------
// ReadDDS — parse DDS header for dimensions and format, read pixel data
// -----------------------------------------------------------------------
inline bool HDTextureReplacer::ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                                       D3DFORMAT& format, std::vector<uint8_t>& pixelData)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    uint8_t header[128];
    f.read(reinterpret_cast<char*>(header), 128);
    if (f.gcount() != 128) return false;
    if (memcmp(header, "DDS ", 4) != 0) return false;

    height = *reinterpret_cast<uint32_t*>(header + 12);
    width  = *reinterpret_cast<uint32_t*>(header + 16);

    uint32_t fourCC      = *reinterpret_cast<uint32_t*>(header + 84);
    uint32_t pfFlags     = *reinterpret_cast<uint32_t*>(header + 80);
    uint32_t rgbBitCount = *reinterpret_cast<uint32_t*>(header + 88);

    if (fourCC == 0x31545844)      // "DXT1"
        format = D3DFMT_DXT1;
    else if (fourCC == 0x33545844) // "DXT3"
        format = D3DFMT_DXT3;
    else if (fourCC == 0x35545844) // "DXT5"
        format = D3DFMT_DXT5;
    else if (fourCC == 0 && (pfFlags & 0x40)) // DDPF_RGB
    {
        if (rgbBitCount == 32)
            format = D3DFMT_A8R8G8B8;
        else if (rgbBitCount == 16)
            format = D3DFMT_A4R4G4B4;
        else
        {
            spdlog::warn("HDTextures: unsupported RGB bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x20000)) // DDPF_LUMINANCE
    {
        if (rgbBitCount == 8)
            format = D3DFMT_L8;
        else
        {
            spdlog::warn("HDTextures: unsupported luminance bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x2)) // DDPF_ALPHA
    {
        if (rgbBitCount == 8)
            format = D3DFMT_A8;
        else
        {
            spdlog::warn("HDTextures: unsupported alpha bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else
    {
        spdlog::warn("HDTextures: unsupported DDS format (fourCC=0x{:08X}, flags=0x{:08X})",
                     fourCC, pfFlags);
        return false;
    }

    f.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(f.tellg());
    size_t dataSize = fileSize - 128;

    pixelData.resize(dataSize);
    f.seekg(128);
    f.read(reinterpret_cast<char*>(pixelData.data()), dataSize);

    return f.gcount() == static_cast<std::streamsize>(dataSize);
}
