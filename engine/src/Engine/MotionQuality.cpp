#include "Engine/MotionQuality.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Engine::MotionQuality{

namespace{

bool contains(const std::string& text, const char* part){ return text.find(part) != std::string::npos; }

//Prsti, oci, celjust i zavrsni zglobovi nose vlastiti sitni pokret (ili nikakav) i u trzaju bi
//samo razvodnili tijelo - klip koji trza laktom ne smije izgledati bolje jer ima trideset prstiju
bool bodyJoint(const std::string& name){
    static const char* skip[] = {"Thumb", "Index", "Middle", "Ring", "Pinky", "Eye", "Jaw", "End"};
    if(name == "Root") return false;    //projekcija na pod, ne dio tijela
    for(const char* part : skip) if(contains(name, part)) return false;
    return true;
}

float percentile(std::vector<float> values, float share){
    if(values.empty()) return 0.0f;
    const size_t at = std::min(values.size() - 1, size_t(share * float(values.size() - 1) + 0.5f));
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(at), values.end());
    return values[at];
}

//Zglob s kanalima polozaja (u Kimodovom BVH-u Root i Hips) nosi APSOLUTNI polozaj u roditelju;
//OFFSET je tada samo T-poza. Kukovi su u Kimodu na ~94 cm s OFFSETOM 100 cm, pa bi zbroj digao
//cijeli lik za metar i "pod" bi ispao na +102 cm - izmjereno na prvom pokusaju ove mjere.
//Uvoz u scenu zbraja oboje, ali lik zatim spusta na pod pa mu to ne smeta; ovdje bi smetalo
std::vector<uint8_t> positionedJoints(const WeaverMotion::Clip& clip){
    std::vector<uint8_t> moves(clip.joints.size(), 0);
    for(const WeaverMotion::Pose& pose : clip.frames)
        for(size_t j = 0; j < pose.translations.size() && j < moves.size(); ++j)
            if(glm::length(pose.translations[j]) > 0.0f) moves[j] = 1;
    return moves;
}

std::vector<glm::vec3> worldPositionsWith(const WeaverMotion::Clip& clip, size_t frame,
                                          const std::vector<uint8_t>& positioned){
    std::vector<glm::mat4> world(clip.joints.size(), glm::mat4(1.0f));
    std::vector<glm::vec3> out(clip.joints.size(), glm::vec3(0.0f));
    if(frame >= clip.frames.size()) return out;
    const WeaverMotion::Pose& pose = clip.frames[frame];
    for(size_t j = 0; j < clip.joints.size(); ++j){
        const WeaverMotion::Joint& joint = clip.joints[j];
        const glm::vec3 moved = j < pose.translations.size() ? pose.translations[j] : glm::vec3(0.0f);
        const glm::vec3 translation = j < positioned.size() && positioned[j] ? moved : joint.offset;
        const glm::quat rotation = j < pose.rotations.size() ? pose.rotations[j] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
        world[j] = joint.parent >= 0 && size_t(joint.parent) < j ? world[size_t(joint.parent)] * local : local;
        out[j] = glm::vec3(world[j][3]);
    }
    return out;
}

}

std::vector<glm::vec3> worldPositions(const WeaverMotion::Clip& clip, size_t frame){
    return worldPositionsWith(clip, frame, positionedJoints(clip));
}

