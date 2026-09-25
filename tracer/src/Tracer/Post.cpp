#include "Tracer/Post.h"
#include "Tracer/Sampler.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace Tracer{

namespace{

float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

//Retci na sve jezgre; tijelo pise samo svoj redak
template<class Body>
void parallelRows(uint32_t height, const Body& body){
    const unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), height));
    std::atomic<uint32_t> next{0};
    auto work = [&]{ for(uint32_t y = next++; y < height; y = next++) body(y); };
    std::vector<std::thread> pool;
    for(unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for(std::thread& t : pool) t.join();
}

struct Plane{
    uint32_t width = 0, height = 0;
    std::vector<glm::vec3> pixels;
    const glm::vec3& at(int x, int y) const{
        x = std::clamp(x, 0, int(width) - 1);
        y = std::clamp(y, 0, int(height) - 1);
        return pixels[size_t(y) * width + size_t(x)];
    }
    //Bilinearno, (u, v) u pikselima sa sredistem piksela na +0.5
    glm::vec3 sample(float x, float y) const{
        x -= 0.5f; y -= 0.5f;
        const float fx = std::floor(x), fy = std::floor(y);
        const float tx = x - fx, ty = y - fy;
        const int ix = int(fx), iy = int(fy);
        return glm::mix(glm::mix(at(ix, iy), at(ix + 1, iy), tx), glm::mix(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
    }
};

//Pola velicine, prosjek 2x2: srednja vrijednost (dakle i ukupno svjetlo po povrsini) ostaje ista
Plane downsample(const Plane& source){
    Plane out;
    out.width = std::max(1u, (source.width + 1) / 2);
    out.height = std::max(1u, (source.height + 1) / 2);
    out.pixels.resize(size_t(out.width) * out.height);
    parallelRows(out.height, [&](uint32_t y){
        for(uint32_t x = 0; x < out.width; ++x){
            const int sx = int(x) * 2, sy = int(y) * 2;
            out.pixels[size_t(y) * out.width + x] =
                (source.at(sx, sy) + source.at(sx + 1, sy) + source.at(sx, sy + 1) + source.at(sx + 1, sy + 1)) * 0.25f;
        }
    });
    return out;
}

//Na zadanu velicinu, bilinearno (satorski filtar): ponovljeno kroz piramidu daje glatko zvono
Plane upsample(const Plane& source, uint32_t width, uint32_t height){
    Plane out;
    out.width = width;
    out.height = height;
    out.pixels.resize(size_t(width) * height);
    const float sx = float(source.width) / float(width), sy = float(source.height) / float(height);
    parallelRows(height, [&](uint32_t y){
        for(uint32_t x = 0; x < width; ++x)
            out.pixels[size_t(y) * width + x] = source.sample((float(x) + 0.5f) * sx, (float(y) + 0.5f) * sy);
    });
    return out;
}

}

bool PostSettings::active() const{
    if(!enabled) return false;
    return exposure != 0.0f || bloom > 0.0f || chromaticAberration != 0.0f || vignette != 0.0f ||
           std::abs(temperature - 6500.0f) > 1.0f || tint != 0.0f || contrast != 1.0f || saturation != 1.0f || grain > 0.0f;
}

void blackBody(float kelvin, float rgb[3]){
    const double t = std::clamp(double(kelvin), 1667.0, 25000.0);
    const double t2 = t * t, t3 = t2 * t;
    const double x = t <= 4000.0 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                                 : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    const double x2 = x * x, x3 = x2 * x;
    const double y = t <= 2222.0 ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
                   : t <= 4000.0 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                                 :  3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    const double X = x / y, Y = 1.0, Z = (1.0 - x - y) / y;
    double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
    r = std::max(r, 0.0); g = std::max(g, 0.0); b = std::max(b, 0.0);
    const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    rgb[0] = float(r / l); rgb[1] = float(g / l); rgb[2] = float(b / l);
}

void applyPost(std::vector<float>& rgba, uint32_t width, uint32_t height, const PostSettings& s){
    if(!s.active() || width == 0 || height == 0) return;
    const size_t n = size_t(width) * height;
    if(rgba.size() < n * 4) return;

    Plane image;
    image.width = width;
    image.height = height;
    image.pixels.resize(n);
    const float gain = std::exp2(s.exposure);
    for(size_t i = 0; i < n; ++i) image.pixels[i] = glm::vec3(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]) * gain;

