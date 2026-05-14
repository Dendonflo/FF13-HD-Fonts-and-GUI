#include <windows.h>
#include <d3d9.h>
#include <memory>
#include "IDirect3DDevice9.h"
#include "HDTextureReplacer.h"
#include "DoFFixer.h"
#include "PSLogger.h"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "MinHook.h"

// -------------------------------------------------------------------
// Diagnostic switches (controlled from version.vcxproj PreprocessorDefinitions)
//   HDTEX_DISABLE_D3D_HOOKS   : do not install MinHook hooks at all
//   HDTEX_DIAG_NO_DEV_WRAP    : return real, unwrapped device from CreateDevice
//   HDTEX_DIAG_NO_HD_TEXTURES : full wrapping but HDTextureReplacer is bypassed
// -------------------------------------------------------------------

// -------------------------------------------------------------------
// Globals
// -------------------------------------------------------------------
static HINSTANCE g_hDLL       = nullptr;
static HDTextureReplacer* g_HDTextures = nullptr;
static std::unique_ptr<HDTextureReplacer> g_HDTexturesOwner;

// Serialises all HDTextureReplacer calls (OnSetTexture, InvalidateTexture,
// ReleaseTextures). FF13 creates its device with D3DCREATE_MULTITHREADED so
// SetTexture and CreateTexture arrive concurrently from multiple threads.
// Without this lock the unordered_map accesses inside HDTextureReplacer race
// and corrupt the map state, causing a crash.
static CRITICAL_SECTION g_hdTexCS;

#ifdef HDTEX_HOT_RELOAD
static HANDLE       g_hotReloadStop   = nullptr;
static HANDLE       g_hotReloadThread = nullptr;
static std::wstring g_shaderDir;

static DWORD WINAPI HotReloadThreadProc(LPVOID)
{
    constexpr DWORD kIntervalMs = 1000 *
#ifdef HDTEX_HOT_RELOAD_INTERVAL_SEC
        HDTEX_HOT_RELOAD_INTERVAL_SEC;
#else
        5;
#endif
    while (WaitForSingleObject(g_hotReloadStop, kIntervalMs) == WAIT_TIMEOUT)
    {
        // Reload textures, hash database, and lazy-load config under the CS.
        EnterCriticalSection(&g_hdTexCS);
        if (g_HDTextures) g_HDTextures->HotReload();
        LeaveCriticalSection(&g_hdTexCS);

        // Reload shader replacements (hash_table.txt + .bin files).
        // DofPtrMap is NOT cleared — original ptr→hash associations persist.
        ReloadShaderHashTable(g_shaderDir);
    }
    return 0;
}
#endif

// -------------------------------------------------------------------
// CreateDevice / CreateDeviceEx vtable hooks
//
// We no longer wrap IDirect3D9 in a proxy. Doing so caused crashes because
// the Steam overlay and d3dx9_43.dll raw-cast / vtable-patch the IDirect3D9
// pointer assuming it is the real d3d9.dll internal object.
//
// Instead, at startup we create a temporary IDirect3D9 object just to read
// its vtable, then MinHook CreateDevice (slot 16) and CreateDeviceEx (slot 20)
// at the function-body level. Every call in the process is intercepted
// regardless of which IDirect3D9 instance it comes from, and the real D3D9
// objects are returned untouched to all callers.
// -------------------------------------------------------------------
static const int VTBL_IDX_CreateDevice   = 16;
static const int VTBL_IDX_CreateDeviceEx = 20;

typedef HRESULT (STDMETHODCALLTYPE* pfnCreateDevice)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

typedef HRESULT (STDMETHODCALLTYPE* pfnCreateDeviceEx)(
    IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);

static pfnCreateDevice   TrueCreateDevice   = nullptr;
static pfnCreateDeviceEx TrueCreateDeviceEx = nullptr;

