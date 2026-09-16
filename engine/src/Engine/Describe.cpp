#include "Engine/Bands.h"
#include "Engine/Describe.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

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


//=============================================================================================
// POREDAK OBILASKA JE DIO REZULTATA, i zato je ovo napisano ovako.
//
// Udaljenosti su cijeli brojevi od 0 do 256, pa se izjednacenja dogadjaju stalno; tko je prvi
// pregledan, taj pobjedjuje. Svako ubrzanje koje promijeni redoslijed mijenja i koja se znacajka
// s kojom poklopila - dakle nije ubrzanje nego druga funkcija.
//
// Sto se smjelo promijeniti:
//   gusta mreza umjesto rasprsivanja   isti raspored celija, samo bez trazenja po tablici
//   bez prepisivanja kandidata          celije se obilaze na mjestu; prije se svaki put slagao
//                                       vektor od tisuce indeksa, i to po znacajki
//   dretve po znacajkama                svaka pise samo svoj izlaz, nista se ne zbraja
//   rano napustanje                     kad djelomicni zbroj vec dosegne drugog po redu, ovaj
//                                       kandidat ne moze promijeniti ni najboljeg ni drugog
//
// Sto se NIJE smjelo: sitnija mreza. Uzela bi manje kandidata, ali bi ih obisla drugim redom.
//=============================================================================================

namespace{

//Hammingova udaljenost koja odustaje cim prijedje granicu. Vraca tocnu vrijednost kad je ispod
//granice, a inace bilo sto >= granice - pozivatelj takav rezultat ionako samo odbacuje
inline uint32_t distanceUnder(const Descriptor& a, const Descriptor& b, uint32_t limit){
    uint32_t total = uint32_t(__builtin_popcountll(a.bits[0] ^ b.bits[0]));
    total += uint32_t(__builtin_popcountll(a.bits[1] ^ b.bits[1]));
    if(total >= limit) return total;
    total += uint32_t(__builtin_popcountll(a.bits[2] ^ b.bits[2]));
    total += uint32_t(__builtin_popcountll(a.bits[3] ^ b.bits[3]));
    return total;
}

//Znacajke razvrstane po celijama mreze. Unutar celije ostaju u rastucem rednom broju, tocno kako
//ih je slagala i tablica prije - poredak je dio rezultata
struct CellGrid{
    int32_t firstX = 0, firstY = 0;
    int32_t countX = 1, countY = 1;
    std::vector<uint32_t> start;    //countX*countY + 1
    std::vector<uint32_t> items;

    uint32_t at(int32_t cx, int32_t cy, uint32_t& count) const {
        const int32_t x = cx - firstX, y = cy - firstY;
        if(x < 0 || y < 0 || x >= countX || y >= countY){ count = 0; return 0; }
        const size_t cell = size_t(y) * size_t(countX) + size_t(x);
        count = start[cell + 1] - start[cell];
        return start[cell];
    }
};

CellGrid buildGrid(const std::vector<Descriptor>& set, const std::vector<glm::vec2>& pixels, float cell){
    CellGrid grid;

    std::vector<int32_t> cx(set.size(), 0), cy(set.size(), 0);
    bool any = false;
    for(size_t i = 0; i < set.size(); ++i){
        if(!set[i].valid) continue;
        cx[i] = int32_t(std::floor(double(pixels[i].x) / double(cell)));
        cy[i] = int32_t(std::floor(double(pixels[i].y) / double(cell)));
        if(!any){ grid.firstX = cx[i]; grid.firstY = cy[i]; grid.countX = cx[i]; grid.countY = cy[i]; any = true; }
        grid.firstX = std::min(grid.firstX, cx[i]);
        grid.firstY = std::min(grid.firstY, cy[i]);
        grid.countX = std::max(grid.countX, cx[i]);
        grid.countY = std::max(grid.countY, cy[i]);
    }
    if(!any){ grid.countX = grid.countY = 0; grid.start.assign(1, 0); return grid; }

    grid.countX = grid.countX - grid.firstX + 1;
    grid.countY = grid.countY - grid.firstY + 1;

    const size_t cells = size_t(grid.countX) * size_t(grid.countY);
    grid.start.assign(cells + 1, 0);
    for(size_t i = 0; i < set.size(); ++i){
        if(!set[i].valid) continue;
        const size_t cellIndex = size_t(cy[i] - grid.firstY) * size_t(grid.countX) + size_t(cx[i] - grid.firstX);
        ++grid.start[cellIndex + 1];
    }
    for(size_t cell = 0; cell < cells; ++cell) grid.start[cell + 1] += grid.start[cell];

    //Drugi prolaz puni rastucim rednim brojem, pa unutar celije poredak ostaje isti
    std::vector<uint32_t> cursor(grid.start.begin(), grid.start.end() - 1);
    grid.items.assign(grid.start[cells], 0);
    for(uint32_t i = 0; i < uint32_t(set.size()); ++i){
        if(!set[i].valid) continue;
        const size_t cellIndex = size_t(cy[i] - grid.firstY) * size_t(grid.countX) + size_t(cx[i] - grid.firstX);
        grid.items[cursor[cellIndex]++] = i;
    }
    return grid;
}

}

