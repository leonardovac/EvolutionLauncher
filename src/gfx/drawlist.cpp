#include "gfx/drawlist.h"
#include <utility>

namespace gfx {

void DrawList::reset(const Rect& viewport) {
    vtx_.clear();
    idx_.clear();
    cmds_.clear();
    clipStack_.clear();
    clipStack_.push_back(viewport);

    Cmd cmd;
    cmd.clip = viewport;
    cmds_.push_back(cmd);
}

void DrawList::reserve(int verts, int inds) {
    vtx_.reserve(vtx_.size() + verts);
    idx_.reserve(idx_.size() + inds);
}

void DrawList::pushClip(const Rect& r) {
    Rect merged = r.clipTo(clipStack_.back());
    clipStack_.push_back(merged);
    Cmd cmd;
    cmd.idxOffset = static_cast<uint32_t>(idx_.size());
    cmd.clip = merged;
    cmd.srv = cmds_.back().srv;
    cmds_.push_back(cmd);
}

void DrawList::popClip() {
    if (clipStack_.size() <= 1) return;
    clipStack_.pop_back();
    Cmd cmd;
    cmd.idxOffset = static_cast<uint32_t>(idx_.size());
    cmd.clip = clipStack_.back();
    cmd.srv = cmds_.back().srv;
    cmds_.push_back(cmd);
}

void DrawList::setTexture(ID3D11ShaderResourceView* srv) {
    if (cmds_.back().srv == srv) return;
    if (cmds_.back().idxCount == 0) {
        cmds_.back().srv = srv;
        return;
    }
    Cmd cmd;
    cmd.idxOffset = static_cast<uint32_t>(idx_.size());
    cmd.clip = clipStack_.back();
    cmd.srv = srv;
    cmds_.push_back(cmd);
}

void DrawList::quad(const Rect& dst, const Rect& uv, const Col& c0, const Col& c1, bool horizontal,
                    const Rect& shape, float radius, float soft, float border, Mode mode) {
    if (dst.w <= 0.f || dst.h <= 0.f) return;
    if (c0.a <= 0.001f && c1.a <= 0.001f) return;

    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    const Vec2 sc = shape.center();
    const float hw = shape.w * 0.5f, hh = shape.h * 0.5f;

    Vertex v;
    v.cx = sc.x;
    v.cy = sc.y;
    v.hw = hw;
    v.hh = hh;
    v.radius = (std::min)(radius, (std::min)(hw, hh));
    v.soft = soft;
    v.border = border;
    v.mode = static_cast<float>(std::to_underlying(mode));

    const uint32_t p0 = c0.packed();
    const uint32_t p1 = c1.packed();

    reserve(4, 6);
    v.col = p0;                   v.x = dst.x;   v.y = dst.y;   v.u = uv.x;        v.v = uv.y;        vtx_.push_back(v);
    v.col = horizontal ? p1 : p0; v.x = dst.r(); v.y = dst.y;   v.u = uv.x + uv.w; v.v = uv.y;        vtx_.push_back(v);
    v.col = p1;                   v.x = dst.r(); v.y = dst.b(); v.u = uv.x + uv.w; v.v = uv.y + uv.h; vtx_.push_back(v);
    v.col = horizontal ? p0 : p1; v.x = dst.x;   v.y = dst.b(); v.u = uv.x;        v.v = uv.y + uv.h; vtx_.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) idx_.push_back(base + order[i]);
    cmds_.back().idxCount += 6;
}

void DrawList::rect(const Rect& r, const Col& c, float radius, float soft) {
    setTexture(nullptr);
    quad(r.expand(1.f), Rect(0, 0, 0, 0), c, c, false, r, radius, soft, 0.f, Mode::Fill);
}

void DrawList::border(const Rect& r, const Col& c, float thickness, float radius) {
    if (thickness <= 0.f) return;
    setTexture(nullptr);
    quad(r.expand(thickness + 1.f), Rect(0, 0, 0, 0), c, c, false, r, radius, 0.f, thickness, Mode::Border);
}