typedef IDirect3D9* (WINAPI* pfnDirect3DCreate9)(UINT);
typedef HRESULT    (WINAPI* pfnDirect3DCreate9Ex)(UINT, IDirect3D9Ex**);
static pfnDirect3DCreate9   TrueDirect3DCreate9   = nullptr;
static pfnDirect3DCreate9Ex TrueDirect3DCreate9Ex = nullptr;
static bool g_createDeviceHooked   = false;
static bool g_createDeviceExHooked = false;

// Forward declaration — defined after HookCreateDeviceEx (which it references).
static void InstallCreateDeviceExHook(IDirect3D9Ex* pRealEx);

// -------------------------------------------------------------------
// Helper: get this DLL's own directory
// -------------------------------------------------------------------
static std::wstring GetDllDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hDLL, path, MAX_PATH);
    wchar_t* last = wcsrchr(path, L'\\');
    if (last) *last = L'\0';
    return path;
}

// -------------------------------------------------------------------
// version.dll forwarders — load the real system version.dll and forward
// every exported call to it.
// -------------------------------------------------------------------
static HMODULE g_RealVersion = nullptr;
typedef DWORD (WINAPI *PFN_GetFileVersionInfoSizeA)(LPCSTR, LPDWORD);
typedef DWORD (WINAPI *PFN_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI *PFN_GetFileVersionInfoA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *PFN_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *PFN_VerQueryValueA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL  (WINAPI *PFN_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
typedef DWORD (WINAPI *PFN_GetFileVersionInfoSizeExA)(DWORD, LPCSTR, LPDWORD);
typedef DWORD (WINAPI *PFN_GetFileVersionInfoSizeExW)(DWORD, LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI *PFN_GetFileVersionInfoExA)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *PFN_GetFileVersionInfoExW)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *PFN_VerFindFileA)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD (WINAPI *PFN_VerFindFileW)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD (WINAPI *PFN_VerInstallFileA)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, PUINT);
typedef DWORD (WINAPI *PFN_VerInstallFileW)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT);
typedef DWORD (WINAPI *PFN_VerLanguageNameA)(DWORD, LPSTR, DWORD);
typedef DWORD (WINAPI *PFN_VerLanguageNameW)(DWORD, LPWSTR, DWORD);

static PFN_GetFileVersionInfoSizeA   Real_GetFileVersionInfoSizeA   = nullptr;
static PFN_GetFileVersionInfoSizeW   Real_GetFileVersionInfoSizeW   = nullptr;
static PFN_GetFileVersionInfoA       Real_GetFileVersionInfoA       = nullptr;
static PFN_GetFileVersionInfoW       Real_GetFileVersionInfoW       = nullptr;
static PFN_VerQueryValueA            Real_VerQueryValueA            = nullptr;
static PFN_VerQueryValueW            Real_VerQueryValueW            = nullptr;
static PFN_GetFileVersionInfoSizeExA Real_GetFileVersionInfoSizeExA = nullptr;
static PFN_GetFileVersionInfoSizeExW Real_GetFileVersionInfoSizeExW = nullptr;
static PFN_GetFileVersionInfoExA     Real_GetFileVersionInfoExA     = nullptr;
static PFN_GetFileVersionInfoExW     Real_GetFileVersionInfoExW     = nullptr;
static PFN_VerFindFileA              Real_VerFindFileA              = nullptr;
static PFN_VerFindFileW              Real_VerFindFileW              = nullptr;
static PFN_VerInstallFileA           Real_VerInstallFileA           = nullptr;
static PFN_VerInstallFileW           Real_VerInstallFileW           = nullptr;
static PFN_VerLanguageNameA          Real_VerLanguageNameA          = nullptr;
static PFN_VerLanguageNameW          Real_VerLanguageNameW          = nullptr;

