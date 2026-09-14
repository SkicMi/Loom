// Sto vrijedi jedna rekonstrukcija - bez poznate istine.
//
// ZASTO OVO POSTOJI. Otkad se Loomov solver usporedjuje s COLMAP-om, brojke moraju dolaziti iz
// ISTOG racuna, inace se usporedjuju dvije definicije umjesto dva rjesenja. COLMAP prijavljuje
// prosjek reprojekcije, mi medijan; on ne mjeri glatkocu putanje, mi mjerimo. Zato oba modela
// prolaze kroz ovaj alat.
//
// STO SE MJERI, i sve bez ijednog poznatog odgovora:
//
//   reprojekcija     gdje tocke padaju naspram toga gdje su izmjerene. Jedina mjera koja izravno
//                    kaze slaze li se rjesenje sa slikom
//   glatkoca         putanja kamere koja trza je dokaz da nesto NE valja. Ne dokazuje tocnost -
//                    glatka a kriva putanja je moguca - ali neglatka je dovoljna za sumnju
//   baza             kut pod kojim se zrake sijeku. Dubina iz uske baze ne postoji, koliko god
//                    tocaka bilo, pa je ovo mjera koliko se dubini SMIJE vjerovati
//   pokrivenost      koliko kamera vidi koliko tocaka; rijetka veza znaci krhku rekonstrukciju
#include <Engine/ColmapImport.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("Upotreba: ModelInfo mapa_s_cameras.txt\n");
        return 1;
    }

    Engine::ColmapModel model;
    if(!Engine::readColmapText(argv[1], model)){
        std::printf("Ne mogu procitati model iz %s\n", argv[1]);
        return 1;
    }

    const Engine::Reconstruction& state = model.reconstruction;

    std::printf("== MODEL ==\n");
    std::printf("  kamere       %u\n", state.posedCameras);
    std::printf("  tocke        %u\n", state.solvedPoints);
    std::printf("  opazanja     %zu (%.0f po kameri)\n", model.observations.size(),
                state.posedCameras ? double(model.observations.size()) / double(state.posedCameras) : 0.0);
    std::printf("  kamera       %s, fx %.1f, glavna tocka %.1f %.1f, %ux%u\n",
                model.cameraModel.c_str(), double(model.intrinsics.fx),
                double(model.intrinsics.cx), double(model.intrinsics.cy),
                model.intrinsics.width, model.intrinsics.height);
    std::printf("  distorzija   k1 %.5f k2 %.5f\n", model.radialK1, model.radialK2);

    std::printf("\n== SLAGANJE SA SLIKOM ==\n");
    std::printf("  reprojekcija %.4f px (medijan)\n", state.medianReprojection);

    //Koliko tocaka vidi svaka kamera, i koliko kamera vidi svaku tocku
    std::vector<double> perCamera(state.posedCameras, 0.0), perPoint(state.solvedPoints, 0.0);
    for(const Engine::Observation& one : model.observations){
        if(one.camera < perCamera.size()) perCamera[one.camera] += 1.0;
        if(one.point < perPoint.size()) perPoint[one.point] += 1.0;
    }
    std::printf("  po kameri    medijan %.0f tocaka\n", medianOf(perCamera));
    std::printf("  po tocki     medijan %.1f kamera (duljina traga)\n", medianOf(perPoint));

    // -------------------------------------------------------------------------------
    // Glatkoca putanje
    // -------------------------------------------------------------------------------

    std::vector<glm::vec3> centres;
    for(size_t i = 0; i < state.poses.size(); ++i) if(state.posed[i]) centres.push_back(state.poses[i].position);

    std::vector<double> turns, speeds, steps;
    for(size_t i = 2; i < centres.size(); ++i){
        const glm::vec3 a = centres[i - 1] - centres[i - 2];
        const glm::vec3 b = centres[i] - centres[i - 1];
        const double lengthA = double(glm::length(a)), lengthB = double(glm::length(b));
        if(lengthA <= 0.0 || lengthB <= 0.0) continue;
        turns.push_back(glm::degrees(std::acos(std::max(-1.0, std::min(1.0,
            double(glm::dot(a, b)) / (lengthA * lengthB))))));
        speeds.push_back(lengthB / lengthA);
        steps.push_back(lengthB);
    }

    std::printf("\n== PUTANJA ==\n");
    std::printf("  skretanje    %.2f st po kadru (medijan; glatko = malo)\n", medianOf(turns));
    std::printf("  omjer brzina %.3f (glatko = oko 1)\n", medianOf(speeds));
    std::printf("  korak        %.4f (mjerilo je slobodno, pa je ovo odnos a ne metri)\n", medianOf(steps));

    // -------------------------------------------------------------------------------
    // Baza: pod kojim se kutom zrake sijeku
    // -------------------------------------------------------------------------------
    //
    // Po tocki se uzima NAJVECI kut medju kamerama koje ju vide - to je najbolja baza koju ta
    // tocka ima. Ako je i on malen, dubina te tocke je nagadjanje ma koliko kamera ju vidjelo

    std::vector<std::vector<uint32_t>> seenBy(state.solvedPoints);
    for(const Engine::Observation& one : model.observations){
        if(one.point < seenBy.size()) seenBy[one.point].push_back(one.camera);
    }

    std::vector<double> bestAngles;
    bestAngles.reserve(seenBy.size());
    for(size_t point = 0; point < seenBy.size(); ++point){
        const std::vector<uint32_t>& cameras = seenBy[point];
        if(cameras.size() < 2) continue;

        double widest = 0.0;
        //Dovoljno je usporediti prvu i zadnju te par izmedju - puna petlja po svim parovima bi na
        //milijun opazanja bila skuplja od svega ostalog zajedno
        for(size_t i = 0; i < cameras.size(); ++i){
            for(size_t j = i + 1; j < std::min(cameras.size(), i + 4); ++j){
                const glm::vec3 toA = glm::normalize(state.points[point] - state.poses[cameras[i]].position);
                const glm::vec3 toB = glm::normalize(state.points[point] - state.poses[cameras[j]].position);
                widest = std::max(widest, double(glm::degrees(std::acos(std::max(-1.0f, std::min(1.0f, glm::dot(toA, toB)))))));
            }
        }
        bestAngles.push_back(widest);
    }

    std::sort(bestAngles.begin(), bestAngles.end());
    std::printf("\n== BAZA ==\n");
    if(bestAngles.empty()){
        std::printf("  nijedna tocka nije vidjena iz dvije kamere\n");
    }else{
        std::printf("  kut po tocki medijan %.2f st, donjih 10%% ispod %.2f st, gornjih 10%% iznad %.2f st\n",
                    bestAngles[bestAngles.size() / 2],
                    bestAngles[bestAngles.size() / 10],
                    bestAngles[bestAngles.size() * 9 / 10]);

        size_t weak = 0;
        for(double angle : bestAngles) if(angle < 1.0) ++weak;
        std::printf("  ispod 1 st  %zu od %zu tocaka (%.0f%%) - njihovoj dubini se ne vjeruje\n",
                    weak, bestAngles.size(), 100.0 * double(weak) / double(bestAngles.size()));
    }

    return 0;
}
