#include "GaussianPly.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Spool{
namespace{

[[noreturn]] void fail(const std::string& path, const std::string& why){
    throw std::runtime_error("Spool::loadGaussianPly(\"" + path + "\"): " + why);
}

//Vlastita, jer poruka nosi ime pozivatelja. Ista poruka s krivim imenom salje onoga tko je
//cita citati pogresnu funkciju
[[noreturn]] void failWrite(const std::string& path, const std::string& why){
    throw std::runtime_error("Spool::saveGaussianPly(\"" + path + "\"): " + why);
}

//Sve sto PLY zna, jer se preko nepoznatog svojstva mora moci PRESKOCITI. Bez ove tablice
//citac ne bi znao koliko je bajtova tudje svojstvo dugacko, pa bi jedino sto moze bilo
//odustati - a fileovi s dodatnim svojstvima su pravilo, ne iznimka
size_t sizeOfType(const std::string& type){
    if(type == "char"   || type == "int8"   || type == "uchar" || type == "uint8")  return 1;
    if(type == "short"  || type == "int16"  || type == "ushort"|| type == "uint16") return 2;
    if(type == "int"    || type == "int32"  || type == "uint"  || type == "uint32") return 4;
    if(type == "float"  || type == "float32") return 4;
    if(type == "double" || type == "float64") return 8;
    return 0;
}

bool isFloat32(const std::string& type){
    return type == "float" || type == "float32";
}

struct Property{
    std::string name;
    std::string type;
    size_t offset = 0;
};

//Jedno svojstvo, po imenu. Vraca false kad ga nema
struct Layout{
    std::vector<Property> properties;
    size_t stride = 0;
    std::unordered_map<std::string, const Property*> byName;

    const Property* find(const std::string& name) const{
        const auto it = byName.find(name);
        return it == byName.end() ? nullptr : it->second;
    }
};

float readFloat(const uint8_t* record, const Property& property){
    float value = 0.0f;
    std::memcpy(&value, record + property.offset, sizeof(float));
    return value;
}

//Trazi svojstvo koje MORA postojati i mora biti float32. Vraca pokazivac, ne referencu:
//referenca vracena iz funkcije i vezana za const& izgleda prevodiocu kao da bi mogla nadzivjeti
//ono na sto pokazuje, pa na to upozorava - a upozorenje koje se ignorira je upozorenje koje
//sljedeci put nitko nece procitati
const Property* require(const Layout& layout, const std::string& name, const std::string& path){
    const Property* property = layout.find(name);
    if(!property){
        fail(path, "glava ne nabraja svojstvo '" + name + "'");
    }
    if(!isFloat32(property->type)){
        fail(path, "svojstvo '" + name + "' je '" + property->type + "', a ocekuje se float");
    }
    return property;
}

//Koliko f_rest_N svojstava glava nosi, i koji je to stupanj. Broj nije proizvoljan: stupanj
//d ima (d+1)^2 koeficijenata po kanalu, prvi je DC koji stoji zasebno, pa ostaje
//((d+1)^2 - 1) * 3. Sve osim 0, 9, 24 i 45 je file koji ne opisuje ono sto tvrdi
uint32_t degreeFromRestCount(size_t restCount, const std::string& path){
    switch(restCount){
        case 0:  return 0;
        case 9:  return 1;
        case 24: return 2;
        case 45: return 3;
        default: break;
    }
    fail(path, "glava nosi " + std::to_string(restCount) +
               " f_rest svojstava, a stupanj sfernih harmonika trazi 0, 9, 24 ili 45");
}

}

