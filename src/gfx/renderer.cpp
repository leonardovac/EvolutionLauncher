#include "gfx/renderer.h"
#include "gfx/shader_vs.h"
#include "gfx/shader_ps.h"

namespace gfx {

bool Renderer::create(ID3D11Device* dev) {
    dev_ = dev;

    // Precompiled bytecode (fxc /T {vs,ps}_5_0 /Fh), no runtime D3DCompile.
    dev->CreateVertexShader(g_vs_main, sizeof(g_vs_main), nullptr, vs_.put());
    dev->CreatePixelShader(g_ps_main, sizeof(g_ps_main), nullptr, ps_.put());

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(elems, ARRAYSIZE(elems), g_vs_main, sizeof(g_vs_main), layout_.put());

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cbd, nullptr, cb_.put());

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, blend_.put());

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, raster_.put());

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, sampler_.put());

    return vs_ && ps_ && layout_;
}

bool Renderer::ensureBuffers(size_t vtxCount, size_t idxCount) {
    if (vtxCount > vbCapacity_) {
        vbCapacity_ = vtxCount + 4096;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = static_cast<UINT>(vbCapacity_ * sizeof(Vertex));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, vb_.put()))) return false;
    }
    if (idxCount > ibCapacity_) {
        ibCapacity_ = idxCount + 8192;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = static_cast<UINT>(ibCapacity_ * sizeof(uint32_t));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, ib_.put()))) return false;
    }
    return true;
}

void Renderer::render(ID3D11DeviceContext* ctx, const DrawList& list, int screenW, int screenH,
                      float globalAlpha) {
    const auto& vtx = list.vertices();
    const auto& idx = list.indices();
    if (vtx.empty() || idx.empty()) return;
    if (!ensureBuffers(vtx.size(), idx.size())) return;

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (SUCCEEDED(ctx->Map(vb_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, vtx.data(), vtx.size() * sizeof(Vertex));
        ctx->Unmap(vb_.get(), 0);
    }
    if (SUCCEEDED(ctx->Map(ib_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, idx.data(), idx.size() * sizeof(uint32_t));
        ctx->Unmap(ib_.get(), 0);
    }
    if (SUCCEEDED(ctx->Map(cb_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        const float data[4] = { 1.f / static_cast<float>(screenW), 1.f / static_cast<float>(screenH),
                                globalAlpha, 0.f };
        memcpy(map.pData, data, sizeof(data));
        ctx->Unmap(cb_.get(), 0);
    }

    UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vb = vb_.get();
    ID3D11Buffer* cb = cb_.get();
    ID3D11SamplerState* sampler = sampler_.get();
    ctx->IASetInputLayout(layout_.get());
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(ib_.get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetShader(ps_.get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->RSSetState(raster_.get());

    const float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(blend_.get(), blendFactor, 0xFFFFFFFF);

    for (const Cmd& cmd : list.commands()) {
        if (cmd.idxCount == 0) continue;
        if (cmd.clip.w <= 0.f || cmd.clip.h <= 0.f) continue;

        D3D11_RECT scissor;
        scissor.left = static_cast<LONG>(cmd.clip.x);
        scissor.top = static_cast<LONG>(cmd.clip.y);
        scissor.right = static_cast<LONG>(std::ceil(cmd.clip.r()));
        scissor.bottom = static_cast<LONG>(std::ceil(cmd.clip.b()));
        ctx->RSSetScissorRects(1, &scissor);

        ID3D11ShaderResourceView* srv = cmd.srv;
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->DrawIndexed(cmd.idxCount, cmd.idxOffset, 0);
    }
}

void Renderer::destroy() {
    sampler_.reset();
    raster_.reset();
    blend_.reset();
    cb_.reset();
    ib_.reset();
    vb_.reset();
    layout_.reset();
    ps_.reset();
    vs_.reset();
    vbCapacity_ = ibCapacity_ = 0;
}

}
