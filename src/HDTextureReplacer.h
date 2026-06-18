#pragma once

#include <string>
#include <vector>
#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <mutex>
#include <atomic>
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
    HDTextureReplacer()  = default;
    ~HDTextureReplacer() { StopPreloadThread(); StopHashThread(); }

    // modDir: directory containing d3d9.dll (white_data\prog\win\bin\).
    // Reads hd_textures\lazyload_config.txt for lazy-load prefixes,
    // and hd_textures\hash_database.txt for the hash → name mapping.
    void Init(const std::wstring& modDir);
    // Fast-path read-only lookup — called under shared (SRW read) lock.
    // Returns the texture to bind (HD replacement or original) if the result is
    // already cached, or nullptr if the texture hasn't been seen before and a full
    // OnSetTexture pass is needed (which will acquire the exclusive lock).
    IDirect3DBaseTexture9* TryFastPath(IDirect3DBaseTexture9* pTexture);

    // Called from SetTexture hook — identifies texture by hash, swaps if HD available.
    // currentTextures: proxy stage->pointer map for proactive HD push.
    // Always called under exclusive (SRW write) lock.
    IDirect3DBaseTexture9* OnSetTexture(
        IDirect3DDevice9* pDevice,
        IDirect3DBaseTexture9* pTexture,
        const std::unordered_map<DWORD, IDirect3DBaseTexture9*>& currentTextures);

    void ReleaseTextures();

    // Called from CreateTexture hook — evicts stale cache entries for reused pointers.
    void InvalidateTexture(IDirect3DBaseTexture9* pTexture);

    // Called from the CostumeTracker callback (under g_hdTexCS) to swap the pixel
    // data for one gui_resident face texture without a full reload.
    // Releases the existing GPU texture for texName so it will be re-uploaded on the
    // next SetTexture call that references it. Leaves the original-pointer caches
    // intact — they are invalidated naturally when the game re-binds the texture.
    void SwapCostumeTexture(const std::string& texName,
                            UINT hdW, UINT hdH, D3DFORMAT format,
                            std::vector<uint8_t> pixels);

    // Parse DDS header + pixel data from disk. Public so dllmain's costume callback
    // can call it outside the critical section before entering to do the swap.
    static bool ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                        D3DFORMAT& format, std::vector<uint8_t>& pixelData);

#ifdef HDTEX_DUMP_TEXTURES
    // Set output directory for HDTEX_DUMP_TEXTURES. Called once from DllMain.
    static void SetTexDumpDir(const std::wstring& dir);
private:
    static std::wstring& TexDumpDir();
    static void DumpTextureDDS(uint64_t hash, D3DFORMAT fmt, UINT w, UINT h,
                               const void* pBits, UINT pitch,
                               UINT rowPitch, UINT rowCount);
public:
#endif

#ifdef HDTEX_HOT_RELOAD
    // Release all GPU textures and pixel data, then re-read DDS files from disk.
    // Called periodically by the hot-reload background thread so in-progress texture
    // edits become visible in-game without a restart.
    void HotReload();
#endif

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

    // Root path of hd_textures\ — stored so HotReload() can re-scan without re-running Init().
    std::wstring m_hdRoot;

    // -----------------------------------------------------------------------
    // Async tile preloader
    //
    // Disk I/O for lazy-loaded map tiles is moved off the render thread.
    // When OnSetTexture detects a namespace switch, QueueNamespacePreload()
    // immediately posts all tiles for the incoming namespace to the background
    // thread. The render thread only does fast GPU uploads (CreateTexture +
    // UpdateTexture) once data arrives in preloadReady_; it never calls
    // ReadDDS inline.
    //
    // Lock ordering (never hold both simultaneously from the same thread
    // except: SRWLock(exclusive) → preloadMtx_ is allowed since the preload
    // thread never acquires the SRWLock):
    //   Render thread:  SRWLock (via dllmain) → preloadMtx_ (brief)
    //   Preload thread: preloadMtx_ only
    // -----------------------------------------------------------------------
    struct PreloadJob {
        std::string  texName;
        std::wstring filePath;
    };

    std::mutex                              preloadMtx_;
    std::deque<PreloadJob>                  preloadJobs_;
    std::unordered_set<std::string>         preloadQueued_;  // in queue or being loaded
    std::unordered_map<std::string,
                       HDTextureData>       preloadReady_;   // loaded, awaiting GPU upload
    HANDLE                                  preloadThread_   = nullptr;
    HANDLE                                  preloadSemaphore_= nullptr;
    std::atomic<bool>                       preloadStop_     { false };

    void StartPreloadThread();
    void StopPreloadThread();
    void QueueTilePreload(const std::string& texName, const std::wstring& path);
    void QueueNamespacePreload(const std::string& prefix, const std::string& number);
    static DWORD WINAPI PreloadThreadProc(LPVOID pThis);

    // -----------------------------------------------------------------------
    // Async hash thread
    //
    // LockRect + FNV1a hash + hashDB lookup are moved off the render thread so
    // it never stalls waiting for hash computation of world/streaming textures.
    //
    // Flow:
    //   QueueHashJob  — render thread pushes first-seen textures (AddRef'd)
    //   HashThreadProc — does GetLevelDesc, LockRect, hash, UnlockRect, hashDB lookup
    //   ConsumeHashResults — render thread (exclusive lock): GPU upload for matches,
    //                        checkedTextures for misses, Release of AddRef'd refs
    //
    // Pointer recycling: each job carries a unique jobId.  If InvalidateTexture fires
    // before a result is consumed, the jobId in pendingJobId_ is erased so the stale
    // result is discarded when it arrives.
    //
    // Lock ordering: SRWLock(exclusive) -> hashMtx_.  The hash thread NEVER
    // acquires the SRWLock so this ordering is never reversed.
    // -----------------------------------------------------------------------
    struct HashJob {
        IDirect3DBaseTexture9* pTex;   // AddRef'd by QueueHashJob
        uint64_t               jobId;
    };
    struct HashResult {
        IDirect3DBaseTexture9* pTex;   // still AddRef'd; ConsumeHashResults releases
        uint64_t               jobId;
        bool                   match;
        std::string            texName; // valid iff match
        uint64_t               hash;
        D3DFORMAT              fmt;
        UINT                   w, h;
    };

    std::mutex                                              hashMtx_;
    std::deque<HashJob>                                     hashJobs_;
    std::deque<HashResult>                                  hashResults_;
    // pTex -> jobId for textures currently in the hash pipeline.
    // Written/read exclusively under SRWLock exclusive; never touched by hash thread.
    std::unordered_map<IDirect3DBaseTexture9*, uint64_t>    pendingJobId_;
    uint64_t                                                nextJobId_ = 0;
    // pTex -> texName for textures that matched hashDB but whose lazy DDS data is
    // still being read by the preload thread. These pointers are AddRef'd.
    std::unordered_map<IDirect3DBaseTexture9*, std::string> preloadWaiting_;
    HANDLE                                                  hashThread_    = nullptr;
    HANDLE                                                  hashSemaphore_ = nullptr;
    std::atomic<bool>                                       hashStop_      { false };
    // Set by hash thread (and preload thread) whenever they push a result.
    // Cleared by ConsumeHashResults after stealing the queue.
    // TryFastPath checks this to force the exclusive-lock path on stable scenes.
    std::atomic<bool>                                       hashResultsReady_ { false };

    void StartHashThread();
    void StopHashThread();
    void QueueHashJob(IDirect3DBaseTexture9* pTex);        // under SRWLock exclusive
    void ConsumeHashResults(IDirect3DDevice9* pDevice,
        const std::unordered_map<DWORD, IDirect3DBaseTexture9*>& currentTextures); // under SRWLock exclusive
    static DWORD WINAPI HashThreadProc(LPVOID pThis);

