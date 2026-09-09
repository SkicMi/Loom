// G1: od zapisanog gaussiana do elipse na ekranu.
//
// Ovdje se ne crta nista, i namjerno: svaka tvrdnja nize ima ANALITICKI odgovor, broj koji se
// zna prije nego se ista pokrene. To je jedina vrsta provjere koja ne moze biti krug - dva
// primjerka istog izraza uvijek se slazu, pa i kad su oba kriva.
//
// Sto se provjerava, i zasto bas to:
//
//   dijagonala        bez rotacije kovarijanca MORA biti diag(a^2, b^2, c^2). Prvi filter
//   zamjena osi       rotacija od 90 stupnjeva oko z zamijeni a i b, i nista vise
//   determinanta      det = (a*b*c)^2 za BILO KOJU rotaciju, jer rotacija ne mijenja volumen.
//                     Ovo je jedina provjera koja hvata nenormaliziran kvaternion, a to je
//                     najtiša greska u cijelom koraku: scena i dalje izgleda kao scena, samo
//                     su sve mrlje krive velicine
//   simetricnost      matrica kovarijance je simetricna po definiciji
//   polumjer u        kugla polumjera s na udaljenosti d kroz zariste f daje krug polumjera
//     pikselima       f*s/d piksela. To je jedini broj u ovom fileu koji povezuje scenu s
//                     ekranom, i zna se iz slicnosti trokuta
//   iza kamere        gaussian iza kamere nema projekciju i mora dati nulu, ne smece
//
// I dvije koje cuvaju ostale: nesto se MORA razlikovati (mrlja izvan osi nije krug), i
// nenormaliziran kvaternion MORA oboriti determinantu.
#include "TestHarness.h"
#include "Core/Splat.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace{

//Najveca apsolutna razlika izmedju dvije matrice
float maxDifference(const glm::mat3& a, const glm::mat3& b){
    float worst = 0.0f;
    for(int c = 0; c < 3; ++c){
        for(int r = 0; r < 3; ++r){
            worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
        }
    }
    return worst;
}

//Kvaternioni razasuti po prostoru rotacija, plus nekoliko rubnih
std::vector<glm::quat> spreadOfRotations(){
    std::vector<glm::quat> rotations;
    rotations.push_back(glm::quat(1,0,0,0));
    for(int i = 0; i < 24; ++i){
        const float a = 0.37f * float(i);
        const glm::vec3 axis = glm::normalize(glm::vec3(std::sin(a), std::cos(1.7f*a), std::sin(0.3f*a) + 0.5f));
        rotations.push_back(glm::angleAxis(0.21f * float(i), axis));
    }
    return rotations;
}

}