std::vector<Match> matchDescriptorsNear(const std::vector<Descriptor>& from,
                                        const std::vector<glm::vec2>& fromPixels,
                                        const std::vector<Descriptor>& to,
                                        const std::vector<glm::vec2>& toPixels,
                                        float radius,
                                        const DescribeConfig& config){
    std::vector<Match> matches;
    if(from.empty() || to.empty()) return matches;
    if(from.size() != fromPixels.size() || to.size() != toPixels.size()) return matches;

    //Mreza celija velicine polumjera: kandidat je u istoj ili susjednoj celiji, pa se pretraga
    //svede na devet celija umjesto na cijeli drugi skup
    const float cell = std::max(1.0f, radius);
    const CellGrid toGrid = buildGrid(to, toPixels, cell);
    const CellGrid fromGrid = buildGrid(from, fromPixels, cell);
    const float radiusSquared = radius * radius;

    std::vector<uint32_t> bestTo(from.size(), 0), bestDistance(from.size(), 257);

    inBands(0, int(from.size()), [&](uint32_t, int firstItem, int lastItem){
        for(uint32_t i = uint32_t(firstItem); i < uint32_t(lastItem); ++i){
            if(!from[i].valid) continue;

            const int32_t cx = int32_t(std::floor(double(fromPixels[i].x) / double(cell)));
            const int32_t cy = int32_t(std::floor(double(fromPixels[i].y) / double(cell)));

            uint32_t best = 257, second = 257, chosen = 0;

            for(int32_t dy = -1; dy <= 1; ++dy){
                for(int32_t dx = -1; dx <= 1; ++dx){
                    uint32_t count = 0;
                    const uint32_t offset = toGrid.at(cx + dx, cy + dy, count);

                    for(uint32_t k = 0; k < count; ++k){
                        const uint32_t j = toGrid.items[offset + k];
                        const glm::vec2 apart = toPixels[j] - fromPixels[i];
                        if(glm::dot(apart, apart) > radiusSquared) continue;

                        //Granica je drugi po redu: sve iznad nje ne mijenja ni jedno ni drugo
                        const uint32_t d = distanceUnder(from[i], to[j], second);
                        if(d < best){ second = best; best = d; chosen = j; }
                        else if(d < second){ second = d; }
                    }
                }
            }

            bestTo[i] = chosen;
            bestDistance[i] = best;

            if(bestDistance[i] > config.maxDistance) bestDistance[i] = 257;
            else if(second < 257 && float(bestDistance[i]) > config.ratio * float(second)) bestDistance[i] = 257;
        }
    });

    //Uzajamnost: isti racun u suprotnom smjeru. Bez njega se deset znacajki preslika na istu jednu,
    //i triangulacija dobije deset imena za istu tocku
    std::vector<uint32_t> bestFrom(to.size(), UINT32_MAX), backDistance(to.size(), 257);

    inBands(0, int(to.size()), [&](uint32_t, int firstItem, int lastItem){
        for(uint32_t j = uint32_t(firstItem); j < uint32_t(lastItem); ++j){
            if(!to[j].valid) continue;

            const int32_t cx = int32_t(std::floor(double(toPixels[j].x) / double(cell)));
            const int32_t cy = int32_t(std::floor(double(toPixels[j].y) / double(cell)));

            uint32_t best = 257, chosen = UINT32_MAX;

            for(int32_t dy = -1; dy <= 1; ++dy){
                for(int32_t dx = -1; dx <= 1; ++dx){
                    uint32_t count = 0;
                    const uint32_t offset = fromGrid.at(cx + dx, cy + dy, count);

                    for(uint32_t k = 0; k < count; ++k){
                        const uint32_t i = fromGrid.items[offset + k];
                        const glm::vec2 apart = toPixels[j] - fromPixels[i];
                        if(glm::dot(apart, apart) > radiusSquared) continue;

                        const uint32_t d = distanceUnder(from[i], to[j], best);
                        if(d < best){ best = d; chosen = i; }
                    }
                }
            }

            backDistance[j] = best;
            bestFrom[j] = chosen;
        }
    });

    for(uint32_t i = 0; i < uint32_t(from.size()); ++i){
        if(bestDistance[i] > 256) continue;
        const uint32_t j = bestTo[i];
        if(bestFrom[j] != i) continue;
        matches.push_back(Match{i, j, bestDistance[i]});
    }
    return matches;
}

}