static void LoadRealVersionDll()
{
    wchar_t sysPath[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysPath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 12) return;
    wcscat_s(sysPath, MAX_PATH, L"\\version.dll");
    g_RealVersion = LoadLibraryW(sysPath);
    if (!g_RealVersion) return;
    Real_GetFileVersionInfoSizeA   = (PFN_GetFileVersionInfoSizeA)  GetProcAddress(g_RealVersion, "GetFileVersionInfoSizeA");
    Real_GetFileVersionInfoSizeW   = (PFN_GetFileVersionInfoSizeW)  GetProcAddress(g_RealVersion, "GetFileVersionInfoSizeW");
    Real_GetFileVersionInfoA       = (PFN_GetFileVersionInfoA)      GetProcAddress(g_RealVersion, "GetFileVersionInfoA");
    Real_GetFileVersionInfoW       = (PFN_GetFileVersionInfoW)      GetProcAddress(g_RealVersion, "GetFileVersionInfoW");
    Real_VerQueryValueA            = (PFN_VerQueryValueA)           GetProcAddress(g_RealVersion, "VerQueryValueA");
    Real_VerQueryValueW            = (PFN_VerQueryValueW)           GetProcAddress(g_RealVersion, "VerQueryValueW");
    Real_GetFileVersionInfoSizeExA = (PFN_GetFileVersionInfoSizeExA)GetProcAddress(g_RealVersion, "GetFileVersionInfoSizeExA");
    Real_GetFileVersionInfoSizeExW = (PFN_GetFileVersionInfoSizeExW)GetProcAddress(g_RealVersion, "GetFileVersionInfoSizeExW");
    Real_GetFileVersionInfoExA     = (PFN_GetFileVersionInfoExA)    GetProcAddress(g_RealVersion, "GetFileVersionInfoExA");
    Real_GetFileVersionInfoExW     = (PFN_GetFileVersionInfoExW)    GetProcAddress(g_RealVersion, "GetFileVersionInfoExW");
    Real_VerFindFileA              = (PFN_VerFindFileA)             GetProcAddress(g_RealVersion, "VerFindFileA");
    Real_VerFindFileW              = (PFN_VerFindFileW)             GetProcAddress(g_RealVersion, "VerFindFileW");
    Real_VerInstallFileA           = (PFN_VerInstallFileA)          GetProcAddress(g_RealVersion, "VerInstallFileA");
    Real_VerInstallFileW           = (PFN_VerInstallFileW)          GetProcAddress(g_RealVersion, "VerInstallFileW");
    Real_VerLanguageNameA          = (PFN_VerLanguageNameA)         GetProcAddress(g_RealVersion, "VerLanguageNameA");
    Real_VerLanguageNameW          = (PFN_VerLanguageNameW)         GetProcAddress(g_RealVersion, "VerLanguageNameW");
}

