#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include "core/types.h"
#include "gfx/com.h"

namespace gfx {

struct Glyph {
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    float w = 0.f, h = 0.f;
    float bearingX = 0.f, bearingY = 0.f;
    float advance = 0.f;
};

class Font {
public:
    bool create(ID3D11Device* dev, std::wstring_view family, float size, int weight);
    bool createFromMemory(ID3D11Device* dev, const void* data, size_t size, float pixelSize);
    void destroy();

    const Glyph* glyph(uint32_t codepoint);
    float measure(std::string_view text);
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }
    ID3D11ShaderResourceView* srv() const { return srv_.get(); }

    static int decode(std::string_view text, size_t& i);

private:
    bool finishSetup();
    bool pack(int w, int h, int& outX, int& outY);
    void upload(int x, int y, int w, int h, const std::vector<uint8_t>& gray);

    ID3D11Device* dev_ = nullptr;   // non-owning; the Device outlives the atlas
    ComPtr<ID3D11Texture2D> tex_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    ComPtr<IDWriteFontFace> face_;

    std::unordered_map<uint32_t, Glyph> cache_;
    float size_ = 0.f, ascent_ = 0.f, descent_ = 0.f, scale_ = 0.f;
    int atlasW_ = 512, atlasH_ = 512;
    int penX_ = 1, penY_ = 1, rowH_ = 0;
};

}
