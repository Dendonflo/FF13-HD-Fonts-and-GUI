#pragma once

#include <d3d9.h>
#include <memory>
#include "HDTextureReplacer.h"
#include "spdlog/spdlog.h"

// Diagnostic-only: log every device method called by the game, so we can
// identify the last method invoked before a crash. Define HDTEX_TRACE_DEVICE
// to enable. Each call writes one short line to HDTextures.log.
#ifdef HDTEX_TRACE_DEVICE
  #define DEV_TRACE(name) spdlog::info("DEV::" name)
#else
  #define DEV_TRACE(name) ((void)0)
#endif

// IDirect3DDevice9 proxy that also implements IDirect3DDevice9Ex.
//
// WHY IDirect3DDevice9Ex: the system d3d9.dll returns IDirect3DDevice9Ex objects
// even from CreateDevice. The Steam overlay and game code call QueryInterface for
// IDirect3DDevice9Ex and invoke Ex methods directly. A proxy inheriting only from
// IDirect3DDevice9 has no Ex vtable slots -> immediate crash.
// Inheriting from IDirect3DDevice9Ex fills every slot callers might touch.

class IDirect3DDevice9Proxy : public IDirect3DDevice9Ex {
public:
    IDirect3DDevice9Proxy(IDirect3DDevice9Ex* pReal, HDTextureReplacer* pTexReplacer)
        : m_pReal(pReal), m_pTexReplacer(pTexReplacer), m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override {
#ifdef HDTEX_TRACE_DEVICE
        spdlog::info("DEV::QueryInterface  riid={{{:08X}-{:04X}-{:04X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
            riid.Data1, riid.Data2, riid.Data3,
            riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
            riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
#endif
        if (!ppvObj) return E_POINTER;
        if (riid == IID_IUnknown         ||
            riid == IID_IDirect3DDevice9)
        {
            *ppvObj = this;
            AddRef();
            return S_OK;
        }
        // For IDirect3DDevice9Ex queries (and any other unrecognized riid),
        // forward to the real device. The caller (typically d3dx9_43.dll
        // inside D3DXCreateTexture/etc.) gets the real Ex pointer and uses
        // it directly. Returning *this for the Ex query causes d3dx9 to
        // crash deep inside D3DXCreateTexture — likely because d3dx9 takes
        // shortcuts that assume the device is a stock d3d9.dll-internal
        // object with a specific vtable layout it can introspect, and our
        // derived class doesn't match.
        //
        // Trade-off: D3DX-internal calls bypass our HD texture interception.
        // FF13's GUI textures are loaded via direct device->CreateTexture,
        // not D3DXCreateTexture, so this path doesn't affect the mod's
        // primary purpose.
        HRESULT hr = m_pReal->QueryInterface(riid, ppvObj);
#ifdef HDTEX_TRACE_DEVICE
        spdlog::info("DEV::QueryInterface forwarded -> hr=0x{:08X} ppv={:p}", (unsigned)hr, *ppvObj);
#endif
        return hr;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        DEV_TRACE("AddRef");
        // Independent refcount: the game's AddRef on the proxy only
        // increments OUR count. We hold exactly one reference on the
        // real device (taken at construction) and release it exactly
        // once when our refcount reaches zero. This avoids the game
        // accidentally over-releasing the real device when it holds
        // aliased pointers to it (via GetDirect3D, child resource
        // GetDevice, etc.).
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        DEV_TRACE("Release");
        const ULONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) {
            // Last reference on our proxy: release our single ref on
            // the real device. Don't delete the proxy itself — the game
            // may still hold aliased pointers; let it leak (small and
            // bounded).
            if (m_pReal) {
                m_pReal->Release();
                m_pReal = nullptr;
            }
        }
        return ref;
    }

    // IDirect3DDevice9 methods - proxy everything
    // Most methods just forward to real device...
    // For brevity, showing key ones

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {
        DEV_TRACE("TestCooperativeLevel");
        return m_pReal->TestCooperativeLevel();
    }

    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override {
        DEV_TRACE("GetAvailableTextureMem");
        return m_pReal->GetAvailableTextureMem();
    }

