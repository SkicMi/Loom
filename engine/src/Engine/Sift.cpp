#include "Engine/Sift.h"

#include "Engine/Bands.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Engine{
namespace{

constexpr float twoPi = 6.28318530718f;

uint32_t strideOf(const GrayImage& image){
    return image.stride > 0 ? image.stride : image.width;
}

float at(const GrayImage& image, int x, int y){
    const int clampedX = std::max(0, std::min(int(image.width) - 1, x));
    const int clampedY = std::max(0, std::min(int(image.height) - 1, y));
    return float(image.pixels[size_t(clampedY) * strideOf(image) + size_t(clampedX)]);
}

//Odvojivo Gaussovo zagladjivanje. Odvojivo jer je jezgra umnozak dvaju jednodimenzionalnih, pa je
//posao O(piksela * polumjer) umjesto O(piksela * polumjer^2)
std::vector<uint8_t> blurred(const GrayImage& image, float sigma, uint32_t& width, uint32_t& height){
    width = image.width; height = image.height;
    const int reach = std::max(1, int(std::ceil(3.0f * sigma)));

    std::vector<float> kernel(size_t(2 * reach + 1));
    float total = 0.0f;
    for(int d = -reach; d <= reach; ++d){
        kernel[size_t(d + reach)] = std::exp(-float(d * d) / (2.0f * sigma * sigma));
        total += kernel[size_t(d + reach)];
    }
    for(float& k : kernel) k /= total;

    std::vector<float> across(size_t(width) * height, 0.0f);
    inBands(0, int(height), [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            const uint8_t* row = image.pixels + size_t(y) * strideOf(image);
            int x = 0;
            const int interiorBegin = std::min(reach, int(width));
            const int interiorEnd = std::max(interiorBegin, int(width) - reach);
            for(; x < interiorBegin; ++x){
                float sum = 0.0f;
                for(int d = -reach; d <= reach; ++d){
                    sum += kernel[size_t(d + reach)] * at(image, x + d, y);
                }
                across[size_t(y) * width + size_t(x)] = sum;
            }
            //Osam susjednih izlaza prolazi isti kernel istim redom. Time svaka suma ostaje
            //bit-identicna, a prevoditelj smije paralelno obraditi osam neovisnih stupaca.
            for(; x + 7 < interiorEnd; x += 8){
                float sums[8] = {};
                for(int d = -reach; d <= reach; ++d){
                    const float weight = kernel[size_t(d + reach)];
                    for(int lane = 0; lane < 8; ++lane) sums[lane] += weight * float(row[x + lane + d]);
                }
                for(int lane = 0; lane < 8; ++lane){
                    across[size_t(y) * width + size_t(x + lane)] = sums[lane];
                }
            }
            for(; x < interiorEnd; ++x){
                float sum = 0.0f;
                for(int d = -reach; d <= reach; ++d){
                    sum += kernel[size_t(d + reach)] * float(row[x + d]);
                }
                across[size_t(y) * width + size_t(x)] = sum;
            }
            for(; x < int(width); ++x){
                float sum = 0.0f;
                for(int d = -reach; d <= reach; ++d){
                    sum += kernel[size_t(d + reach)] * at(image, x + d, y);
                }
                across[size_t(y) * width + size_t(x)] = sum;
            }
        }
    });

    std::vector<uint8_t> out(size_t(width) * height, 0);
    inBands(0, int(height), [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            int x = 0;
            if(y >= reach && y + reach < int(height)){
                for(; x + 7 < int(width); x += 8){
                    float sums[8] = {};
                    for(int d = -reach; d <= reach; ++d){
                        const float weight = kernel[size_t(d + reach)];
                        const float* source = across.data() + size_t(y + d) * width + size_t(x);
                        for(int lane = 0; lane < 8; ++lane) sums[lane] += weight * source[lane];
                    }
                    for(int lane = 0; lane < 8; ++lane){
                        out[size_t(y) * width + size_t(x + lane)] =
                            uint8_t(std::max(0.0f, std::min(255.0f, sums[lane])));
                    }
                }
            }
            for(; x < int(width); ++x){
                float sum = 0.0f;
                if(y >= reach && y + reach < int(height)){
                    for(int d = -reach; d <= reach; ++d){
                        sum += kernel[size_t(d + reach)] * across[size_t(y + d) * width + size_t(x)];
                    }
                }else{
                    for(int d = -reach; d <= reach; ++d){
                        const int row = std::max(0, std::min(int(height) - 1, y + d));
                        sum += kernel[size_t(d + reach)] * across[size_t(row) * width + size_t(x)];
                    }
                }
                out[size_t(y) * width + size_t(x)] = uint8_t(std::max(0.0f, std::min(255.0f, sum)));
            }
        }
    });
    return out;
}

