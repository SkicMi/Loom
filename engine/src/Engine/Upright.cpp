#include "Engine/Upright.h"
#include "Engine/Dense.h"

#include <algorithm>
#include <cmath>

namespace Engine{

namespace{

//Rotacija koja vodi jedinicni vektor from u jedinicni vektor to, najkracim putem
glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to){
    const float cosine = glm::dot(from, to);
    if(cosine > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if(cosine < -0.999999f){
        //Suprotni smjerovi: bilo koja os okomita na from
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
        if(glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), from);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
    return glm::angleAxis(angle, glm::normalize(axis));
}

//DOMINANTNA RAVNINA, RANSAC-om. Odredjeni generator (isto sjeme svaki put), jer ista scena mora dati
//isti sustav - inace bi dva izvoza iste snimke bila okrenuta razlicito. Najvise 20 000 tocaka,
//uzorkovanih korakom: oblik ravnine se ne mijenja, a posao pada sto puta
float medianOf(std::vector<float>& values);

struct Plane{ glm::vec3 normal{0.0f}; float share = 0.0f; };

//TOCKA U KOJU KAMERE ZAJEDNO GLEDAJU: najbliza svim optickim osima, najmanjim kvadratima. Kod
//snimanja predmeta to je predmet; kod zida, zid. Kad su sve osi usporedne (dolly), sustav je
//singularan i vraca se false
bool lookAtPoint(const std::vector<Pose>& poses, const std::vector<uint8_t>& posed, glm::vec3& out){
    double a[3][3] = {{0.0}}, b[3] = {0.0, 0.0, 0.0};
    for(size_t camera = 0; camera < poses.size(); ++camera){
        if(camera < posed.size() && !posed[camera]) continue;
        const glm::dvec3 d = glm::normalize(glm::dvec3(poses[camera].orientation * glm::vec3(0.0f, 0.0f, -1.0f)));
        const glm::dvec3 c(poses[camera].position);
        for(int i = 0; i < 3; ++i){
            for(int j = 0; j < 3; ++j){
                const double m = (i == j ? 1.0 : 0.0) - d[i] * d[j];
                a[i][j] += m;
                b[i] += m * c[j];
            }
        }
    }
    double inverse[3][3];
    if(!invert3(a, inverse)) return false;
    //Blizu singularnog - osi gotovo usporedne - rjesenje odleti u beskonacnost
    double values[3], vectors[3][3];
    symmetricEigen3(a, values, vectors);
    if(values[2] < 1e-3 * values[0]) return false;
    glm::dvec3 x(0.0);
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) x[i] += inverse[i][j] * b[j];
    out = glm::vec3(x);
    return true;
}

Plane horizontalPlane(const std::vector<glm::vec3>& points, const std::vector<uint8_t>& solved,
                      const std::vector<Pose>& poses, const std::vector<uint8_t>& posed,
                      const glm::vec3& meanUp, const glm::vec3& cameraCentre, float minimumAlignment){
    Plane best;
    std::vector<glm::vec3> all;
    for(size_t i = 0; i < points.size(); ++i) if(i >= solved.size() || solved[i]) all.push_back(points[i]);
    if(all.size() < 30) return best;

    //SREDISTE MEDIJANOM, ne prosjekom. Prosjekom je na snimci vrata ispalo 143 jedinice od kamera,
    //jer rekonstrukcija ima pokoju tocku daleko u dubini - a tolerancija ravnine izvedena iz toga
    //bila je tolika da je svaka ravnina kroz vrata "sadrzala" 92 posto tocaka
    std::vector<float> xs, ys, zs;
    for(const glm::vec3& point : all){ xs.push_back(point.x); ys.push_back(point.y); zs.push_back(point.z); }
    glm::vec3 centre(medianOf(xs), medianOf(ys), medianOf(zs));

    //JEZGRA SCENE: oko tocke u koju kamere zajedno gledaju. Daleko smece, lose triangulirano iz
    //gotovo usporednih zraka, cesto je ravnije od stvarne scene i odnese pobjedu
    glm::vec3 focus;
    if(lookAtPoint(poses, posed, focus)) centre = focus;
    std::vector<float> distances;
    for(const glm::vec3& point : all) distances.push_back(glm::length(point - centre));
    std::vector<float> sorted = distances;
    const size_t coreIndex = sorted.size() * 6 / 10;
    std::nth_element(sorted.begin(), sorted.begin() + long(coreIndex), sorted.end());
    const float coreRadius = sorted[coreIndex];
    std::vector<glm::vec3> core;
    for(size_t i = 0; i < all.size(); ++i) if(distances[i] <= coreRadius) core.push_back(all[i]);

    //Najvise 20 000 tocaka, uzorkovanih korakom: oblik ravnine se ne mijenja
    std::vector<glm::vec3> sample;
    const size_t stride = std::max<size_t>(1, core.size() / 20000);
    for(size_t i = 0; i < core.size(); i += stride) sample.push_back(core[i]);
    if(sample.size() < 30) return best;

    const float tolerance = 0.02f * coreRadius;

    //PCA tocaka koje leze uz kandidata: vraca normalu kroz koju one STVARNO prolaze
    auto fitted = [&](const glm::vec3& point, const glm::vec3& normal, size_t& count){
        glm::dvec3 mean(0.0);
        count = 0;
        for(const glm::vec3& p : sample){
            if(std::fabs(glm::dot(p - point, normal)) < tolerance){ mean += glm::dvec3(p); ++count; }
        }
        if(count < 3) return normal;
        mean /= double(count);
        double covariance[3][3] = {{0.0}};
        for(const glm::vec3& p : sample){
            if(std::fabs(glm::dot(p - point, normal)) >= tolerance) continue;
            const glm::dvec3 d = glm::dvec3(p) - mean;
            for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) covariance[i][j] += d[i] * d[j];
        }
        double values[3], vectors[3][3];
        symmetricEigen3(covariance, values, vectors);
        glm::vec3 refined = glm::normalize(glm::vec3{float(vectors[0][2]), float(vectors[1][2]), float(vectors[2][2])});
        return glm::dot(refined, normal) < 0.0f ? -refined : refined;
    };