GaussianCloud loadGaussianPly(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file){
        fail(path, "datoteka se ne da otvoriti");
    }

    std::string line;
    if(!std::getline(file, line)){
        fail(path, "prazna datoteka");
    }
    //Fileovi pisani na Windowsu nose \r koji ovdje nema nikakvo znacenje
    const auto trim = [](std::string& text){
        while(!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    };
    trim(line);
    if(line != "ply"){
        fail(path, "ne pocinje s 'ply'");
    }

    Layout layout;
    size_t vertexCount = 0;
    bool inVertexElement = false;
    bool sawFormat = false;
    bool sawEndHeader = false;

    while(std::getline(file, line)){
        trim(line);
        std::istringstream words(line);
        std::string keyword;
        words >> keyword;

        if(keyword == "comment" || keyword.empty()){
            continue;
        }
        if(keyword == "format"){
            std::string format;
            words >> format;
            if(format != "binary_little_endian"){
                fail(path, "zapis je '" + format + "', a citac zna samo binary_little_endian");
            }
            sawFormat = true;
            continue;
        }
        if(keyword == "element"){
            std::string name;
            size_t countOfElement = 0;
            words >> name >> countOfElement;
            //Elementi iza vertexa postoje (lica, rubovi) i nas se ne ticu, ali njihova
            //svojstva ne smiju upasti u nas raspored
            inVertexElement = (name == "vertex");
            if(inVertexElement){
                vertexCount = countOfElement;
            }
            continue;
        }
        if(keyword == "property"){
            if(!inVertexElement) continue;

            std::string type;
            words >> type;
            if(type == "list"){
                //Lista je promjenjive duljine, pa se preko nje ne da preskociti bez citanja.
                //Na vertexu je nema u nijednom 3DGS fileu, i bolje je stati nego nagadjati
                fail(path, "vertex nosi 'property list', a duljina takvog svojstva se ne da "
                           "izracunati iz glave");
            }

            Property property;
            property.type = type;
            words >> property.name;

            const size_t size = sizeOfType(type);
            if(size == 0){
                fail(path, "nepoznat tip svojstva '" + type + "'");
            }

            property.offset = layout.stride;
            layout.stride += size;
            layout.properties.push_back(property);
            continue;
        }
        if(keyword == "end_header"){
            sawEndHeader = true;
            break;
        }
    }

    if(!sawFormat)    fail(path, "glava ne kaze u kojem je zapisu");
    if(!sawEndHeader) fail(path, "glava nema 'end_header'");
    if(vertexCount == 0) fail(path, "glava ne nabraja nijedan vertex");

    //Pokazivaci se uzimaju tek kad je vektor gotov: rast vektora bi ih inace obesmislio
    for(const Property& property : layout.properties){
        layout.byName[property.name] = &property;
    }

    // -------------------------------------------------------------------------------
    // Sto se iz zapisa cita, i gdje to lezi
    // -------------------------------------------------------------------------------

    const Property* fixed[16] = {
        require(layout, "x", path),        require(layout, "y", path),        require(layout, "z", path),
        layout.find("nx"),                 layout.find("ny"),                 layout.find("nz"),
        require(layout, "f_dc_0", path),   require(layout, "f_dc_1", path),   require(layout, "f_dc_2", path),
        require(layout, "opacity", path),
        require(layout, "scale_0", path),  require(layout, "scale_1", path),  require(layout, "scale_2", path),
        require(layout, "rot_0", path),    require(layout, "rot_1", path),    require(layout, "rot_2", path),
    };
    const Property* rot3 = require(layout, "rot_3", path);

    //Normale su u zapisu prisutne ali nista ne nose, pa fileovi koji ih izostave nisu greska
    for(int i = 3; i < 6; ++i){
        if(fixed[i] && !isFloat32(fixed[i]->type)){
            fail(path, "normala nije float");
        }
    }

    //f_rest_0 pa nadalje, redom. Rupa u nizu znaci da file nabraja nesto sto ne postoji
    std::vector<const Property*> rest;
    for(size_t i = 0; ; ++i){
        const Property* property = layout.find("f_rest_" + std::to_string(i));
        if(!property) break;
        if(!isFloat32(property->type)){
            fail(path, "f_rest_" + std::to_string(i) + " nije float");
        }
        rest.push_back(property);
    }

    GaussianCloud cloud;
    cloud.restStride = uint32_t(rest.size());
    cloud.shDegree = degreeFromRestCount(rest.size(), path);

    // -------------------------------------------------------------------------------
    // I sami zapisi
    // -------------------------------------------------------------------------------

    cloud.gaussians.resize(vertexCount);
    cloud.shRest.resize(vertexCount * cloud.restStride);

    //U KOMADIMA, ne zapis po zapis. Prava scena ima blizu milijun gaussiana i svaki zapis je
    //oko 250 bajtova, pa je citanje po zapisu 741883 poziva. Mjereno na train/7000, 184 MB:
    //
    //   po zapisu          3.50 s
    //   u komadima od 4 MB 2.46 s
    //   cisto citanje bajtova s diska, bez ikakvog parsiranja: 0.08 s
    //
    //Ostatak nije I/O nego raspakiravanje polje po polje, i ono ovisi o optimizaciji: isti
    //ovaj kod je 2.44 s preveden s -O0 i 0.38 s s -O2
    const size_t recordsPerChunk = std::max<size_t>(1, (4u << 20) / layout.stride);
    std::vector<uint8_t> chunk(recordsPerChunk * layout.stride);

    for(size_t first = 0; first < vertexCount; first += recordsPerChunk){
        const size_t howMany = std::min(recordsPerChunk, vertexCount - first);
        const std::streamsize wanted = std::streamsize(howMany * layout.stride);

        file.read(reinterpret_cast<char*>(chunk.data()), wanted);
        if(file.gcount() != wanted){
            fail(path, "glava obecava " + std::to_string(vertexCount) + " gaussiana, a datoteka "
                       "zavrsava na " + std::to_string(first + size_t(file.gcount()) / layout.stride));
        }

        for(size_t offset = 0; offset < howMany; ++offset){
        const size_t i = first + offset;
        const uint8_t* const recordBytes = chunk.data() + offset * layout.stride;

        Gaussian& gaussian = cloud.gaussians[i];
        for(int c = 0; c < 3; ++c){
            gaussian.position[c] = readFloat(recordBytes, *fixed[c]);
            gaussian.normal[c]   = fixed[3 + c] ? readFloat(recordBytes, *fixed[3 + c]) : 0.0f;
            gaussian.dc[c]       = readFloat(recordBytes, *fixed[6 + c]);
            gaussian.scale[c]    = readFloat(recordBytes, *fixed[10 + c]);
        }
        gaussian.opacity = readFloat(recordBytes, *fixed[9]);
        for(int c = 0; c < 3; ++c){
            gaussian.rotation[c] = readFloat(recordBytes, *fixed[13 + c]);
        }
        gaussian.rotation[3] = readFloat(recordBytes, *rot3);

        for(size_t c = 0; c < rest.size(); ++c){
            cloud.shRest[i * cloud.restStride + c] = readFloat(recordBytes, *rest[c]);
        }
        }
    }

    return cloud;
}


