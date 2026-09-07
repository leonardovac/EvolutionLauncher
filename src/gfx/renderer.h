#pragma once
#include <d3d11.h>
#include "gfx/com.h"
#include "gfx/drawlist.h"

namespace gfx {

class Renderer {
public:
    bool create(ID3D11Device* dev);
    void destroy();
    void render(ID3D11DeviceContext* ctx, const DrawList& list, int screenW, int screenH,
                float globalAlpha = 1.f);

private:
    bool ensureBuffers(size_t vtxCount, size_t idxCount);

    ID3D11Device* dev_ = nullptr;   // non-owning; the Device outlives the Renderer
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vb_;
    ComPtr<ID3D11Buffer> ib_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11SamplerState> sampler_;
    size_t vbCapacity_ = 0, ibCapacity_ = 0;
};

}
