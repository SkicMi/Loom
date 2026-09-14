#include "Engine/Describe.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace Engine{
namespace{

uint32_t strideOf(const GrayImage& image){
    return image.stride > 0 ? image.stride : image.width;
}

float at(const GrayImage& image, int x, int y){
    const int cx = std::max(0, std::min(int(image.width) - 1, x));
    const int cy = std::max(0, std::min(int(image.height) - 1, y));
    return float(image.pixels[size_t(cy) * strideOf(image) + size_t(cx)]);
}

float sample(const GrayImage& image, float x, float y){
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const float top = at(image, x0, y0) * (1.0f - fx) + at(image, x0 + 1, y0) * fx;
    const float bottom = at(image, x0, y0 + 1) * (1.0f - fx) + at(image, x0 + 1, y0 + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

//PAROVI SU ISTI ZA SVE SLIKE I SVA POKRETANJA. Potpis ima smisla samo ako je dvaput isti uzorak
//dao isti niz bitova - pa se parovi izvode iz zadanog sjemena, a ne iz slucajnosti u trenutku
//pokretanja. Normalna razdioba, jer parovi blize sredistu nose vise od onih na rubu
struct Pattern{
    std::array<float, 256 * 4> offsets{};

    Pattern(){
        std::mt19937 random(20260915u);
        std::normal_distribution<float> spread(0.0f, 0.28f);   //u jedinicama polumjera
        for(size_t i = 0; i < offsets.size(); ++i){
            offsets[i] = std::max(-0.9f, std::min(0.9f, spread(random)));
        }
    }
};

const Pattern& pattern(){
    static const Pattern one;
    return one;
}

//Zaglađivanje prije uzorkovanja: pojedinacan piksel je sum, a potpis ne smije mjeriti sum.
//Separabilno, iz istog razloga kao u detektoru uglova
std::vector<uint8_t> smooth(const GrayImage& image, float sigma){
    const int width = int(image.width), height = int(image.height);
    std::vector<uint8_t> out(size_t(width) * size_t(height));

    if(sigma <= 0.0f){
        for(int y = 0; y < height; ++y){
            for(int x = 0; x < width; ++x) out[size_t(y) * size_t(width) + size_t(x)] = uint8_t(at(image, x, y));
        }
        return out;
    }

    const int radius = std::max(1, int(std::ceil(3.0f * sigma)));
    std::vector<float> kernel(size_t(2 * radius + 1));
    float total = 0.0f;
    for(int d = -radius; d <= radius; ++d){
        kernel[size_t(d + radius)] = std::exp(-float(d * d) / (2.0f * sigma * sigma));
        total += kernel[size_t(d + radius)];
    }
    for(float& value : kernel) value /= total;

    std::vector<float> horizontal(size_t(width) * size_t(height));
    for(int y = 0; y < height; ++y){
        for(int x = 0; x < width; ++x){
            float sum = 0.0f;
            for(int d = -radius; d <= radius; ++d) sum += kernel[size_t(d + radius)] * at(image, x + d, y);
            horizontal[size_t(y) * size_t(width) + size_t(x)] = sum;
        }
    }
    for(int y = 0; y < height; ++y){
        for(int x = 0; x < width; ++x){
            float sum = 0.0f;
            for(int d = -radius; d <= radius; ++d){
                const int sy = std::max(0, std::min(height - 1, y + d));
                sum += kernel[size_t(d + radius)] * horizontal[size_t(sy) * size_t(width) + size_t(x)];
            }
            out[size_t(y) * size_t(width) + size_t(x)] = uint8_t(std::max(0.0f, std::min(255.0f, sum)));
        }
    }
    return out;
}

//SMJER IZ TEZISTA SVJETLINE. Krug oko ugla ima tezisce koje nije u sredini cim okolina nije
//simetricna, a smjer od sredista do tezista je svojstvo same okoline - ne kamere. Time isti ugao
//snimljen pod drugim kutom daje isti potpis, jer se parovi uzorkuju u zaokrenutom sustavu
float orientation(const GrayImage& image, const glm::vec2& point, int radius){
    double momentX = 0.0, momentY = 0.0;
    for(int dy = -radius; dy <= radius; ++dy){
        for(int dx = -radius; dx <= radius; ++dx){
            if(dx * dx + dy * dy > radius * radius) continue;
            const double value = double(at(image, int(point.x) + dx, int(point.y) + dy));
            momentX += double(dx) * value;
            momentY += double(dy) * value;
        }
    }
    return float(std::atan2(momentY, momentX));
}

bool describeIn(const GrayImage& image, const glm::vec2& point, Descriptor& out,
                const DescribeConfig& config){
    out = Descriptor{};
    const int radius = int(config.patch);
    if(point.x < float(radius + 2) || point.y < float(radius + 2) ||
       point.x >= float(image.width) - float(radius + 2) ||
       point.y >= float(image.height) - float(radius + 2)){
        return false;
    }

    out.angle = orientation(image, point, radius);
    const float cosine = std::cos(out.angle), sine = std::sin(out.angle);
    const float scale = float(radius);

    const Pattern& pairs = pattern();
    for(uint32_t bit = 0; bit < 256; ++bit){
        const float ax = pairs.offsets[bit * 4 + 0] * scale, ay = pairs.offsets[bit * 4 + 1] * scale;
        const float bx = pairs.offsets[bit * 4 + 2] * scale, by = pairs.offsets[bit * 4 + 3] * scale;

        //Parovi se zaokrenu za smjer okoline - to je cijeli razlog zasto se smjer racuna
        const float firstX  = point.x + cosine * ax - sine * ay;
        const float firstY  = point.y + sine   * ax + cosine * ay;
        const float secondX = point.x + cosine * bx - sine * by;
        const float secondY = point.y + sine   * bx + cosine * by;

        if(sample(image, firstX, firstY) < sample(image, secondX, secondY)){
            out.bits[bit / 64] |= (uint64_t(1) << (bit % 64));
        }
    }
    out.valid = true;
    return true;
}

}

bool describe(const GrayImage& image, const glm::vec2& point, Descriptor& out,
              const DescribeConfig& config){
    const std::vector<uint8_t> blurred = smooth(image, config.smoothing);
    const GrayImage view{blurred.data(), image.width, image.height, image.width};
    return describeIn(view, point, out, config);
}

std::vector<Descriptor> describeAll(const GrayImage& image, const std::vector<glm::vec2>& points,
                                    const DescribeConfig& config){
    std::vector<Descriptor> out(points.size());
    if(points.empty()) return out;

    //Zaglađivanje jednom za cijelu sliku, ne po tocki - ista pogreska koja je u trackeru piramidu
    //gradila po tragu i kostala 934 ms po kadru
    const std::vector<uint8_t> blurred = smooth(image, config.smoothing);
    const GrayImage view{blurred.data(), image.width, image.height, image.width};

    for(size_t i = 0; i < points.size(); ++i) describeIn(view, points[i], out[i], config);
    return out;
}

uint32_t distance(const Descriptor& a, const Descriptor& b){
    uint32_t total = 0;
    for(size_t i = 0; i < a.bits.size(); ++i){
        total += uint32_t(__builtin_popcountll(a.bits[i] ^ b.bits[i]));
    }
    return total;
}

std::vector<Match> matchDescriptors(const std::vector<Descriptor>& from,
                                    const std::vector<Descriptor>& to,
                                    const DescribeConfig& config){
    std::vector<Match> matches;
    if(from.empty() || to.empty()) return matches;

    //Za svaki iz prvog skupa najbolji i drugi po redu u drugom
    std::vector<uint32_t> bestTo(from.size(), 0), bestDistance(from.size(), 257);
    for(size_t i = 0; i < from.size(); ++i){
        if(!from[i].valid) continue;
        uint32_t second = 257;
        for(size_t j = 0; j < to.size(); ++j){
            if(!to[j].valid) continue;
            const uint32_t d = distance(from[i], to[j]);
            if(d < bestDistance[i]){ second = bestDistance[i]; bestDistance[i] = d; bestTo[i] = uint32_t(j); }
            else if(d < second){ second = d; }
        }
        //PRAG OMJERA. Ponavljajuci uzorak - fuga, resetka, sahovnica - ima mnogo jednako dobrih
        //pogodaka, i bilo koji od njih je vjerojatno krivi. Par prolazi samo ako je najbolji
        //osjetno bolji od drugog
        if(bestDistance[i] > config.maxDistance) bestDistance[i] = 257;
        else if(second < 257 && float(bestDistance[i]) > config.ratio * float(second)) bestDistance[i] = 257;
    }

    //UZAJAMNOST. Da par prodje, oba moraju biti jedno drugome najbolja - inace se deset uglova
    //preslika na isti jedan, i triangulacija dobije deset imena za istu tocku
    std::vector<uint32_t> bestFrom(to.size(), 0), backDistance(to.size(), 257);
    for(size_t j = 0; j < to.size(); ++j){
        if(!to[j].valid) continue;
        for(size_t i = 0; i < from.size(); ++i){
            if(!from[i].valid) continue;
            const uint32_t d = distance(from[i], to[j]);
            if(d < backDistance[j]){ backDistance[j] = d; bestFrom[j] = uint32_t(i); }
        }
    }

    for(size_t i = 0; i < from.size(); ++i){
        if(bestDistance[i] > 256) continue;
        const uint32_t j = bestTo[i];
        if(bestFrom[j] != uint32_t(i)) continue;
        matches.push_back(Match{uint32_t(i), j, bestDistance[i]});
    }
    return matches;
}

}
