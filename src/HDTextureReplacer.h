#pragma once

#include <string>
#include <vector>
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
//
// Flow:
//   1. Init(): load hash_database.txt (hash -> name) and scan hd_textures/
//      subdirs to record DDS paths on disk (lazyPaths). No pixel data loaded.
//   2. OnSetTexture(): on first bind of an original texture, hash its pixels,
//      look up the name, read the DDS from disk, upload to GPU, free the pixel
//      buffer. Subsequent binds of the same pointer hit the fast-path cache.
//   3. OnOriginalReleased(): called when the game's Release refcount hits zero.
//      Drops pointer caches; when the last original referencing an HD texture
//      is gone (nameRefs -> 0), the GPU texture is released immediately.
//
// Ownership: nameToHDTex is sole owner of all live HD textures.
//            textureMap is a non-owning fast-path pointer -> HD texture cache.
//
// Thread safety: all public methods must be called under g_hdTexCS.
class HDTextureReplacer
{
public:
    // modDir: directory containing version.dll.
    // Reads hd_textures\hash_database.txt and scans hd_textures\ subdirs.
    void Init(const std::wstring& modDir);

    // Called from SetTexture hook — identifies texture by hash, swaps if HD available.
    IDirect3DBaseTexture9* OnSetTexture(IDirect3DDevice9* pDevice,
                                        IDirect3DBaseTexture9* pTexture);

    // Called on device Reset — releases all GPU-side HD textures.
    void ReleaseTextures();

    // Called from CreateTexture hook — evicts stale cache entries for reused pointers.
    void InvalidateTexture(IDirect3DBaseTexture9* pTexture);

    // Called from the IDirect3DTexture9::Release vtable hook when an original
    // game texture's refcount reaches zero. Releases the HD counterpart when
    // no other original still references it (nameRefs hits 0).
    void OnOriginalReleased(IDirect3DBaseTexture9* pTexture);

    // Hot-swap pixel data for one named texture without a full reload.
    // Used by the costume tracking system to swap face artwork at runtime.
    // The existing GPU texture is released and will be re-uploaded on the next bind.
    void SwapCostumeTexture(const std::string& texName,
                            UINT hdW, UINT hdH, D3DFORMAT format,
                            std::vector<uint8_t> pixels);

    // Parse DDS header + pixel data from disk.
    // Public so the costume callback can call it outside g_hdTexCS.
    static bool ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                        D3DFORMAT& format, std::vector<uint8_t>& pixelData);

#ifdef HDTEX_HOT_RELOAD
    void HotReload();
#endif

private:
    // hash -> texture name (read once at Init, never modified)
    std::unordered_map<uint64_t, std::string> hashDB;

    // texture name -> DDS path on disk (populated at Init by scanning hd_textures\)
    std::unordered_map<std::string, std::wstring> lazyPaths;

    // texture name -> live GPU texture (sole owner)
    std::unordered_map<std::string, IDirect3DTexture9*> nameToHDTex;

    // original D3D pointer -> HD texture (non-owning fast-path cache)
    std::unordered_map<IDirect3DBaseTexture9*, IDirect3DTexture9*> textureMap;

    // original D3D pointer -> texture name (needed by OnOriginalReleased)
    std::unordered_map<IDirect3DBaseTexture9*, std::string> pointerKey;

    // pointers already checked with no match (avoid rehashing every bind)
    std::unordered_set<IDirect3DBaseTexture9*> checkedTextures;

    // live original-pointer count per HD texture name.
    // HD texture is kept resident while > 0; released when it hits 0.
    std::unordered_map<std::string, int> nameRefs;

    // costume pixel data pending GPU upload (set by SwapCostumeTexture,
    // consumed and freed by CreateHDTextureFromData on next bind)
    struct PendingSwap { UINT w, h; D3DFORMAT fmt; std::vector<uint8_t> pixels; };
    std::unordered_map<std::string, PendingSwap> pendingSwaps;

    std::wstring m_hdRoot;

    // Read DDS from disk and upload to GPU. Returns new texture (caller registers
    // in nameToHDTex) or nullptr on failure. Pixel buffer is freed after upload.
    IDirect3DTexture9* CreateHDTexture(IDirect3DDevice9* pDevice,
                                       const std::string& texName);

    // Upload from a caller-supplied pixel buffer (used by costume swap path).
    IDirect3DTexture9* CreateHDTextureFromData(IDirect3DDevice9* pDevice,
                                               UINT w, UINT h, D3DFORMAT fmt,
                                               std::vector<uint8_t>& pixels);

    void ScanHDSubdir(const std::wstring& subDirPath, const std::string& ns);
    void RescanDisk();
    bool LoadHashDB();

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
// Init — load hash database + scan hd_textures/ subdirectories for DDS paths
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
                                             const std::string& ns)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((subDirPath + L"\\*.dds").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int count = 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring filePath = subDirPath + L"\\" + fd.cFileName;
        std::wstring fnameW   = fd.cFileName;
        std::string  fname(fnameW.begin(), fnameW.end());
        std::string  key = ns + "/" + StripDDSExtension(fname);

        lazyPaths[key] = filePath;
        ++count;

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    spdlog::debug("HDTextures: indexed {} DDS path(s) in '{}'", count, ns);
}

