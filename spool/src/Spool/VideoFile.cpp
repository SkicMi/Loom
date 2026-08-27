#include "VideoFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/display.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/pixdesc.h>
    #include <libswscale/swscale.h>
}

namespace Spool{

namespace{

std::string lowercase(std::string text){
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    return text;
}

std::string describe(int error){
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

std::string nameOr(const char* name){
    return name ? std::string(name) : std::string();
}

//Okretanje piksela, kad matrica prikaza kaze da snimka nije snimljena uspravno.
//
//Primjenjuje se ovdje a ne prepusta pozivatelju, jer je snimka citana bez toga postrance -
//i sto je gore, glavna joj je tocka na krivom mjestu, pa bi cijela rekonstrukcija bila kriva
//na nacin koji se ne vidi kao greska nego kao losa scena
std::vector<uint8_t> rotatePixels(const std::vector<uint8_t>& source,
                                  uint32_t width, uint32_t height, int degrees){
    if(degrees == 0) return source;

    const bool swaps = (degrees == 90 || degrees == 270);
    const uint32_t outWidth = swaps ? height : width;
    const uint32_t outHeight = swaps ? width : height;

    std::vector<uint8_t> out(source.size());

    for(uint32_t y = 0; y < outHeight; ++y){
        for(uint32_t x = 0; x < outWidth; ++x){
            uint32_t sourceX = 0;
            uint32_t sourceY = 0;

            if(degrees == 90){          //u smjeru kazaljke
                sourceX = y;
                sourceY = height - 1 - x;
            }
            else if(degrees == 180){
                sourceX = width - 1 - x;
                sourceY = height - 1 - y;
            }
            else{                       //270, isto sto i 90 suprotno od kazaljke
                sourceX = width - 1 - y;
                sourceY = x;
            }

            const size_t from = (size_t(sourceY) * width + sourceX) * 4;
            const size_t to = (size_t(y) * outWidth + x) * 4;
            for(int c = 0; c < 4; ++c) out[to + c] = source[from + c];
        }
    }

    return out;
}

int readRotation(const AVStream* stream){
    const uint8_t* matrix = nullptr;

#if LIBAVFORMAT_VERSION_MAJOR >= 61
    const AVPacketSideData* side = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                          stream->codecpar->nb_coded_side_data,
                                                          AV_PKT_DATA_DISPLAYMATRIX);
    if(side) matrix = side->data;
#else
    matrix = av_stream_get_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
#endif

    if(!matrix) return 0;

    //av_display_rotation_get vraca kut suprotno od kazaljke; nama treba koliko treba okrenuti
    //U smjeru kazaljke da bi slika stajala uspravno
    const double angle = -av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
    if(std::isnan(angle)) return 0;

    int degrees = int(std::lround(angle)) % 360;
    if(degrees < 0) degrees += 360;

    //Samo cetvrtine kruga. Sve ostalo je kosa matrica koju ovaj pretvarac ne bi ispravno
    //primijenio, pa je bolje ne dirati sliku nego je iskositi
    if(degrees != 90 && degrees != 180 && degrees != 270) return 0;
    return degrees;
}

void collect(const AVDictionary* dictionary, std::vector<std::pair<std::string,std::string>>& into){
    const AVDictionaryEntry* entry = nullptr;
    while((entry = av_dict_get(dictionary, "", entry, AV_DICT_IGNORE_SUFFIX)) != nullptr){
        into.emplace_back(entry->key ? entry->key : "", entry->value ? entry->value : "");
    }
}

}

double VideoInfo::frameRate() const{
    if(frameRateDenominator == 0) return 0.0;
    return double(frameRateNumerator) / double(frameRateDenominator);
}

std::string VideoInfo::find(const std::string& key) const{
    const std::string wanted = lowercase(key);
    for(const auto& entry : metadata){
        if(lowercase(entry.first) == wanted) return entry.second;
    }
    return {};
}

//-------------------------------------------------------------------------------------------

struct VideoReader::State{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;

    int streamIndex = -1;
    VideoInfo info;

    int64_t nextIndex = 0;
    bool finished = false;

    //Dimenzije prije okretanja - sto dekoder stvarno daje
    uint32_t decodedWidth = 0;
    uint32_t decodedHeight = 0;

    ~State(){
        if(scaler) sws_freeContext(scaler);
        if(frame) av_frame_free(&frame);
        if(packet) av_packet_free(&packet);
        if(codec) avcodec_free_context(&codec);
        if(format) avformat_close_input(&format);
    }

    AVStream* stream() const {return format->streams[streamIndex];}