extern "C" {

DWORD WINAPI VerStub_GetFileVersionInfoSizeA(LPCSTR f, LPDWORD h) {
    return Real_GetFileVersionInfoSizeA ? Real_GetFileVersionInfoSizeA(f, h) : 0;
}
DWORD WINAPI VerStub_GetFileVersionInfoSizeW(LPCWSTR f, LPDWORD h) {
    return Real_GetFileVersionInfoSizeW ? Real_GetFileVersionInfoSizeW(f, h) : 0;
}
BOOL  WINAPI VerStub_GetFileVersionInfoA(LPCSTR f, DWORD h, DWORD l, LPVOID d) {
    return Real_GetFileVersionInfoA ? Real_GetFileVersionInfoA(f, h, l, d) : FALSE;
}
BOOL  WINAPI VerStub_GetFileVersionInfoW(LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
    return Real_GetFileVersionInfoW ? Real_GetFileVersionInfoW(f, h, l, d) : FALSE;
}
BOOL  WINAPI VerStub_VerQueryValueA(LPVOID b, LPCSTR s, LPVOID* p, PUINT u) {
    return Real_VerQueryValueA ? Real_VerQueryValueA(b, s, p, u) : FALSE;
}
BOOL  WINAPI VerStub_VerQueryValueW(LPVOID b, LPCWSTR s, LPVOID* p, PUINT u) {
    return Real_VerQueryValueW ? Real_VerQueryValueW(b, s, p, u) : FALSE;
}
DWORD WINAPI VerStub_GetFileVersionInfoSizeExA(DWORD f, LPCSTR n, LPDWORD h) {
    return Real_GetFileVersionInfoSizeExA ? Real_GetFileVersionInfoSizeExA(f, n, h) : 0;
}
DWORD WINAPI VerStub_GetFileVersionInfoSizeExW(DWORD f, LPCWSTR n, LPDWORD h) {
    return Real_GetFileVersionInfoSizeExW ? Real_GetFileVersionInfoSizeExW(f, n, h) : 0;
}
BOOL  WINAPI VerStub_GetFileVersionInfoExA(DWORD f, LPCSTR n, DWORD h, DWORD l, LPVOID d) {
    return Real_GetFileVersionInfoExA ? Real_GetFileVersionInfoExA(f, n, h, l, d) : FALSE;
}
BOOL  WINAPI VerStub_GetFileVersionInfoExW(DWORD f, LPCWSTR n, DWORD h, DWORD l, LPVOID d) {
    return Real_GetFileVersionInfoExW ? Real_GetFileVersionInfoExW(f, n, h, l, d) : FALSE;
}
DWORD WINAPI VerStub_VerFindFileA(DWORD f, LPSTR n, LPSTR wd, LPSTR ld, LPSTR df, PUINT dfl, LPSTR dd, PUINT ddl) {
    return Real_VerFindFileA ? Real_VerFindFileA(f, n, wd, ld, df, dfl, dd, ddl) : 0;
}
DWORD WINAPI VerStub_VerFindFileW(DWORD f, LPWSTR n, LPWSTR wd, LPWSTR ld, LPWSTR df, PUINT dfl, LPWSTR dd, PUINT ddl) {
    return Real_VerFindFileW ? Real_VerFindFileW(f, n, wd, ld, df, dfl, dd, ddl) : 0;
}
DWORD WINAPI VerStub_VerInstallFileA(DWORD f, LPSTR sf, LPSTR df, LPSTR sd, LPSTR dd, LPSTR cd, LPSTR n, PUINT nl) {
    return Real_VerInstallFileA ? Real_VerInstallFileA(f, sf, df, sd, dd, cd, n, nl) : 0;
}
DWORD WINAPI VerStub_VerInstallFileW(DWORD f, LPWSTR sf, LPWSTR df, LPWSTR sd, LPWSTR dd, LPWSTR cd, LPWSTR n, PUINT nl) {
    return Real_VerInstallFileW ? Real_VerInstallFileW(f, sf, df, sd, dd, cd, n, nl) : 0;
}
DWORD WINAPI VerStub_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD nSize) {
    return Real_VerLanguageNameA ? Real_VerLanguageNameA(wLang, szLang, nSize) : 0;
}
DWORD WINAPI VerStub_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD nSize) {
    return Real_VerLanguageNameW ? Real_VerLanguageNameW(wLang, szLang, nSize) : 0;
}

} // extern "C"

// -------------------------------------------------------------------
// Shared device-wrapping logic
// -------------------------------------------------------------------
static void WrapDevice(IDirect3DDevice9** ppDevice)
{
    if (!ppDevice || !*ppDevice) return;

#ifdef HDTEX_DIAG_NO_DEV_WRAP
    spdlog::info("HDTextures: device PASSTHROUGH real {:p}", (void*)*ppDevice);
    return;
#else
    IDirect3DDevice9Ex* pRealEx = static_cast<IDirect3DDevice9Ex*>(*ppDevice);
    IDirect3DDevice9Proxy* pProxy = new IDirect3DDevice9Proxy(pRealEx, g_HDTextures);
    spdlog::info("HDTextures: device proxy {:p} wrapping real {:p}",
                 (void*)pProxy, (void*)*ppDevice);
    *ppDevice = pProxy;
#endif
}

