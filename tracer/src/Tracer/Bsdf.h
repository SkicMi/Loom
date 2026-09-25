#pragma once
//=============================================================================================
// BSDF - kako ploha vraca svjetlo. Principijelni model, isti parametri kao glTF i Blenderov
// Principled BSDF, racunat kao mjesavina fizikalnih slojeva:
//
//   lak (clearcoat)      GGX, F0 = 0.04, vlastita hrapavost; prigusi sve ispod sebe
//   metal                GGX, Fresnel iz boje (Schlick)
//   dielektrik           GGX odsjaj (F0 iz ior-a) + Lambertova difuzija ispod njega
//   staklo (transmission) hrapavi dielektrik s lomom (Walter 2007), boja tonira ono sto prolazi
//
// OCUVANJE ENERGIJE - ono po cemu se realisticno razlikuje od "lijepog":
//
//   - GGX s jednim odbijanjem gubi energiju na hrapavim plohama (svjetlo koje se medju
//     mikrofasetama odbije vise puta nestane) - hrapav bijeli metal ispadne siv. Nadoknada po
//     Turquinu (2019): f *= 1 + F0 * (1 - E) / E, gdje je E(mu, alfa) albedo GGX-a s F = 1,
//     izracunat Monte Carlom pri pokretanju. Bijeli metal u "peci" (jednoliko nebo) vraca tocno 1
//   - difuzija ispod odsjaja dobije samo ono sto odsjaj NIJE odbio: (1 - Es(mu_o)). Bijela
//     plastika u peci vraca 1, ne 1.04
//   - lak prigusi slojeve ispod za svoj albedo
//
// Test (test_tracer) to mjeri "bijelom peci": objekt u jednolikoj okolini radijancije 1 mora
// vratiti 1 - ni vise (stvara energiju), ni manje (gubi je).
//
// SMJEROVI su u lokalnom sustavu plohe: z je normala okrenuta PREMA PROMATRACU (wo.z > 0), a eta
// je omjer indeksa loma druge strane i strane promatraca - ulazak u staklo 1.5, izlazak 1/1.5.
// Uzorkovanje odsjaja je po VIDLJIVIM normalama (Dupuy i Benyoub 2023, sferne kape): samo
// mikrofasete koje promatrac stvarno vidi, pa nema uzoraka koji se odbiju u plohu
//=============================================================================================
#include <glm/glm.hpp>

namespace Tracer{

//Parametri u tocki, vec s ucitanim mapama
struct SurfaceParameters{
    glm::vec3 baseColor{0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ior = 1.5f;
    float specular = 1.0f;
    float transmission = 0.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.03f;
};

struct BsdfSample{
    glm::vec3 wi{0.0f};                 //lokalno
    glm::vec3 weight{0.0f};             //f * |cos| / pdf
    float pdf = 0.0f;
    bool transmitted = false;
    bool glossy = false;                //uzorak iz uskog odsjaja/loma (za snimku u lomu)
};

class Bsdf{
public:
    //eta: indeks loma druge strane / strane promatraca
    Bsdf(const SurfaceParameters& parameters, const glm::vec3& wo, float eta);

    //f * |cos(wi)|, i gustoca kojom bi sample() izabrao wi. Lokalno
    glm::vec3 eval(const glm::vec3& wi, float& pdf) const;

    bool sample(const glm::vec2& u, float lobeChoice, BsdfSample& out) const;

    //Najuzi alfa medju slojevima koji postoje - koliko je materijal "zrcalan"
    float minimumAlpha() const;

private:
    enum Lobe{ Coat, Metal, Specular, Diffuse, Glass, LobeCount };
    SurfaceParameters p;
    glm::vec3 wo;
    float eta;
    float alpha, coatAlpha;
    float dielectricF0;
    float specularScale = 1.0f;
    glm::vec3 metalF0;
    float weights[LobeCount] = {};      //udjeli pri uzorkovanju, zbroj 1
    float coatAttenuation = 1.0f;       //(1 - c * Ec(mu_o))
    float diffuseScale = 1.0f;          //(1 - Es(mu_o))
    float specularMs = 1.0f;            //Turquinov faktor dielektrika
    glm::vec3 metalMs{1.0f};

    glm::vec3 evalLobe(int lobe, const glm::vec3& wi, float& pdf) const;
};

//Tablice energije, izlozene radi testa: E(mu, hrapavost) je albedo GGX-a s F = 1, a A i B su
//rastav za Schlicka: albedo s F0 je F0*A + B
float ggxAlbedo(float mu, float roughness);
float ggxSchlickA(float mu, float roughness);
float ggxSchlickB(float mu, float roughness);

//Fresnel dielektrika, tocno (obje polarizacije). eta = n_druga / n_upadna
float fresnelDielectric(float cosTheta, float eta);

}