void DrawList::shadow(const Rect& r, const Col& c, float blur, float radius, const Vec2& offset) {
    setTexture(nullptr);
    Rect shape = r.offset(offset.x, offset.y);
    quad(shape.expand(blur + 2.f), Rect(0, 0, 0, 0), c, c, false, shape, radius, blur, 0.f, Mode::Shadow);
}

void DrawList::circle(const Vec2& center, float radius, const Col& c) {
    rect(Rect(center.x - radius, center.y - radius, radius * 2.f, radius * 2.f), c, radius);
}

void DrawList::arc(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& c) {
    arcGradient(center, radius, thickness, a0, a1, c, c);
}

void DrawList::arcGradient(const Vec2& center, float radius, float thickness, float a0, float a1, const Col& left,
                           const Col& right) {
    if (left.a <= 0.001f && right.a <= 0.001f) return;
    setTexture(nullptr);

    const float outer = radius + thickness * 0.5f + 2.f;
    Rect dst(center.x - outer, center.y - outer, outer * 2.f, outer * 2.f);

    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    Vertex v;
    v.cx = center.x;
    v.cy = center.y;
    v.hw = radius;
    v.hh = radius;
    v.radius = radius;
    v.soft = 0.f;
    v.border = thickness;
    v.mode = static_cast<float>(std::to_underlying(Mode::Arc));
    v.u = a0;
    v.v = a1;

    reserve(4, 6);
    v.col = left.packed();  v.x = dst.x;   v.y = dst.y;   vtx_.push_back(v);
    v.col = right.packed(); v.x = dst.r(); v.y = dst.y;   vtx_.push_back(v);
    v.col = right.packed(); v.x = dst.r(); v.y = dst.b(); vtx_.push_back(v);
    v.col = left.packed();  v.x = dst.x;   v.y = dst.b(); vtx_.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) idx_.push_back(base + order[i]);
    cmds_.back().idxCount += 6;
}

void DrawList::triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Col& col) {
    if (col.a <= 0.001f) return;
    setTexture(nullptr);

    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    Vertex v;
    v.col = col.packed();
    v.u = v.v = 0.f;
    v.cx = v.cy = 0.f;
    v.hw = v.hh = 0.f;
    v.radius = v.soft = v.border = 0.f;
    v.mode = static_cast<float>(std::to_underlying(Mode::Raw));

    reserve(3, 3);
    v.x = a.x; v.y = a.y; vtx_.push_back(v);
    v.x = b.x; v.y = b.y; vtx_.push_back(v);
    v.x = c.x; v.y = c.y; vtx_.push_back(v);
    idx_.push_back(base);
    idx_.push_back(base + 1);
    idx_.push_back(base + 2);
    cmds_.back().idxCount += 3;
}

void DrawList::star(const Vec2& c, float radius, float sharpness, const Col& col) {
    if (col.a <= 0.001f || radius <= 0.05f) return;

    const int segments = 32;
    const float core = 0.10f;
    Vec2 prev;
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(segments) * core::kPi * 2.f;
        const float lobe = std::pow(std::fabs(std::cos(a * 2.f)), sharpness);
        const float r = radius * (core + (1.f - core) * lobe);
        const Vec2 p(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        if (i > 0) triangle(c, prev, p, col);
        prev = p;
    }
}

void DrawList::line(const Vec2& a, const Vec2& b, float thickness, const Col& c) {
    if (c.a <= 0.001f) return;
    setTexture(nullptr);

    const float pad = thickness * 0.5f + 1.5f;
    Rect dst((std::min)(a.x, b.x) - pad, (std::min)(a.y, b.y) - pad,
             std::fabs(b.x - a.x) + pad * 2.f, std::fabs(b.y - a.y) + pad * 2.f);

    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    Vertex v;
    v.col = c.packed();
    v.u = v.v = 0.f;
    v.cx = a.x;
    v.cy = a.y;
    v.hw = b.x;
    v.hh = b.y;
    v.radius = thickness * 0.5f;
    v.soft = 0.f;
    v.border = 0.f;
    v.mode = static_cast<float>(std::to_underlying(Mode::Segment));

    reserve(4, 6);
    v.x = dst.x;   v.y = dst.y;   vtx_.push_back(v);
    v.x = dst.r(); v.y = dst.y;   vtx_.push_back(v);
    v.x = dst.r(); v.y = dst.b(); vtx_.push_back(v);
    v.x = dst.x;   v.y = dst.b(); vtx_.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) idx_.push_back(base + order[i]);
    cmds_.back().idxCount += 6;
}

