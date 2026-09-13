#include "Engine/Track.h"

#include <algorithm>
#include <cmath>

namespace Engine{
namespace{

uint32_t strideOf(const GrayImage& image){
    return image.stride > 0 ? image.stride : image.width;
}

float at(const GrayImage& image, int x, int y){
    const int clampedX = std::max(0, std::min(int(image.width) - 1, x));
    const int clampedY = std::max(0, std::min(int(image.height) - 1, y));
    return float(image.pixels[size_t(clampedY) * strideOf(image) + size_t(clampedX)]);
}

//Uzorak izmedju piksela. Bez ovoga bi pracenje bilo tocno na piksel, a pomak koji nas zanima je
//desetina piksela
float sample(const GrayImage& image, float x, float y){
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const float top = at(image, x0, y0) * (1.0f - fx) + at(image, x0 + 1, y0) * fx;
    const float bottom = at(image, x0, y0 + 1) * (1.0f - fx) + at(image, x0 + 1, y0 + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

struct Level{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    GrayImage view() const {return GrayImage{pixels.data(), width, height, width};}
};

//Piramida: svaki nivo je upola manji, prosjek cetiri piksela. Prvi nivo je sama slika
std::vector<Level> buildPyramid(const GrayImage& image, uint32_t levels){
    std::vector<Level> pyramid;
    Level first;
    first.width = image.width;
    first.height = image.height;
    first.pixels.resize(size_t(image.width) * image.height);
    for(uint32_t y = 0; y < image.height; ++y){
        for(uint32_t x = 0; x < image.width; ++x){
            first.pixels[size_t(y) * image.width + x] = image.pixels[size_t(y) * strideOf(image) + x];
        }
    }
    pyramid.push_back(std::move(first));

    for(uint32_t level = 1; level < levels; ++level){
        const Level& source = pyramid.back();
        if(source.width < 16 || source.height < 16) break;

        Level next;
        next.width = source.width / 2;
        next.height = source.height / 2;
        next.pixels.resize(size_t(next.width) * next.height);
        for(uint32_t y = 0; y < next.height; ++y){
            for(uint32_t x = 0; x < next.width; ++x){
                const uint32_t sx = x * 2, sy = y * 2;
                const uint32_t sum = uint32_t(source.pixels[size_t(sy) * source.width + sx])
                                   + source.pixels[size_t(sy) * source.width + sx + 1]
                                   + source.pixels[size_t(sy + 1) * source.width + sx]
                                   + source.pixels[size_t(sy + 1) * source.width + sx + 1];
                next.pixels[size_t(y) * next.width + x] = uint8_t((sum + 2) / 4);
            }
        }
        pyramid.push_back(std::move(next));
    }
    return pyramid;
}

//Jedan nivo Lucas-Kanadea: krece se od pretpostavljenog pomaka i popravlja ga dok se ne smiri
bool refine(const GrayImage& from, const GrayImage& to, const glm::vec2& point,
            glm::vec2& shift, const TrackConfig& config, float& residual){
    const int window = int(config.window);

    //Strukturna matrica ovisi samo o prvoj slici, pa se racuna jednom
    double gxx = 0.0, gxy = 0.0, gyy = 0.0;
    std::vector<float> gradientX, gradientY, base;
    gradientX.reserve(size_t((2 * window + 1) * (2 * window + 1)));
    gradientY.reserve(gradientX.capacity());
    base.reserve(gradientX.capacity());

    for(int dy = -window; dy <= window; ++dy){
        for(int dx = -window; dx <= window; ++dx){
            const float x = point.x + float(dx);
            const float y = point.y + float(dy);
            const float ix = 0.5f * (sample(from, x + 1.0f, y) - sample(from, x - 1.0f, y));
            const float iy = 0.5f * (sample(from, x, y + 1.0f) - sample(from, x, y - 1.0f));
            gradientX.push_back(ix);
            gradientY.push_back(iy);
            base.push_back(sample(from, x, y));
            gxx += double(ix) * ix;
            gxy += double(ix) * iy;
            gyy += double(iy) * iy;
        }
    }

    //Prozor bez teksture: sustav je singularan i pomak nije odredjen. Isto pitanje kao kod
    //paralelnih zraka u triangulaciji - bolje reci "ne znam" nego vratiti broj
    const double determinant = gxx * gyy - gxy * gxy;
    const double trace = gxx + gyy;
    const double smaller = 0.5 * (trace - std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant)));
    if(determinant < 1e-6 || smaller < 1e-3 * double(base.size())){
        return false;
    }

    for(uint32_t iteration = 0; iteration < config.iterations; ++iteration){
        double bx = 0.0, by = 0.0;
        size_t index = 0;
        for(int dy = -window; dy <= window; ++dy){
            for(int dx = -window; dx <= window; ++dx, ++index){
                const float difference = base[index] - sample(to, point.x + shift.x + float(dx),
                                                                  point.y + shift.y + float(dy));
                bx += double(gradientX[index]) * difference;
                by += double(gradientY[index]) * difference;
            }
        }

        const double stepX = (gyy * bx - gxy * by) / determinant;
        const double stepY = (gxx * by - gxy * bx) / determinant;
        shift.x += float(stepX);
        shift.y += float(stepY);

        if(stepX * stepX + stepY * stepY < 1e-6) break;
    }

    //Koliko se okolina promijenila nakon pomaka. Trag koji je promasio ovdje se prijavi sam
    double sum = 0.0;
    size_t index = 0;
    for(int dy = -window; dy <= window; ++dy){
        for(int dx = -window; dx <= window; ++dx, ++index){
            sum += std::fabs(double(base[index]) - sample(to, point.x + shift.x + float(dx),
                                                              point.y + shift.y + float(dy)));
        }
    }
    residual = float(sum / double(base.size()));
    return true;
}

bool insideWithMargin(const GrayImage& image, const glm::vec2& point, float margin){
    return point.x >= margin && point.y >= margin &&
           point.x < float(image.width) - margin && point.y < float(image.height) - margin;
}

}

std::vector<glm::vec2> detectCorners(const GrayImage& image, const TrackConfig& config){
    std::vector<glm::vec2> corners;
    if(!image.pixels || image.width < 8 || image.height < 8) return corners;

    const int window = int(config.window);
    const int margin = window + 2;

    struct Candidate{
        float score = 0.0f;
        uint32_t x = 0, y = 0;
    };
    std::vector<Candidate> candidates;

    //GAUSSOVO TEZINJENJE, a ne ravnomjerno. S ravnomjernim prozorom odziv ugla je PLATO, ne siljak:
    //kod sahovnice gradijent postoji samo na bridovima, pa prozor pomaknut uzduz brida sadrzi isti
    //krizni uzorak i daje isti odziv. Izmjereno: detektor je nalazio tocno 88 uglova (koliko ima
    //krizista) ali pomaknutih za polumjer prozora - 6 px pri prozoru 6, 11 px pri prozoru 10.
    //S tezinama sredina prozora nosi najvise i plato postaje siljak
    std::vector<double> weights;
    weights.reserve(size_t((2 * window + 1) * (2 * window + 1)));
    const double sigma = std::max(1.0, double(window) / 2.0);
    for(int dy = -window; dy <= window; ++dy){
        for(int dx = -window; dx <= window; ++dx){
            weights.push_back(std::exp(-(double(dx) * dx + double(dy) * dy) / (2.0 * sigma * sigma)));
        }
    }

    for(int y = margin; y < int(image.height) - margin; ++y){
        for(int x = margin; x < int(image.width) - margin; ++x){
            double gxx = 0.0, gxy = 0.0, gyy = 0.0;
            size_t index = 0;
            for(int dy = -window; dy <= window; ++dy){
                for(int dx = -window; dx <= window; ++dx, ++index){
                    const float ix = 0.5f * (at(image, x + dx + 1, y + dy) - at(image, x + dx - 1, y + dy));
                    const float iy = 0.5f * (at(image, x + dx, y + dy + 1) - at(image, x + dx, y + dy - 1));
                    const double weight = weights[index];
                    gxx += weight * double(ix) * ix;
                    gxy += weight * double(ix) * iy;
                    gyy += weight * double(iy) * iy;
                }
            }
            //Shi-Tomasi: manja svojstvena vrijednost. Ugao je jak samo ako su OBA smjera jaka
            const double trace = gxx + gyy;
            const double determinant = gxx * gyy - gxy * gxy;
            const double smaller = 0.5 * (trace - std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant)));
            candidates.push_back(Candidate{float(smaller), uint32_t(x), uint32_t(y)});
        }
    }

    if(candidates.empty()) return corners;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){return a.score > b.score;});
    const float threshold = candidates.front().score * config.quality;
    const float minDistanceSquared = config.minDistance * config.minDistance;

    for(const Candidate& candidate : candidates){
        if(candidate.score < threshold) break;
        if(corners.size() >= config.maxCorners) break;

        const glm::vec2 position(float(candidate.x), float(candidate.y));
        bool farEnough = true;
        for(const glm::vec2& accepted : corners){
            const glm::vec2 difference = accepted - position;
            if(glm::dot(difference, difference) < minDistanceSquared){
                farEnough = false;
                break;
            }
        }
        if(farEnough) corners.push_back(position);
    }
    return corners;
}