    HRESULT STDMETHODCALLTYPE EvictManagedResources() override {
        DEV_TRACE("EvictManagedResources");
        return m_pReal->EvictManagedResources();
    }

    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override {
        DEV_TRACE("GetDirect3D");
        return m_pReal->GetDirect3D(ppD3D9);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override {
        DEV_TRACE("GetDeviceCaps");
        return m_pReal->GetDeviceCaps(pCaps);
    }

    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override {
        DEV_TRACE("GetDisplayMode");
        return m_pReal->GetDisplayMode(iSwapChain, pMode);
    }

    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) override {
        DEV_TRACE("GetCreationParameters");
        return m_pReal->GetCreationParameters(pParameters);
    }

    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override {
        DEV_TRACE("SetCursorProperties");
        return m_pReal->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
    }

    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override {
        DEV_TRACE("SetCursorPosition");
        m_pReal->SetCursorPosition(X, Y, Flags);
    }

    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override {
        DEV_TRACE("ShowCursor");
        return m_pReal->ShowCursor(bShow);
    }

    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override {
        DEV_TRACE("CreateAdditionalSwapChain");
        return m_pReal->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
    }

    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override {
        DEV_TRACE("GetSwapChain");
        return m_pReal->GetSwapChain(iSwapChain, pSwapChain);
    }

    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override {
        DEV_TRACE("GetNumberOfSwapChains");
        return m_pReal->GetNumberOfSwapChains();
    }

    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override;

