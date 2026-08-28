#include "DepthFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <stb_image.h>

namespace Spool{

namespace{

void finish(DepthImage& depth){
    if(depth.values.empty()) return;
    const auto range = std::minmax_element(depth.values.begin(), depth.values.end());
    depth.minValue = *range.first;
    depth.maxValue = *range.second;
}

//-------------------------------------------------------------------------------------------
// PFM
//-------------------------------------------------------------------------------------------

//Zaglavlje je tekst, podaci su sirovi float-ovi. Predznak razmjera nosi redoslijed bajtova:
//negativan je little endian, sto je ono sto svaka masina na kojoj ovo radi i jest
std::string readToken(std::istream& stream){
    std::string token;
    int c = stream.get();

    while(std::isspace(c)) c = stream.get();

    //Komentar do kraja retka
    while(c == '#'){
        while(c != '\n' && c != EOF) c = stream.get();
        while(std::isspace(c)) c = stream.get();
    }

    while(c != EOF && !std::isspace(c)){
        token.push_back(char(c));
        c = stream.get();
    }
    return token;
}

DepthImage loadPfm(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("Spool::loadDepthImage: cannot open '" + path + "'");

    const std::string magic = readToken(file);
    const bool grayscale = (magic == "Pf");
    if(!grayscale && magic != "PF"){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' is not a PFM file");
    }

    const int width = std::stoi(readToken(file));
    const int height = std::stoi(readToken(file));
    const float scale = std::stof(readToken(file));

    if(width <= 0 || height <= 0){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' has no size");
    }

    //Razmak iza zadnjeg tokena readToken je vec pojeo - dodatni get() ovdje bi progutao
    //prvi bajt piksela, a to se vidi tek kao slika koja je za jedan bajt pomaknuta

    const int channels = grayscale ? 1 : 3;
    std::vector<float> raw(size_t(width) * height * channels);
    file.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size() * sizeof(float)));
    if(!file){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' ended before its pixels did");
    }

    DepthImage depth;
    depth.width = uint32_t(width);
    depth.height = uint32_t(height);
    depth.sourceBits = 32;
    depth.values.resize(size_t(width) * height);

    //PFM broji retke odozdo prema gore. Sve ostalo ovdje ide odozgo, pa se okrece jednom - na
    //ulazu - umjesto da svaki citatelj poslije toga pamti da bas ovaj format ide naopako
    for(int y = 0; y < height; ++y){
        const int sourceRow = height - 1 - y;
        for(int x = 0; x < width; ++x){
            depth.values[size_t(y) * width + x] = raw[(size_t(sourceRow) * width + x) * channels];
        }
    }

    //Negativan razmjer znaci little endian i inace mnozi vrijednosti; pozitivan je big endian,
    //koji ovdje nitko ne pise
    if(scale > 0.0f){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' is big endian PFM, which this reader does not handle");
    }

    finish(depth);
    return depth;
}

//-------------------------------------------------------------------------------------------
// PGM
//-------------------------------------------------------------------------------------------

DepthImage loadPgm(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("Spool::loadDepthImage: cannot open '" + path + "'");

    const std::string magic = readToken(file);
    if(magic != "P5"){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' is not a binary PGM file");
    }

    const int width = std::stoi(readToken(file));
    const int height = std::stoi(readToken(file));
    const int maxValue = std::stoi(readToken(file));

    if(width <= 0 || height <= 0 || maxValue <= 0){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' has no size");
    }

    const bool sixteen = maxValue > 255;

    DepthImage depth;
    depth.width = uint32_t(width);
    depth.height = uint32_t(height);
    depth.sourceBits = sixteen ? 16 : 8;
    depth.values.resize(size_t(width) * height);

    std::vector<uint8_t> raw(depth.values.size() * (sixteen ? 2 : 1));
    file.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()));
    if(!file){
        throw std::runtime_error("Spool::loadDepthImage: '" + path + "' ended before its pixels did");
    }

    for(size_t i = 0; i < depth.values.size(); ++i){
        //PGM je big endian po specifikaciji, bez obzira na masinu
        const uint32_t value = sixteen ? (uint32_t(raw[i*2]) << 8 | raw[i*2 + 1]) : raw[i];
        depth.values[i] = float(value) / float(maxValue);
    }

    finish(depth);
    return depth;
}