void saveGaussianPly(const std::string& path,
                     const GaussianCloud& cloud,
                     const std::vector<uint8_t>& keep){
    if(!keep.empty() && keep.size() != cloud.count()){
        failWrite(path, "keep ima " + std::to_string(keep.size()) + " ulaza, a oblak " +
                   std::to_string(cloud.count()) + " gaussiana");
    }
    if(cloud.shRest.size() != cloud.count() * cloud.restStride){
        failWrite(path, "oblak nosi " + std::to_string(cloud.shRest.size()) + " koeficijenata, a " +
                   std::to_string(cloud.count()) + " gaussiana po " +
                   std::to_string(cloud.restStride) + " trazi " +
                   std::to_string(cloud.count() * cloud.restStride));
    }

    size_t written = 0;
    for(size_t i = 0; i < cloud.count(); ++i){
        if(keep.empty() || keep[i]) ++written;
    }

    std::filesystem::path file(path);
    if(file.has_parent_path()) std::filesystem::create_directories(file.parent_path());

    std::ofstream out(path, std::ios::binary);
    if(!out) failWrite(path, "ne mogu otvoriti za pisanje");

    //REDOSLIJED SVOJSTAVA JE ONAJ KOJIM IH NABRAJA REFERENTNI 3DGS. Citac ih trazi po imenu pa
    //bi mu svaki poredak odgovarao, ali tudji alati nisu svi tako oprezni
    out << "ply\n";
    out << "format binary_little_endian 1.0\n";
    out << "element vertex " << written << "\n";
    out << "property float x\nproperty float y\nproperty float z\n";
    out << "property float nx\nproperty float ny\nproperty float nz\n";
    out << "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n";
    for(uint32_t c = 0; c < cloud.restStride; ++c) out << "property float f_rest_" << c << "\n";
    out << "property float opacity\n";
    out << "property float scale_0\nproperty float scale_1\nproperty float scale_2\n";
    out << "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n";
    out << "end_header\n";

    //Zapis se slaze u komad pa se pise odjednom. Po gaussianu bi to bilo cetiri milijuna
    //poziva na file od gigabajta, sto je razlika u minutama a ne u postotcima
    const size_t floatsPerRecord = 3 + 3 + 3 + cloud.restStride + 1 + 3 + 4;
    std::vector<float> record(floatsPerRecord);

    constexpr size_t recordsPerChunk = 4096;
    std::vector<float> chunk;
    chunk.reserve(floatsPerRecord * recordsPerChunk);

    auto flush = [&]{
        if(chunk.empty()) return;
        out.write(reinterpret_cast<const char*>(chunk.data()),
                  std::streamsize(chunk.size() * sizeof(float)));
        chunk.clear();
    };

    for(size_t i = 0; i < cloud.count(); ++i){
        if(!keep.empty() && !keep[i]) continue;

        const Gaussian& gaussian = cloud.gaussians[i];
        size_t at = 0;
        for(int c = 0; c < 3; ++c) record[at++] = gaussian.position[c];
        for(int c = 0; c < 3; ++c) record[at++] = gaussian.normal[c];
        for(int c = 0; c < 3; ++c) record[at++] = gaussian.dc[c];
        for(uint32_t c = 0; c < cloud.restStride; ++c) record[at++] = cloud.shRest[i * cloud.restStride + c];
        record[at++] = gaussian.opacity;
        for(int c = 0; c < 3; ++c) record[at++] = gaussian.scale[c];
        for(int c = 0; c < 4; ++c) record[at++] = gaussian.rotation[c];

        chunk.insert(chunk.end(), record.begin(), record.end());
        if(chunk.size() >= floatsPerRecord * recordsPerChunk) flush();
    }
    flush();

    out.flush();
    if(!out) failWrite(path, "pisanje nije uspjelo do kraja");
}

}
