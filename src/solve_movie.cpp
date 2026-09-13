// Camera solve kao SNIMKA: prava snimka udje, a izadje let kroz ono sto je solver nasao.
//
//   ./SolveMovie virtual [kadrova] [luk u stupnjevima] [izlazna mapa]
//   ./SolveMovie snimka.mp4 [korak] [kadrova] [vidno polje] [izlazna mapa]
//
// Dva nacina, i razlika medju njima je cijeli smisao:
//
//   virtual      Loom sam nacrta scenu i vodi kameru poznatim putem. Solver dobije SAMO PIKSELE.
//                Istina je poznata, pa se rijesena putanja crta preko prave i promasaj se vidi
//   snimka       prava datoteka. Istine nema, pa se crta samo ono sto je solver nasao
//
// Zasto uopce: dosad se rjesenje branilo brojkama (reprojekcija, broj rijesenih kamera). Brojka
// kaze je li rjesenje konzistentno, ali ne kaze IZGLEDA LI PUTANJA KAO PUTANJA. Oko to vidi
// odmah - trzaj, zavoj koji se vraca u sebe, kamera koja odleti - a nijedna od dvije mjere to ne
// mora primijetiti.
//
// Tri cina, svaki gleda istu stvar s druge strane:
//
//   1. obilazak      pogled kruzi, a kamere se pale REDOM kojim su snimljene - pa se vidi kako
//                    putanja nastaje, a ne samo kako izgleda gotova
//   2. odozgo        isti oblak, pogled se spusta; tlocrt putanje je najstroziji sudac zavoja
//   3. voznja        pogled SJEDNE u rijesenu kameru i prolazi njezinim putem. Ako su poze krive,
//                    oblak ce se tresti - a to se iz brojke ne vidi
//
// STO JE OVDJE PRIKAZ A STO REZULTAT. Rezultat su poze i tocke; sve ostalo je prikaz i tako je i
// oznaceno:
//
//   mjerilo            rekonstrukcija ga nema (prvi pomak je jedinicni), pa se putanja rastegne
//                      na stalnu velicinu samo da uvijek stane u kadar
//   gore               iz slika se ne zna gdje je gore. Uzima se prosjecna "gore" os kamera -
//                      snimatelj drzi kameru uspravno - i to je pretpostavka, ne mjerenje
//   odbacene tocke     tocke jako daleko od sredista oblaka se ne crtaju. One SU u rezultatu;
//                      samo bi razvukle kadar toliko da se ostalo ne vidi
#include <Loom/Loom.h>

#include "Core/CameraIntrinsics.h"

#include <Spool/VideoFile.h>

