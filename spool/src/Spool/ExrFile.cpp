#include "ExrFile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Spool{

namespace{

enum PixelType : int32_t{ Uint = 0, Half = 1, Float = 2 };

template<class T>
void put(std::vector<uint8_t>& out, const T& value){
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));      //little endian, kao i EXR
}

void putString(std::vector<uint8_t>& out, const std::string& text){
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
}

void attribute(std::vector<uint8_t>& out, const std::string& name, const std::string& type, const std::vector<uint8_t>& value){
    putString(out, name);
    putString(out, type);
    put(out, int32_t(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

struct Reader{
    const std::vector<uint8_t>& data;
    size_t at = 0;
    const std::string& path;
    template<class T> T get(){
        if(at + sizeof(T) > data.size()) throw std::runtime_error("Spool: " + path + " - EXR je odrezan");
        T value;
        std::memcpy(&value, data.data() + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
    std::string string(){
        std::string s;
        while(at < data.size() && data[at] != 0) s.push_back(char(data[at++]));
        if(at >= data.size()) throw std::runtime_error("Spool: " + path + " - EXR je odrezan u zaglavlju");
        ++at;
        return s;
    }
};

}

const ExrChannel* ExrImage::find(const std::string& name) const{
    for(const ExrChannel& c : channels) if(c.name == name) return &c;
    return nullptr;
}

uint16_t floatToHalf(float value){
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t exponent = (bits >> 23) & 0xffu;
    uint32_t mantissa = bits & 0x7fffffu;
    if(exponent == 0xffu) return uint16_t(sign | 0x7c00u | (mantissa ? 0x200u : 0u));     //inf / NaN
    const int e = int(exponent) - 127 + 15;
    if(e >= 31) return uint16_t(sign | 0x7c00u);                                        //prevelik: inf
    if(e <= 0){
        //Subnormal (ili nula): mantisa s implicitnom jedinicom, pomaknuta udesno
        if(e < -10) return uint16_t(sign);
        mantissa |= 0x800000u;
        const int shift = 14 - e;
        uint32_t half = mantissa >> shift;
        const uint32_t rest = mantissa & ((1u << shift) - 1u), halfway = 1u << (shift - 1);
        if(rest > halfway || (rest == halfway && (half & 1u))) ++half;
        return uint16_t(sign | half);
    }
    uint32_t half = sign | (uint32_t(e) << 10) | (mantissa >> 13);
    const uint32_t rest = mantissa & 0x1fffu;
    if(rest > 0x1000u || (rest == 0x1000u && (half & 1u))) ++half;                      //prelijevanje u eksponent je ispravno
    return uint16_t(half);
}

float halfToFloat(uint16_t h){
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1fu;
    uint32_t mantissa = h & 0x3ffu;
    uint32_t bits;
    if(exponent == 0){
        if(mantissa == 0) bits = sign;
        else{
            exponent = 127 - 15 + 1;
            while(!(mantissa & 0x400u)){ mantissa <<= 1; --exponent; }
            mantissa &= 0x3ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }else if(exponent == 31) bits = sign | 0x7f800000u | (mantissa << 13);
    else bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void saveExr(const std::string& path, uint32_t width, uint32_t height, std::vector<ExrChannel> channels){
    if(width == 0 || height == 0) throw std::runtime_error("Spool: " + path + " - EXR bez velicine");
    if(channels.empty()) throw std::runtime_error("Spool: " + path + " - EXR bez kanala");
    const size_t pixels = size_t(width) * height;
    for(const ExrChannel& c : channels){
        if(c.values.size() != pixels)
            throw std::runtime_error("Spool: " + path + " - kanal " + c.name + " ima " + std::to_string(c.values.size()) +
                                     " vrijednosti, a slika " + std::to_string(pixels) + " piksela");
    }
    std::sort(channels.begin(), channels.end(), [](const ExrChannel& a, const ExrChannel& b){ return a.name < b.name; });

    std::vector<uint8_t> out;
    put(out, uint32_t(20000630));       //magicni broj
    put(out, uint32_t(2));              //verzija 2, scanline, kratka imena

    std::vector<uint8_t> list;
    for(const ExrChannel& c : channels){
        putString(list, c.name);
        put(list, int32_t(c.half ? Half : Float));
        list.push_back(0);              //pLinear
        list.push_back(0); list.push_back(0); list.push_back(0);
        put(list, int32_t(1));
        put(list, int32_t(1));
    }
    list.push_back(0);
    attribute(out, "channels", "chlist", list);
    attribute(out, "compression", "compression", {0});
    std::vector<uint8_t> box;
    put(box, int32_t(0)); put(box, int32_t(0)); put(box, int32_t(width - 1)); put(box, int32_t(height - 1));
    attribute(out, "dataWindow", "box2i", box);
    attribute(out, "displayWindow", "box2i", box);
    attribute(out, "lineOrder", "lineOrder", {0});
    std::vector<uint8_t> one;
    put(one, 1.0f);
    attribute(out, "pixelAspectRatio", "float", one);
    std::vector<uint8_t> centre;
    put(centre, 0.0f); put(centre, 0.0f);
    attribute(out, "screenWindowCenter", "v2f", centre);
    attribute(out, "screenWindowWidth", "float", one);
    out.push_back(0);

    size_t lineBytes = 0;
    for(const ExrChannel& c : channels) lineBytes += size_t(width) * (c.half ? 2u : 4u);
    const size_t tableAt = out.size();
    out.resize(out.size() + size_t(height) * 8);
    out.reserve(out.size() + size_t(height) * (8 + lineBytes));
    for(uint32_t y = 0; y < height; ++y){
        const uint64_t offset = out.size();
        std::memcpy(out.data() + tableAt + size_t(y) * 8, &offset, 8);
        put(out, int32_t(y));
        put(out, int32_t(lineBytes));
        for(const ExrChannel& c : channels){
            const float* row = c.values.data() + size_t(y) * width;
            for(uint32_t x = 0; x < width; ++x){
                if(c.half) put(out, floatToHalf(row[x]));
                else put(out, row[x]);
            }
        }
    }

    const std::filesystem::path target(path);
    std::error_code error;
    if(target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), error);
    std::ofstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("Spool: ne mogu otvoriti " + path + " za pisanje");
    file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if(!file) throw std::runtime_error("Spool: zapis u " + path + " nije uspio");
}

ExrImage loadExr(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("Spool: ne mogu otvoriti " + path);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Reader r{data, 0, path};
    if(r.get<uint32_t>() != 20000630u) throw std::runtime_error("Spool: " + path + " nije EXR");
    const uint32_t version = r.get<uint32_t>();
    if(version & 0x200u) throw std::runtime_error("Spool: " + path + " - tile EXR se ne cita, samo scanline");
    if(version & 0x1000u) throw std::runtime_error("Spool: " + path + " - visedijelni EXR se ne cita");

    struct Info{ std::string name; int32_t type; };
    std::vector<Info> infos;
    int compression = -1;
    int32_t box[4] = {0, 0, -1, -1};
    while(true){
        const std::string name = r.string();
        if(name.empty()) break;
        const std::string type = r.string();
        const int32_t size = r.get<int32_t>();
        const size_t end = r.at + size_t(size);
        if(name == "channels"){
            while(true){
                const std::string channel = r.string();
                if(channel.empty()) break;
                const int32_t pixelType = r.get<int32_t>();
                r.at += 4;
                const int32_t xs = r.get<int32_t>(), ys = r.get<int32_t>();
                if(xs != 1 || ys != 1) throw std::runtime_error("Spool: " + path + " - poduzorkovani kanal " + channel);
                infos.push_back({channel, pixelType});
            }
        }else if(name == "compression"){
            compression = r.get<uint8_t>();
        }else if(name == "dataWindow"){
            for(int32_t& v : box) v = r.get<int32_t>();
        }
        r.at = end;
    }
    if(compression != 0)
        throw std::runtime_error("Spool: " + path + " - komprimirani EXR (vrsta " + std::to_string(compression) +
                                 ") se ne cita; izvezi bez kompresije ili kao .hdr");
    ExrImage image;
    if(box[2] < box[0] || box[3] < box[1]) throw std::runtime_error("Spool: " + path + " - prazan dataWindow");
    image.width = uint32_t(box[2] - box[0] + 1);
    image.height = uint32_t(box[3] - box[1] + 1);
    for(const Info& info : infos){
        ExrChannel c;
        c.name = info.name;
        c.half = info.type == Half;
        c.values.assign(size_t(image.width) * image.height, 0.0f);
        image.channels.push_back(std::move(c));
    }
    r.at += size_t(image.height) * 8;           //tablica pomaka: blokovi idu redom
    for(uint32_t line = 0; line < image.height; ++line){
        const int32_t y = r.get<int32_t>() - box[1];
        r.get<int32_t>();
        if(y < 0 || uint32_t(y) >= image.height) throw std::runtime_error("Spool: " + path + " - redak izvan slike");
        for(size_t k = 0; k < infos.size(); ++k){
            float* row = image.channels[k].values.data() + size_t(y) * image.width;
            for(uint32_t x = 0; x < image.width; ++x){
                if(infos[k].type == Half) row[x] = halfToFloat(r.get<uint16_t>());
                else if(infos[k].type == Float) row[x] = r.get<float>();
                else row[x] = float(r.get<uint32_t>());
            }
        }
    }
    return image;
}

}