//Gradijenti cijele slike, jednom. Racunanje po tocki bi isti piksel diralo onoliko puta koliko ga
//okolina pokriva - a okoline se preklapaju
struct Gradients{
    std::vector<float> magnitude;
    std::vector<float> direction;
    uint32_t width = 0, height = 0;
};

Gradients gradientsOf(const GrayImage& image){
    Gradients out;
    out.width = image.width;
    out.height = image.height;
    out.magnitude.assign(size_t(image.width) * image.height, 0.0f);
    out.direction.assign(size_t(image.width) * image.height, 0.0f);

    inBands(0, int(image.height), [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            for(int x = 0; x < int(image.width); ++x){
                const float gx = 0.5f * (at(image, x + 1, y) - at(image, x - 1, y));
                const float gy = 0.5f * (at(image, x, y + 1) - at(image, x, y - 1));
                const size_t index = size_t(y) * image.width + size_t(x);
                out.magnitude[index] = std::sqrt(gx * gx + gy * gy);
                out.direction[index] = std::atan2(gy, gx);
            }
        }
    });
    return out;
}

//Smjer okoline: vrh histograma gradijenata, s parabolom kroz tri susjedna stupca da smjer ne bude
//kvantiziran na sirinu stupca. To je Loweov postupak, a ne tezisni kao kod binarnog potpisa -
//tezisni mjeri gdje je okolina svjetlija, a ovaj gdje su joj rubovi
float dominantAngle(const Gradients& gradients, const glm::vec2& point, uint32_t patch){
    constexpr uint32_t bins = 36;
    float histogram[bins] = {};

    const int centreX = int(std::lround(point.x)), centreY = int(std::lround(point.y));
    const int reach = int(patch);
    const float sigma = 0.5f * float(patch);

    for(int dy = -reach; dy <= reach; ++dy){
        for(int dx = -reach; dx <= reach; ++dx){
            const int x = centreX + dx, y = centreY + dy;
            if(x < 0 || y < 0 || x >= int(gradients.width) || y >= int(gradients.height)) continue;

            const float away = float(dx * dx + dy * dy);
            if(away > float(reach * reach)) continue;

            const size_t index = size_t(y) * gradients.width + size_t(x);
            const float weight = std::exp(-away / (2.0f * sigma * sigma)) * gradients.magnitude[index];

            float turn = gradients.direction[index];
            if(turn < 0.0f) turn += twoPi;
            const uint32_t bin = std::min(bins - 1, uint32_t(turn / twoPi * float(bins)));
            histogram[bin] += weight;
        }
    }

    uint32_t best = 0;
    for(uint32_t bin = 1; bin < bins; ++bin) if(histogram[bin] > histogram[best]) best = bin;

    //Parabola kroz vrh i dva susjeda; stupci su u krug, pa se susjedi racunaju modulo
    const float left = histogram[(best + bins - 1) % bins];
    const float peak = histogram[best];
    const float right = histogram[(best + 1) % bins];
    const float bottom = left - 2.0f * peak + right;

    float offset = 0.0f;
    if(std::fabs(bottom) > 1e-9f) offset = 0.5f * (left - right) / bottom;
    offset = std::max(-0.5f, std::min(0.5f, offset));

    return (float(best) + 0.5f + offset) / float(bins) * twoPi;
}