#include <Engine/CameraHints.h>
#include <Engine/Reconstruct.h>
#include <Engine/Triangulate.h>
#include <Engine/Track.h>

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace{

std::vector<uint8_t> toGray(const Spool::Image& image){
    std::vector<uint8_t> gray(size_t(image.width) * image.height);
    for(size_t i = 0; i < gray.size(); ++i){
        gray[i] = uint8_t(0.299f * float(image.pixels[i * 4 + 0]) +
                          0.587f * float(image.pixels[i * 4 + 1]) +
                          0.114f * float(image.pixels[i * 4 + 2]));
    }
    return gray;
}

Loom::TextureHandle colour(Loom::Scene& scene, uint8_t r, uint8_t g, uint8_t b){
    const uint8_t pixels[16] = {r,g,b,255, r,g,b,255, r,g,b,255, r,g,b,255};
    return scene.createTexture(pixels, 2, 2);
}

//Sum u teksturi: glatka ploha nema sto pratiti, pa scena mora imati zrnce
std::vector<uint8_t> noiseTexture(uint32_t size, uint32_t seed){
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    uint32_t state = seed;
    for(size_t i = 0; i < size_t(size) * size; ++i){
        state = state * 1664525u + 1013904223u;
        const uint8_t value = uint8_t(60 + (state >> 24) % 160);
        pixels[i * 4 + 0] = value;
        pixels[i * 4 + 1] = uint8_t(60 + ((state >> 16) & 0xFF) % 160);
        pixels[i * 4 + 2] = uint8_t(60 + ((state >> 8) & 0xFF) % 160);
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

double angleBetween(const glm::quat& a, const glm::quat& b){
    const glm::dquat first = glm::normalize(glm::dquat(a));
    const glm::dquat second = glm::normalize(glm::dquat(b));
    glm::dquat difference = glm::conjugate(first) * second;
    if(difference.w < 0.0) difference = -difference;
    const double vector = std::sqrt(difference.x * difference.x + difference.y * difference.y +
                                    difference.z * difference.z);
    return glm::degrees(2.0 * std::atan2(vector, difference.w));
}

float smoothStep(float t){
    t = std::min(1.0f, std::max(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

}

int main(int argc, char** argv){
    const std::string source = argc > 1 ? std::string(argv[1]) : std::string("virtual");
    const bool virtualMode = (source == "virtual");

    const uint32_t footageCount = virtualMode ? (argc > 2 ? uint32_t(std::atoi(argv[2])) : 48u) : 0u;
    const float arc = virtualMode ? (argc > 3 ? float(std::atof(argv[3])) : 26.0f) : 0.0f;
    const uint32_t step = virtualMode ? 1u : (argc > 2 ? uint32_t(std::atoi(argv[2])) : 2u);
    const uint32_t wanted = virtualMode ? footageCount : (argc > 3 ? uint32_t(std::atoi(argv[3])) : 24u);
    const double givenFov = virtualMode ? 0.0 : (argc > 4 ? std::atof(argv[4]) : 0.0);
    const std::string outputDirectory = virtualMode ? (argc > 4 ? std::string(argv[4]) : std::string("."))
                                                    : (argc > 5 ? std::string(argv[5]) : std::string("."));
    //Dopustena relativna greska dubine. -1 znaci "ostavi ono sto Engine drzi razumnim"
    const double parallaxArgument = virtualMode ? (argc > 5 ? std::atof(argv[5]) : -1.0)
                                                : (argc > 6 ? std::atof(argv[6]) : -1.0);

    //Izlazna mapa "-" znaci: rijesi i izmjeri, ali ne crtaj. Za sweepove po pragovima, gdje je
    //snimka cist trosak
    const bool render = outputDirectory != "-";

    const uint32_t width = 1280, height = 720;

    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(width, height);
    //Tamna kroz cijelu snimku: Loom trazi da se boja pozadine odabere prije prvog kadra, a u
    //cinovima s rekonstrukcijom je prizor prigusen pa bi svjetlija pozadina progutala obzor
    scene.setClearColor({0.015f, 0.02f, 0.03f, 1.0f});
    scene.sun().setDirection({-0.4f, -1.0f, -0.3f});
    scene.environment().setAmbient({0.22f, 0.23f, 0.27f});

    const std::vector<uint8_t> noise = noiseTexture(128, 20260913);
    const Loom::TextureHandle noisy = scene.createTexture(noise.data(), 128, 128);
    const Loom::TextureHandle pointColour = colour(scene, 130, 175, 240);
    const Loom::TextureHandle solvedColour = colour(scene, 250, 160, 60);
    const Loom::TextureHandle truthColour = colour(scene, 90, 220, 120);
    const Loom::TextureHandle nowColour = colour(scene, 255, 245, 200);
    const Loom::TextureHandle ghostColour = colour(scene, 56, 62, 72);

    //Scena za virtualni nacin. Sve je obavijeno sumom jer glatka ploha nema uglova za pratiti
    auto drawWorld = [&](Loom::TextureHandle texture){
        scene.drawPlane(texture, Loom::Transform().scaled(24.0f));
        scene.drawCube(texture, Loom::Transform().at(-2.0f, 0.8f, -1.0f).scaled(1.6f));
        scene.drawCube(texture, Loom::Transform().at(1.8f, 0.5f, 1.2f).scaled(1.0f));
        scene.drawCube(texture, Loom::Transform().at(0.2f, 1.4f, -3.0f).scaled(2.2f));
        scene.drawSphere(texture, Loom::Transform().at(2.6f, 0.9f, -2.2f).scaled(1.2f));
        scene.drawCube(texture, Loom::Transform().at(-3.2f, 0.6f, 2.4f).scaled(1.2f));
    };

    Loom::Sequence sequence;
    sequence.setDirectory(outputDirectory);
    sequence.setPrefix("solve");

    Engine::TrackConfig trackConfig;
    trackConfig.window = 6;
    trackConfig.minDistance = 14.0f;
    trackConfig.maxCorners = 800;
    trackConfig.minTracks = 400;
    Engine::Tracker tracker(trackConfig);

    Engine::Intrinsics intrinsics;
    std::vector<Engine::Pose> truth;
    uint32_t used = 0;

    // -------------------------------------------------------------------------------
    // Kadrovi. U virtualnom nacinu ih Loom crta i odmah ulaze u snimku - prvi cin je ono
    // STO SOLVER VIDI, da se zna da dalje ne dolazi niotkuda
    // -------------------------------------------------------------------------------

    if(virtualMode){
        std::printf("Virtualna snimka: %u kadrova preko luka od %.0f st (%.2f st po kadru)\n",
                    footageCount, double(arc), double(arc) / double(std::max(1u, footageCount - 1)));

        for(uint32_t frame = 0; frame < footageCount; ++frame){
            const float t = float(frame) / float(footageCount - 1);
            const float angle = glm::radians(-0.5f * arc + arc * t);
            scene.setFrame(frame, 30.0f);
            scene.camera().setPosition({8.0f * std::sin(angle), 3.2f + 0.4f * t, 8.0f * std::cos(angle)});
            scene.camera().lookAt({0.0f, 0.7f, 0.0f});

            scene.startRendering();
                drawWorld(noisy);
            scene.endRendering();

            truth.push_back(Engine::Pose{scene.camera().getPosition(), scene.camera().getOrientation()});

            const std::vector<uint8_t> pixels = scene.readPixels();
            std::vector<uint8_t> gray(size_t(width) * height);
            for(size_t i = 0; i < gray.size(); ++i){
                gray[i] = uint8_t(0.299f * float(pixels[i * 4 + 0]) + 0.587f * float(pixels[i * 4 + 1]) +
                                  0.114f * float(pixels[i * 4 + 2]));
            }
            tracker.addFrame(Engine::GrayImage{gray.data(), width, height, width});
            ++used;

            //Svaki kadar dvaput: snimka ide na 30 kad/s, a prizor se mora stici vidjeti
            if(render){
                sequence.write(scene);
                sequence.write(scene);
            }
        }

        const CameraIntrinsics fromLoom = CameraIntrinsics::fromProjection(
            scene.camera().getProjection(width, height), width, height);
        intrinsics.fx = fromLoom.fx;
        intrinsics.fy = std::fabs(fromLoom.fy);
        intrinsics.cx = fromLoom.cx;
        intrinsics.cy = fromLoom.cy;
        intrinsics.width = width;
        intrinsics.height = height;
    }else{
        Spool::VideoReader reader(source);
        const Spool::VideoInfo& info = reader.info();
        std::printf("Snimka %ux%u, %.2f fps, %s\n", info.width, info.height, info.frameRate(), info.codec.c_str());

        Engine::SourceFacts facts;
        facts.width = info.width;
        facts.height = info.height;
        facts.frameRate = info.frameRate();
        facts.rotation = info.rotation;
        facts.pixelAspect = info.pixelAspect;
        facts.codec = info.codec;
        for(const auto& entry : info.metadata) facts.metadata.push_back(Engine::MetadataEntry{entry.first, entry.second});
        for(const Spool::VideoInfo::StreamNote& stream : info.streams){
            facts.streams.push_back(stream.kind + " " + stream.codec + " (" + stream.handler + ")");
        }
        const Engine::CameraHints hints = Engine::hintsFrom(facts);
        const double fieldOfView = givenFov > 0.0 ? givenFov : hints.horizontalFieldOfView;
        std::printf("  kamera: %s, vidno polje %.1f st (%s)\n",
                    hints.make.empty() ? "nepoznata" : hints.make.c_str(),
                    fieldOfView, givenFov > 0.0 ? "zadano rukom" : Engine::sourceName(hints.focalSource));

        uint32_t index = 0;
        while(!reader.atEnd() && used < wanted){
            const Spool::Image frame = reader.readNext();
            if(frame.pixels.empty()) break;
            if(index++ % step != 0) continue;
            const std::vector<uint8_t> gray = toGray(frame);
            tracker.addFrame(Engine::GrayImage{gray.data(), frame.width, frame.height, frame.width});
            ++used;
            std::printf("\r  kadar %u, tragova zivo %u  ", used, tracker.activeTracks());
            std::fflush(stdout);
        }
        std::printf("\n");

        intrinsics.width = info.width;
        intrinsics.height = info.height;
        intrinsics.cx = 0.5f * float(info.width);
        intrinsics.cy = 0.5f * float(info.height);
        const double focal = (0.5 * double(info.width)) / std::tan(0.5 * fieldOfView * 3.14159265358979 / 180.0);
        intrinsics.fx = float(focal);
        intrinsics.fy = float(focal);
    }

    std::printf("  %u kadrova, %u tragova, %zu opazanja\n", used, tracker.trackCount(),
                tracker.observations().size());
    if(used < 3){ std::printf("Premalo kadrova.\n"); return 1; }

    // -------------------------------------------------------------------------------
    // Rekonstrukcija - solver o sceni ne zna nista osim tragova
    // -------------------------------------------------------------------------------

    Engine::ReconstructConfig reconstructConfig;
    if(parallaxArgument >= 0.0) reconstructConfig.maxRelativeDepthError = parallaxArgument;

    const Engine::Reconstruction state = Engine::reconstruct(tracker.observations(), used,
                                                             tracker.trackCount(), intrinsics,
                                                             reconstructConfig);
    std::printf("  rijeseno %u od %u kamera, %u tocaka, reprojekcija %.3f px (prag paralakse %.3f st)\n",
                state.posedCameras, used, state.solvedPoints, state.medianReprojection,
                state.parallaxLimitDegrees);
    if(state.posedCameras < 3){ std::printf("Premalo rijesenih kamera.\n"); return 1; }

    std::vector<size_t> cameras;
    for(size_t i = 0; i < state.poses.size(); ++i) if(state.posed[i]) cameras.push_back(i);

    // -------------------------------------------------------------------------------
    // Odakle rep. Pretpostavio sam da su daleke tocke one s uskom paralaksom - to se ne smije
    // pretpostaviti nego izmjeriti, pa se ovdje gleda paralaksa bas tih tocaka
    // -------------------------------------------------------------------------------

    {
        std::vector<std::vector<Engine::View>> perPoint(tracker.trackCount());
        for(const Engine::Observation& observation : tracker.observations()){
            if(state.posed[observation.camera]){
                perPoint[observation.point].push_back(Engine::View{observation.camera, observation.pixel});
            }
        }

        glm::vec3 middle(0.0f);
        {
            std::vector<float> axis[3];
            for(size_t i = 0; i < state.points.size(); ++i){
                if(!state.solved[i]) continue;
                for(int a = 0; a < 3; ++a) axis[a].push_back(state.points[i][a]);
            }
            for(int a = 0; a < 3; ++a){
                if(axis[a].empty()) continue;
                std::sort(axis[a].begin(), axis[a].end());
                middle[a] = axis[a][axis[a].size() / 2];
            }
        }

        std::vector<std::pair<double, double>> byDistance;   //udaljenost od sredista, paralaksa
        for(size_t i = 0; i < state.points.size(); ++i){
            if(!state.solved[i]) continue;
            byDistance.emplace_back(double(glm::length(state.points[i] - middle)),
                                    Engine::parallaxDegrees(state.poses, intrinsics, perPoint[i]));
        }
        std::sort(byDistance.begin(), byDistance.end());

        auto parallaxMedian = [&](size_t from, size_t to){
            std::vector<double> angles;
            for(size_t i = from; i < to && i < byDistance.size(); ++i) angles.push_back(byDistance[i].second);
            if(angles.empty()) return 0.0;
            std::sort(angles.begin(), angles.end());
            return angles[angles.size() / 2];
        };
        const size_t tenth = std::max<size_t>(1, byDistance.size() / 10);
        std::printf("  paralaksa: bliskih 10%% %.3f st, dalekih 10%% %.3f st\n",
                    parallaxMedian(0, tenth), parallaxMedian(byDistance.size() - tenth, byDistance.size()));
    }

    // -------------------------------------------------------------------------------
    // Iz rjesenja u kadar. Sve u ovom odjeljku je PRIKAZ, ne rezultat
    // -------------------------------------------------------------------------------

    std::function<glm::vec3(const glm::vec3&)> place;
    std::function<glm::quat(const glm::quat&)> turn;
    float reach = 5.0f;

    if(virtualMode){
        //Istina je poznata, pa se rjesenje vraca u NJEZIN sustav: rekonstrukcija ne zna gdje je
        //scena ni kolika je, i to dvoje se oduzima prije usporedbe. Sve ostalo mora se poklopiti
        //samo - to je isti racun kao u S6 i SolveVieweru
        const Engine::Pose& origin = truth[cameras.front()];
        size_t farthest = cameras.back();
        for(size_t i : cameras){
            if(glm::length(truth[i].position - origin.position) >
               glm::length(truth[farthest].position - origin.position)) farthest = i;
        }
        const float truthBaseline = glm::length(truth[farthest].position - origin.position);
        const float solvedBaseline = glm::length(state.poses[farthest].position - state.poses[cameras.front()].position);
        const float scale = solvedBaseline > 1e-9f ? truthBaseline / solvedBaseline : 1.0f;

        place = [&, scale](const glm::vec3& point){
            return origin.position + origin.orientation * ((point - state.poses[cameras.front()].position) * scale);
        };
        turn = [&](const glm::quat& rotation){
            return glm::normalize(origin.orientation * glm::conjugate(state.poses[cameras.front()].orientation) * rotation);
        };
        reach = 6.0f;
    }else{
        //Bez istine: srediste po medijanu, doseg po p75. Vidi zaglavlje i mjerenja nize
        glm::vec3 centre(0.0f);
        std::vector<float> axis[3];
        for(size_t i = 0; i < state.points.size(); ++i){
            if(!state.solved[i]) continue;
            for(int a = 0; a < 3; ++a) axis[a].push_back(state.points[i][a]);
        }
        for(int a = 0; a < 3; ++a){
            if(axis[a].empty()) continue;
            std::sort(axis[a].begin(), axis[a].end());
            centre[a] = axis[a][axis[a].size() / 2];
        }

        glm::vec3 up(0.0f);
        for(size_t i : cameras) up += state.poses[i].orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        up = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::quat toWorld = glm::rotation(up, glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<float> radius;
        for(size_t i = 0; i < state.points.size(); ++i){
            if(state.solved[i]) radius.push_back(glm::length(state.points[i] - centre));
        }
        std::sort(radius.begin(), radius.end());
        auto percentile = [&](double q){
            return radius.empty() ? 1.0f : radius[size_t(q * double(radius.size() - 1))];
        };
        float pathReach = 0.0f;
        for(size_t i : cameras) pathReach = std::max(pathReach, glm::length(state.poses[i].position - centre));

        //U jedinicama dosega putanje, ne u sirovim brojevima: mjerilo rekonstrukcije je slobodno
        //pa se sirove udaljenosti ne daju usporedjivati izmedju dva pokretanja
        const float unit = std::max(pathReach, 1e-6f);
        std::printf("  udaljenost tocaka (u dosezima putanje): p25 %.2f  p50 %.2f  p75 %.2f  p90 %.2f  p99 %.2f\n",
                    double(percentile(0.25) / unit), double(percentile(0.50) / unit),
                    double(percentile(0.75) / unit), double(percentile(0.90) / unit),
                    double(percentile(0.99) / unit));
        //p75, ne najveca vrijednost: na dronskoj snimci je p50 = 0.11 a p90 = 107 - cetvrtini tocaka
        //su zrake gotovo paralelne pa su odletjele u beskonacnost. One JESU u rezultatu i
        //reprojekcija ih ne kaznjava (daleka tocka dobro reprojicira ma gdje po zraki bila), ali
        //kadar bi odnijele
        const float scale = 5.0f / std::max(std::max(pathReach, percentile(0.75)), 1e-6f);
        place = [&, centre, toWorld, scale](const glm::vec3& point){
            return toWorld * ((point - centre) * scale);
        };
        turn = [&, toWorld](const glm::quat& rotation){ return glm::normalize(toWorld * rotation); };
        reach = 5.0f;
    }

    std::vector<glm::vec3> track;
    std::vector<glm::quat> facing;
    for(size_t i : cameras){
        track.push_back(place(state.poses[i].position));
        facing.push_back(turn(state.poses[i].orientation));
    }

    std::vector<glm::vec3> shown;
    for(size_t i = 0; i < state.points.size(); ++i){
        if(!state.solved[i]) continue;
        const glm::vec3 placed = place(state.points[i]);
        if(glm::length(placed) <= 2.5f * reach) shown.push_back(placed);
    }
    std::printf("  crtam %zu od %u tocaka i %zu kamera\n", shown.size(), state.solvedPoints, track.size());

    //Koliko je promasio - samo kad se ima s cim usporediti
    if(virtualMode){
        std::vector<double> rotations, positions;
        for(size_t k = 0; k < cameras.size(); ++k){
            const size_t i = cameras[k];
            rotations.push_back(angleBetween(facing[k], truth[i].orientation));
            positions.push_back(double(glm::length(track[k] - truth[i].position)));
        }
        std::sort(rotations.begin(), rotations.end());
        std::sort(positions.begin(), positions.end());
        std::printf("  promasaj naspram istine: rotacija %.3f st, polozaj %.3f m (medijan)\n",
                    rotations[rotations.size() / 2], positions[positions.size() / 2]);
    }

    float stride = 0.0f;
    for(size_t i = 1; i < track.size(); ++i) stride += glm::length(track[i] - track[i - 1]);
    stride = track.size() > 1 ? stride / float(track.size() - 1) : 0.2f;
    const float markerSize = std::max(0.055f * reach, 0.55f * stride);
    const float pointSize = 0.008f * reach;

    // -------------------------------------------------------------------------------
    // Snimka
    // -------------------------------------------------------------------------------

    const uint32_t orbitFrames = 150;      //1. cin: obilazak dok putanja nastaje
    const uint32_t topFrames = 120;        //2. cin: pogled se dize na tlocrt
    const uint32_t rideFrames = 180;       //3. cin: voznja kroz rijesene poze
    const uint32_t total = orbitFrames + topFrames + rideFrames;
    const uint32_t already = sequence.frameCount();

    for(uint32_t frame = 0; render && frame < total && scene.isRunning(); ++frame){
        scene.setFrame(already + frame, 30.0f);

        size_t revealed = track.size();
        if(frame < orbitFrames){
            const float grow = smoothStep(float(frame) / float(orbitFrames) * 1.6f);
            revealed = size_t(1.0f + grow * float(track.size() - 1) + 0.5f);
        }

        if(frame < orbitFrames + topFrames){
            const float t = float(frame) / float(orbitFrames + topFrames);
            const float angle = 0.4f + 3.4f * t;
            const float rise = smoothStep((t - 0.55f) / 0.45f);
            //Udaljenost je u jedinicama dosega, a ne broj napamet: pri 3.4x doseg stane u kadar
            //s rezervom, a drugi cin se spusti na tlocrt dizuci kameru iznad same putanje
            const float distance = 3.4f * reach - 2.6f * reach * rise;
            scene.camera().setPosition({distance * std::sin(angle), 0.95f * reach + 2.2f * reach * rise,
                                        distance * std::cos(angle)});
            //Gleda se u sredinu onoga sto se crta, a to je izmedju prizora i putanje iznad njega
            scene.camera().lookAt({0.0f, 0.30f * reach, 0.0f});
        }else{
            const float t = float(frame - orbitFrames - topFrames) / float(rideFrames - 1);
            const float along = t * float(track.size() - 1);
            const size_t first = std::min(size_t(along), track.size() - 2);
            const float blend = along - float(first);
            scene.camera().setPosition(glm::mix(track[first], track[first + 1], blend));
            scene.camera().setOrientation(glm::normalize(glm::slerp(facing[first], facing[first + 1], blend)));
        }

        //Prizor je u ova tri cina samo podloga za tocke. Puno sunce na kosoj ravnini je
        //rasvijetli toliko da plave tocke na njoj nestanu - pa se prigusi
        scene.sun().setIntensity(0.25f);
        scene.environment().setAmbient({0.18f, 0.19f, 0.23f});


        scene.startRendering();

            //Prava scena, tamna i tiha: samo u virtualnom nacinu, jer se samo tamo zna. Tocke
            //koje na nju sjednu su dokaz da je rekonstrukcija pogodila plohu, a ne samo piksele
            if(virtualMode) drawWorld(ghostColour);

            for(const glm::vec3& point : shown){
                scene.drawCube(pointColour, Loom::Transform().at(point).scaled(pointSize));
            }

            for(size_t i = 0; i < revealed; ++i){
                const bool last = (i + 1 == revealed) && frame < orbitFrames;
                //Prava kamera se PODIZE. Poklapa se s rijesenom na milimetre, pa bi jedna
                //progutala drugu i slika bi izgledala tocno i kad nije - isto kao u SolveVieweru.
                //Razmak je prikaz, ne promasaj; promasaj je ispisan u brojkama gore
                if(virtualMode){
                    const Engine::Pose& real = truth[cameras[i]];
                    scene.drawPyramid(truthColour,
                        glm::translate(glm::mat4(1.0f), real.position + glm::vec3(0.0f, 0.09f * reach, 0.0f)) *
                        glm::mat4_cast(real.orientation) * glm::scale(glm::mat4(1.0f), glm::vec3(markerSize)));
                }
                scene.drawPyramid(last ? nowColour : solvedColour,
                    glm::translate(glm::mat4(1.0f), track[i]) * glm::mat4_cast(facing[i]) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(last ? markerSize * 1.7f : markerSize)));
            }

        scene.endRendering();
        sequence.write(scene);
    }

    if(render) std::printf("Zapisano %u kadrova u %s\n", sequence.frameCount(), outputDirectory.c_str());
    return 0;
}
