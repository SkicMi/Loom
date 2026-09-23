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

float medianOf(std::vector<float>& values){
    if(values.empty()) return 0.0f;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + long(middle), values.end());
    return values[middle];
}

}

UprightFrame uprightFrame(const std::vector<Pose>& poses, const std::vector<uint8_t>& posed,
                          const std::vector<glm::vec3>& points, const std::vector<uint8_t>& solved,
                          const UprightConfig& config){
    UprightFrame frame;

    //GORE, IZ DESNIH OSI, a ne iz gornjih.
    //
    //Prva verzija je uzimala prosjek gornjih osi i test ju je uhvatio: kamere koje s djelomicnog
    //luka gledaju malo prema dolje u subjekt imaju gornje osi nagnute prema subjektu, i prosjek se
    //ne ponisti. Na luku od 66 st uz pogled 4.8 st prema dolje prepoznat je nagib 20.3 umjesto 25
    //stupnjeva. A to je tocno snimka predmeta na stolu - kamera iz ruke gleda dolje u njega.
    //
    //Kad kamera gleda dolje, okrene se oko svoje DESNE osi - pa desna os ostaje vodoravna, bez
    //obzira na nagib pogleda. Nagiba oko osi pogleda (horizont koso) snimatelj se ionako cuva.
    //Zato je "gore" smjer OKOMIT NA SVE DESNE OSI: svojstveni vektor najmanje svojstvene vrijednosti
    //od M = zbroj r r'. Isto radi COLMAP-ov IMAGE-ORIENTATION.
    //
    //Kad se kamera nikad ne zakrene oko okomice, sve desne osi su iste i okomitih smjerova ima
    //cijela ravnina - tada se uzme prosjek gornjih osi, ocisten od te jedne desne osi
    double m[3][3] = {{0.0}};
    glm::vec3 upSum(0.0f);
    int firstPosed = -1;
    uint32_t count = 0;
    std::vector<glm::vec3> rights;
    for(size_t camera = 0; camera < poses.size(); ++camera){
        if(camera < posed.size() && !posed[camera]) continue;
        const glm::vec3 right = poses[camera].orientation * glm::vec3(1.0f, 0.0f, 0.0f);
        upSum += poses[camera].orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] += double(right[i]) * double(right[j]);
        rights.push_back(right);
        if(firstPosed < 0) firstPosed = int(camera);
        ++count;
    }
    if(count == 0) return frame;
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] /= double(count);

    const glm::vec3 meanUp = upSum / float(count);
    frame.coherence = glm::length(meanUp);

    double values[3], vectors[3][3];
    symmetricEigen3(m, values, vectors);                     //silazno, vektori u stupcima

    glm::vec3 up;
    if(values[1] > config.minimumYawSpread){
        up = glm::vec3{float(vectors[0][2]), float(vectors[1][2]), float(vectors[2][2])};
        frame.fromRightAxes = true;
    }else{
        //Kamera se nije zakretala: desne osi ne odredjuju gore, uzima se prosjek gornjih
        const glm::vec3 dominant{float(vectors[0][0]), float(vectors[1][0]), float(vectors[2][0])};
        up = meanUp - glm::dot(meanUp, dominant) * dominant;
        if(glm::dot(up, up) < 1e-8f) return frame;
    }
    up = glm::normalize(up);
    if(glm::dot(up, meanUp) < 0.0f) up = -up;                //svojstveni vektor nema predznak

    //DESNE OSI SAME NISU DOVOLJNE - negativna kontrola ih je srusila. Kad su horizonti nasumicno
    //kosi, smjer okomit na sve desne osi ipak postoji: vodoravni smjer POGLEDA, jer su sve desne osi
    //okomite na njega. Metoda tada uredno vrati krivi "gore", s malim ostatkom.
    //
    //Prosjek gornjih osi to vidi: kad se kamere ne slazu, on je kratak. A kad se slazu, s pravim
    //gore ne moze biti daleko ni kod pogleda ravno dolje u stol. Zato oba uvjeta:
    //
    //   sloznost prosjeka gornjih osi   >= minimumCoherence
    //   kut izmedju ta dva smjera       <= maximumDisagreementDegrees
    if(frame.coherence < config.minimumCoherence) return frame;
    frame.disagreementDegrees = std::acos(std::clamp(glm::dot(up, meanUp / frame.coherence), -1.0f, 1.0f)) * 57.2957795f;
    if(frame.disagreementDegrees > config.maximumDisagreementDegrees) return frame;

    //KOLIKO SU HORIZONTI KOSI: srednji kvadrat r.gore. Vodoravne kamere daju nulu; nasumican nagib
    //oko osi pogleda daje velik broj, i tada "gore" iz kamera ne postoji
    double squared = 0.0;
    for(const glm::vec3& right : rights){
        const double along = double(glm::dot(right, up));
        squared += along * along;
    }
    const double rms = std::sqrt(squared / double(count));
    frame.rollDegrees = float(std::asin(std::min(1.0, rms)) * 57.29577951);
    if(frame.rollDegrees > config.maximumRollDegrees) return frame;

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