void buildDescriptor(const Gradients& gradients, const glm::vec2& point, float angle,
                     const SiftConfig& config, SiftDescriptor& out){
    float histogram[siftCells][siftCells][siftBins] = {};

    const int centreX = int(std::lround(point.x)), centreY = int(std::lround(point.y));
    const int reach = int(config.patch);
    const float cellWidth = float(config.patch) * 2.0f / float(siftCells);
    const float sigma = float(config.patch);

    const float cosine = std::cos(-angle), sine = std::sin(-angle);

    for(int dy = -reach; dy <= reach; ++dy){
        for(int dx = -reach; dx <= reach; ++dx){
            const int x = centreX + dx, y = centreY + dy;
            if(x < 1 || y < 1 || x + 1 >= int(gradients.width) || y + 1 >= int(gradients.height)) continue;

            //U sustav okoline: zakrenuto tako da je smjer okoline na nuli
            const float u = float(dx) * cosine - float(dy) * sine;
            const float v = float(dx) * sine + float(dy) * cosine;

            //Mjesto u mrezi celija. Sredista celija su na -1.5, -0.5, 0.5, 1.5 sirina celije
            const float cellX = u / cellWidth + 0.5f * float(siftCells) - 0.5f;
            const float cellY = v / cellWidth + 0.5f * float(siftCells) - 0.5f;
            if(cellX <= -1.0f || cellY <= -1.0f ||
               cellX >= float(siftCells) || cellY >= float(siftCells)) continue;

            const size_t index = size_t(y) * gradients.width + size_t(x);
            const float weight = std::exp(-(u * u + v * v) / (2.0f * sigma * sigma))
                               * gradients.magnitude[index];
            if(weight <= 0.0f) continue;

            float turn = gradients.direction[index] - angle;
            while(turn < 0.0f) turn += twoPi;
            while(turn >= twoPi) turn -= twoPi;
            const float binPlace = turn / twoPi * float(siftBins);

            //TROLINEARNO: uzorak se dijeli izmedju dvije celije po svakoj osi i dva smjera. Bez
            //toga piksel tik uz granicu pri najmanjem pomaku skoci u drugu celiju, pa se potpis
            //mijenja skokovito - a upravo to bi ga ucinilo neupotrebljivim za poklapanje
            const int cx0 = int(std::floor(cellX)), cy0 = int(std::floor(cellY));
            const int b0 = int(std::floor(binPlace));
            const float fx = cellX - float(cx0), fy = cellY - float(cy0), fb = binPlace - float(b0);

            for(int ix = 0; ix <= 1; ++ix){
                const int cx = cx0 + ix;
                if(cx < 0 || cx >= int(siftCells)) continue;
                const float wx = ix ? fx : 1.0f - fx;

                for(int iy = 0; iy <= 1; ++iy){
                    const int cy = cy0 + iy;
                    if(cy < 0 || cy >= int(siftCells)) continue;
                    const float wy = iy ? fy : 1.0f - fy;

                    for(int ib = 0; ib <= 1; ++ib){
                        const int bin = (b0 + ib) % int(siftBins);
                        const float wb = ib ? fb : 1.0f - fb;
                        histogram[cy][cx][bin] += weight * wx * wy * wb;
                    }
                }
            }
        }
    }

    //Normiranje, odsijecanje, pa opet normiranje. Odsijecanje postoji zato sto jedan vrlo jak
    //gradijent inace preuzme cijeli potpis i sve oko njega se prestane razlikovati
    float raw[siftLength];
    size_t at = 0;
    for(uint32_t cy = 0; cy < siftCells; ++cy)
        for(uint32_t cx = 0; cx < siftCells; ++cx)
            for(uint32_t bin = 0; bin < siftBins; ++bin) raw[at++] = histogram[cy][cx][bin];

    auto normalize = [](float* values){
        double sum = 0.0;
        for(size_t i = 0; i < siftLength; ++i) sum += double(values[i]) * double(values[i]);
        const double length = std::sqrt(sum);
        if(length < 1e-12) return false;
        for(size_t i = 0; i < siftLength; ++i) values[i] = float(double(values[i]) / length);
        return true;
    };

    if(!normalize(raw)){ out.valid = false; return; }
    for(size_t i = 0; i < siftLength; ++i) raw[i] = std::min(raw[i], config.clamp);
    if(!normalize(raw)){ out.valid = false; return; }

    for(size_t i = 0; i < siftLength; ++i){
        out.values[i] = uint8_t(std::min(255.0f, std::round(raw[i] * 512.0f)));
    }
    out.angle = angle;
    out.valid = true;
}

}

