// G4: boja koja ovisi o smjeru pogleda.
//
// Ono sto stupanj 0 ne moze: odsjaj na metalu, nebo koje se mijenja, povrsina koja je svijetla
// samo iz jednog kuta. Bez toga scena izgleda ispravno ali plosnato.
//
// SVAKA PROVJERA GLEDA VISE SMJEROVA, i to je cijela poanta. Najvjerojatnija greska ovdje je
// krivo procitan raspored koeficijenata, a ona se NE VIDI NA JEDNOM KADRU - slika je uvjerljiva,
// samo se s kutom mijenja krivo. Test koji uzme jedan smjer prespavao bi je.
//
// Sto se provjerava, i zasto bas to:
//
//   stupanj 0        bez visih stupnjeva boja mora biti ista iz svih smjerova
//   prosjek po sferi svi visi bazisi integriraju se u nulu preko sfere, pa prosjek boje po
//                    ravnomjerno razasutim smjerovima MORA dati natrag stupanj 0. Ovo hvata
//                    krivu konstantu u bilo kojem clanu, jer bi ona pomaknula prosjek
//   parnost          stupanj 1 je neparan (f(-d) = -f(d) oko DC), stupanj 2 paran. To su
//                    svojstva bazisa i ne ovise ni o jednom koeficijentu
//   kanali su odvojeni   koeficijenti crvenog smiju mijenjati SAMO crveno. Ovo je provjera
//                    koja hvata krivo procitan raspored, i jedina koja to moze
//   poznata vrijednost   uz samo z-koeficijent, boja je DC + c1*z, analiticki
//
// I kontrola koja cuva ostale: boja se MORA mijenjati sa smjerom. Inace bi sve gore prolazilo
// i kad bi funkcija ignorirala vise stupnjeve i uvijek vracala DC.
#include "TestHarness.h"
#include "Core/Splat.h"

#include <cmath>
#include <string>
#include <vector>

namespace{

//Smjerovi ravnomjerno razasuti po sferi. Fibonaccijeva spirala, jer nasumicni smjerovi
//nakupljaju se u mrljama i prosjek bi konvergirao presporo da bi bio tvrdnja
std::vector<glm::vec3> sphereDirections(uint32_t count){
    std::vector<glm::vec3> directions;
    directions.reserve(count);
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for(uint32_t i = 0; i < count; ++i){
        const float z = 1.0f - 2.0f * (float(i) + 0.5f) / float(count);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z*z));
        const float a = golden * float(i);
        directions.push_back(glm::vec3(r * std::cos(a), r * std::sin(a), z));
    }
    return directions;
}

//Koeficijenti koji nisu ni nula ni jednaki, pa zamjena dva mjesta odmah pukne
std::vector<float> spread(uint32_t coeffsPerChannel){
    std::vector<float> rest(coeffsPerChannel * 3);
    for(uint32_t c = 0; c < 3; ++c){
        for(uint32_t k = 0; k < coeffsPerChannel; ++k){
            rest[c * coeffsPerChannel + k] = 0.37f * float(k + 1) - 0.21f * float(c) + 0.05f;
        }
    }
    return rest;
}

}