int main(){
    TestReport report("G1 matematika splata");

    // -------------------------------------------------------------------------------
    // Aktivacija
    // -------------------------------------------------------------------------------

    //Sigmoid i exp na vrijednostima koje se znaju napamet
    const float half = SplatMath::activateOpacity(0.0f);
    const glm::vec3 scale = SplatMath::activateScale(glm::vec3(0.0f, 1.0f, -1.0f));

    report.check("logit i logaritam",
        std::fabs(half - 0.5f) < 1e-7f &&
        std::fabs(scale.x - 1.0f) < 1e-6f &&
        std::fabs(scale.y - float(M_E)) < 1e-5f &&
        std::fabs(scale.z - 1.0f/float(M_E)) < 1e-6f,
        fmt("sigmoid(0) = %.7f, exp(0,1,-1) = %.5f %.5f %.5f", double(half),
            double(scale.x), double(scale.y), double(scale.z)));

    //Duljine iz prave scene: izmjereno na 741883 gaussiana, od 0.414 do 1.908
    std::string norms;
    bool normalised = true;
    for(float length : {0.414f, 0.72f, 0.954f, 1.0f, 1.31f, 1.908f}){
        const glm::vec4 stored = glm::vec4(0.31f, -0.52f, 0.66f, 0.43f) * length;
        const glm::quat activated = SplatMath::activateRotation(stored);
        const float got = std::sqrt(activated.w*activated.w + activated.x*activated.x +
                                    activated.y*activated.y + activated.z*activated.z);
        if(std::fabs(got - 1.0f) > 1e-6f) normalised = false;
        norms += fmt("%.3f->%.6f ", double(length), double(got));
    }

    //I nula, koja u zapisu ne bi smjela biti ali ne smije otrovati sve dalje
    const glm::quat fromZero = SplatMath::activateRotation(glm::vec4(0.0f));
    const bool zeroIsSafe = std::isfinite(fromZero.w) && std::fabs(fromZero.w - 1.0f) < 1e-7f;

    report.check("kvaternion postaje jedinicni", normalised && zeroIsSafe,
        fmt("%s| nula daje %s", norms.c_str(), zeroIsSafe ? "neokrenut" : "SMECE"));

    //Stupanj 0: koeficijent nula je srednja siva, ne crna
    const glm::vec3 grey = SplatMath::colorFromSH0(glm::vec3(0.0f));
    const glm::vec3 bright = SplatMath::colorFromSH0(glm::vec3(1.0f, 0.0f, -1.0f));
    report.check("boja iz stupnja 0",
        std::fabs(grey.r - 0.5f) < 1e-7f &&
        std::fabs(bright.r - (0.5f + 0.28209479f)) < 1e-6f &&
        std::fabs(bright.b - (0.5f - 0.28209479f)) < 1e-6f,
        fmt("dc 0 -> %.4f, dc 1 -> %.4f, dc -1 -> %.4f", double(grey.r), double(bright.r), double(bright.b)));

    // -------------------------------------------------------------------------------
    // Kovarijanca: sto se zna bez ijednog piksela
    // -------------------------------------------------------------------------------

    const glm::vec3 radii(0.3f, 0.7f, 1.4f);

    const glm::mat3 unrotated = SplatMath::covariance3D(radii, glm::quat(1,0,0,0));
    glm::mat3 expected(0.0f);
    expected[0][0] = radii.x * radii.x;
    expected[1][1] = radii.y * radii.y;
    expected[2][2] = radii.z * radii.z;

    report.check("bez rotacije je dijagonala",
        maxDifference(unrotated, expected) < 1e-7f,
        fmt("najveca razlika %.2e prema diag(%.2f, %.2f, %.2f)",
            double(maxDifference(unrotated, expected)),
            double(expected[0][0]), double(expected[1][1]), double(expected[2][2])));

    //Cetvrtina okreta oko z: x postaje y. Nista se ne mijesa, samo zamijeni
    const glm::mat3 quarterTurn = SplatMath::covariance3D(
        radii, glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1)));
    glm::mat3 swapped(0.0f);
    swapped[0][0] = radii.y * radii.y;
    swapped[1][1] = radii.x * radii.x;
    swapped[2][2] = radii.z * radii.z;

    report.check("cetvrtina okreta zamijeni osi",
        maxDifference(quarterTurn, swapped) < 1e-6f,
        fmt("najveca razlika %.2e prema diag(%.2f, %.2f, %.2f)",
            double(maxDifference(quarterTurn, swapped)),
            double(swapped[0][0]), double(swapped[1][1]), double(swapped[2][2])));

    //Determinanta i simetricnost, kroz cijeli raspon rotacija
    const float expectedDeterminant = std::pow(radii.x * radii.y * radii.z, 2.0f);
    float worstDeterminant = 0.0f;
    float worstAsymmetry = 0.0f;
    for(const glm::quat& rotation : spreadOfRotations()){
        const glm::mat3 covariance = SplatMath::covariance3D(radii, rotation);
        worstDeterminant = std::max(worstDeterminant,
            std::fabs(glm::determinant(covariance) - expectedDeterminant) / expectedDeterminant);
        worstAsymmetry = std::max(worstAsymmetry, maxDifference(covariance, glm::transpose(covariance)));
    }

    report.check("rotacija ne mijenja volumen",
        worstDeterminant < 1e-5f,
        fmt("kroz 25 rotacija, najgore odstupanje determinante %.2e od %.6f",
            double(worstDeterminant), double(expectedDeterminant)));

    report.check("kovarijanca je simetricna", worstAsymmetry < 1e-7f,
        fmt("najveca razlika prema transponiranoj %.2e", double(worstAsymmetry)));

    //KONTROLA: bez normalizacije determinanta MORA odletjeti. Ovo je test testa - da se vidi
    //da gornja provjera stvarno hvata ono zbog cega postoji
    //Duljine su one izmjerene u pravom fileu: najkraca 0.414, najduza 1.908. Prvi pokusaj
    //ovog testa je pao jer sam uzeo vektor duljine 0.993 - kontrola koja ne kontrolira nista
    std::string ratios;
    float worstRatio = 1.0f;
    for(float length : {0.414f, 0.72f, 1.31f, 1.908f}){
        const glm::vec4 stored = glm::normalize(glm::vec4(0.31f, -0.52f, 0.66f, 0.43f)) * length;
        const glm::quat unnormalised(stored.x, stored.y, stored.z, stored.w);   //bez aktivacije
        const float ratio = glm::determinant(SplatMath::covariance3D(radii, unnormalised)) /
                            expectedDeterminant;
        ratios += fmt("%.3f->%.2fx ", double(length), double(ratio));
        if(std::fabs(ratio - 1.0f) > std::fabs(worstRatio - 1.0f)) worstRatio = ratio;
    }

    report.check("nenormaliziran kvaternion se vidi",
        std::fabs(worstRatio - 1.0f) > 0.5f,
        fmt("duljina -> determinanta prema tocnoj: %s", ratios.c_str()));

    // -------------------------------------------------------------------------------
    // Projekcija: jedini broj koji povezuje scenu s ekranom
    // -------------------------------------------------------------------------------

    //Kamera u ishodistu gleda niz -Z. Kugla polumjera s na udaljenosti d kroz zariste f mora
    //dati krug polumjera f*s/d piksela - slicnost trokuta, bez ijedne aproksimacije
    const float focal = 800.0f;
    const float sphereRadius = 0.05f;
    const float distance = 4.0f;
    const glm::mat4 view = glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0));

    const glm::mat3 sphere = SplatMath::covariance3D(glm::vec3(sphereRadius), glm::quat(1,0,0,0));
    const glm::mat2 onAxis = SplatMath::covariance2D(sphere, glm::vec3(0,0,-distance), view,
                                                     focal, focal, 1.3f, 1.3f, 0.0f);

    const float analytic = focal * sphereRadius / distance;
    const float analyticVariance = analytic * analytic;

    report.check("polumjer u pikselima",
        std::fabs(onAxis[0][0] - analyticVariance) / analyticVariance < 1e-5f &&
        std::fabs(onAxis[1][1] - analyticVariance) / analyticVariance < 1e-5f &&
        std::fabs(onAxis[0][1]) < 1e-6f,
        fmt("f*s/d = %.4f px -> varijanca %.4f, izracunato %.4f i %.4f, izvandijagonalno %.2e",
            double(analytic), double(analyticVariance), double(onAxis[0][0]), double(onAxis[1][1]),
            double(onAxis[0][1])));

    //Blur je jedini clan koji nije matematika nego mjera opreza, pa se mora vidjeti tocno kao
    //dodatak na dijagonalu i nigdje drugdje
    const glm::mat2 blurred = SplatMath::covariance2D(sphere, glm::vec3(0,0,-distance), view,
                                                      focal, focal, 1.3f, 1.3f, 0.3f);
    report.check("blur dira samo dijagonalu",
        std::fabs((blurred[0][0] - onAxis[0][0]) - 0.3f) < 1e-5f &&
        std::fabs((blurred[1][1] - onAxis[1][1]) - 0.3f) < 1e-5f &&
        std::fabs(blurred[0][1] - onAxis[0][1]) < 1e-7f,
        fmt("dijagonala +%.4f i +%.4f, izvandijagonalno +%.2e",
            double(blurred[0][0] - onAxis[0][0]), double(blurred[1][1] - onAxis[1][1]),
            double(blurred[0][1] - onAxis[0][1])));

    //KONTROLA: ista kugla izvan osi vise nije krug. Bez ovoga bi provjera iznad prosla i kad bi
    //projekcija ignorirala polozaj
    const glm::mat2 offAxis = SplatMath::covariance2D(sphere, glm::vec3(1.2f, 0.9f, -distance),
                                                      view, focal, focal, 1.3f, 1.3f, 0.0f);
    const float skew = std::fabs(offAxis[0][1]) / analyticVariance;

    report.check("izvan osi vise nije krug", skew > 0.01f,
        fmt("izvandijagonalni clan %.4f, to je %.1f%% varijance", double(offAxis[0][1]), double(100.0f*skew)));

    //Dvostruko dalje je dvostruko manje, i to na kvadrat u varijanci
    const glm::mat2 farther = SplatMath::covariance2D(sphere, glm::vec3(0,0,-2.0f*distance), view,
                                                      focal, focal, 1.3f, 1.3f, 0.0f);
    report.check("dvostruko dalje je cetvrtina varijance",
        std::fabs(farther[0][0] * 4.0f - onAxis[0][0]) / onAxis[0][0] < 1e-5f,
        fmt("%.4f naspram %.4f, omjer %.4f", double(onAxis[0][0]), double(farther[0][0]),
            double(onAxis[0][0] / farther[0][0])));

    //Iza kamere nema projekcije
    const glm::mat2 behind = SplatMath::covariance2D(sphere, glm::vec3(0,0,1.0f), view,
                                                     focal, focal, 1.3f, 1.3f, 0.3f);
    report.check("iza kamere daje nulu",
        behind[0][0] == 0.0f && behind[1][1] == 0.0f && behind[0][1] == 0.0f,
        fmt("%.4f %.4f %.4f", double(behind[0][0]), double(behind[1][1]), double(behind[0][1])));

    //I ogranicenje omjera: daleko izvan kadra mrlja se ne smije nastaviti siriti
    const glm::mat2 justOutside = SplatMath::covariance2D(sphere, glm::vec3(6.0f, 0.0f, -distance),
                                                          view, focal, focal, 1.3f, 1.3f, 0.0f);
    const glm::mat2 farOutside = SplatMath::covariance2D(sphere, glm::vec3(20.0f, 0.0f, -distance),
                                                         view, focal, focal, 1.3f, 1.3f, 0.0f);
    report.check("izvan kadra se mrlja ne razvlaci",
        std::fabs(justOutside[0][0] - farOutside[0][0]) < 1e-4f,
        fmt("na 6 jedinica %.4f, na 20 jedinica %.4f", double(justOutside[0][0]), double(farOutside[0][0])));

    return report.result();
}
