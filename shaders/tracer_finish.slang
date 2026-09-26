// LOOMTRACER NA KARTICI: filtar suma i post za sliku koja se cisti - isti kadar kao gotov render.
//
// Port procesorskog puta redak po redak, u prolazima (mode u push konstantama):
//   0 PRIPREMA   zbrojevi -> prosjek (Renderer::frame), vodici (normala, dubina, albedo,
//                pokrivenost) i svjetlo = boja / albedo s varijancom (Denoise.cpp)
//   1 A-TROUS    jedan prolaz razmaka `step`, A -> B ili B -> A (Denoise.cpp, pass)
//   2 KOMPOZIT   svjetlo * albedo (ili sirovo), sjena catchera, nebo / prozirno / snimka,
//                puta ekspozicija (Tracer::composite)
//   3 SVIJETLO   prag bloom-a u razinu 0 piramide
//   4 DOLJE      razina k-1 -> k, prosjek 2x2 (Post.cpp, downsample)
//   5 GORE       razina k += gore(razina k+1) (Post.cpp, upsample)
//   6 BLOOM      slika += udio * (zamuceno * skala - svijetlo)
//   7 KRAJ       aberacija, vinjeta, balans bijele, kontrast, zasicenje, zrno (Post.cpp), pa
//                Standard/AgX, sRGB i sahovnica (Film.cpp toDisplay, tracer_resolve.slang)
//
// Sjena catchera se ovdje ne filtrira (A-trous na procesoru to radi u gotovom kadru): pregled.

struct Push{
    uint mode, width, height, step;
    uint srcOffset, dstOffset, srcWidth, srcHeight;
    uint dstWidth, dstHeight, backdrop, view;
    uint plate, flags, grainSeed, padding;          // flags: 1 sahovnica, 2 filtar, 4 post, 16 izvor B
    float gain, bloom, bloomThreshold, bloomScale;
    float chromatic, vignette, contrast, saturation;
    float grain, balanceR, balanceG, balanceB;
};

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] StructuredBuffer<float4> accCg;
[[vk::binding(1,0)]] StructuredBuffer<float4> accBackground;
[[vk::binding(2,0)]] StructuredBuffer<float4> accLit;
[[vk::binding(3,0)]] StructuredBuffer<float4> accShadowed;
[[vk::binding(4,0)]] StructuredBuffer<float4> accAlbedo;
[[vk::binding(5,0)]] StructuredBuffer<float4> accNormal;
[[vk::binding(6,0)]] StructuredBuffer<float> accDepth;
[[vk::binding(7,0)]] StructuredBuffer<uint4> textureInfo;
[[vk::binding(8,0)]] StructuredBuffer<uint> texels;
[[vk::binding(9,0)]] RWStructuredBuffer<float4> bufferA;     // svjetlo, varijanca
[[vk::binding(10,0)]] RWStructuredBuffer<float4> bufferB;
[[vk::binding(11,0)]] RWStructuredBuffer<float4> guideN;     // normala, dubina
[[vk::binding(12,0)]] RWStructuredBuffer<float4> guideA;     // albedo, pokrivenost
[[vk::binding(13,0)]] RWStructuredBuffer<float4> image;      // linearno rgb (s ekspozicijom), alfa
[[vk::binding(14,0)]] RWStructuredBuffer<float4> pyramid;    // razine bloom-a jedna za drugom
[[vk::binding(15,0)]] RWStructuredBuffer<uint> display;

static const uint NONE = 0xFFFFFFFFu;
static const float KERNEL[3] = {3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};