// -------------------------------------------------------------------
// HookCreateDevice — intercepts IDirect3D9::CreateDevice on all instances
// -------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE HookCreateDevice(
    IDirect3D9* pThis,
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPP,
    IDirect3DDevice9** ppDevice)
{
    spdlog::info("HDTextures: CreateDevice called (Adapter={}, DevType={})",
                 Adapter, (int)DeviceType);
    HRESULT hr = TrueCreateDevice(pThis, Adapter, DeviceType, hFocusWindow,
                                  BehaviorFlags, pPP, ppDevice);
    spdlog::info("HDTextures: real CreateDevice hr=0x{:08x}", (unsigned)hr);
    if (FAILED(hr)) return hr;

    // Stage 2: install CreateDeviceEx hook if it wasn't done in EnsureCreateDeviceHooks
    // (happens when Direct3DCreate9 returned a proxy whose QI rejects IDirect3D9Ex).
    // The real device returned here is d3d9.dll's own object — GetDirect3D() on it
    // returns the REAL IDirect3D9Ex singleton, bypassing every proxy wrapper.
    if (!g_createDeviceExHooked && ppDevice && *ppDevice) {
        IDirect3D9* pParent = nullptr;
        if (SUCCEEDED((*ppDevice)->GetDirect3D(&pParent)) && pParent) {
            IDirect3D9Ex* pParentEx = nullptr;
            if (SUCCEEDED(pParent->QueryInterface(__uuidof(IDirect3D9Ex),
                                                  reinterpret_cast<void**>(&pParentEx)))
                && pParentEx) {
                spdlog::info("HDTextures: got real IDirect3D9Ex {:p} via device->GetDirect3D",
                             (void*)pParentEx);
                InstallCreateDeviceExHook(pParentEx);
                pParentEx->Release();
            }
            pParent->Release();
        }
    }

    WrapDevice(ppDevice);
    return S_OK;
}

// -------------------------------------------------------------------
// HookCreateDeviceEx — intercepts IDirect3D9Ex::CreateDeviceEx on all instances
// -------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE HookCreateDeviceEx(
    IDirect3D9Ex* pThis,
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPP,
    D3DDISPLAYMODEEX* pFullscreenDisplayMode,
    IDirect3DDevice9Ex** ppDevice)
{
    spdlog::info("HDTextures: CreateDeviceEx called (Adapter={}, DevType={})",
                 Adapter, (int)DeviceType);
    HRESULT hr = TrueCreateDeviceEx(pThis, Adapter, DeviceType, hFocusWindow,
                                    BehaviorFlags, pPP, pFullscreenDisplayMode, ppDevice);
    spdlog::info("HDTextures: real CreateDeviceEx hr=0x{:08x}", (unsigned)hr);
    if (FAILED(hr)) return hr;
    WrapDevice(reinterpret_cast<IDirect3DDevice9**>(ppDevice));
    return S_OK;
}

// -------------------------------------------------------------------
// Install CreateDeviceEx hook from a verified real IDirect3D9Ex pointer.
// Separated from EnsureCreateDeviceHooks so it can be called deferred
// (from HookCreateDevice) when the Direct3DCreate9 return value is a
// proxy whose QI rejects IID_IDirect3D9Ex.
// -------------------------------------------------------------------
static void InstallCreateDeviceExHook(IDirect3D9Ex* pRealEx)
{
    if (g_createDeviceExHooked) return;
    g_createDeviceExHooked = true;

    void** vtbl = *reinterpret_cast<void***>(pRealEx);
    void* pFnCreateEx = vtbl[VTBL_IDX_CreateDeviceEx];
    if (MH_CreateHook(pFnCreateEx, &HookCreateDeviceEx,
                      reinterpret_cast<void**>(&TrueCreateDeviceEx)) == MH_OK) {
        MH_EnableHook(pFnCreateEx);
        spdlog::info("HDTextures: CreateDeviceEx hook installed");
    } else {
        spdlog::error("HDTextures: failed to hook CreateDeviceEx");
    }
}