inline void HDTextureReplacer::Init(const std::wstring& modDir)
{
    m_hdRoot = modDir + L"\\hd_textures";

    if (!LoadHashDB())
        return;

    DWORD attr = GetFileAttributesW(m_hdRoot.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        spdlog::info("HDTextures: no hd_textures directory found");
        return;
    }

    RescanDisk();

    spdlog::info("HDTextures: {} hash(es), {} HD path(s) indexed",
                 hashDB.size(), lazyPaths.size());
}


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
inline void HDTextureReplacer::HotReload()
{
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();

    textureMap.clear();
    checkedTextures.clear();
    pointerKey.clear();
    nameRefs.clear();
    pendingSwaps.clear();

    hashDB.clear();
    lazyPaths.clear();

    LoadHashDB();
    RescanDisk();

    spdlog::info("HDTextures: hot reload complete ({} hash(es), {} path(s) indexed)",
                 hashDB.size(), lazyPaths.size());
}
#endif


// -----------------------------------------------------------------------
// OnSetTexture — identify texture by hash, swap if HD replacement available
// -----------------------------------------------------------------------
inline IDirect3DBaseTexture9* HDTextureReplacer::OnSetTexture(IDirect3DDevice9* pDevice,
                                                              IDirect3DBaseTexture9* pTexture)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    // Fast path: already mapped.
    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end())
        return mapIt->second;

    // Already checked with no match.
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

#ifdef HDTEX_DUMP_TEXTURES
    DumpTextureDDS(h, desc.Format, desc.Width, desc.Height,
                   locked.pBits, locked.Pitch, rowPitch, rowCount);
#endif

    tex->UnlockRect(0);

    auto dbIt = hashDB.find(h);
    if (dbIt == hashDB.end())
        return pTexture;

    const std::string& texName = dbIt->second;

    // If already resident (e.g. game reloaded same texture at new address), reuse.
    {
        auto nameIt = nameToHDTex.find(texName);
        if (nameIt != nameToHDTex.end())
        {
            textureMap[pTexture] = nameIt->second;
            pointerKey[pTexture] = texName;
            nameRefs[texName]++;
            return nameIt->second;
        }
    }

    IDirect3DTexture9* hdTex = CreateHDTexture(pDevice, texName);
    if (!hdTex)
        return pTexture;

    nameToHDTex[texName] = hdTex;
    textureMap[pTexture]  = hdTex;
    pointerKey[pTexture]  = texName;
    nameRefs[texName]     = 1;

    spdlog::debug("HDTextures: '{}' matched by hash {:016x}, swapped to HD",
                  texName, h);

    return hdTex;
}


