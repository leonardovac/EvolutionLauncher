#pragma once
#include <d3d11.h>
#include <vector>
#include "core/types.h"
#include <string_view>
#include "gfx/font.h"

namespace gfx {

using core::Col;
using core::Rect;
using core::Vec2;

// Values are read as style.w by the pixel shader; order is the wire format.
enum class Mode : uint8_t { Fill, Border, Shadow, Image, Text, Arc, Raw, Segment, Chevron };

struct Vertex {
    float x, y;
    float u, v;
    uint32_t col;
    float cx, cy, hw, hh;
    float radius, soft, border, mode;
};

struct Cmd {
    uint32_t idxOffset = 0;
    uint32_t idxCount = 0;
    Rect clip;
    ID3D11ShaderResourceView* srv = nullptr;
};

class DrawList {
public:
    void reset(const Rect& viewport);

    void pushClip(const Rect& r);
    void popClip();
    Rect currentClip() const { return clipStack_.back(); }

    void rect(const Rect& r, const Col& c, float radius = 0.f, float soft = 0.f);
    void border(const Rect& r, const Col& c, float thickness, float radius = 0.f);
    void shadow(const Rect& r, const Col& c, float blur, float radius, const Vec2& offset = Vec2());
    void circle(const Vec2& center, float radius, const Col& c);
    void arc(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& c);
    void arcGradient(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& left,
                     const Col& right);
    void line(const Vec2& a, const Vec2& b, float thickness, const Col& c);
    void chevronShape(const Vec2& tip, const Vec2& vertex, float thickness, const Col& c);
    void triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Col& col);
    void star(const Vec2& center, float radius, float sharpness, const Col& c);

    void gradientVMasked(const Rect& r, const Col& top, const Col& bottom, const Rect& shape, float radius);
    void gradientHMasked(const Rect& r, const Col& left, const Col& right, const Rect& shape, float radius);

    // Passed as `desaturate` to draw the texture as a solid silhouette in `tint`.
    static constexpr float kAlphaMask = 2.f;

    void image(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, float radius = 0.f,
               const Rect& uv = Rect(0.f, 0.f, 1.f, 1.f), float desaturate = 0.f);
    void imageShaped(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, const Rect& shape,
                     float radius, const Rect& uv = Rect(0.f, 0.f, 1.f, 1.f), float desaturate = 0.f);

    void text(Font* font, const Vec2& pos, const Col& c, std::string_view txt, float tracking = 0.f);

    const std::vector<Vertex>& vertices() const { return vtx_; }
    const std::vector<uint32_t>& indices() const { return idx_; }
    const std::vector<Cmd>& commands() const { return cmds_; }

private:
    void setTexture(ID3D11ShaderResourceView* srv);
    void quad(const Rect& dst, const Rect& uv, const Col& c0, const Col& c1, bool horizontal, const Rect& shape,
              float radius, float soft, float border, Mode mode);
    void reserve(int verts, int inds);

    std::vector<Vertex> vtx_;
    std::vector<uint32_t> idx_;
    std::vector<Cmd> cmds_;
    std::vector<Rect> clipStack_;
};

}