bool describeSift(const GrayImage& image, const glm::vec2& point, SiftDescriptor& out,
                  const SiftConfig& config){
    out = SiftDescriptor{};

    const int reach = int(config.patch) + 1;
    if(point.x < float(reach) || point.y < float(reach) ||
       point.x >= float(image.width) - float(reach) || point.y >= float(image.height) - float(reach)){
        return false;
    }

    const float sigma = config.smoothing > 0.0f ? config.smoothing : float(config.patch) / 4.0f;
    uint32_t width = 0, height = 0;
    const std::vector<uint8_t> soft = blurred(image, sigma, width, height);
    const GrayImage view{soft.data(), width, height, width};

    const Gradients gradients = gradientsOf(view);
    const float angle = config.orient ? dominantAngle(gradients, point, config.patch) : 0.0f;
    buildDescriptor(gradients, point, angle, config, out);
    return out.valid;
}

std::vector<SiftDescriptor> describeSiftScaled(const GrayImage& image,
                                               const std::vector<glm::vec2>& points,
                                               const std::vector<float>& scales,
                                               const SiftConfig& config,
                                               SiftTiming* timing){
    std::vector<SiftDescriptor> out(points.size());
    if(points.empty() || scales.size() != points.size()) return out;

    //POJASEVI PO MJERILU. Zagladjivanje cijele slike je skupo, a znacajke se po mjerilu grupiraju
    //same - pa se radi jednom po pojasu, a ne jednom po znacajki
    const uint32_t bands = std::max(1u, config.scaleBands);
    std::unordered_map<int, std::vector<uint32_t>> byBand;
    for(uint32_t i = 0; i < uint32_t(points.size()); ++i){
        if(!(scales[i] > 0.0f)) continue;
        const int band = int(std::lround(std::log2(double(scales[i])) * double(bands)));
        byBand[band].push_back(i);
    }

    //Poredak pojaseva je zadan, da izlaz ne ovisi o tablici
    std::vector<int> order;
    order.reserve(byBand.size());
    for(const auto& entry : byBand) order.push_back(entry.first);
    std::sort(order.begin(), order.end());

    for(int band : order){
        using Clock = std::chrono::steady_clock;
        auto secondsSince = [](Clock::time_point from){
            return std::chrono::duration<double>(Clock::now() - from).count();
        };
        const float scale = float(std::pow(2.0, double(band) / double(bands)));

        //SLIKA VEC NOSI NESTO ZAGLADJIVANJA. Senzorska slika ima oko pola piksela vlastitog, pa se
        //dodaje samo ono sto nedostaje - dvije Gaussove se zbrajaju po kvadratu
        const float already = 0.5f;
        const float step = scale > already ? std::sqrt(scale * scale - already * already) : 0.0f;

        const auto smoothingStarted = Clock::now();
        uint32_t width = 0, height = 0;
        const std::vector<uint8_t> soft = step > 0.0f
            ? blurred(image, step, width, height)
            : std::vector<uint8_t>();
        if(timing) timing->smoothingSeconds += secondsSince(smoothingStarted);

        GrayImage view = image;
        if(step > 0.0f) view = GrayImage{soft.data(), width, height, width};

        const auto gradientStarted = Clock::now();
        const Gradients gradients = gradientsOf(view);
        if(timing) timing->gradientSeconds += secondsSince(gradientStarted);

        const std::vector<uint32_t>& mine = byBand[band];
        const auto descriptorStarted = Clock::now();
        inBands(0, int(mine.size()), [&](uint32_t, int firstItem, int lastItem){
            for(int index = firstItem; index < lastItem; ++index){
                const uint32_t which = mine[size_t(index)];
                const glm::vec2& point = points[which];

                SiftConfig local = config;
                local.patch = std::max(4u, uint32_t(std::lround(double(config.patchPerScale) * double(scales[which]))));
                local.smoothing = scale;

                const int reach = int(local.patch) + 1;
                if(point.x < float(reach) || point.y < float(reach) ||
                   point.x >= float(image.width) - float(reach) ||
                   point.y >= float(image.height) - float(reach)) continue;

                const float angle = local.orient ? dominantAngle(gradients, point, local.patch) : 0.0f;
                buildDescriptor(gradients, point, angle, local, out[which]);
            }
        });
        if(timing) timing->descriptorSeconds += secondsSince(descriptorStarted);
    }
    return out;
}

