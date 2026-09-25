#include "Tracer/Bsdf.h"
#include "Tracer/Sampler.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Tracer{

namespace{

constexpr float Pi = 3.14159265358979f;

//Najmanji alfa. Nula bi bila savrseno zrcalo s beskonacnom gustocom; 2e-4 je hrapavost 0.014 -
//okom zrcalo, a brojevi (D do ~1e7) ostaju u floatu
float alphaFrom(float roughness){ return std::max(roughness * roughness, 2e-4f); }

float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

//GGX, izotropan. Zapisan preko tan^2 (x^2 + y^2 u brojniku) umjesto 1 - cos^2: kod malog alfa
//je 1 - cos^2 ispod preciznosti floata i vrh bi se odrezao
float ggxD(const glm::vec3& h, float a){
    if(h.z <= 0.0f) return 0.0f;
    const float a2 = a * a;
    const float s = (h.x * h.x + h.y * h.y) / a2 + h.z * h.z;
    return 1.0f / (Pi * a2 * s * s);
}

float ggxLambda(const glm::vec3& w, float a){
    const float z2 = w.z * w.z;
    if(z2 <= 0.0f) return 1e30f;
    const float t2 = (w.x * w.x + w.y * w.y) / z2;
    return 0.5f * (-1.0f + std::sqrt(1.0f + a * a * t2));
}

float ggxG1(const glm::vec3& w, float a){ return 1.0f / (1.0f + ggxLambda(w, a)); }
float ggxG2(const glm::vec3& wo, const glm::vec3& wi, float a){ return 1.0f / (1.0f + ggxLambda(wo, a) + ggxLambda(wi, a)); }

//Vidljive normale, sferne kape (Dupuy i Benyoub 2023). wo.z > 0
glm::vec3 sampleVisibleNormal(const glm::vec3& wo, float a, const glm::vec2& u){
    const glm::vec3 stretched = glm::normalize(glm::vec3(wo.x * a, wo.y * a, wo.z));
    const float phi = 2.0f * Pi * u.x;
    const float z = (1.0f - u.y) * (1.0f + stretched.z) - stretched.z;
    const float sinTheta = std::sqrt(std::clamp(1.0f - z * z, 0.0f, 1.0f));
    const glm::vec3 cap(sinTheta * std::cos(phi), sinTheta * std::sin(phi), z);
    const glm::vec3 h = cap + stretched;
    return glm::normalize(glm::vec3(h.x * a, h.y * a, std::max(h.z, 1e-9f)));
}

//Gustoca vidljive normale h iz smjera wo
float visibleNormalPdf(const glm::vec3& wo, const glm::vec3& h, float a){
    if(wo.z <= 0.0f) return 0.0f;
    return ggxG1(wo, a) * std::max(0.0f, glm::dot(wo, h)) * ggxD(h, a) / wo.z;
}

float schlick(float f0, float c){
    const float m = std::clamp(1.0f - c, 0.0f, 1.0f);
    const float m2 = m * m;
    return f0 + (1.0f - f0) * m2 * m2 * m;
}
glm::vec3 schlick(const glm::vec3& f0, float c){
    const float m = std::clamp(1.0f - c, 0.0f, 1.0f);
    const float m2 = m * m;
    return f0 + (glm::vec3(1.0f) - f0) * (m2 * m2 * m);
}

//---------------------------------------------------------------------------------------------
// TABLICE ENERGIJE. 32 x 32 (mu, hrapavost), svaka celija 1024 uzorka vidljivih normala iz
// Sobola - deterministicki, isti brojevi pri svakom pokretanju. Racunaju se jednom, pri prvom
// materijalu (~30 ms)
//---------------------------------------------------------------------------------------------
constexpr int TableSize = 32;

struct EnergyTables{
    std::array<float, TableSize * TableSize> e{}, a{}, b{};
    EnergyTables(){
        const int samples = 1024;
        for(int r = 0; r < TableSize; ++r){
            const float alpha = alphaFrom(float(r) / float(TableSize - 1));
            for(int m = 0; m < TableSize; ++m){
                const float mu = std::max(float(m) / float(TableSize - 1), 1e-3f);
                const glm::vec3 wo(std::sqrt(1.0f - mu * mu), 0.0f, mu);
                double sumE = 0.0, sumA = 0.0, sumB = 0.0;
                for(int i = 0; i < samples; ++i){
                    const glm::vec2 u(sampling::toUnit(sampling::sobol0(uint32_t(i))) + 0.5f / float(samples),
                                      sampling::toUnit(sampling::sobol1(uint32_t(i))) + 0.5f / float(samples));
                    const glm::vec3 h = sampleVisibleNormal(wo, alpha, glm::fract(u));
                    const float oh = glm::dot(wo, h);
                    const glm::vec3 wi = 2.0f * oh * h - wo;
                    if(wi.z <= 0.0f) continue;
                    //Uz uzorkovanje vidljivih normala tezina odsjaja je G2/G1 (D i Jacobian se pokrate)
                    const double weight = double(ggxG2(wo, wi, alpha) / ggxG1(wo, alpha));
                    const float m1 = 1.0f - std::clamp(oh, 0.0f, 1.0f);
                    const double m5 = double(m1 * m1 * m1 * m1 * m1);
                    sumE += weight;
                    sumA += weight * (1.0 - m5);
                    sumB += weight * m5;
                }
                const size_t at = size_t(r) * TableSize + size_t(m);
                e[at] = float(sumE / samples);
                a[at] = float(sumA / samples);
                b[at] = float(sumB / samples);
            }
        }
    }
    static float lookup(const std::array<float, TableSize * TableSize>& t, float mu, float roughness){
        const float x = std::clamp(mu, 0.0f, 1.0f) * float(TableSize - 1);
        const float y = std::clamp(roughness, 0.0f, 1.0f) * float(TableSize - 1);
        const int x0 = std::min(int(x), TableSize - 2), y0 = std::min(int(y), TableSize - 2);
        const float tx = x - float(x0), ty = y - float(y0);
        auto at = [&](int xi, int yi){ return t[size_t(yi) * TableSize + size_t(xi)]; };
        return (at(x0, y0) * (1.0f - tx) + at(x0 + 1, y0) * tx) * (1.0f - ty) +
               (at(x0, y0 + 1) * (1.0f - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
    }
};

const EnergyTables& tables(){
    static const EnergyTables instance;
    return instance;
}

}

const float* ggxAlbedoTable(){ return tables().e.data(); }
const float* ggxSchlickATable(){ return tables().a.data(); }
const float* ggxSchlickBTable(){ return tables().b.data(); }

float ggxAlbedo(float mu, float roughness){ return EnergyTables::lookup(tables().e, mu, roughness); }
float ggxSchlickA(float mu, float roughness){ return EnergyTables::lookup(tables().a, mu, roughness); }
float ggxSchlickB(float mu, float roughness){ return EnergyTables::lookup(tables().b, mu, roughness); }

float fresnelDielectric(float cosI, float eta){
    cosI = std::clamp(cosI, -1.0f, 1.0f);
    if(cosI < 0.0f){ eta = 1.0f / eta; cosI = -cosI; }
    const float sin2T = (1.0f - cosI * cosI) / (eta * eta);
    if(sin2T >= 1.0f) return 1.0f;                     //totalna unutarnja refleksija
    const float cosT = std::sqrt(std::max(0.0f, 1.0f - sin2T));
    const float parallel = (eta * cosI - cosT) / (eta * cosI + cosT);
    const float perpendicular = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5f * (parallel * parallel + perpendicular * perpendicular);
}

Bsdf::Bsdf(const SurfaceParameters& parameters, const glm::vec3& wo_, float eta_) : p(parameters), wo(wo_), eta(eta_){
    p.metallic = std::clamp(p.metallic, 0.0f, 1.0f);
    p.roughness = std::clamp(p.roughness, 0.0f, 1.0f);
    p.transmission = std::clamp(p.transmission, 0.0f, 1.0f);
    p.clearcoat = std::clamp(p.clearcoat, 0.0f, 1.0f);
    p.clearcoatRoughness = std::clamp(p.clearcoatRoughness, 0.0f, 1.0f);
    p.baseColor = glm::clamp(p.baseColor, glm::vec3(0.0f), glm::vec3(1.0f));
    wo.z = std::max(wo.z, 1e-5f);
    wo = glm::normalize(wo);
    alpha = alphaFrom(p.roughness);
    coatAlpha = alphaFrom(p.clearcoatRoughness);
    const float f = (p.ior - 1.0f) / (p.ior + 1.0f);
    dielectricF0 = std::clamp(f * f, 0.0f, 1.0f);
    //specular mnozi CIJELI Fresnel, ne samo F0: s nulom nema ni odsjaja pri okrznucu, pa je
    //ploha cisti Lambert (test sunca na podu to trazi)
    specularScale = std::clamp(p.specular, 0.0f, 1.0f);
    metalF0 = p.baseColor;

    const float mu = wo.z;
    const float E = std::max(1e-3f, ggxAlbedo(mu, p.roughness));
    const float A = ggxSchlickA(mu, p.roughness), B = ggxSchlickB(mu, p.roughness);
    specularMs = 1.0f + dielectricF0 * (1.0f - E) / E;
    metalMs = glm::vec3(1.0f) + metalF0 * ((1.0f - E) / E);
    const float specularAlbedo = std::min(1.0f, specularScale * (dielectricF0 * A + B) * specularMs);
    diffuseScale = 1.0f - specularAlbedo;
    const float coatAlbedo = p.clearcoat > 0.0f
        ? 0.04f * ggxSchlickA(mu, p.clearcoatRoughness) + ggxSchlickB(mu, p.clearcoatRoughness) : 0.0f;
    coatAttenuation = 1.0f - p.clearcoat * coatAlbedo;

    const float metal = p.metallic, dielectric = 1.0f - metal;
    const float glass = dielectric * p.transmission, opaque = dielectric * (1.0f - p.transmission);
    weights[Coat] = p.clearcoat * coatAlbedo;
    weights[Metal] = coatAttenuation * metal * std::max(0.05f, luminance((metalF0 * A + glm::vec3(B)) * metalMs));
    weights[Specular] = coatAttenuation * opaque * specularAlbedo;
    weights[Diffuse] = coatAttenuation * opaque * diffuseScale * luminance(p.baseColor);
    weights[Glass] = coatAttenuation * glass;
    float sum = 0.0f;
    for(float w : weights) sum += w;
    if(sum <= 0.0f){
        //Crno i neprozirno: nista se ne vraca, ali uzorkovanje mora nesto izabrati
        weights[Diffuse] = 1.0f;
        sum = 1.0f;
    }
    for(float& w : weights) w /= sum;
}

float Bsdf::minimumAlpha() const{
    float best = 1.0f;
    if(weights[Coat] > 0.0f) best = std::min(best, coatAlpha);
    if(weights[Metal] > 0.0f || weights[Specular] > 0.0f || weights[Glass] > 0.0f) best = std::min(best, alpha);
    return best;
}

glm::vec3 Bsdf::evalLobe(int lobe, const glm::vec3& wi, float& pdf) const{
    pdf = 0.0f;
    const float metal = p.metallic, dielectric = 1.0f - metal;
    const float glass = dielectric * p.transmission, opaque = dielectric * (1.0f - p.transmission);
    switch(lobe){
    case Coat: case Metal: case Specular:{
        if(wi.z <= 0.0f) return glm::vec3(0.0f);
        const float a = lobe == Coat ? coatAlpha : alpha;
        const glm::vec3 h = glm::normalize(wo + wi);
        const float oh = glm::dot(wo, h);
        if(oh <= 0.0f) return glm::vec3(0.0f);
        const float D = ggxD(h, a);
        pdf = ggxG1(wo, a) * D / (4.0f * wo.z);
        const float common = D * ggxG2(wo, wi, a) / (4.0f * wo.z);          //f*cos bez Fresnela
        if(lobe == Coat) return glm::vec3(p.clearcoat * schlick(0.04f, oh) * common);
        if(lobe == Metal) return coatAttenuation * metal * schlick(metalF0, oh) * metalMs * common;
        return glm::vec3(coatAttenuation * opaque * specularScale * schlick(dielectricF0, oh) * specularMs * common);
    }
    case Diffuse:{
        if(wi.z <= 0.0f) return glm::vec3(0.0f);
        pdf = wi.z / Pi;
        return coatAttenuation * opaque * diffuseScale * p.baseColor * (wi.z / Pi);
    }
    case Glass:{
        //Hrapavi dielektrik (Walter 2007), zapis kao u pbrt-v4. wo.z > 0 uvijek
        const float cosO = wo.z, cosI = wi.z;
        if(cosI == 0.0f) return glm::vec3(0.0f);
        const bool reflect = cosI > 0.0f;
        const float etap = reflect ? 1.0f : eta;
        glm::vec3 wm = wi * etap + wo;
        if(glm::dot(wm, wm) <= 0.0f) return glm::vec3(0.0f);
        wm = glm::normalize(wm);
        if(wm.z < 0.0f) wm = -wm;
        if(glm::dot(wm, wi) * cosI < 0.0f || glm::dot(wm, wo) * cosO < 0.0f) return glm::vec3(0.0f);
        const float F = fresnelDielectric(glm::dot(wo, wm), eta);
        const float D = ggxD(wm, alpha), G = ggxG2(wo, wi, alpha);
        const float scale = coatAttenuation * glass;
        if(reflect){
            pdf = visibleNormalPdf(wo, wm, alpha) / (4.0f * std::abs(glm::dot(wo, wm))) * F;
            return glm::vec3(scale * D * G * F / (4.0f * cosO));
        }
        const float denom = glm::dot(wi, wm) + glm::dot(wo, wm) / etap;
        const float denom2 = denom * denom;
        if(denom2 <= 0.0f) return glm::vec3(0.0f);
        pdf = visibleNormalPdf(wo, wm, alpha) * std::abs(glm::dot(wi, wm)) / denom2 * (1.0f - F);
        //Radijancija se pri prelasku u gusce sredstvo zbije u manji kut: / etap^2
        const float ft = (1.0f - F) * D * G * std::abs(glm::dot(wi, wm) * glm::dot(wo, wm) / (cosO * denom2)) / (etap * etap);
        return scale * p.baseColor * ft;
    }
    }
    return glm::vec3(0.0f);
}

glm::vec3 Bsdf::eval(const glm::vec3& wi, float& pdf) const{
    glm::vec3 f(0.0f);
    pdf = 0.0f;
    for(int lobe = 0; lobe < LobeCount; ++lobe){
        if(weights[lobe] <= 0.0f) continue;
        float lobePdf = 0.0f;
        f += evalLobe(lobe, wi, lobePdf);
        pdf += weights[lobe] * lobePdf;
    }
    return f;
}

bool Bsdf::sample(const glm::vec2& u, float choice, BsdfSample& out) const{
    int lobe = LobeCount - 1;
    float start = 0.0f;
    for(int k = 0; k < LobeCount; ++k){
        if(weights[k] <= 0.0f) continue;
        if(choice < start + weights[k] || k == LobeCount - 1){ lobe = k; break; }
        start += weights[k];
    }
    while(lobe > 0 && weights[lobe] <= 0.0f) --lobe;
    //Ostatak istog broja za odluku unutar sloja (odsjaj ili lom)
    const float rest = std::clamp((choice - start) / std::max(weights[lobe], 1e-12f), 0.0f, 0.99999994f);

    glm::vec3 wi;
    switch(lobe){
    case Diffuse:{
        const float r = std::sqrt(u.x), phi = 2.0f * Pi * u.y;
        wi = glm::vec3(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u.x)));
        break;
    }
    case Coat: case Metal: case Specular:{
        const glm::vec3 h = sampleVisibleNormal(wo, lobe == Coat ? coatAlpha : alpha, u);
        wi = 2.0f * glm::dot(wo, h) * h - wo;
        if(wi.z <= 0.0f) return false;
        break;
    }
    case Glass:{
        const glm::vec3 h = sampleVisibleNormal(wo, alpha, u);
        const float oh = glm::dot(wo, h);
        const float F = fresnelDielectric(oh, eta);
        if(rest < F){
            wi = 2.0f * oh * h - wo;
            if(wi.z <= 0.0f) return false;
        }else{
            const float sin2T = (1.0f - oh * oh) / (eta * eta);
            if(sin2T >= 1.0f) return false;
            const float cosT = std::sqrt(1.0f - sin2T);
            wi = -wo / eta + (oh / eta - cosT) * h;
            if(wi.z >= 0.0f) return false;
            wi = glm::normalize(wi);
        }
        break;
    }
    default: return false;
    }

    float pdf = 0.0f;
    const glm::vec3 f = eval(wi, pdf);
    if(!(pdf > 0.0f) || !std::isfinite(pdf)) return false;
    out.wi = wi;
    out.pdf = pdf;
    out.weight = f / pdf;
    out.transmitted = wi.z < 0.0f;
    out.glossy = lobe != Diffuse && (lobe == Coat ? coatAlpha : alpha) < 0.09f;
    return true;
}

}