#ifdef HDTEX_HOT_RELOAD
    // Snapshot of path → last-write-time for every file and subdir under hd_textures\.
    // HotReload() compares against this to skip the expensive reload when nothing changed.
    std::unordered_map<std::wstring, FILETIME> m_diskMtimes;
#endif

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
    void RescanDisk();
    void LoadLazyConfig();
    bool LoadHashDB();

#ifdef HDTEX_HOT_RELOAD
    // Record modification times of all files and subdirs under hd_textures\
    // so the next HotReload() cycle can detect whether anything actually changed.
    void RecordDiskMtimes();
    // Returns true if any recorded path has been modified, added, or removed since
    // the last RecordDiskMtimes() call.
    bool HasDiskChanges() const;
#endif

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
    m_hdRoot = modDir + L"\\hd_textures";

    LoadLazyConfig();

    if (!LoadHashDB())
        return;

    DWORD attr = GetFileAttributesW(m_hdRoot.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        spdlog::info("HDTextures: no hd_textures directory found");
        return;
    }

    RescanDisk();

    if (!hdData.empty())
        spdlog::info("HDTextures: {} HD texture(s) available for replacement", hdData.size());

    // Start background preload thread for lazy-loaded namespaces.
    if (!lazyPrefixes.empty())
        StartPreloadThread();

    // Start background hash thread.  Every first-seen texture is hashed off the
    // render thread, eliminating LockRect/FNV1a stalls during world streaming.
    if (!hashDB.empty())
        StartHashThread();

#ifdef HDTEX_HOT_RELOAD
    RecordDiskMtimes();
#endif
}


// -----------------------------------------------------------------------
// LoadLazyConfig — read hd_textures\lazyload_config.txt into lazyPrefixes +
// lazyLruCaps. Safe to call on an already-populated instance; simply adds new
// entries (call site should clear first for a full hot reload).
// -----------------------------------------------------------------------
inline void HDTextureReplacer::LoadLazyConfig()
{
    std::wstring cfgPath = m_hdRoot + L"\\lazyload_config.txt";
    std::ifstream cfg(cfgPath);
    if (!cfg.is_open())
    {
        spdlog::info("HDTextures: no lazyload_config.txt found, all namespaces preloaded");
        return;
    }

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
}


