#include "dx11.h"
#include <array>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace dx11 {

    bool create_blend_state()
    {
        D3D11_BLEND_DESC desc{};
        desc.AlphaToCoverageEnable = FALSE;
        desc.IndependentBlendEnable = FALSE;
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        HRESULT hr = device->CreateBlendState(&desc, &blend_state);
        if (FAILED(hr)) { printf("[-] create_blend_state 0x%lX\n", (unsigned long)hr); return false; }
        return true;
    }

    bool create_render_target()
    {
        ID3D11Texture2D* back_buffer = nullptr;
        swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (!back_buffer) { printf("[-] swapchain back buffer\n"); return false; }

        D3D11_TEXTURE2D_DESC bb_desc{};
        back_buffer->GetDesc(&bb_desc);

        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        device->CreateRenderTargetView(back_buffer, &rtv, &render_target);
        back_buffer->Release();

        if (!render_target) { printf("[-] create render target\n"); return false; }

        // depth buffer that only the native-cham 3D pass uses; cleared each frame
        // so per-part boxes occlude themselves correctly (pixel perfect).
        D3D11_TEXTURE2D_DESC dd{};
        dd.Width = bb_desc.Width;
        dd.Height = bb_desc.Height;
        dd.MipLevels = 1;
        dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.SampleDesc.Count = 1;
        dd.SampleDesc.Quality = 0;
        dd.Usage = D3D11_USAGE_DEFAULT;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        if (SUCCEEDED(device->CreateTexture2D(&dd, nullptr, &depth_texture)) && depth_texture)
            device->CreateDepthStencilView(depth_texture, nullptr, &depth_view);

        if (!depth_view) { printf("[-] create depth view\n"); return false; }
        return true;
    }

    void cleanup_render_target()
    {
        if (render_target) { render_target->Release(); render_target = nullptr; }
        if (depth_view) { depth_view->Release(); depth_view = nullptr; }
        if (depth_texture) { depth_texture->Release(); depth_texture = nullptr; }
    }

    bool create(HWND hwnd)
    {
        refresh_rate = 60.0;
        DEVMODEA dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 0)
            refresh_rate = static_cast<double>(dm.dmDisplayFrequency);

        std::uint32_t flags = 0;
        D3D_FEATURE_LEVEL level{};
        const std::array<D3D_FEATURE_LEVEL, 2> levels{ D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels.data(), (UINT)levels.size(), D3D11_SDK_VERSION, &device, &level, &context);

        if (hr == DXGI_ERROR_UNSUPPORTED)
        {
            printf("[*] hardware device unsupported, trying WARP\n");
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels.data(), (UINT)levels.size(), D3D11_SDK_VERSION, &device, &level, &context);
        }

        if (FAILED(hr)) { printf("[-] D3D11CreateDevice 0x%lX\n", (unsigned long)hr); return false; }

        IDXGIDevice* dxgi_device = nullptr;
        device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);

        IDXGIAdapter* dxgi_adapter = nullptr;
        dxgi_device->GetAdapter(&dxgi_adapter);

        IDXGIFactory2* dxgi_factory = nullptr;
        dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgi_factory);

        RECT rect{};
        GetClientRect(hwnd, &rect);

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = rect.right - rect.left;
        sd.Height = rect.bottom - rect.top;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.Stereo = FALSE;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        hr = dxgi_factory->CreateSwapChainForComposition(device, &sd, nullptr, &swap_chain);
        if (FAILED(hr))
        {
            printf("[-] CreateSwapChainForComposition 0x%lX\n", (unsigned long)hr);
            dxgi_factory->Release(); dxgi_adapter->Release(); dxgi_device->Release();
            return false;
        }

        {
            IDXGISwapChain2* sc2 = nullptr;
            if (SUCCEEDED(swap_chain->QueryInterface(&sc2))) { sc2->SetMaximumFrameLatency(1); sc2->Release(); }
        }

        hr = DCompositionCreateDevice(dxgi_device, __uuidof(IDCompositionDevice), (void**)&dcomp_device);
        if (FAILED(hr))
        {
            printf("[-] DCompositionCreateDevice 0x%lX\n", (unsigned long)hr);
            dxgi_factory->Release(); dxgi_adapter->Release(); dxgi_device->Release();
            return false;
        }

        hr = dcomp_device->CreateTargetForHwnd(hwnd, TRUE, &dcomp_target);
        if (FAILED(hr))
        {
            printf("[-] CreateTargetForHwnd 0x%lX\n", (unsigned long)hr);
            dxgi_factory->Release(); dxgi_adapter->Release(); dxgi_device->Release();
            return false;
        }

        dcomp_device->CreateVisual(&dcomp_visual);
        dcomp_visual->SetContent(swap_chain);
        dcomp_target->SetRoot(dcomp_visual);
        dcomp_device->Commit();

        dxgi_factory->Release();
        dxgi_adapter->Release();
        dxgi_device->Release();

        if (!create_blend_state()) return false;

        return create_render_target();
    }

    bool resize(UINT width, UINT height)
    {
        if (!swap_chain || width == 0 || height == 0) return false;

        cleanup_render_target();
        HRESULT hr = swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) { printf("[-] ResizeBuffers 0x%lX\n", (unsigned long)hr); return false; }

        return create_render_target();
    }

    void destroy()
    {
        cleanup_render_target();

        if (blend_state) { blend_state->Release(); blend_state = nullptr; }
        if (dcomp_visual) { dcomp_visual->Release(); dcomp_visual = nullptr; }
        if (dcomp_target) { dcomp_target->Release(); dcomp_target = nullptr; }
        if (dcomp_device) { dcomp_device->Release(); dcomp_device = nullptr; }
        if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
    }
}
