#pragma once
#include <d3d11.h>
#include <expected>
#include <span>
#include <string>
#include "gfx/com.h"

namespace gfx {

enum class ImageError { NoDecoder, Decode, Upload };

struct Image {
    ComPtr<ID3D11ShaderResourceView> srv;
    int w = 0, h = 0;

    bool valid() const { return static_cast<bool>(srv); }
    float aspect() const { return h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f; }
};

std::expected<Image, ImageError> loadImage(ID3D11Device* dev, const std::wstring& path);
std::expected<Image, ImageError> loadImageMemory(ID3D11Device* dev, std::span<const uint8_t> data);

// Must run before CoUninitialize: releasing the cached factory after COM has been
// torn down is an access violation, and a static ComPtr would do exactly that.
void shutdownImaging();

} // namespace gfx
