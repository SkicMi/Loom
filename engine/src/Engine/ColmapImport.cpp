#include "Engine/ColmapImport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace Engine{
namespace{

//COLMAP-ove datoteke imaju komentare koji pocinju s #, i prazne retke izmedju
bool nextLine(std::ifstream& file, std::string& line){
    while(std::getline(file, line)){
        const size_t first = line.find_first_not_of(" \t\r\n");
        if(first == std::string::npos) continue;
        if(line[first] == '#') continue;
        return true;
    }
    return false;
}

}

bool readColmapText(const std::string& directory, ColmapModel& model){
    model = ColmapModel{};

    // ---------------------------------------------------------------------------------
    // cameras.txt: ID MODEL SIRINA VISINA PARAMETRI...
    // ---------------------------------------------------------------------------------

    {
        std::ifstream file(directory + "/cameras.txt");
        if(!file) return false;

        std::string line;
        if(!nextLine(file, line)) return false;   //jedna kamera je dovoljna; COLMAP ih moze imati vise

        std::istringstream in(line);
        uint32_t id = 0, width = 0, height = 0;
        std::string name;
        in >> id >> name >> width >> height;
        if(!in) return false;

        std::vector<double> params;
        double value = 0.0;
        while(in >> value) params.push_back(value);

        model.cameraModel = name;

        //Svaki model slaze parametre drukcije. Ono sto nam treba je uvijek prvo: zarista pa
        //glavna tocka; distorzija dolazi iza i cita se kao izvjestaj
        if(name == "SIMPLE_PINHOLE" && params.size() >= 3){
            model.intrinsics.fx = float(params[0]);
            model.intrinsics.fy = float(params[0]);
            model.intrinsics.cx = float(params[1]);
            model.intrinsics.cy = float(params[2]);
        }else if(name == "PINHOLE" && params.size() >= 4){
            model.intrinsics.fx = float(params[0]);
            model.intrinsics.fy = float(params[1]);
            model.intrinsics.cx = float(params[2]);
            model.intrinsics.cy = float(params[3]);
        }else if(name == "SIMPLE_RADIAL" && params.size() >= 4){
            model.intrinsics.fx = float(params[0]);
            model.intrinsics.fy = float(params[0]);
            model.intrinsics.cx = float(params[1]);
            model.intrinsics.cy = float(params[2]);
            model.radialK1 = params[3];
            model.intrinsics.k1 = float(params[3]);
        }else if(name == "RADIAL" && params.size() >= 5){
            model.intrinsics.fx = float(params[0]);
            model.intrinsics.fy = float(params[0]);
            model.intrinsics.cx = float(params[1]);
            model.intrinsics.cy = float(params[2]);
            model.radialK1 = params[3];
            model.radialK2 = params[4];
            model.intrinsics.k1 = float(params[3]);
            model.intrinsics.k2 = float(params[4]);
        }else{
            return false;   //nepoznat model je bolje odbiti nego procitati nasumicne brojeve
        }

        //fy SE NE OKRECE. Loomov renderer vodi fy negativan jer kamera gleda niz -Z, i to sam
        //prvo prenio ovamo - ali Engineova Intrinsics je pozitivna u oba zarista, a ColmapExport
        //ju zapisuje takvu kakva jest. Krug je to odmah pokazao: fy je izasao -600 umjesto 600,
        //a reprojekcija procitanog modela 133 px umjesto nule
    }

    // ---------------------------------------------------------------------------------
    // images.txt: dva retka po slici - poza, pa opazanja
    // ---------------------------------------------------------------------------------

    struct Entry{
        std::string name;
        Pose pose;
        std::vector<std::pair<int64_t, glm::vec2>> seen;   //id tocke (-1 ako nije triangulirana)
    };
    std::vector<Entry> entries;

    {
        std::ifstream file(directory + "/images.txt");
        if(!file) return false;

        std::string line;
        while(nextLine(file, line)){
            std::istringstream in(line);
            uint32_t id = 0, cameraId = 0;
            double qw = 0, qx = 0, qy = 0, qz = 0, tx = 0, ty = 0, tz = 0;
            std::string name;
            in >> id >> qw >> qx >> qy >> qz >> tx >> ty >> tz >> cameraId >> name;
            if(!in) return false;

            //Inverz izvoza: R' = M R_colmap, pa je nasa R transponat toga; c = -R_colmap' t
            const glm::quat colmapRotation = glm::normalize(glm::quat(float(qw), float(qx), float(qy), float(qz)));
            const glm::mat3 colmap = glm::mat3_cast(colmapRotation);

            glm::mat3 ours(1.0f);
            const float mirror[3] = {1.0f, -1.0f, -1.0f};
            for(int row = 0; row < 3; ++row){
                for(int column = 0; column < 3; ++column){
                    //(M R_colmap) transponirano: element (row, column) uzima mirror[column]
                    ours[column][row] = mirror[column] * colmap[row][column];
                }
            }

            const glm::vec3 translation{float(tx), float(ty), float(tz)};
            Entry entry;
            entry.name = name;
            entry.pose.orientation = glm::normalize(glm::quat_cast(ours));
            entry.pose.position = -(glm::transpose(colmap) * translation);

            if(!nextLine(file, line)) return false;
            std::istringstream pixels(line);
            double x = 0, y = 0;
            int64_t pointId = 0;
            while(pixels >> x >> y >> pointId){
                entry.seen.push_back({pointId, glm::vec2(float(x), float(y))});
            }
            entries.push_back(std::move(entry));
        }
    }
    if(entries.empty()) return false;

    //COLMAP slike vodi po imenu i ne jamci redoslijed. Sortira se po imenu, jer su imena kadrova
    //nastala iz rednog broja - inace bi kamera 0 bila ona koju je COLMAP slucajno prvu zapisao
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b){return a.name < b.name;});

    // ---------------------------------------------------------------------------------
    // points3D.txt
    // ---------------------------------------------------------------------------------

    std::map<int64_t, uint32_t> pointIndex;
    std::vector<glm::vec3> positions;

    {
        std::ifstream file(directory + "/points3D.txt");
        if(!file) return false;

        std::string line;
        while(nextLine(file, line)){
            std::istringstream in(line);
            int64_t id = 0;
            double x = 0, y = 0, z = 0;
            in >> id >> x >> y >> z;
            if(!in) return false;

            pointIndex[id] = uint32_t(positions.size());
            positions.push_back(glm::vec3(float(x), float(y), float(z)));
        }
    }

    // ---------------------------------------------------------------------------------
    // Slaganje u nas tip
    // ---------------------------------------------------------------------------------

    Reconstruction& out = model.reconstruction;
    out.poses.resize(entries.size());
    out.posed.assign(entries.size(), 1);
    out.points = positions;
    out.solved.assign(positions.size(), 1);
    out.posedCameras = uint32_t(entries.size());
    out.solvedPoints = uint32_t(positions.size());
    out.ok = !entries.empty() && !positions.empty();

    model.imageNames.reserve(entries.size());
    for(size_t camera = 0; camera < entries.size(); ++camera){
        out.poses[camera] = entries[camera].pose;
        model.imageNames.push_back(entries[camera].name);

        for(const auto& one : entries[camera].seen){
            if(one.first < 0) continue;                       //piksel bez triangulirane tocke
            const auto found = pointIndex.find(one.first);
            if(found == pointIndex.end()) continue;
            model.observations.push_back(Observation{uint32_t(camera), found->second, one.second});
        }
    }

    //Reprojekcija se MJERI, ne prepisuje iz tudjeg izvjestaja - inace bi usporedba s nasim
    //solverom usporedjivala dvije razlicite definicije
    std::vector<double> misses;
    misses.reserve(model.observations.size());
    for(const Observation& one : model.observations){
        glm::vec2 pixel;
        if(!project(out.poses[one.camera], model.intrinsics, out.points[one.point], pixel)) continue;
        misses.push_back(double(glm::length(pixel - one.pixel)));
    }
    if(!misses.empty()){
        std::nth_element(misses.begin(), misses.begin() + long(misses.size() / 2), misses.end());
        out.medianReprojection = misses[misses.size() / 2];
    }

    return out.ok;
}

}