    //-- BLOOM: piramida (pola, pola, ...), pa natrag gore zbrajajuci razine. Svaka razina je
    //   zamucenje na dvostruko vecem mjerilu; prosjek razina je mekano zvono sirokog repa - kao
    //   rasprsenje u stvarnom objektivu, koje nije Gauss nego ima dugi rep
    if(s.bloom > 0.0f){
        Plane bright = image;
        if(s.bloomThreshold > 0.0f){
            for(glm::vec3& c : bright.pixels){
                const float l = luminance(c);
                c = l > s.bloomThreshold ? c * ((l - s.bloomThreshold) / l) : glm::vec3(0.0f);
            }
        }
        const float radius = std::max(2.0f, s.bloomRadius * float(width));
        const int levels = std::clamp(int(std::ceil(std::log2(radius))), 1, 12);
        std::vector<Plane> pyramid{bright};
        for(int k = 1; k <= levels && (pyramid.back().width > 1 || pyramid.back().height > 1); ++k)
            pyramid.push_back(downsample(pyramid.back()));
        const int top = int(pyramid.size()) - 1;
        //Gore: u_k = gore(u_{k+1}) + d_k; na kraju prosjek svih razina 1..top (razina 0 je ostra)
        Plane accumulated = pyramid[size_t(top)];
        for(int k = top - 1; k >= 1; --k){
            Plane up = upsample(accumulated, pyramid[size_t(k)].width, pyramid[size_t(k)].height);
            for(size_t i = 0; i < up.pixels.size(); ++i) up.pixels[i] += pyramid[size_t(k)].pixels[i];
            accumulated = std::move(up);
        }
        const Plane blur = upsample(accumulated, width, height);
        const float mixAmount = std::clamp(s.bloom, 0.0f, 1.0f), scale = 1.0f / float(std::max(1, top));
        //Svjetlo koje se rasprsi ODE iz svog piksela: slika + udio * (zamuceno - ostro)
        for(size_t i = 0; i < n; ++i) image.pixels[i] += mixAmount * (blur.pixels[i] * scale - bright.pixels[i]);
    }

    const glm::vec2 centre(float(width) * 0.5f, float(height) * 0.5f);
    const float halfDiagonal = glm::length(centre);

    //-- KROMATSKA ABERACIJA: crveno se na rubu pomakne van, plavo unutra, zeleno ostaje -------------
    std::vector<float> alpha(n);
    for(size_t i = 0; i < n; ++i) alpha[i] = rgba[i * 4 + 3];
    if(s.chromaticAberration != 0.0f){
        const Plane source = image;
        parallelRows(height, [&](uint32_t y){
            for(uint32_t x = 0; x < width; ++x){
                const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                const glm::vec2 d = (p - centre) / halfDiagonal;
                const float r = glm::length(d);
                const glm::vec2 shift = r > 0.0f ? d / r * (s.chromaticAberration * r * r) : glm::vec2(0.0f);
                glm::vec3& out = image.pixels[size_t(y) * width + x];
                //Crveno se uvecava vise: piksel uzima crveno od blize sredini, pa crveni rub ide van
                out.r = source.sample(p.x - shift.x, p.y - shift.y).r;
                out.b = source.sample(p.x + shift.x, p.y + shift.y).b;
            }
        });
    }

    float white[3] = {1.0f, 1.0f, 1.0f}, neutral[3];
    blackBody(s.temperature, white);
    blackBody(6500.0f, neutral);
    glm::vec3 balance(white[0] / neutral[0], white[1] / neutral[1], white[2] / neutral[2]);
    balance.g *= 1.0f - 0.25f * std::clamp(s.tint, -1.0f, 1.0f);
    balance /= luminance(balance);

    parallelRows(height, [&](uint32_t y){
        for(uint32_t x = 0; x < width; ++x){
            const size_t i = size_t(y) * width + x;
            glm::vec3 c = image.pixels[i];
            //Vinjeta: kutovi gube `vignette` svjetla, glatko od sredine
            if(s.vignette != 0.0f){
                const glm::vec2 d = (glm::vec2(float(x) + 0.5f, float(y) + 0.5f) - centre) / halfDiagonal;
                c *= std::max(0.0f, 1.0f - s.vignette * glm::dot(d, d));
            }
            c *= balance;
            if(s.contrast != 1.0f){
                for(int k = 0; k < 3; ++k) if(c[k] > 0.0f) c[k] = 0.18f * std::pow(c[k] / 0.18f, s.contrast);
            }
            if(s.saturation != 1.0f){
                const float l = luminance(c);
                c = glm::max(glm::vec3(0.0f), glm::vec3(l) + (c - glm::vec3(l)) * s.saturation);
            }
            //Zrno: relativni sum kao fotoni, jaci u sjeni (sigma ~ 1/sqrt(svjetla)), srednja 0
            if(s.grain > 0.0f){
                const uint32_t h = sampling::hash(uint32_t(i) * 0x9E3779B1u ^ sampling::hash(s.grainSeed));
                const float u1 = std::max(1e-7f, sampling::toUnit(h)), u2 = sampling::toUnit(sampling::hash(h));
                const float gauss = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.28318531f * u2);
                const float l = luminance(c);
                c *= std::max(0.0f, 1.0f + s.grain * gauss * std::sqrt(0.18f / std::max(l, 1e-3f)) * std::min(1.0f, l / 1e-3f));
            }
            rgba[i * 4] = c.r;
            rgba[i * 4 + 1] = c.g;
            rgba[i * 4 + 2] = c.b;
            rgba[i * 4 + 3] = alpha[i];
        }
    });
}

}
