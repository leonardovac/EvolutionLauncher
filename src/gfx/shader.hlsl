// UI shader: SDF rounded-box fills/borders/shadows, arcs, segments, chevrons,
// text (grayscale coverage), and (de)saturated images. Precompiled with fxc at
// build time; the bytecode is embedded via shader_vs.h / shader_ps.h.

cbuffer CB : register(b0) { float4 uScreen; };

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
    float4 shape : TEXCOORD1;
    float4 style : TEXCOORD2;
};

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
    float4 shape : TEXCOORD1;
    float4 style : TEXCOORD2;
};

PSIn vs_main(VSIn i) {
    PSIn o;
    o.pos   = float4(i.pos.x * uScreen.x * 2.0 - 1.0, 1.0 - i.pos.y * uScreen.y * 2.0, 0.0, 1.0);
    o.uv    = i.uv;
    o.col   = i.col;
    o.shape = i.shape;
    o.style = i.style;
    return o;
}

Texture2D tex0 : register(t0);
SamplerState smp0 : register(s0);

float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 0.00001));
    return length(pa - ba * h);
}

float4 ps_main(PSIn i) : SV_Target {
    float4 sampled = tex0.Sample(smp0, i.uv);

    float boxD  = sdRoundBox(i.pos.xy - i.shape.xy, i.shape.zw, i.style.x);
    float ringD = abs(boxD + i.style.z * 0.5) - i.style.z * 0.5;
    float boxAA = max(fwidth(boxD), 0.0001);

    float2 rel  = i.pos.xy - i.shape.xy;
    float dist  = length(rel);
    float arcD  = abs(dist - i.style.x) - i.style.z * 0.5;
    float arcAA = max(fwidth(arcD), 0.0001);
    float ang   = atan2(rel.y, rel.x);

    int mode = (int)(i.style.w + 0.5);
    float3 rgb = i.col.rgb;
    float a = i.col.a;

    if (mode == 6) {
    } else if (mode == 7) {
        float d = sdSegment(i.pos.xy, i.shape.xy, i.shape.zw) - i.style.x;
        a *= saturate(0.5 - d / max(fwidth(d), 0.0001));
    } else if (mode == 8) {
        float2 q = float2(i.pos.x, i.shape.w - abs(i.pos.y - i.shape.w));
        float d = sdSegment(q, i.shape.xy, i.shape.zw) - i.style.x;
        a *= saturate(0.5 - d / max(fwidth(d), 0.0001));
    } else if (mode == 4) {
        a *= sampled.r;
    } else if (mode == 5) {
        a *= saturate(0.5 - arcD / arcAA);
        float sweep = i.uv.y - i.uv.x;
        if (sweep < 6.2831853) {
            float t = ang - i.uv.x;
            t = t - 6.2831853 * floor(t / 6.2831853);
            float angAA = max(2.0 / max(i.style.x, 1.0), 0.0005);
            a *= min(smoothstep(0.0, angAA, t), smoothstep(0.0, angAA, sweep - t));
        }
    } else if (mode == 2) {
        float soft = max(i.style.y, 0.5);
        a *= 1.0 - smoothstep(-soft, soft, boxD);
    } else if (mode == 1) {
        a *= saturate(0.5 - ringD / boxAA);
    } else if (mode == 3) {
        a *= saturate(0.5 - boxD / boxAA);
        // style.y > 1 = alpha-mask: coverage from the texture, color from the
        // vertex. Turns any icon - a full-color game logo included - into a
        // tintable silhouette, so the tab strip can be one flat icon set.
        if (i.style.y <= 1.0) {
            float lum = dot(sampled.rgb, float3(0.299, 0.587, 0.114));
            rgb *= lerp(sampled.rgb, lum.xxx, saturate(i.style.y));
        }
        a *= sampled.a;
    } else {
        float soft = i.style.y;
        if (soft > 0.0) a *= 1.0 - smoothstep(-soft, soft, boxD);
        else a *= saturate(0.5 - boxD / boxAA);
    }

    return float4(rgb * a, a) * uScreen.z;
}
