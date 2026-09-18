#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

namespace dx11 {
    inline ID3D11Device* device = nullptr;
    inline ID3D11DeviceContext* context = nullptr;
    inline IDXGISwapChain1* swap_chain = nullptr;
    inline ID3D11RenderTargetView* render_target = nullptr;
    inline ID3D11BlendState* blend_state = nullptr;

    inline IDCompositionDevice* dcomp_device = nullptr;
    inline IDCompositionTarget* dcomp_target = nullptr;
    inline IDCompositionVisual* dcomp_visual = nullptr;

    inline bool occluded = false;
    inline double refresh_rate = 60.0;

    bool create(HWND hwnd);
    bool create_render_target();
    void cleanup_render_target();
    bool create_blend_state();
    bool resize(UINT width, UINT height);
    void destroy();
}