// -----------------------------------------------------------------------
// LoadHashDB — read hd_textures\hash_database.txt into hashDB.
// Returns false if the file is absent (texture replacement stays disabled).
// -----------------------------------------------------------------------
inline bool HDTextureReplacer::LoadHashDB()
{
    std::wstring hashDBPath = m_hdRoot + L"\\hash_database.txt";
    std::ifstream f(hashDBPath);
    if (!f.is_open())
    {
        spdlog::info("HDTextures: no hash_database.txt found, texture replacement disabled");
        return false;
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
    return true;
}


// -----------------------------------------------------------------------
// RescanDisk — iterate hd_textures\ subdirs and call ScanHDSubdir for each.
// Safe to call multiple times: ScanHDSubdir overwrites existing hdData/lazyPaths
// entries, so the result is always up-to-date with disk.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::RescanDisk()
{
    if (m_hdRoot.empty()) return;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((m_hdRoot + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring subDirPath = m_hdRoot + L"\\" + fd.cFileName;
        std::wstring subNameW   = fd.cFileName;
        std::string  subName(subNameW.begin(), subNameW.end());

        ScanHDSubdir(subDirPath, subName);

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}


#ifdef HDTEX_HOT_RELOAD
// -----------------------------------------------------------------------
// RecordDiskMtimes — snapshot last-write times of every .dds file, every
// subdirectory, and the two config files under hd_textures\. Subdir mtimes
// change when files are added or removed; individual file mtimes change on
// edit. Called after Init() and after each successful HotReload() so the
// next cycle has a baseline to compare against.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::RecordDiskMtimes()
{
    m_diskMtimes.clear();

    // Stat a single path and store its write time
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    std::wstring hashDBPath   = m_hdRoot + L"\\hash_database.txt";
    std::wstring lazyCfgPath  = m_hdRoot + L"\\lazyload_config.txt";
    if (GetFileAttributesExW(hashDBPath.c_str(),  GetFileExInfoStandard, &attrData))
        m_diskMtimes[hashDBPath]  = attrData.ftLastWriteTime;
    if (GetFileAttributesExW(lazyCfgPath.c_str(), GetFileExInfoStandard, &attrData))
        m_diskMtimes[lazyCfgPath] = attrData.ftLastWriteTime;

    // Walk immediate subdirectories of hd_textures/
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((m_hdRoot + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        std::wstring subDir = m_hdRoot + L"\\" + fd.cFileName;
        // Use GetFileAttributesExW directly on each path — same API as HasDiskChanges()
        // uses for comparison, ensuring we never get a false mismatch from NTFS parent-
        // directory-entry vs MFT-record timestamp skew.
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(subDir.c_str(), GetFileExInfoStandard, &fa))
            m_diskMtimes[subDir] = fa.ftLastWriteTime;

        // Individual .dds file mtimes catch in-place edits of existing files
        WIN32_FIND_DATAW ffd;
        HANDLE hSub = FindFirstFileW((subDir + L"\\*.dds").c_str(), &ffd);
        if (hSub == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring filePath = subDir + L"\\" + ffd.cFileName;
            if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fa))
                m_diskMtimes[filePath] = fa.ftLastWriteTime;
        } while (FindNextFileW(hSub, &ffd));
        FindClose(hSub);

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

// -----------------------------------------------------------------------
// HasDiskChanges — returns true if any recorded path was modified, deleted,
// or if a new subdirectory appeared under hd_textures\ since the last
// RecordDiskMtimes() call. If false, HotReload() skips the expensive reload.
// -----------------------------------------------------------------------
inline bool HDTextureReplacer::HasDiskChanges() const
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    for (auto& [path, recorded] : m_diskMtimes)
    {
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
            return true; // file or subdir was deleted
        FILETIME cur = info.ftLastWriteTime;
        if (cur.dwLowDateTime  != recorded.dwLowDateTime ||
            cur.dwHighDateTime != recorded.dwHighDateTime)
            return true; // file modified or subdir contents changed
    }

    // Also detect new subdirectories that weren't present at last record time
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((m_hdRoot + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    bool newDir = false;
    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring subDir = m_hdRoot + L"\\" + fd.cFileName;
        if (m_diskMtimes.find(subDir) == m_diskMtimes.end()) { newDir = true; break; }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return newDir;
}

// -----------------------------------------------------------------------
// HotReload — flush all caches and pixel data, then re-read DDS files from disk.
// Called under g_hdTexCS by the hot-reload background thread.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::HotReload()
{
    // Skip the expensive reload if nothing on disk has changed since last cycle.
    // This is the common case — only pay the cost when a file is actually edited.
    if (!HasDiskChanges())
    {
        spdlog::trace("HDTextures: hot reload — no disk changes detected, skipping");
        return;
    }

    // Release all GPU-side HD textures (nameToHDTex is sole owner).
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();

    // Clear runtime pointer caches (non-owning, no Release needed).
    textureMap.clear();
    checkedTextures.clear();
    pointerKey.clear();
    numberedGroups.clear();

    // Drop all pixel data and lazy paths so RescanDisk reads fresh bytes.
    hdData.clear();
    lazyPaths.clear();

    // Drop config maps so the re-read picks up any edits to the txt files.
    hashDB.clear();
    lazyPrefixes.clear();
    lazyLruCaps.clear();

    // Re-read everything from disk — config first, then texture assets.
    LoadLazyConfig();
    LoadHashDB();
    RescanDisk();

    RecordDiskMtimes();
    spdlog::info("HDTextures: hot reload complete ({} hash(es), {} static texture(s) resident)",
                 hashDB.size(), hdData.size());
}
#endif


// -----------------------------------------------------------------------
// Async hash thread — lifecycle
// -----------------------------------------------------------------------
inline void HDTextureReplacer::StartHashThread()
{
    if (hashThread_) return;
    hashStop_.store(false);
    hashSemaphore_ = CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr);
    if (!hashSemaphore_) {
        spdlog::error("HDTextures: hash semaphore creation failed");
        return;
    }
    hashThread_ = CreateThread(nullptr, 0, HashThreadProc, this, 0, nullptr);
    if (!hashThread_) {
        spdlog::error("HDTextures: hash thread creation failed");
        CloseHandle(hashSemaphore_);
        hashSemaphore_ = nullptr;
    } else {
        spdlog::info("HDTextures: async hash thread started");
    }
}

inline void HDTextureReplacer::StopHashThread()
{
    if (!hashThread_) return;
    hashStop_.store(true);
    if (hashSemaphore_) ReleaseSemaphore(hashSemaphore_, 1, nullptr); // wake thread
    WaitForSingleObject(hashThread_, 3000);
    CloseHandle(hashThread_);  hashThread_    = nullptr;
    if (hashSemaphore_) { CloseHandle(hashSemaphore_); hashSemaphore_ = nullptr; }
}

// -----------------------------------------------------------------------
// HashThreadProc — background worker: LockRect + FNV1a + hashDB lookup.
// Runs without any application-level lock; only acquires hashMtx_ briefly
// to pop a job and to push a result.
// -----------------------------------------------------------------------
inline DWORD WINAPI HDTextureReplacer::HashThreadProc(LPVOID pThis)
{
    HDTextureReplacer* self = static_cast<HDTextureReplacer*>(pThis);
    while (true)
    {
        WaitForSingleObject(self->hashSemaphore_, INFINITE);
        if (self->hashStop_.load()) break;

        // Pop one job.
        HashJob job;
        {
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            if (self->hashJobs_.empty()) continue;
            job = std::move(self->hashJobs_.front());
            self->hashJobs_.pop_front();
        }

        // Build a default miss result.
        HashResult res;
        res.pTex  = job.pTex;
        res.jobId = job.jobId;
        res.match = false;
        res.hash  = 0;
        res.fmt   = D3DFMT_UNKNOWN;
        res.w = res.h = 0;

        // Must be a plain 2D texture.
        if (job.pTex->GetType() != D3DRTYPE_TEXTURE)
        {
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            self->hashResults_.push_back(std::move(res));
            self->hashResultsReady_.store(true, std::memory_order_release);
            continue;
        }

        IDirect3DTexture9* tex = static_cast<IDirect3DTexture9*>(job.pTex);
        D3DSURFACE_DESC desc;
        if (FAILED(tex->GetLevelDesc(0, &desc)))
        {
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            self->hashResults_.push_back(std::move(res));
            self->hashResultsReady_.store(true, std::memory_order_release);
            continue;
        }

        UINT rowPitch = ComputeRowPitch(desc.Format, desc.Width);
        UINT rowCount = ComputeRowCount(desc.Format, desc.Height);
        res.fmt = desc.Format;
        res.w   = desc.Width;
        res.h   = desc.Height;

        if (!rowPitch || !rowCount)
        {
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            self->hashResults_.push_back(std::move(res));
            self->hashResultsReady_.store(true, std::memory_order_release);
            continue;
        }

        // LockRect — no application lock held.  D3DCREATE_MULTITHREADED serialises
        // this internally; the call itself is fast for CPU-resident textures.
        D3DLOCKED_RECT locked;
        if (FAILED(tex->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        {
            // Non-lockable texture (DEFAULT pool, not DYNAMIC) — permanent miss.
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            self->hashResults_.push_back(std::move(res));
            self->hashResultsReady_.store(true, std::memory_order_release);
            continue;
        }

        uint64_t h = 14695981039346656037ULL;
        const uint8_t* bits = static_cast<const uint8_t*>(locked.pBits);
        for (UINT row = 0; row < rowCount; row++)
            h = FNV1a64(bits + row * locked.Pitch, rowPitch, h);

#ifdef HDTEX_DUMP_TEXTURES
        DumpTextureDDS(h, desc.Format, desc.Width, desc.Height,
                       locked.pBits, locked.Pitch, rowPitch, rowCount);
#endif

        tex->UnlockRect(0);

        res.hash = h;

        // hashDB is populated in Init() and never modified during normal operation.
        auto dbIt = self->hashDB.find(h);
        if (dbIt != self->hashDB.end())
        {
            res.match   = true;
            res.texName = dbIt->second;
        }

        {
            std::lock_guard<std::mutex> lk(self->hashMtx_);
            self->hashResults_.push_back(std::move(res));
            self->hashResultsReady_.store(true, std::memory_order_release);
        }
    }
    return 0;
}

// -----------------------------------------------------------------------
// QueueHashJob — AddRef pTex and push it for background hashing.
// Must be called under SRWLock exclusive.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::QueueHashJob(IDirect3DBaseTexture9* pTex)
{
    if (!hashSemaphore_) return;
    if (pendingJobId_.count(pTex)) return; // already queued

    uint64_t id = nextJobId_++;
    pendingJobId_[pTex] = id;
    pTex->AddRef();

    {
        std::lock_guard<std::mutex> lk(hashMtx_);
        hashJobs_.push_back({pTex, id});
    }
    ReleaseSemaphore(hashSemaphore_, 1, nullptr);
}

// -----------------------------------------------------------------------
// ConsumeHashResults — process completed hash results on the render thread.
//   - matches: look up / create HD texture, populate textureMap, push to GPU
//   - misses:  add to checkedTextures
//   - lazy-tile matches whose preload isn't ready yet: park in preloadWaiting_
// Must be called under SRWLock exclusive.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::ConsumeHashResults(
    IDirect3DDevice9* pDevice,
    const std::unordered_map<DWORD, IDirect3DBaseTexture9*>& currentTextures)
{
    // Helper: for every stage that currently has pOrig bound, push the HD
    // replacement directly to the real device.  This covers textures the game
    // bound once (dirty-state cache) and will never rebind on its own.
    auto PushToStages = [&](IDirect3DBaseTexture9* pOrig, IDirect3DTexture9* pHD) {
        for (auto& [stage, pBound] : currentTextures)
            if (pBound == pOrig)
                pDevice->SetTexture(stage, pHD);
    };

    // 1. Check preload-waiting entries — promote those whose DDS data has arrived.
    if (!preloadWaiting_.empty())
    {
        struct ReadyEntry {
            IDirect3DBaseTexture9* pTex;
            std::string            texName;
            HDTextureData          data;
        };
        std::vector<ReadyEntry> ready;

        {
            std::lock_guard<std::mutex> plk(preloadMtx_);
            for (auto& [pTex, texName] : preloadWaiting_)
            {
                auto readyIt = preloadReady_.find(texName);
                if (readyIt == preloadReady_.end()) continue;
                ready.push_back({pTex, texName, std::move(readyIt->second)});
                preloadReady_.erase(readyIt);
                preloadQueued_.erase(texName);
            }
        }

        for (auto& re : ready)
        {
            hdData[re.texName] = std::move(re.data);
            IDirect3DTexture9* hdTex = CreateHDTexture(pDevice, re.texName);
            if (hdTex)
            {
                nameToHDTex[re.texName] = hdTex;
                textureMap[re.pTex]     = hdTex;
                pointerKey[re.pTex]     = re.texName;
                PushToStages(re.pTex, hdTex);
                spdlog::info("HDTextures: MATCH(preload-ready) '{}' -> {}x{}",
                             re.texName, hdData.at(re.texName).hdW, hdData.at(re.texName).hdH);
            }
            else
            {
                checkedTextures.insert(re.pTex);
            }
            re.pTex->Release();
            preloadWaiting_.erase(re.pTex);
        }
    }

    // 2. Steal the hash results queue and clear the ready flag.
    // Flag is cleared inside hashMtx_ so neither thread can push between
    // the swap and the clear; if they push after we release, the flag is
    // set again and we'll drain on the next SetTexture call.
    // We do NOT bail early on an empty batch — the preload thread may have
    // set the flag without producing any hash results.
    std::deque<HashResult> batch;
    {
        std::lock_guard<std::mutex> lk(hashMtx_);
        batch.swap(hashResults_);
        hashResultsReady_.store(false, std::memory_order_relaxed);
    }

    if (batch.empty() && preloadWaiting_.empty()) return;

    for (auto& r : batch)
    {
        // Validate: if the pointer was invalidated (or re-queued) since this job was
        // pushed, the jobId will be absent or mismatched — discard the stale result.
        auto jobIt = pendingJobId_.find(r.pTex);
        if (jobIt == pendingJobId_.end() || jobIt->second != r.jobId)
        {
            r.pTex->Release();
            continue;
        }
        pendingJobId_.erase(jobIt);

        if (!r.match)
        {
            spdlog::info("HDTextures: MISS {:016x} {}x{} fmt={:x}",
                         r.hash, r.w, r.h, (DWORD)r.fmt);
            checkedTextures.insert(r.pTex);
            r.pTex->Release();
            continue;
        }

        const std::string& texName = r.texName;
        std::string numberedPrefix;

        if (IsNumberedTile(texName))
        {
            const std::string ns = texName.substr(0, texName.find('/'));
            auto [pfx, num] = SplitNumberedNamespace(ns);
            numberedPrefix = pfx;

            auto& group = numberedGroups[pfx];
            if (group.lruCap == 0)
                group.lruCap = LruCapForPrefix(pfx);

            if (!group.currentNumber.empty() && group.currentNumber != num)
            {
                spdlog::debug("HDTextures: numbered group switching '{}{}' -> '{}{}'",
                              pfx, group.currentNumber, pfx, num);
                FlushGroup(pfx, group);
                QueueNamespacePreload(pfx, num);
            }
            group.currentNumber = num;
        }

        // Already resident in VRAM?  Reuse and fast-path cache.
        auto nameIt = nameToHDTex.find(texName);
        if (nameIt != nameToHDTex.end())
        {
            if (!numberedPrefix.empty())
            {
                auto& group = numberedGroups[numberedPrefix];
                auto lruIt  = group.lruIndex.find(texName);
                if (lruIt != group.lruIndex.end())
                {
                    group.lruOrder.erase(lruIt->second);
                    group.lruOrder.push_front(texName);
                    group.lruIndex[texName] = group.lruOrder.begin();
                }
            }
            textureMap[r.pTex] = nameIt->second;
            pointerKey[r.pTex] = texName;
            PushToStages(r.pTex, nameIt->second);
            r.pTex->Release();
            continue;
        }

        // Check preload thread for lazy-loaded tile data.
        if (!numberedPrefix.empty() && hdData.find(texName) == hdData.end())
        {
            auto pathIt = lazyPaths.find(texName);
            if (pathIt != lazyPaths.end())
            {
                std::lock_guard<std::mutex> plk(preloadMtx_);
                auto readyIt = preloadReady_.find(texName);
                if (readyIt != preloadReady_.end())
                {
                    // Data ready — move into hdData and continue to GPU upload.
                    spdlog::debug("HDTextures: preload hit '{}' ({}x{})",
                                  texName, readyIt->second.hdW, readyIt->second.hdH);
                    hdData[texName] = std::move(readyIt->second);
                    preloadReady_.erase(readyIt);
                    preloadQueued_.erase(texName);
                }
                else
                {
                    // Still loading — queue preload if not already in flight and
                    // park the pointer in preloadWaiting_ (keeps AddRef alive).
                    if (!preloadQueued_.count(texName))
                    {
                        preloadQueued_.insert(texName);
                        preloadJobs_.push_back({texName, pathIt->second});
                        if (preloadSemaphore_)
                            ReleaseSemaphore(preloadSemaphore_, 1, nullptr);
                    }
                    preloadWaiting_[r.pTex] = texName; // ref stays alive
                    continue; // do NOT Release — preloadWaiting_ owns the ref
                }
            }
        }

        // GPU upload.
        IDirect3DTexture9* hdTex = CreateHDTexture(pDevice, texName);
        if (!hdTex)
        {
            checkedTextures.insert(r.pTex);
            r.pTex->Release();
            continue;
        }

        nameToHDTex[texName] = hdTex;
        textureMap[r.pTex]   = hdTex;
        pointerKey[r.pTex]   = texName;
        PushToStages(r.pTex, hdTex);

        if (!numberedPrefix.empty())
        {
            auto& group = numberedGroups[numberedPrefix];
            group.lruOrder.push_front(texName);
            group.lruIndex[texName] = group.lruOrder.begin();
            while (group.lruOrder.size() > group.lruCap)
                EvictOldest(numberedPrefix, group);
        }

        spdlog::info("HDTextures: MATCH '{}' {:016x} {}x{} -> {}x{}",
                     texName, r.hash, r.w, r.h,
                     hdData.at(texName).hdW, hdData.at(texName).hdH);

        r.pTex->Release();
    }
}


// -----------------------------------------------------------------------
// Async preload — thread lifecycle
// -----------------------------------------------------------------------
inline void HDTextureReplacer::StartPreloadThread()
{
    if (preloadThread_) return;
    preloadStop_.store(false);
    preloadSemaphore_ = CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr);
    if (!preloadSemaphore_) {
        spdlog::error("HDTextures: preload semaphore creation failed");
        return;
    }
    preloadThread_ = CreateThread(nullptr, 0, PreloadThreadProc, this, 0, nullptr);
    if (!preloadThread_) {
        spdlog::error("HDTextures: preload thread creation failed");
        CloseHandle(preloadSemaphore_);
        preloadSemaphore_ = nullptr;
    } else {
        spdlog::info("HDTextures: tile preload thread started");
    }
}

inline void HDTextureReplacer::StopPreloadThread()
{
    if (!preloadThread_) return;
    preloadStop_.store(true);
    if (preloadSemaphore_) ReleaseSemaphore(preloadSemaphore_, 1, nullptr); // wake thread
    WaitForSingleObject(preloadThread_, 3000);
    CloseHandle(preloadThread_);  preloadThread_    = nullptr;
    if (preloadSemaphore_) { CloseHandle(preloadSemaphore_); preloadSemaphore_ = nullptr; }
}

inline DWORD WINAPI HDTextureReplacer::PreloadThreadProc(LPVOID pThis)
{
    HDTextureReplacer* self = static_cast<HDTextureReplacer*>(pThis);
    while (true)
    {
        WaitForSingleObject(self->preloadSemaphore_, INFINITE);
        if (self->preloadStop_.load()) break;

        // Pop one job (brief lock).
        PreloadJob job;
        {
            std::lock_guard<std::mutex> lk(self->preloadMtx_);
            if (self->preloadJobs_.empty()) continue;
            job = std::move(self->preloadJobs_.front());
            self->preloadJobs_.pop_front();
        }

        // Read the DDS file with no lock held — this is the whole point.
        UINT hdW, hdH;
        D3DFORMAT fmt;
        std::vector<uint8_t> pixels;
        if (ReadDDS(job.filePath, hdW, hdH, fmt, pixels))
        {
            HDTextureData hd;
            hd.hdW = hdW; hd.hdH = hdH;
            hd.format = fmt;
            hd.pixelData = std::move(pixels);

            std::lock_guard<std::mutex> lk(self->preloadMtx_);
            self->preloadReady_[job.texName] = std::move(hd);
            spdlog::debug("HDTextures: preloaded '{}'", job.texName);
            self->hashResultsReady_.store(true, std::memory_order_release);
        }
        else
        {
            // File missing / unreadable — remove from queued so the render
            // thread doesn't wait forever. Will fall through to original texture.
            std::lock_guard<std::mutex> lk(self->preloadMtx_);
            self->preloadQueued_.erase(job.texName);
            spdlog::warn("HDTextures: preload failed for '{}'", job.texName);
        }
    }
    return 0;
}

// -----------------------------------------------------------------------
// QueueTilePreload — enqueue one tile if not already queued or loaded.
// Called under SRWLock exclusive.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::QueueTilePreload(const std::string& texName,
                                                const std::wstring& path)
{
    if (!preloadSemaphore_) return;
    std::lock_guard<std::mutex> lk(preloadMtx_);
    if (preloadQueued_.count(texName)) return;
    preloadQueued_.insert(texName);
    preloadJobs_.push_back({texName, path});
    ReleaseSemaphore(preloadSemaphore_, 1, nullptr);
}

// -----------------------------------------------------------------------
// QueueNamespacePreload — enqueue every unloaded tile for a namespace.
// Called when a namespace switch is detected, giving the background thread
// a head-start before the game starts binding those textures.
// Called under SRWLock exclusive (lazyPaths / hdData are safe to read).
// -----------------------------------------------------------------------
inline void HDTextureReplacer::QueueNamespacePreload(const std::string& prefix,
                                                     const std::string& number)
{
    if (!preloadSemaphore_) return;
    const std::string nsKey = prefix + number + "/";
    LONG count = 0;
    {
        std::lock_guard<std::mutex> lk(preloadMtx_);
        for (auto& [key, path] : lazyPaths)
        {
            if (key.size() >= nsKey.size() &&
                key.compare(0, nsKey.size(), nsKey) == 0 &&
                !preloadQueued_.count(key) &&
                hdData.find(key) == hdData.end())
            {
                preloadQueued_.insert(key);
                preloadJobs_.push_back({key, path});
                ++count;
            }
        }
    }
    if (count > 0)
    {
        ReleaseSemaphore(preloadSemaphore_, count, nullptr);
        spdlog::info("HDTextures: queued {} tile(s) for async preload ('{}{}')",
                     count, prefix, number);
    }
}


// -----------------------------------------------------------------------
// TryFastPath — shared-lock read-only cache check, no disk I/O or D3D calls.
// Returns the texture to bind if already resolved, nullptr on cache miss.
// -----------------------------------------------------------------------
inline IDirect3DBaseTexture9* HDTextureReplacer::TryFastPath(IDirect3DBaseTexture9* pTexture)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end()) return mapIt->second;

    if (checkedTextures.count(pTexture)) return pTexture;

    // Textures waiting for preload data: pass through unless the preload is
    // actually done (hashResultsReady_ set by PreloadThreadProc).  In that
    // case fall through to the drain path so ConsumeHashResults can promote
    // the entry.  Without this check the preload-waiting texture intercepts
    // first and the drain never fires on screens where it's the only texture
    // being bound (e.g. shop031 zone preview).
    if (preloadWaiting_.count(pTexture))
    {
        if (hashResultsReady_.load(std::memory_order_acquire)) return nullptr;
        return pTexture;
    }

    // If either thread has results ready, force the exclusive-lock path so
    // ConsumeHashResults runs even on a stable scene with no new textures.
    // This check MUST come before pendingJobId_ so that textures sitting in
    // the hash pipeline trigger the drain once their result arrives.
    if (hashResultsReady_.load(std::memory_order_acquire)) return nullptr;

    // Already queued for hashing — use original until result arrives.
    if (pendingJobId_.count(pTexture)) return pTexture;

    return nullptr; // true cache miss — caller must escalate to exclusive lock
}