bool trackPoint(const GrayImage& from, const GrayImage& to,
                const glm::vec2& start, glm::vec2& end, const TrackConfig& config){
    if(!from.pixels || !to.pixels) return false;

    const std::vector<Level> fromPyramid = buildPyramid(from, config.levels);
    const std::vector<Level> toPyramid = buildPyramid(to, config.levels);
    if(fromPyramid.size() != toPyramid.size()) return false;

    glm::vec2 shift(0.0f);
    float residual = 0.0f;

    //Od najgrubljeg nivoa prema najfinijem: pomak nadjen gore je pretpostavka dolje
    for(int level = int(fromPyramid.size()) - 1; level >= 0; --level){
        const float factor = float(1u << uint32_t(level));
        const glm::vec2 point = start / factor;

        const GrayImage levelFrom = fromPyramid[size_t(level)].view();
        const GrayImage levelTo = toPyramid[size_t(level)].view();

        if(!insideWithMargin(levelFrom, point, float(config.window) + 2.0f)) return false;
        if(!refine(levelFrom, levelTo, point, shift, config, residual)) return false;

        if(level > 0) shift *= 2.0f;   //spust na dvostruko vecu sliku
    }

    end = start + shift;
    if(!insideWithMargin(to, end, float(config.window) + 2.0f)) return false;
    return residual <= config.maxResidual;
}

