// LOOMTRACER NA KARTICI: iz zbrojeva po pikselu u sliku za prikaz (RGBA8, sRGB).
//
// Isto sto na procesoru rade Renderer::frame (prosjek, omjer sjene catchera), Tracer::composite
// (nebo / prozirno / snimka) i Tracer::toDisplay (ekspozicija, Standard ili AgX, sRGB krivulja),
// plus sahovnica ispod prozirnog - kao prozor rendera u editoru. Tako se slika koja se cisti ne
// mora citati natrag u punoj preciznosti: natrag ide cetiri bajta po pikselu.

struct Push{
    uint width;
    uint height;
    uint backdrop;          // 0 nebo, 1 prozirno, 2 snimka
    uint view;              // 0 Standard, 1 AgX
    float gain;             // 2^ekspozicija
    uint plate;             // indeks teksture snimke ili NONE
    uint checker;           // 1: prozirno preko sahovnice
    uint padding;
};

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] StructuredBuffer<float4> accCg;
[[vk::binding(1,0)]] StructuredBuffer<float4> accBackground;
[[vk::binding(2,0)]] StructuredBuffer<float4> accLit;
[[vk::binding(3,0)]] StructuredBuffer<float4> accShadowed;
[[vk::binding(4,0)]] StructuredBuffer<uint4> textureInfo;
[[vk::binding(5,0)]] StructuredBuffer<uint> texels;
[[vk::binding(6,0)]] RWStructuredBuffer<uint> display;

static const uint NONE = 0xFFFFFFFFu;

float srgbToLinear(float v){ return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float linearToSrgb(float v){ v = max(0.0, v); return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }

float4 fetch(uint4 info, int x, int y){
    x = clamp(x, 0, int(info.y) - 1);
    y = clamp(y, 0, int(info.z) - 1);
    const uint p = texels[info.x + uint(y) * info.y + uint(x)];
    float4 c = float4(float(p & 255u), float((p >> 8) & 255u), float((p >> 16) & 255u), float(p >> 24)) / 255.0;
    if((info.w & 1u) != 0) c.rgb = float3(srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b));
    return c;
}

float3 samplePlate(float2 uv){
    const uint4 info = textureInfo[push.plate];
    const float x = uv.x * float(info.y) - 0.5, y = uv.y * float(info.z) - 0.5;
    const float fx = floor(x), fy = floor(y);
    const float tx = x - fx, ty = y - fy;
    const int ix = int(fx), iy = int(fy);
    const float4 a = fetch(info, ix, iy), b = fetch(info, ix + 1, iy);
    const float4 c = fetch(info, ix, iy + 1), d = fetch(info, ix + 1, iy + 1);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty).rgb;
}

// AgX (Troy Sobotka), polinom Benjamina Wrenscha - isto kao Film.cpp
float3 agx(float3 v){
    const float3x3 inset = float3x3(0.842479062253094, 0.0784335999999992, 0.0792237451477643,
                                    0.0423282422610123, 0.878468636469772, 0.0791661274605434,
                                    0.0423756549057051, 0.0784336, 0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0980208811401368, -0.0990297440797205,
                                     -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
                                     -0.0529716355144438, -0.0980434501171241, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    v = mul(inset, max(v, float3(1e-10)));
    v = clamp(log2(max(v, float3(1e-10))), float3(minEv), float3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    const float3 x2 = v * v, x4 = x2 * x2;
    v = 15.5 * x4 * x2 - 40.14 * x4 * v + 31.96 * x4 - 6.868 * x2 * v + 0.4298 * x2 + 0.1191 * v - 0.00232;
    v = mul(outset, v);
    return pow(max(v, float3(0.0)), float3(2.2));
}

uint toByte(float v){ return uint(clamp(round(clamp(v, 0.0, 1.0) * 255.0), 0.0, 255.0)); }

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= push.width || id.y >= push.height) return;
    const uint i = id.y * push.width + id.x;
    const float4 shadowed = accShadowed[i];
    const float n = shadowed.w;
    if(n <= 0.0){ display[i] = 0xFF000000u; return; }
    const float4 cgSum = accCg[i], backgroundSum = accBackground[i], litSum = accLit[i];
    const float3 cg = cgSum.rgb / n;
    const float a = clamp(cgSum.w / n, 0.0, 1.0);
    const float3 background = backgroundSum.rgb / n;
    float3 s = float3(1.0);
    const float missSamples = backgroundSum.w, catcherSamples = litSum.w;
    if(catcherSamples > 0.0){
        for(int k = 0; k < 3; ++k){
            const float ratio = litSum[k] > 0.0 ? clamp(shadowed[k] / litSum[k], 0.0, 1.0) : 1.0;
            s[k] = (missSamples + catcherSamples * ratio) / (missSamples + catcherSamples);
        }
    }
    float3 rgb;
    float alpha = 1.0;
    if(push.backdrop == 0) rgb = cg + s * background;
    else if(push.backdrop == 1){
        const float taken = 1.0 - clamp((s.r + s.g + s.b) / 3.0, 0.0, 1.0);
        rgb = cg;
        alpha = a + (1.0 - a) * taken;
    }else{
        float3 plate = float3(0.0);
        if(push.plate != NONE) plate = samplePlate(float2((float(id.x) + 0.5) / float(push.width), (float(id.y) + 0.5) / float(push.height)));
        rgb = cg + (1.0 - a) * s * plate;
    }
    float3 c = rgb * push.gain;
    if(alpha > 0.0 && alpha < 1.0) c /= alpha;
    if(push.view == 1) c = agx(c);
    float3 outColour = float3(float(toByte(linearToSrgb(c.r))), float(toByte(linearToSrgb(c.g))), float(toByte(linearToSrgb(c.b))));
    uint outAlpha = toByte(alpha);
    if(push.checker != 0 && outAlpha < 255){
        const float checker = ((id.x / 16 + id.y / 16) & 1u) != 0 ? 70.0 : 45.0;
        const float af = float(outAlpha) / 255.0;
        outColour = floor(outColour * af + checker * (1.0 - af));
        outAlpha = 255;
    }
    display[i] = uint(outColour.r) | (uint(outColour.g) << 8) | (uint(outColour.b) << 16) | (outAlpha << 24);
}
