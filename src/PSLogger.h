// PSLogger.h — temporary shader identification helper
// Enabled only when HDTEX_LOG_SHADERS and/or HDTEX_DUMP_SHADERS are defined.
//
// HDTEX_LOG_SHADERS  — logs every CreatePixelShader (hash + token count) and
//                      draw call to HDTextures.log.
// HDTEX_DUMP_SHADERS — writes each shader's raw bytecode to
//                      shader_dumps\<hash>.bin alongside version.dll.
//                      Can be used independently of HDTEX_LOG_SHADERS.
//                      The .bin files are valid D3D9 shader binaries that
//                      any disassembler (fxc /dumpbin, D3DXDisassembleShader,
//                      d3dspy, etc.) can process directly.
//
// Both defines are off by default. Add to PreprocessorDefinitions in
// version.vcxproj when you need them, remove when done.
#pragma once

#include <d3d9.h>
#include <cstdint>
#include <string>
#include "spdlog/spdlog.h"

// Output directory for shader/texture dumps. Set once from DllMain via
// SetDumpDir — always available regardless of diagnostic defines.
namespace PSLogger {

inline std::wstring& DumpDir()
{
    static std::wstring s;
    return s;
}

inline void SetDumpDir(const std::wstring& dir)
{
    DumpDir() = dir;
#if defined(HDTEX_DUMP_SHADERS)
    CreateDirectoryW(dir.c_str(), nullptr);
#endif
}

} // namespace PSLogger

#if defined(HDTEX_LOG_SHADERS) || defined(HDTEX_DUMP_SHADERS)

#include <unordered_map>
#include <fstream>

namespace PSLogger {

// FNV-1a 64-bit hash over D3D9 pixel shader bytecode.
// Scans from the version token until the END token (0x0000FFFF), inclusive.
// Guard of 16384 DWORDs (~64 KB) covers any realistic ps_2_0/ps_3_0 shader.
static uint64_t HashBytecode(const DWORD* pFunc)
{
    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t h = kOffset;
    const DWORD* p = pFunc;
    for (size_t guard = 0; guard < 16384; ++guard)
    {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
        h ^= b[0]; h *= kPrime;
        h ^= b[1]; h *= kPrime;
        h ^= b[2]; h *= kPrime;
        h ^= b[3]; h *= kPrime;
        if (*p == 0x0000FFFFu) break; // D3DSIO_END
        ++p;
    }
    return h;
}

#ifdef HDTEX_DUMP_SHADERS
// Write raw bytecode to DumpDir\<hash>.bin. Skips if file already exists.
static void DumpShader(const DWORD* pFunction, uint64_t hash)
{
    if (DumpDir().empty() || !pFunction) return;

    // Measure bytecode length (DWORDs, inclusive of END token).
    const DWORD* p = pFunction;
    size_t guard = 0;
    while (guard < 16384 && *p++ != 0x0000FFFFu) ++guard;
    size_t bytes = static_cast<size_t>(p - pFunction) * sizeof(DWORD);

    wchar_t fname[32];
    swprintf_s(fname, L"%016llx.bin", static_cast<unsigned long long>(hash));
    std::wstring path = DumpDir() + L"\\" + fname;

    // Don't overwrite — a shader seen twice has the same hash so no new info.
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    std::ofstream f(path, std::ios::binary);
    if (f) {
        f.write(reinterpret_cast<const char*>(pFunction), bytes);
        spdlog::info("PSLogger: dumped {:016x} ({} bytes) -> {}",
                     hash, bytes,
                     std::string(path.begin(), path.end()));
    } else {
        spdlog::warn("PSLogger: failed to write dump for {:016x}", hash);
    }
}
#endif // HDTEX_DUMP_SHADERS

#ifdef HDTEX_LOG_SHADERS

// Shared ptr->hash map, populated on CreatePixelShader, never modified after.
// Both proxy devices write here; safe to read from either without locking since
// shader creation happens before any draw calls.
inline std::unordered_map<IDirect3DPixelShader9*, uint64_t>& PtrMap()
{
    static std::unordered_map<IDirect3DPixelShader9*, uint64_t> s;
    return s;
}

inline void Log(const DWORD* pFunction, IDirect3DPixelShader9* pResult)
{
    if (!pFunction || !pResult) return;

    const DWORD* p = pFunction;
    size_t tokens = 0;
    while (tokens < 16384 && *p++ != 0x0000FFFFu) ++tokens;
    ++tokens; // count END token

    uint64_t h = HashBytecode(pFunction);
    PtrMap()[pResult] = h;

    spdlog::info("PSLogger: ptr={:p}  hash={:016x}  tokens={}",
                 static_cast<void*>(pResult), h, tokens);

#ifdef HDTEX_DUMP_SHADERS
    DumpShader(pFunction, h);
#endif
}

// Per-instance helpers — called with refs to proxy member variables so two
// devices don't clobber each other's active-shader state.
inline void LogSetPS(IDirect3DPixelShader9* pShader, uint64_t& activeHash)
{
    if (!pShader) { activeHash = 0; return; }
    auto it = PtrMap().find(pShader);
    activeHash = (it != PtrMap().end()) ? it->second : 0;
}

inline const char* PrimTypeName(D3DPRIMITIVETYPE t)
{
    switch (t) {
        case D3DPT_POINTLIST:     return "POINTLIST";
        case D3DPT_LINELIST:      return "LINELIST";
        case D3DPT_LINESTRIP:     return "LINESTRIP";
        case D3DPT_TRIANGLELIST:  return "TRILIST";
        case D3DPT_TRIANGLESTRIP: return "TRISTRIP";
        case D3DPT_TRIANGLEFAN:   return "TRIFAN";
        default:                  return "UNKNOWN";
    }
}

inline void LogDraw(const char* call, D3DPRIMITIVETYPE type, UINT count,
                    uint64_t activeHash, uint32_t& drawCounter)
{
    uint32_t n = ++drawCounter;
    spdlog::info("{} #{}: type={}  primCount={}  shader_hash={:016x}",
                 call, n, PrimTypeName(type), count, activeHash);
}

#endif // HDTEX_LOG_SHADERS

} // namespace PSLogger

#endif // HDTEX_LOG_SHADERS || HDTEX_DUMP_SHADERS
