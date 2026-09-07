#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <string>
#include "gfx/com.h"

namespace gfx {

class Device {
public:
    bool create(HWND hwnd, int width, int height);
    void destroy();

    // Rebuild after DXGI_ERROR_DEVICE_REMOVED; false if the GPU is still gone.
    bool recreate();

    void resize(int width, int height);
    void waitForFrame();
    void beginFrame();
    HRESULT present(bool vsync);                  // caller inspects for device loss
    bool captureBackbuffer(const std::wstring& path);

    ID3D11Device* dev() const { return dev_.get(); }
    ID3D11DeviceContext* ctx() const { return ctx_.get(); }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    void createTarget();
    bool createSwapchain();

    HWND hwnd_ = nullptr;
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain2> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<IDCompositionDevice> comp_;
    ComPtr<IDCompositionTarget> compTarget_;
    ComPtr<IDCompositionVisual> visual_;
    HANDLE waitable_ = nullptr;   // owned by the swapchain
    UINT flags_ = 0;
    int w_ = 0, h_ = 0;
};

} // namespace gfx