Report evaluate(const WeaverMotion::Clip& clip, const Options& options){
    Report report;
    if(clip.empty() || clip.frames.size() < 4){
        report.problem = "clip needs at least 4 frames";
        return report;
    }
    const float fps = float(clip.framesPerSecond > 0.0 ? clip.framesPerSecond : 30.0);
    report.frames = clip.frames.size();
    report.seconds = double(report.frames) / double(fps);

    std::vector<size_t> toes, body;
    size_t hips = 0;
    for(size_t j = 0; j < clip.joints.size(); ++j){
        const std::string& name = clip.joints[j].name;
        if(contains(name, "ToeBase")) toes.push_back(j);
        if(name == "Hips") hips = j;
        if(bodyJoint(name)) body.push_back(j);
    }
    if(toes.empty()){
        for(size_t j = 0; j < clip.joints.size(); ++j)
            if(contains(clip.joints[j].name, "Toe") || contains(clip.joints[j].name, "Foot")) toes.push_back(j);
    }
    if(toes.empty()){
        report.problem = "no foot or toe joints in the skeleton";
        return report;
    }

    std::vector<std::vector<glm::vec3>> positions(clip.frames.size());
    const std::vector<uint8_t> positioned = positionedJoints(clip);
    for(size_t f = 0; f < clip.frames.size(); ++f) positions[f] = worldPositionsWith(clip, f, positioned);

    //-- pod --------------------------------------------------------------------------------------
    //Kimodov Root je projekcija kukova na pod i stoji na y = 0 u svakom kadru - to JE pod. Procjena
    //iz samih prstiju je bila kriva upravo kad je trebala najvise: varijanta koja cijelo vrijeme
    //lebdi 15-40 cm iznad poda dobila je pod na 17 cm, pa je Root ispao "propao 16 cm", a stvarna
    //greska (lebdenje) nije bila nigdje. Procjena ostaje samo za kostur bez Roota na podu
    float floor = 0.0f;
    std::vector<float> lowestToe(clip.frames.size());
    for(size_t f = 0; f < clip.frames.size(); ++f){
        float low = 1e9f;
        for(size_t j : toes) low = std::min(low, positions[f][j].y);
        lowestToe[f] = low;
    }
    bool rootOnFloor = false;
    for(size_t j = 0; j < clip.joints.size(); ++j){
        if(clip.joints[j].name != "Root" || clip.joints[j].parent >= 0) continue;
        rootOnFloor = true;
        for(size_t f = 0; f < clip.frames.size() && rootOnFloor; ++f)
            rootOnFloor = std::fabs(positions[f][j].y) < 1e-3f;
    }
    if(!rootOnFloor){
        floor = percentile(lowestToe, 0.05f);
        report.floorEstimated = true;
    }
    report.floorHeightCm = floor * 100.0f;

    //Zglob prsta ne lezi na podu nego iznad tabana: u Kimodovim klipovima stopalo na podu drzi ga
    //na tocno 2.0 cm (izmjereno na trcanju, plesu, puzanju i hodu - 5. percentil je 2.0 u svima).
    //Dodir se zato mjeri od te visine mirovanja, ne od poda
    std::vector<float> toeHeights;
    for(size_t f = 0; f < clip.frames.size(); ++f)
        for(size_t j : toes) toeHeights.push_back(positions[f][j].y - floor);
    //Visina mirovanja je Kimodova (2 cm) kad klip ikad stane na pod; lebdeci klip ne smije svoju
    //visinu proglasiti mirovanjem, pa je gornja granica 6 cm
    const float toeRest = std::clamp(percentile(toeHeights, 0.05f), 0.0f, 0.06f);

    //-- lebdenje ---------------------------------------------------------------------------------
    //Medijan, ne najmanja: skok smije imati kadrove bez tla, ali lik koji vecinu vremena ne dotice
    //pod nije skocio nego lebdi
    std::vector<float> clearance(clip.frames.size());
    for(size_t f = 0; f < clip.frames.size(); ++f) clearance[f] = lowestToe[f] - floor - toeRest;
    report.floatingCm = std::max(0.0f, percentile(clearance, 0.5f) * 100.0f);

    //-- klizanje stopala -------------------------------------------------------------------------
    const float contactHeight = options.contactHeightCm;
    double skateSum = 0.0;
    size_t contactSamples = 0, contactFrames = 0;
    for(size_t f = 1; f < clip.frames.size(); ++f){
        bool anyContact = false;
        for(size_t j : toes){
            const float heightCm = (std::min(positions[f][j].y, positions[f - 1][j].y) - floor - toeRest) * 100.0f;
            if(heightCm >= contactHeight) continue;
            anyContact = true;
            const glm::vec2 step(positions[f][j].x - positions[f - 1][j].x, positions[f][j].z - positions[f - 1][j].z);
            const float speedCm = glm::length(step) * fps * 100.0f;
            const float weight = std::clamp(2.0f - std::pow(2.0f, std::max(0.0f, heightCm) / contactHeight), 0.0f, 1.0f);
            skateSum += double(speedCm * weight);
            ++contactSamples;
        }
        if(anyContact) ++contactFrames;
    }
    report.footSkateCmPerSecond = contactSamples ? float(skateSum / double(contactSamples)) : 0.0f;
    report.contactShare = float(contactFrames) / float(clip.frames.size() - 1);

    //-- propadanje -------------------------------------------------------------------------------
    //Prst ispod svoje visine mirovanja je taban ispod poda; ostali zglobovi (koljena, ruke pri
    //puzanju) propadaju kad im sredista odu ispod nule
    float sink = 0.0f;
    for(size_t f = 0; f < clip.frames.size(); ++f){
        for(size_t j : body){
            const bool toe = std::find(toes.begin(), toes.end(), j) != toes.end();
            const float below = (toe ? floor + toeRest : floor) - positions[f][j].y;
            sink = std::max(sink, below);
        }
    }
    report.penetrationCm = std::max(0.0f, sink * 100.0f);

    //-- trzaji -----------------------------------------------------------------------------------
    double jerkSum = 0.0;
    size_t jerkSamples = 0;
    const float fps3 = fps * fps * fps;
    for(size_t f = 3; f < clip.frames.size(); ++f){
        for(size_t j : body){
            const glm::vec3 jerk = positions[f][j] - 3.0f * positions[f - 1][j] + 3.0f * positions[f - 2][j] - positions[f - 3][j];
            jerkSum += double(glm::length(jerk) * fps3);
            ++jerkSamples;
        }
    }
    report.jerkMetersPerSecond3 = jerkSamples ? float(jerkSum / double(jerkSamples)) : 0.0f;

    //-- skok korijena ----------------------------------------------------------------------------
    //Apsolutna brzina kukova, ne omjer prema medijanu: omjer je namjeran pad na pod (medijan malen,
    //jedan brz kadar) proglasavao skokom. Sprint je oko 9 m/s kukova; iznad toga je teleport
    float fastest = 0.0f;
    for(size_t f = 1; f < clip.frames.size(); ++f)
        fastest = std::max(fastest, glm::length(positions[f][hips] - positions[f - 1][hips]) * fps);
    report.rootSpeedMetersPerSecond = fastest;

    //-- putanja ----------------------------------------------------------------------------------
    if(!options.route.empty()){
        double sum = 0.0;
        size_t used = 0;
        for(const RoutePoint& point : options.route){
            if(point.frame < 0 || size_t(point.frame) >= clip.frames.size()) continue;
            const glm::vec3& at = positions[size_t(point.frame)][hips];
            sum += double(glm::length(glm::vec2(at.x - point.x, at.z - point.z)) * 100.0f);
            ++used;
        }
        if(used) report.routeErrorCm = float(sum / double(used));
    }

    //-- ocjena -----------------------------------------------------------------------------------
    const Limits& limits = options.limits;
    struct Term{ float value; const char* name; };
    std::vector<Term> terms{
        {report.footSkateCmPerSecond / limits.footSkateCmPerSecond, "foot skating"},
        {report.penetrationCm / limits.penetrationCm, "floor penetration"},
        {report.floatingCm / limits.floatingCm, "floating"},
        {report.jerkMetersPerSecond3 / limits.jerkMetersPerSecond3, "jitter"},
        {std::max(0.0f, report.rootSpeedMetersPerSecond / limits.rootSpeedMetersPerSecond - 1.0f) * 4.0f, "root jump"},
    };
    if(report.routeErrorCm >= 0.0f) terms.push_back({report.routeErrorCm / limits.routeErrorCm, "off the route"});
    const Term* worst = &terms.front();
    for(const Term& term : terms){
        report.score += term.value;
        if(term.value > worst->value) worst = &term;
    }
    report.worst = worst->name;
    report.valid = true;
    return report;
}