//-------------------------------------------------------------------------------------------
// PNG i ostalo sto stb razumije
//-------------------------------------------------------------------------------------------

DepthImage loadThroughStb(const std::string& path){
    int width = 0, height = 0, channels = 0;

    //Prvo se pokusa sa sesnaest bita. stb ih vrati samo ako ih file stvarno nosi, pa je ovo
    //ujedno i nacin da se sazna koliko ih je bilo
    if(stbi_is_16_bit(path.c_str())){
        uint16_t* decoded = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
        if(!decoded){
            throw std::runtime_error("Spool::loadDepthImage: cannot decode '" + path + "' - " + stbi_failure_reason());
        }

        DepthImage depth;
        depth.width = uint32_t(width);
        depth.height = uint32_t(height);
        depth.sourceBits = 16;
        depth.values.resize(size_t(width) * height);
        for(size_t i = 0; i < depth.values.size(); ++i){
            depth.values[i] = float(decoded[i]) / 65535.0f;
        }
        stbi_image_free(decoded);
        finish(depth);
        return depth;
    }

    uint8_t* decoded = stbi_load(path.c_str(), &width, &height, &channels, 1);
    if(!decoded){
        throw std::runtime_error("Spool::loadDepthImage: cannot decode '" + path + "' - " + stbi_failure_reason());
    }

    DepthImage depth;
    depth.width = uint32_t(width);
    depth.height = uint32_t(height);
    depth.sourceBits = 8;
    depth.values.resize(size_t(width) * height);
    for(size_t i = 0; i < depth.values.size(); ++i){
        depth.values[i] = float(decoded[i]) / 255.0f;
    }
    stbi_image_free(decoded);
    finish(depth);
    return depth;
}

}

//-------------------------------------------------------------------------------------------

DepthImage loadDepthImage(const std::string& path){
    std::ifstream probe(path, std::ios::binary);
    if(!probe){
        throw std::runtime_error("Spool::loadDepthImage: cannot open '" + path + "'");
    }

    //Po sadrzaju, ne po nastavku. Datoteka nazvana .png koja to nije je cesca nego sto bi se
    //ocekivalo, a poruka "nije PNG" je korisnija od tihog smeca
    char magic[2] = {};
    probe.read(magic, 2);
    probe.close();

    if(magic[0] == 'P' && (magic[1] == 'F' || magic[1] == 'f')) return loadPfm(path);
    if(magic[0] == 'P' && magic[1] == '5') return loadPgm(path);
    return loadThroughStb(path);
}

void saveDepthImage(const std::string& path, const DepthImage& depth){
    if(!depth.isValid()){
        throw std::runtime_error("Spool::saveDepthImage: there is nothing to write");
    }
    if(depth.values.size() != depth.pixelCount()){
        throw std::runtime_error("Spool::saveDepthImage: " + std::to_string(depth.values.size()) +
            " values for " + std::to_string(depth.pixelCount()) + " pixels");
    }

    const std::filesystem::path file(path);
    if(file.has_parent_path()){
        std::filesystem::create_directories(file.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if(!out){
        throw std::runtime_error("Spool::saveDepthImage: cannot write '" + path + "'");
    }

    //Pf = jedan kanal. -1.0 = little endian, sto je jedino sto ovdje i pisemo
    out << "Pf\n" << depth.width << " " << depth.height << "\n-1.0\n";

    //Odozdo prema gore, kako format trazi
    for(uint32_t y = 0; y < depth.height; ++y){
        const uint32_t row = depth.height - 1 - y;
        out.write(reinterpret_cast<const char*>(depth.values.data() + size_t(row) * depth.width),
                  std::streamsize(sizeof(float) * depth.width));
    }

    if(!out){
        throw std::runtime_error("Spool::saveDepthImage: writing '" + path + "' failed part way through");
    }
}

}
