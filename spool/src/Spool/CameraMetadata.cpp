#include "CameraMetadata.h"

#include <cstring>
#include <map>

extern "C"{
    #include <libavformat/avformat.h>
}

namespace Spool{
namespace{

uint32_t be32(const uint8_t* p){ return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
uint16_t be16(const uint8_t* p){ return uint16_t((p[0] << 8) | p[1]); }

//Lokalne oznake svih KLV skupova jednog uzorka: oznaka -> vrijednost (zadnja pobjedjuje)
std::map<uint16_t, std::vector<uint8_t>> localTags(const uint8_t* data, size_t size){
    std::map<uint16_t, std::vector<uint8_t>> out;
    if(size < 2) return out;
    size_t i = be16(data);                        //prva dva bajta su duljina zaglavlja (28)
    while(i + 17 <= size && data[i] == 0x06 && data[i + 1] == 0x0e && data[i + 2] == 0x2b && data[i + 3] == 0x34){
        size_t length = data[i + 16], body = i + 17;
        if(length & 0x80){
            const size_t bytes = length & 0x7f;
            if(body + bytes > size) break;
            length = 0;
            for(size_t k = 0; k < bytes; ++k) length = (length << 8) | data[body + k];
            body += bytes;
        }
        if(body + length > size) break;
        for(size_t k = body; k + 4 <= body + length;){
            const uint16_t tag = be16(data + k), bytes = be16(data + k + 2);
            if(k + 4 + bytes > body + length) break;
            out[tag] = std::vector<uint8_t>(data + k + 4, data + k + 4 + bytes);
            k += 4 + size_t(bytes);
        }
        i = body + length;
    }
    return out;
}

}

CameraMetadata readCameraMetadata(const std::string& path){
    CameraMetadata out;
    AVFormatContext* format = nullptr;
    if(avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) return out;
    if(avformat_find_stream_info(format, nullptr) < 0){ avformat_close_input(&format); return out; }

    int stream = -1;
    for(unsigned s = 0; s < format->nb_streams; ++s){
        const AVCodecParameters* p = format->streams[s]->codecpar;
        if(p->codec_type == AVMEDIA_TYPE_DATA && p->codec_tag == MKTAG('r', 't', 'm', 'd')) stream = int(s);
    }
    if(stream < 0){ avformat_close_input(&format); return out; }
    for(unsigned s = 0; s < format->nb_streams; ++s) format->streams[s]->discard = int(s) == stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

    AVPacket* packet = av_packet_alloc();
    while(av_read_frame(format, packet) >= 0){
        if(packet->stream_index == stream){
            const auto tags = localTags(packet->data, size_t(packet->size));
            auto has = [&](uint16_t tag, size_t bytes){ auto f = tags.find(tag); return f != tags.end() && f->second.size() >= bytes; };
            if(out.frames == 0){
                if(has(0x8106, 8)){ const auto& v = tags.at(0x8106); const uint32_t d = be32(v.data() + 4); if(d) out.framesPerSecond = double(be32(v.data())) / double(d); }
                if(has(0x8109, 8)){ const auto& v = tags.at(0x8109); const uint32_t d = be32(v.data() + 4); if(d) out.exposureSeconds = double(be32(v.data())) / double(d); }
                if(has(0xe40a, 8)){ const auto& v = tags.at(0xe40a); const uint32_t a = be32(v.data()); if(a) out.readoutFrames = double(be32(v.data() + 4)) / double(a); }
                if(has(0xe435, 4)) out.gyroRate = be32(tags.at(0xe435).data());
                if(has(0xe439, 4)){ const uint32_t bits = be32(tags.at(0xe439).data()); float f; std::memcpy(&f, &bits, 4); out.gyroUnitsPerDegreePerSecond = f; }
            }
            if(has(0x810b, 2)) out.isoPerFrame.push_back(be16(tags.at(0x810b).data()));
            if(has(0xe43b, 8)){
                const auto& v = tags.at(0xe43b);
                const uint32_t count = be32(v.data()), element = be32(v.data() + 4);
                for(uint32_t k = 0; element == 6 && 8 + size_t(k + 1) * 6 <= v.size() && k < count; ++k){
                    const uint8_t* p = v.data() + 8 + size_t(k) * 6;
                    out.gyro.push_back({int16_t(be16(p)), int16_t(be16(p + 2)), int16_t(be16(p + 4))});
                }
            }
            ++out.frames;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    out.present = out.frames > 0 && out.framesPerSecond > 0.0;
    if(out.present) out.source = "sony rtmd";
    return out;
}

}