std::vector<SiftDescriptor> describeSiftAll(const GrayImage& image,
                                            const std::vector<glm::vec2>& points,
                                            const SiftConfig& config){
    std::vector<SiftDescriptor> out(points.size());
    if(points.empty()) return out;

    const float sigma = config.smoothing > 0.0f ? config.smoothing : float(config.patch) / 4.0f;
    uint32_t width = 0, height = 0;
    const std::vector<uint8_t> soft = blurred(image, sigma, width, height);
    const GrayImage view{soft.data(), width, height, width};

    const Gradients gradients = gradientsOf(view);
    const int reach = int(config.patch) + 1;

    inBands(0, int(points.size()), [&](uint32_t, int firstItem, int lastItem){
        for(int index = firstItem; index < lastItem; ++index){
            const glm::vec2& point = points[size_t(index)];
            if(point.x < float(reach) || point.y < float(reach) ||
               point.x >= float(image.width) - float(reach) ||
               point.y >= float(image.height) - float(reach)) continue;

            const float angle = config.orient ? dominantAngle(gradients, point, config.patch) : 0.0f;
            buildDescriptor(gradients, point, angle, config, out[size_t(index)]);
        }
    });
    return out;
}

float distance(const SiftDescriptor& a, const SiftDescriptor& b){
    int32_t sum = 0;
    for(size_t i = 0; i < siftLength; ++i){
        const int32_t difference = int32_t(a.values[i]) - int32_t(b.values[i]);
        sum += difference * difference;
    }
    return std::sqrt(float(sum));
}


//=============================================================================================
// Poklapanje. Mreza celija i poredak obilaska su isti kao kod binarnog potpisa (Describe.cpp) -
// namjerno, jer je ondje izmjereno da poredak odlucuje kod izjednacenih udaljenosti. Kod je
// zasad prepisan a ne dijeljen: dok se ne zna isplati li se ovaj potpis, spajanje dvaju putova u
// jedan bi znacilo dirati onaj koji radi. Ako ostane, ide u zajednicku mrezu
//=============================================================================================
namespace{

//Kvadrat udaljenosti koji odustaje cim prijedje granicu - granica je drugi po redu, a iznad nje
//kandidat ne moze promijeniti ni najboljeg ni drugog. Radi u KVADRATIMA da se izbjegne korijen
inline int32_t squaredUnder(const SiftDescriptor& a, const SiftDescriptor& b, int32_t limit){
    int32_t sum = 0;
    for(size_t block = 0; block < siftLength; block += 32){
        for(size_t i = block; i < block + 32; ++i){
            const int32_t difference = int32_t(a.values[i]) - int32_t(b.values[i]);
            sum += difference * difference;
        }
        if(sum >= limit) return sum;
    }
    return sum;
}

}

uint32_t SiftMatchGrid::at(int32_t cx, int32_t cy, uint32_t& count) const {
    const int32_t x = cx - firstX, y = cy - firstY;
    if(x < 0 || y < 0 || x >= countX || y >= countY){ count = 0; return 0; }
    const size_t cell = size_t(y) * size_t(countX) + size_t(x);
    count = start[cell + 1] - start[cell];
    return start[cell];
}