    //Jedan dekodirani frame u RGBA. Vraca prazno kad je snimka gotova
    Image pull();
    Image convert();
};

Image VideoReader::State::convert(){
    Image out;
    out.width = decodedWidth;
    out.height = decodedHeight;
    out.sourceChannels = 4;
    out.pixels.resize(size_t(decodedWidth) * decodedHeight * 4);

    if(!scaler){
        scaler = sws_getContext(int(decodedWidth), int(decodedHeight), AVPixelFormat(frame->format),
                                int(decodedWidth), int(decodedHeight), AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
        if(!scaler){
            throw std::runtime_error("Spool::VideoReader: this pixel format cannot be converted to RGBA");
        }
    }

    uint8_t* destination[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
    int stride[4] = {int(decodedWidth) * 4, 0, 0, 0};

    sws_scale(scaler, frame->data, frame->linesize, 0, int(decodedHeight), destination, stride);

    if(info.rotation != 0){
        out.pixels = rotatePixels(out.pixels, decodedWidth, decodedHeight, info.rotation);
        out.width = info.width;
        out.height = info.height;
    }

    return out;
}

Image VideoReader::State::pull(){
    if(finished) return {};

    for(;;){
        int received = avcodec_receive_frame(codec, frame);
        if(received == 0){
            return convert();
        }
        if(received != AVERROR(EAGAIN) && received != AVERROR_EOF){
            throw std::runtime_error("Spool::VideoReader: decoding failed - " + describe(received));
        }
        if(received == AVERROR_EOF){
            finished = true;
            return {};
        }

        //Dekoderu treba jos podataka
        int read = av_read_frame(format, packet);
        if(read < 0){
            //Kraj filea: dekoder jos moze imati frameove u sebi, pa mu se to kaze i cita do kraja
            avcodec_send_packet(codec, nullptr);
            continue;
        }

        if(packet->stream_index == streamIndex){
            const int sent = avcodec_send_packet(codec, packet);
            if(sent < 0 && sent != AVERROR(EAGAIN)){
                av_packet_unref(packet);
                throw std::runtime_error("Spool::VideoReader: the decoder refused a packet - " + describe(sent));
            }
        }
        av_packet_unref(packet);
    }
}

//-------------------------------------------------------------------------------------------

VideoReader::VideoReader(const std::string& path) : state(std::make_unique<State>()){
    int opened = avformat_open_input(&state->format, path.c_str(), nullptr, nullptr);
    if(opened < 0){
        throw std::runtime_error("Spool::VideoReader: cannot open '" + path + "' - " + describe(opened));
    }

    int found = avformat_find_stream_info(state->format, nullptr);
    if(found < 0){
        throw std::runtime_error("Spool::VideoReader: '" + path + "' has no readable stream information - " + describe(found));
    }

    const AVCodec* decoder = nullptr;
    state->streamIndex = av_find_best_stream(state->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if(state->streamIndex < 0 || !decoder){
        throw std::runtime_error("Spool::VideoReader: '" + path + "' holds no video track this build can decode");
    }

    AVStream* stream = state->stream();

    state->codec = avcodec_alloc_context3(decoder);
    if(!state->codec){
        throw std::runtime_error("Spool::VideoReader: out of memory allocating a decoder");
    }

    int copied = avcodec_parameters_to_context(state->codec, stream->codecpar);
    if(copied < 0){
        throw std::runtime_error("Spool::VideoReader: cannot set up the decoder for '" + path + "' - " + describe(copied));
    }

    int started = avcodec_open2(state->codec, decoder, nullptr);
    if(started < 0){
        throw std::runtime_error("Spool::VideoReader: cannot start the decoder for '" + path + "' - " + describe(started));
    }

    state->frame = av_frame_alloc();
    state->packet = av_packet_alloc();
    if(!state->frame || !state->packet){
        throw std::runtime_error("Spool::VideoReader: out of memory");
    }

    VideoInfo& info = state->info;

    state->decodedWidth = uint32_t(state->codec->width);
    state->decodedHeight = uint32_t(state->codec->height);

    info.rotation = readRotation(stream);

    //Prijavljene dimenzije su one koje readFrame stvarno vraca, dakle poslije okretanja
    const bool swaps = (info.rotation == 90 || info.rotation == 270);
    info.width = swaps ? state->decodedHeight : state->decodedWidth;
    info.height = swaps ? state->decodedWidth : state->decodedHeight;

    const AVRational rate = stream->avg_frame_rate.num != 0 ? stream->avg_frame_rate : stream->r_frame_rate;
    info.frameRateNumerator = uint32_t(std::max(rate.num, 0));
    info.frameRateDenominator = uint32_t(std::max(rate.den, 1));

    if(stream->duration != AV_NOPTS_VALUE){
        info.duration = double(stream->duration) * av_q2d(stream->time_base);
    }
    else if(state->format->duration != AV_NOPTS_VALUE){
        info.duration = double(state->format->duration) / double(AV_TIME_BASE);
    }

    if(stream->nb_frames > 0){
        info.frameCount = stream->nb_frames;
        info.frameCountIsExact = true;
    }
    else{
        info.frameCount = int64_t(std::llround(info.duration * info.frameRate()));
        info.frameCountIsExact = false;
    }

    const AVRational aspect = stream->sample_aspect_ratio;
    info.pixelAspect = (aspect.num > 0 && aspect.den > 0) ? av_q2d(aspect) : 1.0;

    info.codec = nameOr(decoder->name);
    info.pixelFormat = nameOr(av_get_pix_fmt_name(state->codec->pix_fmt));
    info.colorPrimaries = nameOr(av_color_primaries_name(stream->codecpar->color_primaries));
    info.colorTransfer = nameOr(av_color_transfer_name(stream->codecpar->color_trc));
    info.colorSpace = nameOr(av_color_space_name(stream->codecpar->color_space));

    //Kontejner prvi, pa zapis - tim redom se i citaju kad ista dva kljuca postoje na oba
    collect(state->format->metadata, info.metadata);
    collect(stream->metadata, info.metadata);
}

VideoReader::~VideoReader() = default;
VideoReader::VideoReader(VideoReader&&) noexcept = default;
VideoReader& VideoReader::operator=(VideoReader&&) noexcept = default;

const VideoInfo& VideoReader::info() const{
    return state->info;
}

bool VideoReader::atEnd() const{
    return state->finished;
}

int64_t VideoReader::position() const{
    return state->nextIndex;
}

Image VideoReader::readNext(){
    Image out = state->pull();
    if(out.isValid()) ++state->nextIndex;
    return out;
}

void VideoReader::rewind(){
    av_seek_frame(state->format, state->streamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(state->codec);
    state->nextIndex = 0;
    state->finished = false;
}

Image VideoReader::readFrame(int64_t index){
    if(index < 0){
        throw std::runtime_error("Spool::VideoReader: frame " + std::to_string(index) + " is before the start of the file");
    }

    //Vec smo tocno tamo gdje treba: citanje naprijed je jedini jeftin nacin, pa se koristi
    //kad god moze
    if(index == state->nextIndex && !state->finished){
        return readNext();
    }

    //Unatrag se mora traziti; unaprijed se isplati citati samo ako je blizu
    if(index < state->nextIndex || index > state->nextIndex + 64 || state->finished){
        const AVStream* stream = state->stream();
        const double seconds = state->info.frameRate() > 0.0
            ? double(index) / state->info.frameRate() : 0.0;

        int64_t target = int64_t(seconds / av_q2d(stream->time_base));
        if(stream->start_time != AV_NOPTS_VALUE) target += stream->start_time;

        const int sought = av_seek_frame(state->format, state->streamIndex, target, AVSEEK_FLAG_BACKWARD);
        if(sought < 0){
            throw std::runtime_error("Spool::VideoReader: cannot seek to frame " + std::to_string(index) +
                " - " + describe(sought));
        }
        avcodec_flush_buffers(state->codec);
        state->finished = false;

        //Trazenje sleti na kljucni frame ISPRED trazenog, i taj nije nuzno onaj koji hocemo.
        //Gdje smo stvarno sletjeli kaze tek prvi dekodirani frame, po svojoj vremenskoj oznaci
        Image landed = state->pull();
        if(!landed.isValid()){
            state->finished = true;
            return {};
        }

        int64_t pts = state->frame->best_effort_timestamp;
        if(pts == AV_NOPTS_VALUE) pts = state->frame->pts;

        if(pts != AV_NOPTS_VALUE && stream->start_time != AV_NOPTS_VALUE) pts -= stream->start_time;

        state->nextIndex = (pts == AV_NOPTS_VALUE) ? 0
            : int64_t(std::llround(double(pts) * av_q2d(stream->time_base) * state->info.frameRate()));

        if(state->nextIndex == index){
            ++state->nextIndex;
            return landed;
        }
        ++state->nextIndex;
    }

    //Dekodiraj naprijed do trazenog. Medufrejm se ne da dekodirati sam, pa se ovo ne moze
    //preskociti - moze se samo poceti sto blize
    while(state->nextIndex <= index){
        Image out = readNext();
        if(!out.isValid()) return {};
        if(state->nextIndex - 1 == index) return out;
    }

    return {};
}

}