// -------------------------------------------------------------------
// Install CreateDevice / CreateDeviceEx vtable hooks (once).
// Called from HookDirect3DCreate9/Ex on first firing.
//
// pD3DObject may be a proxy (e.g. FF13Fix wraps IDirect3D9 in its own
// hkIDirect3D9 which only inherits IDirect3D9 and returns E_NOINTERFACE
// for IID_IDirect3D9Ex from QueryInterface).
//
// CreateDevice (slot 16): safe to read from any IDirect3D9 proxy —
//   slot 16 is valid on everything that implements IDirect3D9.
//
// CreateDeviceEx (slot 20): MUST come from a real IDirect3D9Ex vtable.
//   Reading slot 20 from an IDirect3D9-only proxy reads 12 bytes past
//   the end of its vtable — a garbage pointer.  Hooking that corrupts
//   a random unrelated function and causes an immediate crash.
//   Two-stage approach:
//     Stage 1 (here): try QI(IID_IDirect3D9Ex). Works when no proxy is
//       present. If QI fails, skip slot 20 entirely.
//     Stage 2 (HookCreateDevice): after the first real device is created,
//       call device->GetDirect3D() which returns the REAL IDirect3D9Ex
//       from inside d3d9.dll, completely bypassing all wrappers, and
//       install the CreateDeviceEx hook from that pointer.
// -------------------------------------------------------------------
static void EnsureCreateDeviceHooks(void* pD3DObject)
{
    if (g_createDeviceHooked) return;
    g_createDeviceHooked = true;

    // Stage 1: try QI for real IDirect3D9Ex vtable source.
    IDirect3D9Ex* pReal = nullptr;
    const bool bQIOk = SUCCEEDED(
        reinterpret_cast<IUnknown*>(pD3DObject)->QueryInterface(
            __uuidof(IDirect3D9Ex), reinterpret_cast<void**>(&pReal))
    ) && pReal != nullptr;

    void* pVtblSrc = bQIOk ? static_cast<void*>(pReal) : pD3DObject;
    spdlog::info("HDTextures: reading CreateDevice vtable from {:p} (QI IDirect3D9Ex: {})",
                 pVtblSrc, bQIOk ? "ok" : "failed - CreateDeviceEx hook deferred to first CreateDevice");

    void** vtbl = *reinterpret_cast<void***>(pVtblSrc);

    // Hook CreateDevice (slot 16) — valid on proxy and real alike.
    void* pFnCreate = vtbl[VTBL_IDX_CreateDevice];
    if (MH_CreateHook(pFnCreate, &HookCreateDevice,
                      reinterpret_cast<void**>(&TrueCreateDevice)) == MH_OK) {
        MH_EnableHook(pFnCreate);
        spdlog::info("HDTextures: CreateDevice hook installed");
    } else {
        spdlog::error("HDTextures: failed to hook CreateDevice");
    }

    // Hook CreateDeviceEx (slot 20) — only when we have the real IDirect3D9Ex.
    // If QI failed, installation is deferred to HookCreateDevice (stage 2).
    if (bQIOk) {
        InstallCreateDeviceExHook(pReal);
        pReal->Release(); // balance QI AddRef; game still holds its own ref
    }
}

// -------------------------------------------------------------------
// Direct3DCreate9 / Direct3DCreate9Ex hooks
// These exist solely to intercept the first call so we can read the
// vtable and install CreateDevice hooks. The real object is returned
// untouched — no IDirect3D9 proxy, no wrapping.
// -------------------------------------------------------------------
static IDirect3D9* WINAPI HookDirect3DCreate9(UINT SDKVersion)
{
    IDirect3D9* pRaw = TrueDirect3DCreate9(SDKVersion);
    if (!pRaw) {
        spdlog::warn("HDTextures: Direct3DCreate9 returned null");
        return nullptr;
    }
    EnsureCreateDeviceHooks(pRaw);
    spdlog::info("HDTextures: Direct3DCreate9 -> real {:p}", (void*)pRaw);
    return pRaw;
}

