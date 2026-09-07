#include "gfx/image.h"
#include <windows.h>
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace gfx {

static ComPtr<IWICImagingFactory> wicFactory;

static IWICImagingFactory* wic() {
    if (!wicFactory)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(wicFactory.put()));
    return wicFactory.get();
}

static std::expected<Image, ImageError> decodeToTexture(ID3D11Device* dev, IWICBitmapDecoder* decoder) {
    IWICImagingFactory* factory = wic();
    if (!factory || !decoder) return std::unexpected(ImageError::NoDecoder);

    UINT frameCount = 1;
    decoder->GetFrameCount(&frameCount);

    UINT bestFrame = 0, bestArea = 0;
    for (UINT i = 0; i < frameCount; ++i) {
        ComPtr<IWICBitmapFrameDecode> probe;
        if (FAILED(decoder->GetFrame(i, probe.put()))) continue;
        UINT pw = 0, ph = 0;
        probe->GetSize(&pw, &ph);
        if (pw * ph > bestArea) { bestArea = pw * ph; bestFrame = i; }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(bestFrame, frame.put()))) return std::unexpected(ImageError::Decode);

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put()))) return std::unexpected(ImageError::Decode);
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0.f, WICBitmapPaletteTypeCustom)))
        return std::unexpected(ImageError::Decode);

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return std::unexpected(ImageError::Decode);

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        return std::unexpected(ImageError::Decode);

    // Icons minify ~46x, and GenerateMips needs a writable render target.
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 0;               // full chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, tex.put()))) return std::unexpected(ImageError::Upload);

    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(ctx.put());
    if (!ctx) return std::unexpected(ImageError::Upload);

    Image out;
    ctx->UpdateSubresource(tex.get(), 0, nullptr, pixels.data(), w * 4, 0);
    if (FAILED(dev->CreateShaderResourceView(tex.get(), nullptr, out.srv.put())))
        return std::unexpected(ImageError::Upload);
    ctx->GenerateMips(out.srv.get());

    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    return out;
}

std::expected<Image, ImageError> loadImage(ID3D11Device* dev, const std::wstring& path) {
    IWICImagingFactory* factory = wic();
    if (!factory) return std::unexpected(ImageError::NoDecoder);

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, decoder.put())))
        return std::unexpected(ImageError::Decode);

    return decodeToTexture(dev, decoder.get());
}

std::expected<Image, ImageError> loadImageMemory(ID3D11Device* dev, std::span<const uint8_t> data) {
    IWICImagingFactory* factory = wic();
    if (!factory || data.empty()) return std::unexpected(ImageError::NoDecoder);

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put()))) return std::unexpected(ImageError::Decode);
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()),
                                            static_cast<DWORD>(data.size()))))
        return std::unexpected(ImageError::Decode);

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                decoder.put())))
        return std::unexpected(ImageError::Decode);

    return decodeToTexture(dev, decoder.get());
}

void shutdownImaging() {
    wicFactory.reset();
}

} // namespace gfx