// -----------------------------------------------------------------------
// OnSetTexture — identify texture by hash, swap if HD replacement available.
//
// Hashing is now fully async: every first-seen texture is pushed to the
// background hash thread.  This function only processes completed results
// and queues new work; it never blocks on LockRect or hash computation.
// -----------------------------------------------------------------------
inline IDirect3DBaseTexture9* HDTextureReplacer::OnSetTexture(
    IDirect3DDevice9* pDevice,
    IDirect3DBaseTexture9* pTexture,
    const std::unordered_map<DWORD, IDirect3DBaseTexture9*>& currentTextures)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    // Drain any results the hash thread (or preload thread) has completed.
    // This may populate textureMap / checkedTextures for textures seen earlier,
    // and proactively push HD textures to stages the game hasn't rebound.
    ConsumeHashResults(pDevice, currentTextures);

    // Fast path: already mapped to an HD texture (may have just been populated above).
    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end())
        return mapIt->second;

    // Already checked with no match — skip.
    if (checkedTextures.count(pTexture))
        return pTexture;

    // Already in hash pipeline or waiting for lazy-tile preload data.
    if (pendingJobId_.count(pTexture) || preloadWaiting_.count(pTexture))
        return pTexture;

    // New texture — queue for background hashing.
    QueueHashJob(pTexture);
    return pTexture;
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

    // Flush preload queues — discard in-flight reads whose pixel data
    // would be stale after the device reset clears all GPU textures.
    {
        std::lock_guard<std::mutex> lk(preloadMtx_);
        preloadJobs_.clear();
        preloadQueued_.clear();
        preloadReady_.clear();
    }

    // Flush hash queues — release all AddRef'd textures in the pipeline.
    // Any result that arrives after this is discarded (pendingJobId_ is cleared).
    {
        std::lock_guard<std::mutex> lk(hashMtx_);
        for (auto& job : hashJobs_)  job.pTex->Release();
        hashJobs_.clear();
        for (auto& res : hashResults_) res.pTex->Release();
        hashResults_.clear();
    }
    for (auto& [pTex, texName] : preloadWaiting_) pTex->Release();
    preloadWaiting_.clear();
    pendingJobId_.clear();
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

    // Clear hash-pipeline entry.  When the in-flight result arrives, the missing
    // (or mismatched) jobId causes it to be discarded without touching any cache.
    pendingJobId_.erase(pTexture);

    // Release preload-waiting reference.
    auto waitIt = preloadWaiting_.find(pTexture);
    if (waitIt != preloadWaiting_.end())
    {
        pTexture->Release();
        preloadWaiting_.erase(waitIt);
    }
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

    spdlog::debug("HDTextures: CreateTexture '{}' {}x{} fmt=0x{:X}",
                  texName, hd.hdW, hd.hdH, (unsigned)hd.format);

    // Helper: fill a lockable texture (MANAGED or SYSTEMMEM) with our pixel data.
    auto FillLockable = [&](IDirect3DTexture9* tex) -> bool {
        D3DLOCKED_RECT locked;
        HRESULT hr2 = tex->LockRect(0, &locked, nullptr, 0);
        if (FAILED(hr2)) {
            spdlog::error("HDTextures: failed to lock texture for '{}' (hr=0x{:08X})", texName, (unsigned)hr2);
            return false;
        }
        UINT rowPitch = ComputeRowPitch(hd.format, hd.hdW);
        UINT rowCount = ComputeRowCount(hd.format, hd.hdH);
        const uint8_t* src = hd.pixelData.data();
        for (UINT row = 0; row < rowCount; row++)
            memcpy(static_cast<uint8_t*>(locked.pBits) + row * locked.Pitch,
                   src + row * rowPitch, rowPitch);
        tex->UnlockRect(0);
        return true;
    };

    IDirect3DTexture9* hdTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                        hd.format, D3DPOOL_MANAGED,
                                        &hdTex, nullptr);
    if (SUCCEEDED(hr))
    {
        // D3D9 (non-Ex) path: managed pool, lock and fill directly.
        if (!FillLockable(hdTex)) { hdTex->Release(); return nullptr; }
        return hdTex;
    }

    if (hr == D3DERR_INVALIDCALL)
    {
        // D3D9Ex path: managed pool is not supported. Upload via a SYSTEMMEM
        // staging texture copied into a DEFAULT pool texture with UpdateTexture.
        spdlog::info("HDTextures: D3DPOOL_MANAGED rejected for '{}' — using SYSTEMMEM staging (D3D9Ex device)",
                     texName);

        IDirect3DTexture9* staging = nullptr;
        hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                    hd.format, D3DPOOL_SYSTEMMEM,
                                    &staging, nullptr);
        if (FAILED(hr)) {
            spdlog::error("HDTextures: failed to create staging texture for '{}' (hr=0x{:08X})", texName, (unsigned)hr);
            return nullptr;
        }
        if (!FillLockable(staging)) { staging->Release(); return nullptr; }

        hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                    hd.format, D3DPOOL_DEFAULT,
                                    &hdTex, nullptr);
        if (FAILED(hr)) {
            spdlog::error("HDTextures: failed to create DEFAULT texture for '{}' (hr=0x{:08X})", texName, (unsigned)hr);
            staging->Release();
            return nullptr;
        }

        hr = pDevice->UpdateTexture(staging, hdTex);
        staging->Release();
        if (FAILED(hr)) {
            spdlog::error("HDTextures: UpdateTexture failed for '{}' (hr=0x{:08X})", texName, (unsigned)hr);
            hdTex->Release();
            return nullptr;
        }
        return hdTex;
    }

    spdlog::error("HDTextures: failed to create HD texture for '{}' {}x{} fmt=0x{:X} (hr=0x{:08X})",
                  texName, hd.hdW, hd.hdH, (unsigned)hd.format, (unsigned)hr);
    return nullptr;
}


