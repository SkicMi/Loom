#pragma once
//=============================================================================================
// SNIMKA IZA SCENE: kadrovi ploce (plate) za pogled kroz rijesenu kameru.
//
// Tu se matchmove presudjuje: kroz kameru iz solvea, preko pravog kadra snimke, kocka mora
// stajati na istom mjestu poda kroz cijeli kadar. Ako klizi, solve ne valja - i to nijedan broj
// ne kaze tako brzo kao oko.
//
// DEKODIRA SE U ZASEBNOJ NITI. Kadar 4K snimke se dekodira desetke milisekundi, a skok na
// proizvoljan kadar i vise (trazi se kljucni kadar ispred pa dekodira naprijed, vidi
// VideoReader::readFrame). U petlji editora bi to zamrznulo prozor pri svakom pomaku timelinea.
// Ovako editor samo kaze koji kadar zeli, a crta zadnji koji je stigao.
//
// Uzastopni kadrovi (reprodukcija) se citaju NAPRIJED, sto je dekoderu jeftino; tek skok unatrag
// ili daleko naprijed trazi.
//
// SMANJUJE SE NA NAJVISE 1920 PIKSELA SIRINE, cijelim faktorom (prosjek kvadrata piksela). Pogled
// je ionako manji od 4K, a 33 MB po kadru prema kartici je razlika izmedju glatke i trzave
// reprodukcije
//=============================================================================================
#include <Spool/VideoFile.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Loom{

class PlateStream{
public:
    PlateStream() = default;
    PlateStream(const PlateStream&) = delete;
    PlateStream& operator=(const PlateStream&) = delete;
    ~PlateStream(){ close(); }

    //Otvara snimku; ista snimka se ne otvara ponovno
    void open(const std::string& path){
        if(path == currentPath && worker.joinable()) return;
        close();
        currentPath = path;
        quit = false;
        wanted = -1;
        fresh = false;
        problem.clear();
        worker = std::thread([this]{ run(); });
    }

    void close(){
        {
            std::lock_guard<std::mutex> guard(lock);
            quit = true;
        }
        wake.notify_all();
        if(worker.joinable()) worker.join();
        currentPath.clear();
    }

    //Koji kadar snimke (od nule) se zeli. Stari zahtjev koji jos nije dekodiran se zaboravlja
    void request(int64_t index){
        {
            std::lock_guard<std::mutex> guard(lock);
            if(index == wanted) return;
            wanted = index;
        }
        wake.notify_all();
    }

    //Novi kadar, kad je stigao od proslog poziva. Pikseli su RGBA, gusto
    bool take(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, int64_t& index){
        std::lock_guard<std::mutex> guard(lock);
        if(!fresh) return false;
        pixels.swap(ready);
        width = readyWidth;
        height = readyHeight;
        index = readyIndex;
        fresh = false;
        return true;
    }

    const std::string& path() const {return currentPath;}

    std::string error() const{
        std::lock_guard<std::mutex> guard(lock);
        return problem;
    }

    //Smanjenje cijelim faktorom, prosjekom kvadrata. Javno jer ga test provjerava
    static void shrink(const std::vector<uint8_t>& source, uint32_t width, uint32_t height, uint32_t factor,
                       std::vector<uint8_t>& out, uint32_t& outWidth, uint32_t& outHeight){
        outWidth = width / factor;
        outHeight = height / factor;
        out.assign(size_t(outWidth) * outHeight * 4, 0);
        const uint32_t area = factor * factor;
        for(uint32_t y = 0; y < outHeight; ++y){
            for(uint32_t x = 0; x < outWidth; ++x){
                uint32_t sum[4] = {0, 0, 0, 0};
                for(uint32_t dy = 0; dy < factor; ++dy){
                    const uint8_t* row = source.data() + (size_t(y * factor + dy) * width + size_t(x) * factor) * 4;
                    for(uint32_t dx = 0; dx < factor; ++dx){
                        for(int c = 0; c < 4; ++c) sum[c] += row[dx * 4 + c];
                    }
                }
                uint8_t* to = out.data() + (size_t(y) * outWidth + x) * 4;
                for(int c = 0; c < 4; ++c) to[c] = uint8_t(sum[c] / area);
            }
        }
    }

private:
    void run(){
        std::unique_ptr<Spool::VideoReader> reader;
        try{
            reader = std::make_unique<Spool::VideoReader>(currentPath);
        }catch(const std::exception& failure){
            std::lock_guard<std::mutex> guard(lock);
            problem = failure.what();
            return;
        }
        const uint32_t maxWidth = 1920;
        int64_t decoded = -1;
        std::vector<uint8_t> small;
        while(true){
            int64_t target = -1;
            {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait(guard, [&]{ return quit || (wanted >= 0 && wanted != decoded); });
                if(quit) return;
                target = wanted;
            }

            //Naprijed do 30 kadrova se cita, dalje ili unatrag se trazi
            Spool::Image frame;
            const int64_t position = reader->position();
            if(target >= position && target - position < 30){
                while(reader->position() < target && !reader->atEnd()) reader->readNext();
                if(!reader->atEnd()) frame = reader->readNext();
            }else{
                frame = reader->readFrame(target);
            }
            decoded = target;
            if(frame.pixels.empty()) continue;

            const uint32_t factor = std::max(1u, (frame.width + maxWidth - 1) / maxWidth);
            uint32_t width = frame.width, height = frame.height;
            if(factor > 1) shrink(frame.pixels, frame.width, frame.height, factor, small, width, height);
            else small.swap(frame.pixels);

            std::lock_guard<std::mutex> guard(lock);
            ready.swap(small);
            readyWidth = width;
            readyHeight = height;
            readyIndex = target;
            fresh = true;
        }
    }

    mutable std::mutex lock;
    std::condition_variable wake;
    std::thread worker;
    bool quit = false;
    std::string currentPath;
    std::string problem;
    int64_t wanted = -1;

    std::vector<uint8_t> ready;
    uint32_t readyWidth = 0, readyHeight = 0;
    int64_t readyIndex = -1;
    bool fresh = false;
};

}
