#pragma once
#include <d3d9.h>

// IDirect3D9 proxy that also implements IDirect3D9Ex.
//
// WHY IDirect3D9Ex: the system d3d9.dll returns an IDirect3D9Ex singleton from
// Direct3DCreate9. Some callers (Steam overlay, the game itself) query for
// IDirect3D9Ex via QueryInterface, or even raw-cast the returned pointer and
// call Ex methods directly. If our proxy only inherits from IDirect3D9, those
// calls land in vtable slots beyond our object -> immediate crash.
// Inheriting from IDirect3D9Ex fills every slot the callers might touch.

class IDirect3D9Proxy : public IDirect3D9Ex {
public:
    explicit IDirect3D9Proxy(IDirect3D9Ex* pReal) : m_pReal(pReal), m_ref(1) {}

    // -----------------------------------------------------------------------
    // IUnknown
    // -----------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (!ppvObj) return E_POINTER;

        if (riid == __uuidof(IUnknown)    ||
            riid == __uuidof(IDirect3D9)  ||
            riid == __uuidof(IDirect3D9Ex))
        {
            AddRef();
            *ppvObj = this;
            return S_OK;
        }
        return m_pReal->QueryInterface(riid, ppvObj);
    }

    // Independent refcount: AddRef/Release only touch the proxy's own counter.
    // The proxy holds exactly ONE reference on the real object (inherited from the
    // Direct3DCreate9/Ex call that returned it) and releases that single reference
    // when the proxy's own refcount reaches zero.
    //
    // WHY NOT MIRRORED: Direct3DCreate9 returns the same IDirect3D9Ex singleton
    // pointer for multiple calls (calls 2/3/4 in the log all return the same
    // address). That means several IDirect3D9Proxy objects wrap the same real
    // object. With mirrored refcounting, a QueryInterface + Release cycle on any
    // one proxy mirrors to the shared real object and can drive its refcount to
    // zero while the other proxies still hold pointers to it → use-after-free crash.
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG ref = InterlockedDecrement(&m_ref);
        if (ref != 0) return ref;
        // Last reference: release the one real reference we've held since construction.
        IDirect3D9Ex* pWrapped = m_pReal;
        m_pReal = nullptr;
        delete this;
        pWrapped->Release();
        return 0;
    }

    // -----------------------------------------------------------------------
    // IDirect3D9
    // -----------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) override {
        return m_pReal->RegisterSoftwareDevice(pInitializeFunction);
    }
    UINT STDMETHODCALLTYPE GetAdapterCount() override {
        return m_pReal->GetAdapterCount();
    }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override {
        return m_pReal->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override {
        return m_pReal->GetAdapterModeCount(Adapter, Format);
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override {
        return m_pReal->EnumAdapterModes(Adapter, Format, Mode, pMode);
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override {
        return m_pReal->GetAdapterDisplayMode(Adapter, pMode);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) override {
        return m_pReal->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override {
        return m_pReal->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) override {
        return m_pReal->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
    }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override {
        return m_pReal->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override {
        return m_pReal->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override {
        return m_pReal->GetDeviceCaps(Adapter, DeviceType, pCaps);
    }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override {
        return m_pReal->GetAdapterMonitor(Adapter);
    }

    // CreateDevice — implemented in dllmain.cpp (needs HDTextureReplacer)
    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
        DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
        IDirect3DDevice9** ppReturnedDeviceInterface) override;

    // -----------------------------------------------------------------------
    // IDirect3D9Ex  (all forward to m_pReal as IDirect3D9Ex)
    // -----------------------------------------------------------------------
    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER* pFilter) override {
        return m_pReal->GetAdapterModeCountEx(Adapter, pFilter);
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER* pFilter, UINT Mode, D3DDISPLAYMODEEX* pMode) override {
        return m_pReal->EnumAdapterModesEx(Adapter, pFilter, Mode, pMode);
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) override {
        return m_pReal->GetAdapterDisplayModeEx(Adapter, pMode, pRotation);
    }
    // CreateDeviceEx — also implemented in dllmain.cpp
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
        DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
        D3DDISPLAYMODEEX* pFullscreenDisplayMode,
        IDirect3DDevice9Ex** ppReturnedDeviceInterface) override;

    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT Adapter, LUID* pLUID) override {
        return m_pReal->GetAdapterLUID(Adapter, pLUID);
    }

private:
    IDirect3D9Ex*  m_pReal;
    volatile LONG  m_ref;
};