// -----------------------------------------------------------------------
// SwapCostumeTexture — hot-swap pixel data for one named gui_resident texture.
//
// Releases the existing GPU texture so it will be re-created on the next
// SetTexture call that references it. Original-pointer caches (textureMap,
// checkedTextures, pointerKey) are cleared for any entry that pointed at
// the old GPU object so the rehash and re-upload path is taken cleanly.
//
// Must be called under g_hdTexCS.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::SwapCostumeTexture(const std::string& texName,
                                                   UINT hdW, UINT hdH,
                                                   D3DFORMAT format,
                                                   std::vector<uint8_t> pixels)
{
    // Release existing GPU texture and evict every pointer that referenced it.
    auto nit = nameToHDTex.find(texName);
    if (nit != nameToHDTex.end()) {
        IDirect3DTexture9* oldTex = nit->second;

        // Remove all fast-path cache entries pointing at this GPU texture.
        std::vector<IDirect3DBaseTexture9*> stale;
        for (auto& [ptr, hdTex] : textureMap)
            if (hdTex == oldTex) stale.push_back(ptr);
        for (auto ptr : stale) {
            textureMap.erase(ptr);
            checkedTextures.erase(ptr);
            pointerKey.erase(ptr);
        }

        if (oldTex) oldTex->Release();
        nameToHDTex.erase(nit);
    }

    // Install new pixel data.
    HDTextureData& hd = hdData[texName];
    hd.hdW       = hdW;
    hd.hdH       = hdH;
    hd.format    = format;
    hd.pixelData = std::move(pixels);

    spdlog::info("CostumeTracker: texture '{}' swapped ({}x{}) — will upload on next bind",
                 texName, hdW, hdH);
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

#ifdef HDTEX_DUMP_TEXTURES
// -----------------------------------------------------------------------
// HDTEX_DUMP_TEXTURES — write every seen texture to textures_dump\<hash>.dds
// Skips already-dumped hashes (checked via file existence). Never dumps
// the same hash twice per session even across hot-reloads.
// -----------------------------------------------------------------------
inline std::wstring& HDTextureReplacer::TexDumpDir()
{
    static std::wstring s_dir;
    return s_dir;
}

inline void HDTextureReplacer::SetTexDumpDir(const std::wstring& dir)
{
    TexDumpDir() = dir;
    CreateDirectoryW(dir.c_str(), nullptr);
    spdlog::info("HDTextures: texture dump enabled -> textures_dump\\");
}

inline void HDTextureReplacer::DumpTextureDDS(uint64_t hash, D3DFORMAT fmt,
                                              UINT w, UINT h,
                                              const void* pBits, UINT pitch,
                                              UINT rowPitch, UINT rowCount)
{
    const std::wstring& dir = TexDumpDir();
    if (dir.empty()) return;

    wchar_t fname[32];
    swprintf_s(fname, L"%016llx.dds", static_cast<unsigned long long>(hash));
    std::wstring path = dir + L"\\" + fname;

    // Skip if already written this session or in a previous one.
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    // Build a minimal 128-byte DDS header.
    uint8_t hdr[128] = {};
    memcpy(hdr, "DDS ", 4);
    *reinterpret_cast<uint32_t*>(hdr +  4) = 124;  // dwSize
    *reinterpret_cast<uint32_t*>(hdr + 12) = h;
    *reinterpret_cast<uint32_t*>(hdr + 16) = w;
    *reinterpret_cast<uint32_t*>(hdr + 76) = 32;   // pfSize
    *reinterpret_cast<uint32_t*>(hdr +108) = 0x1000; // DDSCAPS_TEXTURE

    bool isBlock = (fmt == D3DFMT_DXT1 || fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT5);

    if (isBlock)
    {
        *reinterpret_cast<uint32_t*>(hdr +  8) = 0x00081007; // CAPS|HEIGHT|WIDTH|PF|LINEARSIZE
        *reinterpret_cast<uint32_t*>(hdr + 20) = rowPitch * rowCount; // linear size
        *reinterpret_cast<uint32_t*>(hdr + 80) = 0x4;  // DDPF_FOURCC
        *reinterpret_cast<uint32_t*>(hdr + 84) = static_cast<uint32_t>(fmt); // DXT fourCC
    }
    else
    {
        *reinterpret_cast<uint32_t*>(hdr +  8) = 0x0000100F; // CAPS|HEIGHT|WIDTH|PF|PITCH
        *reinterpret_cast<uint32_t*>(hdr + 20) = rowPitch;   // pitch

        uint32_t pfFlags = 0, bpp = 0, rM = 0, gM = 0, bM = 0, aM = 0;
        switch (fmt)
        {
        case D3DFMT_A8R8G8B8:
            pfFlags=0x41; bpp=32; rM=0x00FF0000; gM=0x0000FF00; bM=0x000000FF; aM=0xFF000000; break;
        case D3DFMT_X8R8G8B8:
            pfFlags=0x40; bpp=32; rM=0x00FF0000; gM=0x0000FF00; bM=0x000000FF; break;
        case D3DFMT_R5G6B5:
            pfFlags=0x40; bpp=16; rM=0xF800; gM=0x07E0; bM=0x001F; break;
        case D3DFMT_A1R5G5B5:
            pfFlags=0x41; bpp=16; rM=0x7C00; gM=0x03E0; bM=0x001F; aM=0x8000; break;
        case D3DFMT_A4R4G4B4:
            pfFlags=0x41; bpp=16; rM=0x0F00; gM=0x00F0; bM=0x000F; aM=0xF000; break;
        case D3DFMT_L8:
            pfFlags=0x20000; bpp=8; rM=0xFF; break;    // DDPF_LUMINANCE
        case D3DFMT_A8:
            pfFlags=0x2;    bpp=8; aM=0xFF; break;     // DDPF_ALPHA
        default:
            // Unknown format: write raw pixel data with no valid pixel format info.
            // The file will open in tools that do raw inspection; at least it has dims.
            pfFlags=0; bpp=0; break;
        }
        *reinterpret_cast<uint32_t*>(hdr + 80) = pfFlags;
        *reinterpret_cast<uint32_t*>(hdr + 88) = bpp;
        *reinterpret_cast<uint32_t*>(hdr + 92) = rM;
        *reinterpret_cast<uint32_t*>(hdr + 96) = gM;
        *reinterpret_cast<uint32_t*>(hdr +100) = bM;
        *reinterpret_cast<uint32_t*>(hdr +104) = aM;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;

    f.write(reinterpret_cast<const char*>(hdr), 128);

    // Write rows, stripping any extra pitch padding from the locked rect.
    const uint8_t* src = static_cast<const uint8_t*>(pBits);
    for (UINT row = 0; row < rowCount; ++row)
        f.write(reinterpret_cast<const char*>(src + row * pitch), rowPitch);
}
#endif // HDTEX_DUMP_TEXTURES
