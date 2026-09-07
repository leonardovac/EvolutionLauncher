#include "gfx/font.h"

#pragma comment(lib, "dwrite.lib")

namespace gfx {

static IDWriteFactory* g_dwrite = nullptr;
static IDWriteFactory5* g_dwrite5 = nullptr;
static IDWriteInMemoryFontFileLoader* g_memLoader = nullptr;

static IDWriteFactory* dwrite() {
    if (!g_dwrite) {
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                          (IUnknown**)&g_dwrite5)) &&
            g_dwrite5) {
            g_dwrite5->QueryInterface(__uuidof(IDWriteFactory), (void**)&g_dwrite);
        } else {
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwrite);
        }
    }
    return g_dwrite;
}

static IDWriteInMemoryFontFileLoader* memoryLoader() {
    dwrite();
    if (!g_dwrite5) return nullptr;
    if (!g_memLoader) {
        if (FAILED(g_dwrite5->CreateInMemoryFontFileLoader(&g_memLoader))) return nullptr;
        g_dwrite5->RegisterFontFileLoader(g_memLoader);
    }
    return g_memLoader;
}

int Font::decode(std::string_view text, size_t& i) {
    const auto at = [&](size_t k) { return k < text.size() ? static_cast<uint8_t>(text[k]) : uint8_t{ 0 }; };
    const uint8_t c = at(i);
    if (c < 0x80) { i += 1; return c; }
    if ((c & 0xE0) == 0xC0) { int cp = ((c & 0x1F) << 6) | (at(i + 1) & 0x3F); i += 2; return cp; }
    if ((c & 0xF0) == 0xE0) { int cp = ((c & 0x0F) << 12) | ((at(i + 1) & 0x3F) << 6) | (at(i + 2) & 0x3F); i += 3; return cp; }
    if ((c & 0xF8) == 0xF0) {
        int cp = ((c & 0x07) << 18) | ((at(i + 1) & 0x3F) << 12) | ((at(i + 2) & 0x3F) << 6) | (at(i + 3) & 0x3F);
        i += 4;
        return cp;
    }
    i += 1;
    return '?';
}

bool Font::create(ID3D11Device* dev, std::wstring_view family, float size, int weight) {
    const std::wstring familyName(family);
    dev_ = dev;
    size_ = size;

    IDWriteFactory* f = dwrite();
    if (!f) return false;

    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(f->GetSystemFontCollection(collection.put(), FALSE))) return false;

    UINT32 index = 0;
    BOOL exists = FALSE;
    collection->FindFamilyName(familyName.c_str(), &index, &exists);
    if (!exists) {
        collection->FindFamilyName(L"Segoe UI", &index, &exists);
        if (!exists) return false;
    }

    ComPtr<IDWriteFontFamily> fam;
    collection->GetFontFamily(index, fam.put());

    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(weight), DWRITE_FONT_STRETCH_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, font.put());
    if (!font) return false;

    if (FAILED(font->CreateFontFace(face_.put()))) return false;

    return finishSetup();
}

bool Font::createFromMemory(ID3D11Device* dev, const void* data, size_t size, float pixelSize) {
    dev_ = dev;
    size_ = pixelSize;

    IDWriteInMemoryFontFileLoader* loader = memoryLoader();
    if (!loader || !g_dwrite5) return false;

    ComPtr<IDWriteFontFile> file;
    if (FAILED(loader->CreateInMemoryFontFileReference(g_dwrite5, data, static_cast<UINT32>(size), nullptr,
                                                       file.put())))
        return false;

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 faceCount = 0;
    file->Analyze(&supported, &fileType, &faceType, &faceCount);
    if (!supported || faceCount == 0) return false;

    IDWriteFontFile* files[] = { file.get() };
    if (FAILED(g_dwrite5->CreateFontFace(faceType, 1, files, 0, DWRITE_FONT_SIMULATIONS_NONE, face_.put())))
        return false;

    return finishSetup();
}