Tracker::Tracker(const TrackConfig& config)
: config(config){
}

void Tracker::addFrame(const GrayImage& image){
    if(!image.pixels || image.width == 0 || image.height == 0) return;

    const uint32_t frame = frames;

    if(frames == 0){
        for(const glm::vec2& corner : detectCorners(image, config)){
            active.push_back(Active{corner, nextTrack});
            collected.push_back(Observation{frame, nextTrack, corner});
            ++nextTrack;
        }
    }else{
        const GrayImage before{previous.data(), previousWidth, previousHeight, previousWidth};

        std::vector<Active> survived;
        survived.reserve(active.size());
        for(const Active& track : active){
            glm::vec2 moved;
            if(!trackPoint(before, image, track.position, moved, config)) continue;
            survived.push_back(Active{moved, track.track});
            collected.push_back(Observation{frame, track.track, moved});
        }
        active = std::move(survived);

        //Dopuna: tragovi se gube, a rekonstrukciji trebaju tocke razasute po slici
        if(active.size() < config.minTracks){
            const float minDistanceSquared = config.minDistance * config.minDistance;
            for(const glm::vec2& corner : detectCorners(image, config)){
                if(active.size() >= config.maxCorners) break;
                bool farEnough = true;
                for(const Active& track : active){
                    const glm::vec2 difference = track.position - corner;
                    if(glm::dot(difference, difference) < minDistanceSquared){
                        farEnough = false;
                        break;
                    }
                }
                if(!farEnough) continue;

                active.push_back(Active{corner, nextTrack});
                collected.push_back(Observation{frame, nextTrack, corner});
                ++nextTrack;
            }
        }
    }

    previousWidth = image.width;
    previousHeight = image.height;
    previous.resize(size_t(image.width) * image.height);
    for(uint32_t y = 0; y < image.height; ++y){
        for(uint32_t x = 0; x < image.width; ++x){
            previous[size_t(y) * image.width + x] = image.pixels[size_t(y) * strideOf(image) + x];
        }
    }
    ++frames;
}

}