//Nije opci JSON citac: Loom zna tocno sto je sam zapisao (LoomMotionPanel.h,
//writeMotionRootConstraints), pa je dovoljno naci dva niza brojeva po imenu
std::vector<RoutePoint> routeFromConstraints(const std::string& json){
    std::vector<RoutePoint> route;
    const size_t block = json.find("\"root2d\"");
    if(block == std::string::npos) return route;
    auto numbersAfter = [&](const char* key, size_t from){
        std::vector<float> values;
        const size_t at = json.find(key, from);
        if(at == std::string::npos) return values;
        size_t i = json.find('[', at);
        if(i == std::string::npos) return values;
        int depth = 0;
        for(; i < json.size(); ++i){
            const char c = json[i];
            if(c == '[') ++depth;
            else if(c == ']'){ if(--depth == 0) break; }
            else if(c == '-' || c == '.' || (c >= '0' && c <= '9')){
                char* end = nullptr;
                values.push_back(std::strtof(json.c_str() + i, &end));
                i = size_t(end - json.c_str()) - 1;
            }
        }
        return values;
    };
    const std::vector<float> frames = numbersAfter("\"frame_indices\"", block);
    const std::vector<float> xz = numbersAfter("\"smooth_root_2d\"", block);
    if(frames.empty() || xz.size() != frames.size() * 2) return route;
    for(size_t i = 0; i < frames.size(); ++i) route.push_back({int(std::lround(frames[i])), xz[2 * i], xz[2 * i + 1]});
    return route;
}

std::vector<RoutePoint> readRouteConstraints(const std::string& path){
    std::ifstream file(path);
    if(!file) return {};
    std::stringstream text;
    text << file.rdbuf();
    return routeFromConstraints(text.str());
}

int bestOf(const std::vector<Report>& reports){
    int best = -1;
    for(size_t i = 0; i < reports.size(); ++i){
        if(!reports[i].valid) continue;
        if(best < 0 || reports[i].score < reports[size_t(best)].score) best = int(i);
    }
    return best;
}

std::string summary(const Report& report){
    if(!report.valid) return report.problem.empty() ? "not measured" : report.problem;
    char text[160];
    std::snprintf(text, sizeof(text), "skate %.1f cm/s  sink %.1f  float %.1f cm  jitter %.0f",
                  double(report.footSkateCmPerSecond), double(report.penetrationCm),
                  double(report.floatingCm), double(report.jerkMetersPerSecond3));
    std::string out = text;
    if(report.routeErrorCm >= 0.0f){
        std::snprintf(text, sizeof(text), "  route %.0f cm", double(report.routeErrorCm));
        out += text;
    }
    return out;
}

}