bool Font::finishSetup() {
    DWRITE_FONT_METRICS fm = {};
    face_->GetMetrics(&fm);
    scale_ = size_ / static_cast<float>(fm.designUnitsPerEm);
    ascent_ = fm.ascent * scale_;
    descent_ = fm.descent * scale_;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(atlasW_);
    td.Height = static_cast<UINT>(atlasH_);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<uint8_t> zero(static_cast<size_t>(atlasW_) * atlasH_, 0);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = zero.data();
    sd.SysMemPitch = static_cast<UINT>(atlasW_);

    if (FAILED(dev_->CreateTexture2D(&td, &sd, tex_.put()))) return false;
    return SUCCEEDED(dev_->CreateShaderResourceView(tex_.get(), nullptr, srv_.put()));
}

bool Font::pack(int w, int h, int& outX, int& outY) {
    if (penX_ + w + 1 > atlasW_) {
        penX_ = 1;
        penY_ += rowH_ + 1;
        rowH_ = 0;
    }
    if (penY_ + h + 1 > atlasH_) return false;
    outX = penX_;
    outY = penY_;
    penX_ += w + 1;
    if (h > rowH_) rowH_ = h;
    return true;
}

void Font::upload(int x, int y, int w, int h, const std::vector<uint8_t>& gray) {
    ComPtr<ID3D11DeviceContext> ctx;
    dev_->GetImmediateContext(ctx.put());
    D3D11_BOX box = {};
    box.left = static_cast<UINT>(x);
    box.top = static_cast<UINT>(y);
    box.right = static_cast<UINT>(x + w);
    box.bottom = static_cast<UINT>(y + h);
    box.back = 1;
    ctx->UpdateSubresource(tex_.get(), 0, &box, gray.data(), static_cast<UINT>(w), 0);
}

const Glyph* Font::glyph(uint32_t codepoint) {
    auto it = cache_.find(codepoint);
    if (it != cache_.end()) return &it->second;
    if (!face_) return nullptr;

    UINT16 gi = 0;
    face_->GetGlyphIndices(&codepoint, 1, &gi);

    DWRITE_GLYPH_METRICS gm = {};
    face_->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE);

    Glyph g;
    g.advance = gm.advanceWidth * scale_;

    float zeroAdvance = 0.f;
    DWRITE_GLYPH_OFFSET zeroOffset = {};
    DWRITE_GLYPH_RUN run = {};
    run.fontFace = face_.get();
    run.fontEmSize = size_;
    run.glyphCount = 1;
    run.glyphIndices = &gi;
    run.glyphAdvances = &zeroAdvance;
    run.glyphOffsets = &zeroOffset;

    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    if (SUCCEEDED(dwrite()->CreateGlyphRunAnalysis(&run, 1.f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                   DWRITE_MEASURING_MODE_NATURAL, 0.f, 0.f, analysis.put())) &&
        analysis) {
        RECT bounds = {};
        analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        int w = bounds.right - bounds.left;
        int h = bounds.bottom - bounds.top;
        if (w > 0 && h > 0) {
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3, 0);
            if (SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, rgb.data(),
                                                       static_cast<UINT>(rgb.size())))) {
                std::vector<uint8_t> gray(static_cast<size_t>(w) * h, 0);
                for (size_t p = 0; p < gray.size(); ++p)
                    gray[p] = static_cast<uint8_t>((rgb[p * 3] + rgb[p * 3 + 1] + rgb[p * 3 + 2]) / 3);

                int ax = 0, ay = 0;
                if (pack(w, h, ax, ay)) {
                    upload(ax, ay, w, h, gray);
                    g.u0 = static_cast<float>(ax) / atlasW_;
                    g.v0 = static_cast<float>(ay) / atlasH_;
                    g.u1 = static_cast<float>(ax + w) / atlasW_;
                    g.v1 = static_cast<float>(ay + h) / atlasH_;
                    g.w = static_cast<float>(w);
                    g.h = static_cast<float>(h);
                    g.bearingX = static_cast<float>(bounds.left);
                    g.bearingY = static_cast<float>(bounds.top);
                }
            }
        }
    }

    cache_[codepoint] = g;
    return &cache_[codepoint];
}

float Font::measure(std::string_view text) {
    float x = 0.f;
    for (size_t i = 0; i < text.size();) {
        const Glyph* g = glyph(static_cast<uint32_t>(decode(text, i)));
        if (g) x += g->advance;
    }
    return x;
}

void Font::destroy() {
    srv_.reset();
    tex_.reset();
    face_.reset();
    cache_.clear();
}

}