static HRESULT WINAPI HookDirect3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D)
{
    HRESULT hr = TrueDirect3DCreate9Ex(SDKVersion, ppD3D);
    if (FAILED(hr) || !ppD3D || !*ppD3D) return hr;
    EnsureCreateDeviceHooks(*ppD3D);
    spdlog::info("HDTextures: Direct3DCreate9Ex -> real {:p}", (void*)*ppD3D);
    return S_OK;
}

// -------------------------------------------------------------------
// IDirect3DDevice9Proxy — three intercepted methods
// -------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE IDirect3DDevice9Proxy::Reset(D3DPRESENT_PARAMETERS* pPP)
{
    DEV_TRACE("Reset");
    EnterCriticalSection(&g_hdTexCS);
    if (g_HDTextures) g_HDTextures->ReleaseTextures();
    LeaveCriticalSection(&g_hdTexCS);
    return m_pReal->Reset(pPP);
}

HRESULT STDMETHODCALLTYPE IDirect3DDevice9Proxy::CreateTexture(
    UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
    HRESULT hr = m_pReal->CreateTexture(Width, Height, Levels, Usage, Format,
                                        Pool, ppTexture, pSharedHandle);
#ifndef HDTEX_DIAG_NO_HD_TEXTURES
    if (SUCCEEDED(hr) && ppTexture && *ppTexture && g_HDTextures) {
        EnterCriticalSection(&g_hdTexCS);
        g_HDTextures->InvalidateTexture(*ppTexture);
        LeaveCriticalSection(&g_hdTexCS);
    }
#endif
    return hr;
}

HRESULT STDMETHODCALLTYPE IDirect3DDevice9Proxy::SetTexture(
    DWORD Stage, IDirect3DBaseTexture9* pTexture)
{
    IDirect3DBaseTexture9* pFinal = pTexture;
#ifndef HDTEX_DIAG_NO_HD_TEXTURES
    if (pTexture && g_HDTextures) {
        EnterCriticalSection(&g_hdTexCS);
        IDirect3DBaseTexture9* pHD = g_HDTextures->OnSetTexture(m_pReal, pTexture);
        LeaveCriticalSection(&g_hdTexCS);
        if (pHD) pFinal = pHD;
    }
#endif
    return m_pReal->SetTexture(Stage, pFinal);
}

// -------------------------------------------------------------------
// DLL entry point
// -------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hDLL = hInstDLL;
        InitializeCriticalSection(&g_hdTexCS);

        // NOTE: Do NOT call DisableThreadLibraryCalls — static CRT (/MT)
        // requires DLL_THREAD_ATTACH / DLL_THREAD_DETACH notifications.
        //
        // Pin our module so it can't be unloaded.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR)&DllMain, &pinned);

        LoadRealVersionDll();

        std::wstring dllDir = GetDllDir();
        std::string logPath =
            std::string(dllDir.begin(), dllDir.end()) + "\\HDTextures.log";
        try {
            auto logger = spdlog::basic_logger_mt("HDTextures", logPath, true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v");
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::trace);
            spdlog::info("HDTextures mod loaded");
        } catch (...) {}

        LoadShaderHashTable(dllDir + L"\\hd_textures_shaders");

        // Always set dump dir — used by HDTEX_DUMP_SHADERS.
        PSLogger::SetDumpDir(dllDir + L"\\shader_dumps");
#ifdef HDTEX_DUMP_SHADERS
        spdlog::info("HDTextures: shader dump enabled -> shader_dumps\\");
#endif