SiftMatchGrid prepareSiftMatchGrid(const std::vector<SiftDescriptor>& set,
                                   const std::vector<glm::vec2>& pixels,
                                   float cell){
    SiftMatchGrid grid;
    grid.cellSize = std::max(1.0f, cell);
    if(set.size() != pixels.size()){
        grid.countX = grid.countY = 0;
        grid.start.assign(1, 0);
        return grid;
    }

    std::vector<int32_t> cx(set.size(), 0), cy(set.size(), 0);
    bool any = false;
    for(size_t i = 0; i < set.size(); ++i){
        if(!set[i].valid) continue;
        cx[i] = int32_t(std::floor(double(pixels[i].x) / double(grid.cellSize)));
        cy[i] = int32_t(std::floor(double(pixels[i].y) / double(grid.cellSize)));
        if(!any){ grid.firstX = grid.countX = cx[i]; grid.firstY = grid.countY = cy[i]; any = true; }
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
        ++grid.start[size_t(cy[i] - grid.firstY) * size_t(grid.countX) + size_t(cx[i] - grid.firstX) + 1];
    }
    for(size_t which = 0; which < cells; ++which) grid.start[which + 1] += grid.start[which];

    std::vector<uint32_t> cursor(grid.start.begin(), grid.start.end() - 1);
    grid.items.assign(grid.start[cells], 0);
    grid.rankInCell.assign(set.size(), 0);
    for(uint32_t i = 0; i < uint32_t(set.size()); ++i){
        if(!set[i].valid) continue;
        const size_t which = size_t(cy[i] - grid.firstY) * size_t(grid.countX) + size_t(cx[i] - grid.firstX);
        grid.rankInCell[i] = cursor[which] - grid.start[which];
        grid.items[cursor[which]++] = i;
    }
    return grid;
}

std::vector<SiftMatch> matchSiftNear(const std::vector<SiftDescriptor>& from,
                                     const std::vector<glm::vec2>& fromPixels,
                                     const std::vector<SiftDescriptor>& to,
                                     const std::vector<glm::vec2>& toPixels,
                                     float radius,
                                     const SiftConfig& config){
    const float cell = std::max(1.0f, radius);
    const SiftMatchGrid fromGrid = prepareSiftMatchGrid(from, fromPixels, cell);
    const SiftMatchGrid toGrid = prepareSiftMatchGrid(to, toPixels, cell);
    return matchSiftNear(from, fromPixels, fromGrid, to, toPixels, toGrid, radius, config);
}