void DrawList::chevronShape(const Vec2& tip, const Vec2& vertex, float thickness, const Col& c) {
    if (c.a <= 0.001f) return;
    setTexture(nullptr);

    const float pad = thickness * 0.5f + 1.5f;
    const float halfH = std::fabs(vertex.y - tip.y);
    Rect dst(tip.x - pad, vertex.y - halfH - pad, (vertex.x - tip.x) + pad * 2.f, halfH * 2.f + pad * 2.f);

    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    Vertex v;
    v.col = c.packed();
    v.u = v.v = 0.f;
    v.cx = tip.x;
    v.cy = tip.y;
    v.hw = vertex.x;
    v.hh = vertex.y;
    v.radius = thickness * 0.5f;
    v.soft = 0.f;
    v.border = 0.f;
    v.mode = static_cast<float>(std::to_underlying(Mode::Chevron));

    reserve(4, 6);
    v.x = dst.x;   v.y = dst.y;   vtx_.push_back(v);
    v.x = dst.r(); v.y = dst.y;   vtx_.push_back(v);
    v.x = dst.r(); v.y = dst.b(); vtx_.push_back(v);
    v.x = dst.x;   v.y = dst.b(); vtx_.push_back(v);

    const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) idx_.push_back(base + order[i]);
    cmds_.back().idxCount += 6;
}

void DrawList::gradientVMasked(const Rect& r, const Col& top, const Col& bottom, const Rect& shape, float radius) {
    setTexture(nullptr);
    quad(r, Rect(0, 0, 0, 0), top, bottom, false, shape, radius, 0.f, 0.f, Mode::Fill);
}

void DrawList::gradientHMasked(const Rect& r, const Col& left, const Col& right, const Rect& shape, float radius) {
    setTexture(nullptr);
    quad(r, Rect(0, 0, 0, 0), left, right, true, shape, radius, 0.f, 0.f, Mode::Fill);
}

void DrawList::image(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, float radius, const Rect& uv,
                     float desaturate) {
    if (!srv) return;
    setTexture(srv);
    quad(dst, uv, tint, tint, false, dst, radius, desaturate, 0.f, Mode::Image);
}

void DrawList::imageShaped(ID3D11ShaderResourceView* srv, const Rect& dst, const Col& tint, const Rect& shape,
                           float radius, const Rect& uv, float desaturate) {
    if (!srv) return;
    setTexture(srv);
    quad(dst, uv, tint, tint, false, shape, radius, desaturate, 0.f, Mode::Image);
}

void DrawList::text(Font* font, const Vec2& pos, const Col& c, std::string_view txt, float tracking) {
    if (!font || txt.empty() || c.a <= 0.001f) return;
    setTexture(font->srv());

    float x = pos.x;
    const float baseline = pos.y + font->ascent();
    for (size_t i = 0; i < txt.size();) {
        const Glyph* g = font->glyph(static_cast<uint32_t>(Font::decode(txt, i)));
        if (!g) continue;
        if (g->w > 0.f && g->h > 0.f) {
            Rect dst(std::floor(x + g->bearingX), std::floor(baseline + g->bearingY), g->w, g->h);
            Rect uv(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0);
            setTexture(font->srv());
            quad(dst, uv, c, c, false, dst, 0.f, 0.f, 0.f, Mode::Text);
        }
        x += g->advance + tracking;
    }
}

}