#ifndef HDTEX_DIAG_NO_CONSTRUCT
        try {
            g_HDTexturesOwner = std::make_unique<HDTextureReplacer>();
            g_HDTextures      = g_HDTexturesOwner.get();
#ifndef HDTEX_DIAG_SKIP_HD_INIT
            g_HDTextures->Init(dllDir);
#ifdef HDTEX_HOT_RELOAD
            g_shaderDir       = dllDir + L"\\hd_textures_shaders";
            g_hotReloadStop   = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            g_hotReloadThread = CreateThread(nullptr, 0, HotReloadThreadProc, nullptr, 0, nullptr);
            spdlog::info("HDTextures: hot reload enabled (interval {}s) — textures, hashes, shaders",
#ifdef HDTEX_HOT_RELOAD_INTERVAL_SEC
                HDTEX_HOT_RELOAD_INTERVAL_SEC
#else
                5
#endif
            );
#endif
#else
            spdlog::info("HDTextures: Init() SKIPPED (diagnostic build)");
#endif
        } catch (...) {
            spdlog::error("HDTextures: init failed (exception)");
            g_HDTexturesOwner.reset();
            g_HDTextures = nullptr;
        }
#else
        spdlog::info("HDTextures: HDTextureReplacer NOT constructed (diagnostic build)");
#endif

#ifndef HDTEX_DISABLE_D3D_HOOKS
        MH_Initialize();

        // Hook Direct3DCreate9 and Direct3DCreate9Ex. These hooks exist only to
        // intercept the first call at game-runtime so we can read the vtable of
        // the real IDirect3D9 object and install CreateDevice/CreateDeviceEx hooks.
        // The real IDirect3D9 objects are returned untouched — no proxy wrapping.
        // Hooking function bytes here is safe from DllMain; actually calling
        // Direct3DCreate9 from DllMain is not (d3d9.dll loads additional DLLs on
        // first init which deadlocks against the loader lock we hold).
        HMODULE hD3D9 = LoadLibraryW(L"d3d9.dll");
        if (hD3D9) {
            void* pCreate9 = GetProcAddress(hD3D9, "Direct3DCreate9");
            if (pCreate9) {
                MH_CreateHook(pCreate9, HookDirect3DCreate9,
                              reinterpret_cast<void**>(&TrueDirect3DCreate9));
                MH_EnableHook(pCreate9);
                spdlog::info("HDTextures: Direct3DCreate9 hook installed");
            }
            void* pCreate9Ex = GetProcAddress(hD3D9, "Direct3DCreate9Ex");
            if (pCreate9Ex) {
                MH_CreateHook(pCreate9Ex, HookDirect3DCreate9Ex,
                              reinterpret_cast<void**>(&TrueDirect3DCreate9Ex));
                MH_EnableHook(pCreate9Ex);
                spdlog::info("HDTextures: Direct3DCreate9Ex hook installed");
            }
        } else {
            spdlog::error("HDTextures: d3d9.dll not found");
        }
#else
        spdlog::info("HDTextures: D3D hooks DISABLED (diagnostic build)");
#endif
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
#ifdef HDTEX_HOT_RELOAD
        if (g_hotReloadStop)   { SetEvent(g_hotReloadStop); }
        if (g_hotReloadThread) { WaitForSingleObject(g_hotReloadThread, 2000); CloseHandle(g_hotReloadThread); g_hotReloadThread = nullptr; }
        if (g_hotReloadStop)   { CloseHandle(g_hotReloadStop); g_hotReloadStop = nullptr; }
#endif
#ifndef HDTEX_DISABLE_D3D_HOOKS
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
#endif
        g_HDTexturesOwner.reset();
        g_HDTextures = nullptr;
        if (g_RealVersion) { FreeLibrary(g_RealVersion); g_RealVersion = nullptr; }
        DeleteCriticalSection(&g_hdTexCS);
        spdlog::shutdown();
    }

    return TRUE;
}