    struct Candidate{ size_t count; glm::vec3 point, normal; };
    std::vector<Candidate> candidates;
    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&](){ state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for(int iteration = 0; iteration < 800; ++iteration){
        const glm::vec3 a = sample[next() % sample.size()];
        const glm::vec3 b = sample[next() % sample.size()];
        const glm::vec3 c = sample[next() % sample.size()];
        glm::vec3 normal = glm::cross(b - a, c - a);
        const float length = glm::length(normal);
        if(length < 1e-12f) continue;
        normal /= length;

        //NORMALA PREMA KAMERAMA, pa PREDZNAK. Pod ili stol: kamere su iznad, a gornja os kamere
        //gleda od ravnine - prosjek_gore . n = cos(pogled dolje) > 0. Zid u koji se gleda odozgo:
        //gornja os se naginje PREMA zidu - prosjek_gore . n < 0
        if(glm::dot(cameraCentre - a, normal) < 0.0f) normal = -normal;
        if(glm::dot(normal, meanUp) < minimumAlignment) continue;

        size_t count = 0;
        for(const glm::vec3& point : sample) if(std::fabs(glm::dot(point - a, normal)) < tolerance) ++count;
        candidates.push_back({count, a, normal});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y){ return x.count > y.count; });

    //PRAVA RAVNINA ILI TRAKA KOJA SIJECE DRUGU? Tocke prave ravnine leze U njoj, pa je njihova PCA
    //normala kandidatova normala. Kandidat blizu okomitog na gustom zidu uhvati traku zida - i
    //njezina PCA normala je normala ZIDA. Na snimci vrata je bas to polozilo scenu na bok: prvo
    //sam kod neslaganja zadrzavao kandidata, a treba ga odbaciti i pogledati sljedeceg
    const float agree = std::cos(10.0f / 57.2957795f);
    for(size_t i = 0; i < candidates.size() && i < 40; ++i){
        size_t count = 0;
        const glm::vec3 refined = fitted(candidates[i].point, candidates[i].normal, count);
        if(glm::dot(refined, candidates[i].normal) < agree) continue;
        if(glm::dot(refined, meanUp) < minimumAlignment) continue;
        best.normal = refined;
        best.share = float(candidates[i].count) / float(sample.size());
        return best;
    }
    return best;
}

float medianOf(std::vector<float>& values){
    if(values.empty()) return 0.0f;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + long(middle), values.end());
    return values[middle];
}

}