std::vector<SiftMatch> matchSiftNear(const std::vector<SiftDescriptor>& from,
                                     const std::vector<glm::vec2>& fromPixels,
                                     const SiftMatchGrid& fromGrid,
                                     const std::vector<SiftDescriptor>& to,
                                     const std::vector<glm::vec2>& toPixels,
                                     const SiftMatchGrid& toGrid,
                                     float radius,
                                     const SiftConfig& config){
    std::vector<SiftMatch> matches;
    if(from.empty() || to.empty()) return matches;
    if(from.size() != fromPixels.size() || to.size() != toPixels.size()) return matches;

    const float cell = std::max(1.0f, radius);
    if(fromGrid.cellSize != cell || toGrid.cellSize != cell ||
       fromGrid.rankInCell.size() != from.size() || toGrid.rankInCell.size() != to.size()) return matches;
    const float radiusSquared = radius * radius;

    const int32_t farthest = std::numeric_limits<int32_t>::max();
    const int32_t maxSquared = int32_t(config.maxDistance * config.maxDistance);

    std::vector<uint32_t> bestTo(from.size(), 0);
    std::vector<int32_t> bestSquared(from.size(), farthest);

    //Uzajamno najbolji par prije se trazio trecim punim prolazom kroz iste udaljenosti. Svaki
    //pojas sada uz A->B prolaz vodi i svoj najbolji B->A kandidat, bez dijeljenog pisanja. Nakon
    //spajanja po pojasevima dobije se isti izbor. order cuva TOCAN stari red obilaska kod
    //izjednacenih udaljenosti: celija po dy/dx, zatim indeks unutar celije.
    struct ReverseChoice{
        int32_t squared;
        uint32_t from;
        uint64_t order;
    };
    const uint32_t matchingBands = bandCount(int(from.size()));
    std::vector<ReverseChoice> reverse(size_t(matchingBands) * to.size(),
                                       ReverseChoice{farthest, UINT32_MAX, UINT64_MAX});

    inBands(0, int(from.size()), [&](uint32_t band, int firstItem, int lastItem){
        ReverseChoice* reverseHere = reverse.data() + size_t(band) * to.size();
        std::vector<std::pair<uint32_t, int32_t>> measured;
        measured.reserve(4096);
        for(uint32_t i = uint32_t(firstItem); i < uint32_t(lastItem); ++i){
            if(!from[i].valid) continue;
            measured.clear();

            const int32_t cx = int32_t(std::floor(double(fromPixels[i].x) / double(cell)));
            const int32_t cy = int32_t(std::floor(double(fromPixels[i].y) / double(cell)));

            int32_t best = farthest, second = farthest;
            uint32_t chosen = 0;

            //PRVI PROLAZ: najbolji. Granica ranog napustanja je najbolji dosad, jer drugi po redu
            //jos ne znamo - njega trazi drugi prolaz, koji zna gdje najbolji lezi
            for(int32_t dy = -1; dy <= 1; ++dy){
                for(int32_t dx = -1; dx <= 1; ++dx){
                    uint32_t count = 0;
                    const uint32_t offset = toGrid.at(cx + dx, cy + dy, count);

                    for(uint32_t k = 0; k < count; ++k){
                        const uint32_t j = toGrid.items[offset + k];
                        const glm::vec2 apart = toPixels[j] - fromPixels[i];
                        if(glm::dot(apart, apart) > radiusSquared) continue;

                        ReverseChoice& backward = reverseHere[j];
                        const int32_t d = squaredUnder(from[i], to[j], farthest);
                        measured.push_back({j, d});
                        if(d < best){ best = d; chosen = j; }

                        const uint32_t reverseCell = uint32_t((-dy + 1) * 3 + (-dx + 1));
                        const uint64_t order = (uint64_t(reverseCell) << 32) | fromGrid.rankInCell[i];
                        if(d < backward.squared || (d == backward.squared && order < backward.order)){
                            backward = ReverseChoice{d, i, order};
                        }
                    }
                }
            }

            //DRUGI PROLAZ: drugi po redu, ali samo medju onima koji NISU susjedi najboljeg -
            //vidi SiftConfig::secondBestApart
            if(best < farthest){
                const float apartSquared = config.secondBestApart * config.secondBestApart;
                for(const auto& candidate : measured){
                    const uint32_t j = candidate.first;
                    if(j == chosen) continue;

                    if(config.secondBestApart > 0.0f){
                        const glm::vec2 fromBest = toPixels[j] - toPixels[chosen];
                        if(glm::dot(fromBest, fromBest) < apartSquared) continue;
                    }

                    if(candidate.second < second) second = candidate.second;
                }
            }

            bestTo[i] = chosen;
            bestSquared[i] = best;

            if(best > maxSquared) bestSquared[i] = farthest;
            else if(second < farthest &&
                    double(best) > double(config.ratio) * double(config.ratio) * double(second)){
                bestSquared[i] = farthest;
            }
        }
    });

    std::vector<uint32_t> bestFrom(to.size(), UINT32_MAX);

    inBands(0, int(to.size()), [&](uint32_t, int firstItem, int lastItem){
        for(uint32_t j = uint32_t(firstItem); j < uint32_t(lastItem); ++j){
            ReverseChoice best{farthest, UINT32_MAX, UINT64_MAX};
            for(uint32_t band = 0; band < matchingBands; ++band){
                const ReverseChoice& candidate = reverse[size_t(band) * to.size() + j];
                if(candidate.squared < best.squared ||
                   (candidate.squared == best.squared && candidate.order < best.order)){
                    best = candidate;
                }
            }
            bestFrom[j] = best.from;
        }
    });

    for(uint32_t i = 0; i < uint32_t(from.size()); ++i){
        if(bestSquared[i] == farthest) continue;
        const uint32_t j = bestTo[i];
        if(bestFrom[j] != i) continue;
        matches.push_back(SiftMatch{i, j, std::sqrt(float(bestSquared[i]))});
    }
    return matches;
}

}