// -----------------------------------------------------------------------
// ReleaseTextures — called on device Reset
// -----------------------------------------------------------------------
inline void HDTextureReplacer::ReleaseTextures()
{
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();

    textureMap.clear();
    checkedTextures.clear();
    pointerKey.clear();
    nameRefs.clear();
    pendingSwaps.clear();
}


// -----------------------------------------------------------------------
// InvalidateTexture — pointer reused by a new CreateTexture call
// -----------------------------------------------------------------------
inline void HDTextureReplacer::InvalidateTexture(IDirect3DBaseTexture9* pTexture)
{
    // Treat the same as a release: the old object at this address is gone.
    // OnOriginalReleased is idempotent if the Release hook already ran.
    OnOriginalReleased(pTexture);
}


// -----------------------------------------------------------------------
// OnOriginalReleased — original game texture freed, release HD if last ref
// -----------------------------------------------------------------------
inline void HDTextureReplacer::OnOriginalReleased(IDirect3DBaseTexture9* pTexture)
{
    textureMap.erase(pTexture);
    checkedTextures.erase(pTexture);

    auto it = pointerKey.find(pTexture);
    if (it == pointerKey.end())
        return;

    const std::string name = it->second;
    pointerKey.erase(it);

    auto rc = nameRefs.find(name);
    if (rc == nameRefs.end() || --rc->second > 0)
        return;

    nameRefs.erase(rc);

    auto nit = nameToHDTex.find(name);
    if (nit != nameToHDTex.end())
    {
        if (nit->second) nit->second->Release();
        nameToHDTex.erase(nit);
    }

    spdlog::debug("HDTextures: released HD '{}' (last original freed)", name);
}


// -----------------------------------------------------------------------
// CreateHDTexture — read DDS from disk, upload to GPU, free pixel buffer
// -----------------------------------------------------------------------
inline IDirect3DTexture9* HDTextureReplacer::CreateHDTexture(IDirect3DDevice9* pDevice,
                                                              const std::string& texName)
{
    // Check for a pending costume swap first.
    auto swapIt = pendingSwaps.find(texName);
    if (swapIt != pendingSwaps.end())
    {
        auto& s = swapIt->second;
        IDirect3DTexture9* t = CreateHDTextureFromData(pDevice, s.w, s.h, s.fmt, s.pixels);
        pendingSwaps.erase(swapIt);
        return t;
    }

    auto pathIt = lazyPaths.find(texName);
    if (pathIt == lazyPaths.end())
        return nullptr;

    UINT hdW, hdH;
    D3DFORMAT format;
    std::vector<uint8_t> pixels;
    if (!ReadDDS(pathIt->second, hdW, hdH, format, pixels))
    {
        spdlog::warn("HDTextures: failed to read DDS for '{}'", texName);
        return nullptr;
    }

    return CreateHDTextureFromData(pDevice, hdW, hdH, format, pixels);
}


