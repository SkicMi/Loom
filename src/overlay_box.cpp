// Kocka nacrtana preko pravih kadrova, iz rijesenih poza.
//
// ZASTO OVO, KAD VEC IMAMO BROJKE. Reprojekcija mjeri koliko se rjesenje slaze s tockama koje je
// SAMO nasло - to je zatvoren krug. Kocka ga otvara: postavi se na jedno mjesto u SVIJETU i crta
// iz svake poze redom. Ako poze valjaju, kocka stoji zalijepljena za scenu kroz cijelu snimku; ako
// ne valjaju, pluta, klizi ili diše. To je provjera koju gledatelj presudi u sekundi, a nijedan
// broj ne moze zamijeniti - ista ona koja se u matchmoveu radi prije nego se isporuci.
//
// Uz kocku se crtaju i rekonstruirane tocke: one moraju sjediti na stvarnim detaljima u slici.
// Kocka kaze jesu li POZE dobre, tocke kazu je li GEOMETRIJA dobra, i te dvije stvari mogu
// promasiti odvojeno.
//
// SAMO ZICANI OKVIR, bez skrivanja bridova. Kocka koja se vidi kroz zid je ovdje znacajka a ne
// mana: zanima nas stoji li mirno, a ne izgleda li uvjerljivo.
#include <Engine/ColmapImport.h>
#include <Spool/ImageFile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace{

struct Colour{ uint8_t r, g, b; };

void plot(Spool::Image& image, int x, int y, const Colour& colour, int thickness){
    for(int dy = -thickness; dy <= thickness; ++dy){
        for(int dx = -thickness; dx <= thickness; ++dx){
            const int px = x + dx, py = y + dy;
            if(px < 0 || py < 0 || px >= int(image.width) || py >= int(image.height)) continue;
            const size_t at = (size_t(py) * image.width + size_t(px)) * 4;
            image.pixels[at + 0] = colour.r;
            image.pixels[at + 1] = colour.g;
            image.pixels[at + 2] = colour.b;
        }
    }
}

//Bresenham bi bio uredniji, ali crta se preko 4K slike i jasnoca je ovdje vaznija od elegancije
void line(Spool::Image& image, glm::vec2 a, glm::vec2 b, const Colour& colour, int thickness){
    const float length = glm::length(b - a);
    const int steps = std::max(2, int(length));
    for(int i = 0; i <= steps; ++i){
        const glm::vec2 at = a + (b - a) * (float(i) / float(steps));
        plot(image, int(at.x), int(at.y), colour, thickness);
    }
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(int argc, char** argv){
    if(argc < 4){
        std::printf("Upotreba: OverlayBox model_txt mapa_sa_slikama izlazna_mapa [velicina_kocke]\n");
        std::printf("  Kocka se postavi u teziste oblaka tocaka; velicina je udio raspona scene\n");
        return 1;
    }

    const std::string modelPath = argv[1];
    const std::string imageDirectory = argv[2];
    const std::string outputDirectory = argv[3];
    const double sizeFraction = argc > 4 ? std::atof(argv[4]) : 0.15;

    Engine::ColmapModel model;
    if(!Engine::readColmapText(modelPath, model)){
        std::printf("Ne mogu procitati model iz %s\n", modelPath.c_str());
        return 1;
    }

    const Engine::Reconstruction& state = model.reconstruction;

    // -------------------------------------------------------------------------------
    // Gdje postaviti kocku
    // -------------------------------------------------------------------------------
    //
    // U teziste oblaka tocaka, jer je to mjesto koje najveci broj kamera vidi. Uzima se MEDIJAN po
    // osi a ne prosjek: jedna odbjegla tocka na sto metara povukla bi prosjek za sobom, a medijan
    // ne primijeti

    std::vector<double> xs, ys, zs;
    for(size_t point = 0; point < state.points.size(); ++point){
        if(!state.solved[point]) continue;
        xs.push_back(double(state.points[point].x));
        ys.push_back(double(state.points[point].y));
        zs.push_back(double(state.points[point].z));
    }
    if(xs.empty()){
        std::printf("Model nema nijednu tocku\n");
        return 1;
    }

    const glm::vec3 centre(float(medianOf(xs)), float(medianOf(ys)), float(medianOf(zs)));

    //Raspon scene kao razmak izmedju desetog i devedesetog percentila - opet da odbjegle tocke ne
    //odrede velicinu kocke
    std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end()); std::sort(zs.begin(), zs.end());
    const double spread = std::max({xs[xs.size() * 9 / 10] - xs[xs.size() / 10],
                                    ys[ys.size() * 9 / 10] - ys[ys.size() / 10],
                                    zs[zs.size() * 9 / 10] - zs[zs.size() / 10]});
    const float half = float(0.5 * sizeFraction * spread);

    std::printf("Kocka: srediste %.3f %.3f %.3f, poluosi %.3f (raspon scene %.3f)\n",
                double(centre.x), double(centre.y), double(centre.z), double(half), spread);

    //Osam vrhova i dvanaest bridova
    std::vector<glm::vec3> corners;
    for(int i = 0; i < 8; ++i){
        corners.push_back(centre + glm::vec3(
            (i & 1) ? half : -half,
            (i & 2) ? half : -half,
            (i & 4) ? half : -half));
    }
    const int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};

    // -------------------------------------------------------------------------------
    // Kadar po kadar
    // -------------------------------------------------------------------------------

    const Colour boxColour{255, 90, 20};        //narancasta: ne pojavljuje se u sobi slucajno
    const Colour pointColour{40, 220, 255};

    uint32_t drawn = 0, skipped = 0;
    for(size_t camera = 0; camera < state.poses.size(); ++camera){
        if(!state.posed[camera]) continue;
        if(camera >= model.imageNames.size()) continue;

        const std::string source = imageDirectory + "/" + model.imageNames[camera];
        Spool::Image image = Spool::loadImage(source);
        if(!image.isValid()){ ++skipped; continue; }

        //Tocke prve, da ih kocka prekrije gdje se poklope
        for(size_t point = 0; point < state.points.size(); ++point){
            if(!state.solved[point]) continue;
            glm::vec2 pixel;
            if(!Engine::project(state.poses[camera], model.intrinsics, state.points[point], pixel)) continue;
            plot(image, int(pixel.x), int(pixel.y), pointColour, 1);
        }

        //Kocka: brid se crta samo ako su OBA vrha ispred kamere. Vrh iza kamere projicira se u
        //besmislicu, a brid povucen do te besmislice presjekao bi pola slike
        uint32_t visibleEdges = 0;
        for(const auto& edge : edges){
            glm::vec2 a, b;
            if(!Engine::project(state.poses[camera], model.intrinsics, corners[size_t(edge[0])], a)) continue;
            if(!Engine::project(state.poses[camera], model.intrinsics, corners[size_t(edge[1])], b)) continue;
            line(image, a, b, boxColour, 3);
            ++visibleEdges;
        }

        char name[64];
        std::snprintf(name, sizeof(name), "/%05zu.png", camera);
        Spool::saveImage(outputDirectory + name, image);
        ++drawn;

        if(drawn % 50 == 0) std::printf("  %u kadrova (%u bridova zadnji put)\n", drawn, visibleEdges);
    }

    std::printf("Nacrtano %u kadrova, preskoceno %u (slika se nije nasla)\n", drawn, skipped);
    return 0;
}
