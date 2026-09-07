#include "gfx/device.h"
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace gfx {

bool Device::create(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;
    w_ = width;
    h_ = height;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL got;

    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                 D3D11_SDK_VERSION, dev_.put(), &got, ctx_.put())))
        return false;

    return createSwapchain();
}

bool Device::createSwapchain() {
    ComPtr<IDXGIDevice> dxgiDev;
    if (FAILED(dev_->QueryInterface(__uuidof(IDXGIDevice), dxgiDev.putVoid()))) return false;

    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(adapter.put());
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(__uuidof(IDXGIFactory2), factory.putVoid());

    flags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<UINT>(w_);
    desc.Height = static_cast<UINT>(h_);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = flags_;

    ComPtr<IDXGISwapChain1> swap1;
    HRESULT hr = factory->CreateSwapChainForComposition(dev_.get(), &desc, nullptr, swap1.put());
    if (SUCCEEDED(hr)) hr = swap1->QueryInterface(__uuidof(IDXGISwapChain2), swap_.putVoid());
    if (FAILED(hr)) return false;

    swap_->SetMaximumFrameLatency(1);
    waitable_ = swap_->GetFrameLatencyWaitableObject();

    if (FAILED(DCompositionCreateDevice(dxgiDev.get(), __uuidof(IDCompositionDevice), comp_.putVoid())))
        return false;

    if (FAILED(comp_->CreateTargetForHwnd(hwnd_, TRUE, compTarget_.put()))) return false;
    if (FAILED(comp_->CreateVisual(visual_.put()))) return false;

    visual_->SetContent(swap_.get());
    compTarget_->SetRoot(visual_.get());
    comp_->Commit();

    createTarget();
    return static_cast<bool>(rtv_);
}

bool Device::recreate() {
    destroy();
    return create(hwnd_, w_, h_);
}

void Device::createTarget() {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), back.putVoid())))
        dev_->CreateRenderTargetView(back.get(), nullptr, rtv_.put());
}

void Device::resize(int width, int height) {
    if (!swap_ || width <= 0 || height <= 0) return;
    if (width == w_ && height == h_) return;

    rtv_.reset();
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    if (SUCCEEDED(swap_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                       DXGI_FORMAT_UNKNOWN, flags_))) {
        w_ = width;
        h_ = height;
    }
    createTarget();
}

void Device::waitForFrame() {
    if (waitable_) WaitForSingleObjectEx(waitable_, 1000, TRUE);
}

void Device::beginFrame() {
    const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    ID3D11RenderTargetView* target = rtv_.get();
    ctx_->ClearRenderTargetView(target, clear);
    ctx_->OMSetRenderTargets(1, &target, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(w_);
    vp.Height = static_cast<float>(h_);
    vp.MaxDepth = 1.f;
    ctx_->RSSetViewports(1, &vp);
}

HRESULT Device::present(bool vsync) {
    HRESULT hr = swap_->Present(vsync ? 1 : 0, 0);
    if (comp_) comp_->Commit();
    return hr;
}

bool Device::captureBackbuffer(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), back.putVoid()))) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev_->CreateTexture2D(&desc, nullptr, staging.put()))) return false;
    ctx_->CopyResource(staging.get(), back.get());

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(ctx_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return false;

    const int w = static_cast<int>(desc.Width), h = static_cast<int>(desc.Height);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
        uint8_t* dst = pixels.data() + static_cast<size_t>(h - 1 - y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const float a = src[x * 4 + 3] / 255.f;
            const float bg = 0.86f * (1.f - a);
            for (int c = 0; c < 3; ++c)
                dst[x * 4 + c] = static_cast<uint8_t>((std::min)(255.f, src[x * 4 + c] + bg * 255.f));
            dst[x * 4 + 3] = 255;
        }
    }
    ctx_->Unmap(staging.get(), 0);

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = static_cast<DWORD>(pixels.size());
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(file, &fh, sizeof(fh), &written, nullptr);
    WriteFile(file, &ih, sizeof(ih), &written, nullptr);
    WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &written, nullptr);
    CloseHandle(file);
    return true;
}

void Device::destroy() {
    rtv_.reset();
    visual_.reset();
    compTarget_.reset();
    comp_.reset();
    swap_.reset();
    waitable_ = nullptr;
    ctx_.reset();
    dev_.reset();
}

} // namespace gfx