//"GORE" IZ DESNIH OSI zadanog podskupa kamera: smjer okomit na sve desne osi. Vraca false kad se
//kamere nisu zakretale pa desne osi ne odredjuju nista
bool upFromRights(const std::vector<glm::vec3>& rights, const std::vector<size_t>& subset,
                  const glm::vec3& meanUp, double minimumYawSpread, glm::vec3& up){
    double m[3][3] = {{0.0}};
    for(size_t index : subset){
        const glm::vec3& r = rights[index];
        for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] += double(r[i]) * double(r[j]);
    }
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] /= double(subset.size());
    double values[3], vectors[3][3];
    symmetricEigen3(m, values, vectors);                     //silazno, vektori u stupcima
    if(values[1] <= minimumYawSpread) return false;
    up = glm::normalize(glm::vec3{float(vectors[0][2]), float(vectors[1][2]), float(vectors[2][2])});
    if(glm::dot(up, meanUp) < 0.0f) up = -up;                //svojstveni vektor nema predznak
    return true;
}

UprightFrame uprightFrame(const std::vector<Pose>& poses, const std::vector<uint8_t>& posed,
                          const std::vector<glm::vec3>& points, const std::vector<uint8_t>& solved,
                          const UprightConfig& config){
    UprightFrame frame;

    std::vector<glm::vec3> rights;
    glm::vec3 upSum(0.0f), cameraCentre(0.0f);
    int firstPosed = -1;
    for(size_t camera = 0; camera < poses.size(); ++camera){
        if(camera < posed.size() && !posed[camera]) continue;
        rights.push_back(poses[camera].orientation * glm::vec3(1.0f, 0.0f, 0.0f));
        upSum += poses[camera].orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        cameraCentre += poses[camera].position;
        if(firstPosed < 0) firstPosed = int(camera);
    }
    const size_t count = rights.size();
    if(count == 0) return frame;
    const glm::vec3 meanUp = upSum / float(count);
    cameraCentre /= float(count);
    frame.coherence = glm::length(meanUp);
    if(frame.coherence < 1e-6f) return frame;
    const glm::vec3 meanDirection = meanUp / frame.coherence;

    //=========================================================================================
    // 1. KAMERE, KAD SU SIGURNE.
    //
    // Snimatelj drzi horizont ravno, pa su desne osi vodoravne i gore je okomito na njih. Prosjek
    // gornjih osi to NIJE: nagnut je prema predmetu kad kamera gleda dolje (izmjereno 26.9 st
    // promasaja uz pogled 30 st dolje).
    //
    // Sigurnost se NE cita iz preostalog nagiba horizonta - on laze: uz horizont kos +-35 st
    // prilagodba osi upije pola, ostatak izgleda malen, a procjena promasi 12 st. Cita se iz
    // STABILNOSTI: gore iz jedne polovice kamera mora se slagati s gore iz druge. Kad su horizonti
    // ravni, polovice se slazu na desetinku stupnja; kad nisu, razilaze se
    //=========================================================================================
    std::vector<size_t> everyone(count);
    for(size_t i = 0; i < count; ++i) everyone[i] = i;
    glm::vec3 up;
    bool fromCameras = upFromRights(rights, everyone, meanDirection, config.minimumYawSpread, up);
    frame.fromRightAxes = fromCameras;
    if(!fromCameras){
        //Kamera se nije zakretala: desne osi ne odredjuju gore, uzima se prosjek gornjih ociscen
        //od jedine desne osi koja postoji
        glm::vec3 dominant(0.0f);
        for(const glm::vec3& r : rights) dominant += glm::dot(r, rights[0]) >= 0.0f ? r : -r;
        dominant = glm::normalize(dominant);
        up = meanUp - glm::dot(meanUp, dominant) * dominant;
        if(glm::dot(up, up) > 1e-8f){ up = glm::normalize(up); fromCameras = true; }
    }

    if(fromCameras){
        double squared = 0.0;
        for(const glm::vec3& r : rights){ const double along = double(glm::dot(r, up)); squared += along * along; }
        frame.rollDegrees = float(std::asin(std::min(1.0, std::sqrt(squared / double(count)))) * 57.29577951);
        frame.disagreementDegrees = std::acos(std::clamp(glm::dot(up, meanDirection), -1.0f, 1.0f)) * 57.2957795f;

        //Polovice: parne i neparne, prva i druga, i dva preplitanja - odredjeno, ne nasumicno
        if(frame.fromRightAxes && count >= 8){
            std::vector<std::vector<size_t>> halves(6);
            for(size_t i = 0; i < count; ++i){
                halves[i % 2].push_back(i);
                halves[2 + (i < count / 2 ? 0 : 1)].push_back(i);
                halves[4 + ((i / 3) % 2)].push_back(i);
            }
            for(const std::vector<size_t>& half : halves){
                glm::vec3 partial;
                if(!upFromRights(rights, half, meanDirection, config.minimumYawSpread, partial)){ frame.instabilityDegrees = 90.0f; break; }
                frame.instabilityDegrees = std::max(frame.instabilityDegrees,
                    std::acos(std::clamp(glm::dot(partial, up), -1.0f, 1.0f)) * 57.2957795f);
            }
        }

        const bool sure = frame.coherence >= config.minimumCoherence &&
                          frame.rollDegrees <= config.maximumRollDegrees &&
                          frame.disagreementDegrees <= config.maximumDisagreementDegrees &&
                          frame.instabilityDegrees <= config.maximumInstabilityDegrees;
        if(sure) frame.source = UprightSource::Cameras;
    }

    //=========================================================================================
    // 2. POD ILI STOL, KAD KAMERE NISU SIGURNE.
    //
    // Joystick: kamera gotovo ravno dolje u stol nema horizont pa je ruka vrti - horizont 17.9 st,
    // procjena iz kamera promasi stol za 51 st. Tocke na stolu ne ovise o tome kako je ruka drzala
    // kameru. Ali ravnina nema prednost: na snimci vrata su kamere savrsene (horizont 0.8 st), a
    // ravnina iz osrednje rekonstrukcije ispala je nagnuta 71 st i polozila bi scenu na bok
    //=========================================================================================
    if(frame.source == UprightSource::None){
        const Plane plane = horizontalPlane(points, solved, poses, posed, meanUp, cameraCentre, config.minimumUpAlignment);
        frame.planeShare = plane.share;
        if(plane.share < config.minimumPlaneShare) return frame;
        up = glm::dot(plane.normal, meanDirection) >= 0.0f ? plane.normal : -plane.normal;
        frame.planeDegrees = std::acos(std::clamp(glm::dot(up, meanDirection), -1.0f, 1.0f)) * 57.2957795f;
        frame.source = UprightSource::Plane;
    }

    frame.tiltDegrees = std::acos(std::clamp(up.y, -1.0f, 1.0f)) * 57.2957795f;
    const glm::quat level = rotationBetween(up, glm::vec3(0.0f, 1.0f, 0.0f));

    //NAPRIJED: pogled prve kamere spusten u vodoravnu ravninu. Kad prva kamera gleda gotovo ravno
    //gore ili dolje, vodoravnog dijela nema i zakret oko okomice se ne dira
    glm::quat yaw(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 forward = level * (poses[size_t(firstPosed)].orientation * glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 flat(forward.x, 0.0f, forward.z);
    if(glm::dot(flat, flat) > 1e-4f){
        yaw = rotationBetween(glm::normalize(flat), glm::vec3(0.0f, 0.0f, -1.0f));
    }
    frame.rotation = glm::normalize(yaw * level);

    //ISHODISTE: medijan po osima. Srednja vrijednost ne valja jer rekonstrukcija redovito ima
    //pokoju tocku trianguliranu iz gotovo paralelnih zraka, daleko izvan scene
    std::vector<float> xs, ys, zs;
    xs.reserve(points.size()); ys.reserve(points.size()); zs.reserve(points.size());
    for(size_t point = 0; point < points.size(); ++point){
        if(point < solved.size() && !solved[point]) continue;
        xs.push_back(points[point].x); ys.push_back(points[point].y); zs.push_back(points[point].z);
    }
    if(!xs.empty()) frame.origin = glm::vec3(medianOf(xs), medianOf(ys), medianOf(zs));

    frame.applied = true;
    return frame;
}

void applyUpright(const UprightFrame& frame, std::vector<Pose>& poses, std::vector<glm::vec3>& points){
    if(!frame.applied) return;
    for(Pose& pose : poses){
        pose.position = frame.rotation * (pose.position - frame.origin);
        pose.orientation = glm::normalize(frame.rotation * pose.orientation);
    }
    for(glm::vec3& point : points) point = frame.rotation * (point - frame.origin);
}

void applyUpright(const UprightFrame& frame, Reconstruction& reconstruction){
    applyUpright(frame, reconstruction.poses, reconstruction.points);
}

}