inline IDirect3DTexture9* HDTextureReplacer::CreateHDTextureFromData(
    IDirect3DDevice9* pDevice, UINT w, UINT h, D3DFORMAT fmt,
    std::vector<uint8_t>& pixels)
{
    IDirect3DTexture9* hdTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED,
                                        &hdTex, nullptr);
    if (FAILED(hr))
    {
        // D3D9Ex devices (e.g. LR:FFXIII) don't support MANAGED pool.
        // Fall back to SYSTEMMEM staging + UpdateTexture into DEFAULT.
        IDirect3DTexture9* staging = nullptr;
        hr = pDevice->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_SYSTEMMEM,
                                    &staging, nullptr);
        if (FAILED(hr))
        {
            spdlog::error("HDTextures: CreateTexture SYSTEMMEM failed (hr=0x{:08X})", (unsigned)hr);
            return nullptr;
        }
        IDirect3DTexture9* def = nullptr;
        hr = pDevice->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, fmt, D3DPOOL_DEFAULT,
                                    &def, nullptr);
        if (FAILED(hr))
        {
            staging->Release();
            spdlog::error("HDTextures: CreateTexture DEFAULT failed (hr=0x{:08X})", (unsigned)hr);
            return nullptr;
        }

        D3DLOCKED_RECT lk;
        if (SUCCEEDED(staging->LockRect(0, &lk, nullptr, 0)))
        {
            UINT rowPitch = ComputeRowPitch(fmt, w);
            UINT rowCount = ComputeRowCount(fmt, h);
            for (UINT row = 0; row < rowCount; row++)
                memcpy(static_cast<uint8_t*>(lk.pBits) + row * lk.Pitch,
                       pixels.data() + row * rowPitch, rowPitch);
            staging->UnlockRect(0);
        }
        pDevice->UpdateTexture(staging, def);
        staging->Release();
        pixels.clear();
        pixels.shrink_to_fit();
        return def;
    }

    D3DLOCKED_RECT lk;
    hr = hdTex->LockRect(0, &lk, nullptr, 0);
    if (FAILED(hr))
    {
        spdlog::error("HDTextures: LockRect failed (hr=0x{:08X})", (unsigned)hr);
        hdTex->Release();
        return nullptr;
    }

    UINT rowPitch = ComputeRowPitch(fmt, w);
    UINT rowCount = ComputeRowCount(fmt, h);
    for (UINT row = 0; row < rowCount; row++)
        memcpy(static_cast<uint8_t*>(lk.pBits) + row * lk.Pitch,
               pixels.data() + row * rowPitch, rowPitch);

    hdTex->UnlockRect(0);
    pixels.clear();
    pixels.shrink_to_fit();
    return hdTex;
}


// -----------------------------------------------------------------------
// SwapCostumeTexture — queue new pixel data for one named texture.
// The GPU texture is released immediately; pixel data is stored as a pending
// swap and uploaded on the next SetTexture bind.
// -----------------------------------------------------------------------
inline void HDTextureReplacer::SwapCostumeTexture(const std::string& texName,
                                                   UINT hdW, UINT hdH,
                                                   D3DFORMAT format,
                                                   std::vector<uint8_t> pixels)
{
    // Release existing GPU texture and clear all pointer caches for it.
    auto nit = nameToHDTex.find(texName);
    if (nit != nameToHDTex.end())
    {
        IDirect3DTexture9* oldTex = nit->second;

        std::vector<IDirect3DBaseTexture9*> stale;
        for (auto& [ptr, hdTex] : textureMap)
            if (hdTex == oldTex) stale.push_back(ptr);
        for (auto ptr : stale)
        {
            textureMap.erase(ptr);
            checkedTextures.erase(ptr);
            pointerKey.erase(ptr);
        }
        nameRefs.erase(texName);

        if (oldTex) oldTex->Release();
        nameToHDTex.erase(nit);
    }

    PendingSwap& ps = pendingSwaps[texName];
    ps.w      = hdW;
    ps.h      = hdH;
    ps.fmt    = format;
    ps.pixels = std::move(pixels);

    spdlog::info("CostumeTracker: '{}' queued for swap ({}x{}) — uploads on next bind",
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

    if (fourCC == 0x31545844)
        format = D3DFMT_DXT1;
    else if (fourCC == 0x33545844)
        format = D3DFMT_DXT3;
    else if (fourCC == 0x35545844)
        format = D3DFMT_DXT5;
    else if (fourCC == 0 && (pfFlags & 0x40))
    {
        if      (rgbBitCount == 32) format = D3DFMT_A8R8G8B8;
        else if (rgbBitCount == 16) format = D3DFMT_A4R4G4B4;
        else
        {
            spdlog::warn("HDTextures: unsupported RGB bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x20000))
    {
        if (rgbBitCount == 8) format = D3DFMT_L8;
        else
        {
            spdlog::warn("HDTextures: unsupported luminance bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x2))
    {
        if (rgbBitCount == 8) format = D3DFMT_A8;
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