int main(){
    TestReport report("G4 boja iz smjera pogleda");

    const glm::vec3 dc(0.31f, -0.52f, 0.18f);
    const glm::vec3 flat = SplatMath::colorFromSH0(dc);
    const std::vector<glm::vec3> directions = sphereDirections(4096);

    // -------------------------------------------------------------------------------
    // Stupanj 0: nista se ne smije mijenjati sa smjerom
    // -------------------------------------------------------------------------------

    {
        const std::vector<float> rest = spread(15);
        float worst = 0.0f;
        for(const glm::vec3& d : directions){
            const glm::vec3 got = SplatMath::colorFromSH(dc, rest.data(), 15, 0, d);
            worst = std::max(worst, glm::length(got - flat));
        }
        report.check("stupanj 0 ne ovisi o smjeru", worst < 1e-7f,
            fmt("kroz %zu smjerova, najveca promjena %.2e", directions.size(), double(worst)));
    }

    // -------------------------------------------------------------------------------
    // Prosjek po sferi mora dati natrag stupanj 0
    // -------------------------------------------------------------------------------

    {
        std::string trace;
        bool allHold = true;
        const uint32_t counts[3] = {3, 8, 15};

        for(uint32_t degree = 1; degree <= 3; ++degree){
            const uint32_t perChannel = counts[degree - 1];
            const std::vector<float> rest = spread(perChannel);

            glm::vec3 sum(0.0f);
            for(const glm::vec3& d : directions){
                sum += SplatMath::colorFromSH(dc, rest.data(), perChannel, degree, d);
            }
            const glm::vec3 mean = sum / float(directions.size());
            const float error = glm::length(mean - flat);

            trace += fmt("st%u:%.1e ", degree, double(error));
            //Fibonaccijeva spirala nije egzaktna kvadratura, pa ostaje ostatak reda 1e-4
            if(error > 2e-3f) allHold = false;
        }

        report.check("prosjek po sferi je stupanj 0", allHold,
            fmt("odstupanje prosjeka od ravne boje: %s", trace.c_str()));
    }

    // -------------------------------------------------------------------------------
    // Parnost: stupanj 1 je neparan, stupanj 2 paran
    // -------------------------------------------------------------------------------

    {
        //Samo stupanj 1: f(d) + f(-d) = 2*DC, jer se neparni clanovi ponistavaju
        const std::vector<float> rest = spread(3);
        float worstOdd = 0.0f;
        for(const glm::vec3& d : directions){
            const glm::vec3 a = SplatMath::colorFromSH(dc, rest.data(), 3, 1, d);
            const glm::vec3 b = SplatMath::colorFromSH(dc, rest.data(), 3, 1, -d);
            worstOdd = std::max(worstOdd, glm::length(a + b - 2.0f * flat));
        }

        //Stupanj 2 s ugasenim stupnjem 1: f(d) = f(-d), jer su parni clanovi simetricni
        std::vector<float> onlySecond = spread(8);
        for(uint32_t c = 0; c < 3; ++c){
            for(uint32_t k = 0; k < 3; ++k) onlySecond[c * 8 + k] = 0.0f;
        }
        float worstEven = 0.0f;
        for(const glm::vec3& d : directions){
            const glm::vec3 a = SplatMath::colorFromSH(dc, onlySecond.data(), 8, 2, d);
            const glm::vec3 b = SplatMath::colorFromSH(dc, onlySecond.data(), 8, 2, -d);
            worstEven = std::max(worstEven, glm::length(a - b));
        }

        report.check("parnost bazisa", worstOdd < 1e-5f && worstEven < 1e-5f,
            fmt("stupanj 1 neparan do %.2e, stupanj 2 paran do %.2e",
                double(worstOdd), double(worstEven)));
    }

    // -------------------------------------------------------------------------------
    // KANALI SU ODVOJENI - jedina provjera koja hvata krivo procitan raspored
    // -------------------------------------------------------------------------------

    {
        std::string trace;
        bool allHold = true;

        for(uint32_t channel = 0; channel < 3; ++channel){
            //Sve nule osim jednog kanala. Ako je raspored krivo procitan, ovo procuri drugamo
            std::vector<float> rest(45, 0.0f);
            for(uint32_t k = 0; k < 15; ++k){
                rest[channel * 15 + k] = 0.4f + 0.1f * float(k);
            }

            float leak = 0.0f;
            float own = 0.0f;
            for(const glm::vec3& d : directions){
                const glm::vec3 got = SplatMath::colorFromSH(dc, rest.data(), 15, 3, d);
                const glm::vec3 delta = got - flat;
                for(uint32_t c = 0; c < 3; ++c){
                    if(c == channel) own = std::max(own, std::fabs(delta[int(c)]));
                    else leak = std::max(leak, std::fabs(delta[int(c)]));
                }
            }

            trace += fmt("k%u:svoj %.3f tudji %.1e ", channel, double(own), double(leak));
            if(leak > 1e-7f || own < 0.1f) allHold = false;
        }

        report.check("kanali su odvojeni", allHold, trace.c_str());
    }

    // -------------------------------------------------------------------------------
    // Poznata vrijednost: samo z-koeficijent
    // -------------------------------------------------------------------------------

    {
        //sh(1) je clan uz +c1*z. Sve ostalo nula, pa je boja tocno DC + c1*z*koeficijent
        std::vector<float> rest(9, 0.0f);
        rest[0 * 3 + 1] = 1.0f;   //crveni, drugi koeficijent stupnja 1

        constexpr float c1 = 0.4886025119029199f;
        float worst = 0.0f;
        for(const glm::vec3& d : directions){
            const glm::vec3 got = SplatMath::colorFromSH(dc, rest.data(), 3, 1, d);
            const float expected = flat.r + c1 * d.z;
            worst = std::max(worst, std::fabs(got.r - expected));
        }

        report.check("poznata vrijednost uz z", worst < 1e-6f,
            fmt("boja = DC + %.7f*z kroz %zu smjerova, najgore odstupanje %.2e",
                double(c1), directions.size(), double(worst)));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: boja se MORA mijenjati sa smjerom
    // -------------------------------------------------------------------------------

    {
        const std::vector<float> rest = spread(15);
        glm::vec3 low(1e30f), high(-1e30f);
        for(const glm::vec3& d : directions){
            const glm::vec3 got = SplatMath::colorFromSH(dc, rest.data(), 15, 3, d);
            for(int c = 0; c < 3; ++c){
                low[c] = std::min(low[c], got[c]);
                high[c] = std::max(high[c], got[c]);
            }
        }
        const glm::vec3 range = high - low;

        report.check("boja se stvarno mijenja",
            range.r > 0.1f && range.g > 0.1f && range.b > 0.1f,
            fmt("raspon po kanalima %.3f %.3f %.3f", double(range.r), double(range.g), double(range.b)));
    }

    //I da smjer koji ne postoji ne srusi nista
    {
        const std::vector<float> rest = spread(15);
        const glm::vec3 got = SplatMath::colorFromSH(dc, rest.data(), 15, 3, glm::vec3(0.0f));
        report.check("nulti smjer daje ravnu boju",
            glm::length(got - flat) < 1e-7f,
            fmt("%.4f %.4f %.4f", double(got.r), double(got.g), double(got.b)));
    }

    return report.result();
}