    HRESULT STDMETHODCALLTYPE Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) override {
        DEV_TRACE("Present");
        return m_pReal->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override {
        DEV_TRACE("GetBackBuffer");
        return m_pReal->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
    }

    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override {
        DEV_TRACE("GetRasterStatus"); return m_pReal->GetRasterStatus(iSwapChain, pRasterStatus);
    }

    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override {
        DEV_TRACE("SetDialogBoxMode"); return m_pReal->SetDialogBoxMode(bEnableDialogs);
    }

    void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) override {
        DEV_TRACE("SetGammaRamp"); m_pReal->SetGammaRamp(iSwapChain, Flags, pRamp);
    }

    void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override {
        DEV_TRACE("GetGammaRamp"); m_pReal->GetGammaRamp(iSwapChain, pRamp);
    }

    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle);

    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateVolumeTexture"); return m_pReal->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateCubeTexture"); return m_pReal->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateVertexBuffer"); return m_pReal->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateIndexBuffer"); return m_pReal->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateRenderTarget"); return m_pReal->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateDepthStencilSurface"); return m_pReal->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, const POINT* pDestPoint) override {
        DEV_TRACE("UpdateSurface"); return m_pReal->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    }

    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override {
        DEV_TRACE("UpdateTexture"); return m_pReal->UpdateTexture(pSourceTexture, pDestinationTexture);
    }

    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override {
        DEV_TRACE("GetRenderTargetData"); return m_pReal->GetRenderTargetData(pRenderTarget, pDestSurface);
    }

    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override {
        DEV_TRACE("GetFrontBufferData"); return m_pReal->GetFrontBufferData(iSwapChain, pDestSurface);
    }

    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestSurface, const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override {
        DEV_TRACE("StretchRect"); return m_pReal->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
    }

    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* pSurface, const RECT* pRect, D3DCOLOR color) override {
        DEV_TRACE("ColorFill"); return m_pReal->ColorFill(pSurface, pRect, color);
    }

    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        DEV_TRACE("CreateOffscreenPlainSurface"); return m_pReal->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
    }

    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override {
        DEV_TRACE("SetRenderTarget"); return m_pReal->SetRenderTarget(RenderTargetIndex, pRenderTarget);
    }

    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override {
        DEV_TRACE("GetRenderTarget"); return m_pReal->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
    }

    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override {
        DEV_TRACE("SetDepthStencilSurface"); return m_pReal->SetDepthStencilSurface(pNewZStencil);
    }

    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override {
        DEV_TRACE("GetDepthStencilSurface"); return m_pReal->GetDepthStencilSurface(ppZStencilSurface);
    }

    HRESULT STDMETHODCALLTYPE BeginScene() override {
        DEV_TRACE("BeginScene");
        return m_pReal->BeginScene();
    }

    HRESULT STDMETHODCALLTYPE EndScene() override {
        DEV_TRACE("EndScene");
        return m_pReal->EndScene();
    }

    HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override {
        DEV_TRACE("Clear");
        return m_pReal->Clear(Count, pRects, Flags, Color, Z, Stencil);
    }

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
        DEV_TRACE("SetTransform"); return m_pReal->SetTransform(State, pMatrix);
    }

    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override {
        DEV_TRACE("GetTransform"); return m_pReal->GetTransform(State, pMatrix);
    }

    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
        DEV_TRACE("MultiplyTransform"); return m_pReal->MultiplyTransform(State, pMatrix);
    }

    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* pViewport) override {
        DEV_TRACE("SetViewport"); return m_pReal->SetViewport(pViewport);
    }

    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override {
        DEV_TRACE("GetViewport"); return m_pReal->GetViewport(pViewport);
    }

    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* pMaterial) override {
        DEV_TRACE("SetMaterial"); return m_pReal->SetMaterial(pMaterial);
    }

    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override {
        DEV_TRACE("GetMaterial"); return m_pReal->GetMaterial(pMaterial);
    }

    HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, const D3DLIGHT9* pLight) override {
        DEV_TRACE("SetLight"); return m_pReal->SetLight(Index, pLight);
    }

    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9* pLight) override {
        DEV_TRACE("GetLight"); return m_pReal->GetLight(Index, pLight);
    }

    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override {
        DEV_TRACE("LightEnable"); return m_pReal->LightEnable(Index, Enable);
    }

    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override {
        DEV_TRACE("GetLightEnable"); return m_pReal->GetLightEnable(Index, pEnable);
    }

    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, const float* pPlane) override {
        DEV_TRACE("SetClipPlane"); return m_pReal->SetClipPlane(Index, pPlane);
    }

    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override {
        DEV_TRACE("GetClipPlane"); return m_pReal->GetClipPlane(Index, pPlane);
    }

    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
        DEV_TRACE("SetRenderState"); return m_pReal->SetRenderState(State, Value);
    }

    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override {
        DEV_TRACE("GetRenderState"); return m_pReal->GetRenderState(State, pValue);
    }

    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override {
        DEV_TRACE("CreateStateBlock"); return m_pReal->CreateStateBlock(Type, ppSB);
    }

    HRESULT STDMETHODCALLTYPE BeginStateBlock() override {
        DEV_TRACE("BeginStateBlock"); return m_pReal->BeginStateBlock();
    }

    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override {
        DEV_TRACE("EndStateBlock"); return m_pReal->EndStateBlock(ppSB);
    }

    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9* pClipStatus) override {
        DEV_TRACE("SetClipStatus"); return m_pReal->SetClipStatus(pClipStatus);
    }

    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override {
        DEV_TRACE("GetClipStatus"); return m_pReal->GetClipStatus(pClipStatus);
    }

    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override {
        DEV_TRACE("GetTexture"); return m_pReal->GetTexture(Stage, ppTexture);
    }

    // SetTexture - HOOKED to check for HD replacements
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture);

    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
        DEV_TRACE("SetTextureStageState"); return m_pReal->SetTextureStageState(Stage, Type, Value);
    }

    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override {
        DEV_TRACE("GetTextureStageState"); return m_pReal->GetTextureStageState(Stage, Type, pValue);
    }

    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override {
        DEV_TRACE("SetSamplerState"); return m_pReal->SetSamplerState(Sampler, Type, Value);
    }

    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override {
        DEV_TRACE("GetSamplerState"); return m_pReal->GetSamplerState(Sampler, Type, pValue);
    }

    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) override {
        DEV_TRACE("ValidateDevice"); return m_pReal->ValidateDevice(pNumPasses);
    }

    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY* pEntries) override {
        DEV_TRACE("SetPaletteEntries"); return m_pReal->SetPaletteEntries(PaletteNumber, pEntries);
    }

    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override {
        DEV_TRACE("GetPaletteEntries"); return m_pReal->GetPaletteEntries(PaletteNumber, pEntries);
    }

    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override {
        DEV_TRACE("SetCurrentTexturePalette"); return m_pReal->SetCurrentTexturePalette(PaletteNumber);
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* PaletteNumber) override {
        DEV_TRACE("GetCurrentTexturePalette"); return m_pReal->GetCurrentTexturePalette(PaletteNumber);
    }

    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* pRect) override {
        DEV_TRACE("SetScissorRect"); return m_pReal->SetScissorRect(pRect);
    }

    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override {
        DEV_TRACE("GetScissorRect"); return m_pReal->GetScissorRect(pRect);
    }

    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override {
        DEV_TRACE("SetSoftwareVertexProcessing"); return m_pReal->SetSoftwareVertexProcessing(bSoftware);
    }

    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override {
        DEV_TRACE("GetSoftwareVertexProcessing"); return m_pReal->GetSoftwareVertexProcessing();
    }

    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override {
        DEV_TRACE("SetNPatchMode"); return m_pReal->SetNPatchMode(nSegments);
    }

    float STDMETHODCALLTYPE GetNPatchMode() override {
        DEV_TRACE("GetNPatchMode"); return m_pReal->GetNPatchMode();
    }

    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override {
        DEV_TRACE("DrawPrimitive"); return m_pReal->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount) override {
        DEV_TRACE("DrawIndexedPrimitive"); return m_pReal->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }

    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override {
        DEV_TRACE("DrawPrimitiveUP"); return m_pReal->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override {
        DEV_TRACE("DrawIndexedPrimitiveUP"); return m_pReal->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
    }

    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override {
        DEV_TRACE("ProcessVertices"); return m_pReal->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
    }

    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override {
        DEV_TRACE("CreateVertexDeclaration"); return m_pReal->CreateVertexDeclaration(pVertexElements, ppDecl);
    }

    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override {
        DEV_TRACE("SetVertexDeclaration"); return m_pReal->SetVertexDeclaration(pDecl);
    }

    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override {
        DEV_TRACE("GetVertexDeclaration"); return m_pReal->GetVertexDeclaration(ppDecl);
    }

    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override {
        DEV_TRACE("SetFVF"); return m_pReal->SetFVF(FVF);
    }

    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override {
        DEV_TRACE("GetFVF"); return m_pReal->GetFVF(pFVF);
    }

    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) override {
        DEV_TRACE("CreateVertexShader"); return m_pReal->CreateVertexShader(pFunction, ppShader);
    }

    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override {
        DEV_TRACE("SetVertexShader"); return m_pReal->SetVertexShader(pShader);
    }

    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override {
        DEV_TRACE("GetVertexShader"); return m_pReal->GetVertexShader(ppShader);
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override {
        DEV_TRACE("SetVertexShaderConstantF"); return m_pReal->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override {
        DEV_TRACE("GetVertexShaderConstantF"); return m_pReal->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) override {
        DEV_TRACE("SetVertexShaderConstantI"); return m_pReal->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override {
        DEV_TRACE("GetVertexShaderConstantI"); return m_pReal->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) override {
        DEV_TRACE("SetVertexShaderConstantB"); return m_pReal->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override {
        DEV_TRACE("GetVertexShaderConstantB"); return m_pReal->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }

    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override {
        DEV_TRACE("SetStreamSource"); return m_pReal->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
    }

    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override {
        DEV_TRACE("GetStreamSource"); return m_pReal->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
    }

    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override {
        DEV_TRACE("SetStreamSourceFreq"); return m_pReal->SetStreamSourceFreq(StreamNumber, Setting);
    }

    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override {
        DEV_TRACE("GetStreamSourceFreq"); return m_pReal->GetStreamSourceFreq(StreamNumber, pSetting);
    }

    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override {
        DEV_TRACE("SetIndices"); return m_pReal->SetIndices(pIndexData);
    }

    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override {
        DEV_TRACE("GetIndices"); return m_pReal->GetIndices(ppIndexData);
    }

    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) override {
        DEV_TRACE("CreatePixelShader"); return m_pReal->CreatePixelShader(pFunction, ppShader);
    }

    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override {
        DEV_TRACE("SetPixelShader"); return m_pReal->SetPixelShader(pShader);
    }

    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override {
        DEV_TRACE("GetPixelShader"); return m_pReal->GetPixelShader(ppShader);
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override {
        DEV_TRACE("SetPixelShaderConstantF"); return m_pReal->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override {
        DEV_TRACE("GetPixelShaderConstantF"); return m_pReal->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) override {
        DEV_TRACE("SetPixelShaderConstantI"); return m_pReal->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override {
        DEV_TRACE("GetPixelShaderConstantI"); return m_pReal->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) override {
        DEV_TRACE("SetPixelShaderConstantB"); return m_pReal->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override {
        DEV_TRACE("GetPixelShaderConstantB"); return m_pReal->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }

    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, const float* pNumSegments, const D3DRECTPATCH_INFO* pRectPatchInfo) override {
        DEV_TRACE("DrawRectPatch"); return m_pReal->DrawRectPatch(Handle, pNumSegments, pRectPatchInfo);
    }

    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, const float* pNumSegments, const D3DTRIPATCH_INFO* pTriPatchInfo) override {
        DEV_TRACE("DrawTriPatch"); return m_pReal->DrawTriPatch(Handle, pNumSegments, pTriPatchInfo);
    }

    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override {
        DEV_TRACE("DeletePatch"); return m_pReal->DeletePatch(Handle);
    }

    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override {
        DEV_TRACE("CreateQuery"); return m_pReal->CreateQuery(Type, ppQuery);
    }

    // -----------------------------------------------------------------------
    // IDirect3DDevice9Ex methods (all forward to m_pReal)
    // -----------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT width, UINT height, float* rows, float* columns) override {
        DEV_TRACE("SetConvolutionMonoKernel"); return m_pReal->SetConvolutionMonoKernel(width, height, rows, columns);
    }
    HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset) override {
        DEV_TRACE("ComposeRects"); return m_pReal->ComposeRects(pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs, Operation, Xoffset, Yoffset);
    }
    HRESULT STDMETHODCALLTYPE PresentEx(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion, DWORD dwFlags) override {
        DEV_TRACE("PresentEx"); return m_pReal->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* pPriority) override {
        DEV_TRACE("GetGPUThreadPriority"); return m_pReal->GetGPUThreadPriority(pPriority);
    }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override {
        DEV_TRACE("SetGPUThreadPriority"); return m_pReal->SetGPUThreadPriority(Priority);
    }
    HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT iSwapChain) override {
        DEV_TRACE("WaitForVBlank"); return m_pReal->WaitForVBlank(iSwapChain);
    }
    HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9** pResourceArray, UINT32 NumResources) override {
        DEV_TRACE("CheckResourceResidency"); return m_pReal->CheckResourceResidency(pResourceArray, NumResources);
    }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override {
        DEV_TRACE("SetMaximumFrameLatency"); return m_pReal->SetMaximumFrameLatency(MaxLatency);
    }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override {
        DEV_TRACE("GetMaximumFrameLatency"); return m_pReal->GetMaximumFrameLatency(pMaxLatency);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND hDestinationWindow) override {
        DEV_TRACE("CheckDeviceState"); return m_pReal->CheckDeviceState(hDestinationWindow);
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override {
        DEV_TRACE("CreateRenderTargetEx"); return m_pReal->CreateRenderTargetEx(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, Usage);
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override {
        DEV_TRACE("CreateOffscreenPlainSurfaceEx"); return m_pReal->CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override {
        DEV_TRACE("CreateDepthStencilSurfaceEx"); return m_pReal->CreateDepthStencilSurfaceEx(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage);
    }
    HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode) override {
        if (m_pTexReplacer) m_pTexReplacer->ReleaseTextures();
        return m_pReal->ResetEx(pPresentationParameters, pFullscreenDisplayMode);
    }
    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) override {
        DEV_TRACE("GetDisplayModeEx"); return m_pReal->GetDisplayModeEx(iSwapChain, pMode, pRotation);
    }

private:
    IDirect3DDevice9Ex* m_pReal;
    HDTextureReplacer*  m_pTexReplacer;
    volatile LONG       m_refCount;
};