float luminance(float3 c){ return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
float srgbToLinear(float v){ return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float linearToSrgb(float v){ v = max(0.0, v); return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
uint hash1(uint x){
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}
float toUnit(uint x){ return float(x >> 8) * (1.0 / 16777216.0); }

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

// Razina piramide / slika kao Plane iz Post.cpp: at (rub se ponavlja) i sample (bilinearno)
float3 levelAt(uint offset, uint w, uint h, int x, int y){
    x = clamp(x, 0, int(w) - 1);
    y = clamp(y, 0, int(h) - 1);
    return pyramid[offset + uint(y) * w + uint(x)].rgb;
}
float3 levelSample(uint offset, uint w, uint h, float x, float y){
    x -= 0.5; y -= 0.5;
    const float fx = floor(x), fy = floor(y);
    const float tx = x - fx, ty = y - fy;
    const int ix = int(fx), iy = int(fy);
    return lerp(lerp(levelAt(offset, w, h, ix, iy), levelAt(offset, w, h, ix + 1, iy), tx),
                lerp(levelAt(offset, w, h, ix, iy + 1), levelAt(offset, w, h, ix + 1, iy + 1), tx), ty);
}
float3 imageAt(int x, int y){
    x = clamp(x, 0, int(push.width) - 1);
    y = clamp(y, 0, int(push.height) - 1);
    return image[uint(y) * push.width + uint(x)].rgb;
}
float3 imageSample(float x, float y){
    x -= 0.5; y -= 0.5;
    const float fx = floor(x), fy = floor(y);
    const float tx = x - fx, ty = y - fy;
    const int ix = int(fx), iy = int(fy);
    return lerp(lerp(imageAt(ix, iy), imageAt(ix + 1, iy), tx), lerp(imageAt(ix, iy + 1), imageAt(ix + 1, iy + 1), tx), ty);
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

float4 source(uint i){ return (push.flags & 16u) != 0 ? bufferB[i] : bufferA[i]; }
void target(uint i, float4 v){ if((push.flags & 16u) != 0) bufferA[i] = v; else bufferB[i] = v; }

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    const uint mode = push.mode;
    // razine piramide imaju svoju velicinu
    if(mode == 4 || mode == 5){
        if(id.x >= push.dstWidth || id.y >= push.dstHeight) return;
        const uint o = push.dstOffset + id.y * push.dstWidth + id.x;
        if(mode == 4){
            const int sx = int(id.x) * 2, sy = int(id.y) * 2;
            const uint s = push.srcOffset, w = push.srcWidth, h = push.srcHeight;
            pyramid[o] = float4((levelAt(s, w, h, sx, sy) + levelAt(s, w, h, sx + 1, sy) + levelAt(s, w, h, sx, sy + 1) +
                                 levelAt(s, w, h, sx + 1, sy + 1)) * 0.25, 0.0);
        }else{
            const float sx = float(push.srcWidth) / float(push.dstWidth), sy = float(push.srcHeight) / float(push.dstHeight);
            const float3 up = levelSample(push.srcOffset, push.srcWidth, push.srcHeight, (float(id.x) + 0.5) * sx, (float(id.y) + 0.5) * sy);
            pyramid[o] = float4(pyramid[o].rgb + up, 0.0);
        }
        return;
    }
    if(id.x >= push.width || id.y >= push.height) return;
    const uint w = push.width, h = push.height;
    const uint i = id.y * w + id.x;

    if(mode == 0){
        const float n = accShadowed[i].w;
        if(n <= 0.0){
            bufferA[i] = float4(0.0); guideN[i] = float4(0.0, 0.0, 0.0, 1e10); guideA[i] = float4(0.0);
            return;
        }
        const float4 cgSum = accCg[i], albedoSum = accAlbedo[i], normalSum = accNormal[i];
        const float3 albedo = albedoSum.rgb / n;
        float3 normal = normalSum.xyz;
        if(dot(normal, normal) > 0.0) normal = normalize(normal);
        const float mean = albedoSum.w / n;
        float variance = max(0.0, normalSum.w / n - mean * mean) / n;
        const float3 modulation = max(albedo, float3(0.02));
        const float m = max(0.02, luminance(modulation));
        variance /= m * m;
        bufferA[i] = float4(cgSum.rgb / n / modulation, variance);
        guideN[i] = float4(normal, accDepth[i]);
        guideA[i] = float4(albedo, cgSum.w / n);
        return;
    }
    if(mode == 1){
        const int step = int(push.step);
        const float4 sp = source(i);
        const float4 gn = guideN[i], ga = guideA[i];
        const float cp = ga.w;
        if(cp <= 0.0){ target(i, sp); return; }
        const float3 np = gn.xyz, ap = ga.rgb;
        const float zp = gn.w, lp = luminance(sp.rgb);
        const bool emptyP = dot(np, np) == 0.0;
        const float inverseSigmaL = 1.0 / (4.0 * sqrt(max(0.0, sp.w)) + 1e-4);
        float3 sum = float3(0.0);
        float weightSum = 0.0, varianceSum = 0.0;
        for(int dy = -2; dy <= 2; ++dy){
            const int qy = int(id.y) + dy * step;
            if(qy < 0 || qy >= int(h)) continue;
            for(int dx = -2; dx <= 2; ++dx){
                const int qx = int(id.x) + dx * step;
                if(qx < 0 || qx >= int(w)) continue;
                const uint q = uint(qy) * w + uint(qx);
                const float4 gq = guideN[q], aq = guideA[q];
                float weight = KERNEL[abs(dx)] * KERNEL[abs(dy)];
                if(!(emptyP && dot(gq.xyz, gq.xyz) == 0.0)){
                    float nn = max(0.0, dot(np, gq.xyz));
                    nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn;
                    weight *= nn;
                }
                weight *= max(0.0, 1.0 - 4.0 * abs(cp - aq.w));
                if(weight <= 0.0) continue;
                const float4 sq = source(q);
                const float zq = gq.w;
                const float scale = max(1e-6, 0.02 * float(step) * min(zp, zq));
                const float exponent = abs(zp - zq) / scale + length(ap - aq.rgb) * 8.0 + abs(lp - luminance(sq.rgb)) * inverseSigmaL;
                weight *= exp(-exponent);
                if(weight <= 0.0) continue;
                sum += sq.rgb * weight;
                weightSum += weight;
                varianceSum += weight * weight * sq.w;
            }
        }
        target(i, weightSum > 0.0 ? float4(sum / weightSum, varianceSum / (weightSum * weightSum)) : sp);
        return;
    }
    if(mode == 2){
        const float4 shadowed = accShadowed[i];
        const float n = shadowed.w;
        if(n <= 0.0){ image[i] = float4(0.0, 0.0, 0.0, 1.0); return; }
        const float4 cgSum = accCg[i], backgroundSum = accBackground[i], litSum = accLit[i];
        float3 cg = cgSum.rgb / n;
        if((push.flags & 2u) != 0) cg = source(i).rgb * max(accAlbedo[i].rgb / n, float3(0.02));
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
            if(push.plate != NONE) plate = samplePlate(float2((float(id.x) + 0.5) / float(w), (float(id.y) + 0.5) / float(h)));
            rgb = cg + (1.0 - a) * s * plate;
        }
        image[i] = float4(rgb * push.gain, alpha);
        return;
    }
    if(mode == 3){
        float3 c = image[i].rgb;
        if(push.bloomThreshold > 0.0){
            const float l = luminance(c);
            c = l > push.bloomThreshold ? c * ((l - push.bloomThreshold) / l) : float3(0.0);
        }
        pyramid[i] = float4(c, 0.0);
        return;
    }
    if(mode == 6){
        const float sx = float(push.srcWidth) / float(w), sy = float(push.srcHeight) / float(h);
        const float3 blur = levelSample(push.srcOffset, push.srcWidth, push.srcHeight, (float(id.x) + 0.5) * sx, (float(id.y) + 0.5) * sy);
        const float4 c = image[i];
        image[i] = float4(c.rgb + push.bloom * (blur * push.bloomScale - pyramid[i].rgb), c.w);
        return;
    }
    // mode 7: ostatak posta i prikaz
    const float4 pixel = image[i];
    float3 c = pixel.rgb;
    const float alpha = clamp(pixel.w, 0.0, 1.0);
    if((push.flags & 4u) != 0){
        const float2 centre = float2(float(w) * 0.5, float(h) * 0.5);
        const float halfDiagonal = length(centre);
        const float2 p = float2(float(id.x) + 0.5, float(id.y) + 0.5);
        if(push.chromatic != 0.0){
            const float2 d = (p - centre) / halfDiagonal;
            const float r = length(d);
            const float2 shift = r > 0.0 ? d / r * (push.chromatic * r * r) : float2(0.0);
            c.r = imageSample(p.x - shift.x, p.y - shift.y).r;
            c.b = imageSample(p.x + shift.x, p.y + shift.y).b;
        }
        if(push.vignette != 0.0){
            const float2 d = (p - centre) / halfDiagonal;
            c *= max(0.0, 1.0 - push.vignette * dot(d, d));
        }
        c *= float3(push.balanceR, push.balanceG, push.balanceB);
        if(push.contrast != 1.0){
            for(int k = 0; k < 3; ++k) if(c[k] > 0.0) c[k] = 0.18 * pow(c[k] / 0.18, push.contrast);
        }
        if(push.saturation != 1.0){
            const float l = luminance(c);
            c = max(float3(0.0), float3(l) + (c - float3(l)) * push.saturation);
        }
        if(push.grain > 0.0){
            const uint g = hash1(i * 0x9E3779B1u ^ hash1(push.grainSeed));
            const float u1 = max(1e-7, toUnit(g)), u2 = toUnit(hash1(g));
            const float gauss = sqrt(-2.0 * log(u1)) * cos(6.28318531 * u2);
            const float l = luminance(c);
            c *= max(0.0, 1.0 + push.grain * gauss * sqrt(0.18 / max(l, 1e-3)) * min(1.0, l / 1e-3));
        }
    }
    if(alpha > 0.0 && alpha < 1.0) c /= alpha;
    if(push.view == 1) c = agx(c);
    float3 outColour = float3(float(toByte(linearToSrgb(c.r))), float(toByte(linearToSrgb(c.g))), float(toByte(linearToSrgb(c.b))));
    uint outAlpha = toByte(alpha);
    if((push.flags & 1u) != 0 && outAlpha < 255){
        const float checker = ((id.x / 16 + id.y / 16) & 1u) != 0 ? 70.0 : 45.0;
        const float af = float(outAlpha) / 255.0;
        outColour = floor(outColour * af + checker * (1.0 - af));
        outAlpha = 255;
    }
    display[i] = uint(outColour.r) | (uint(outColour.g) << 8) | (uint(outColour.b) << 16) | (outAlpha << 24);
}
