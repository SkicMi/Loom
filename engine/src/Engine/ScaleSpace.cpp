#include "Engine/Bands.h"
#include "Engine/ScaleSpace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Engine{
namespace{

struct Plane{
    std::vector<float> values;
    uint32_t width = 0, height = 0;

    float at(int x, int y) const {
        const int cx = std::max(0, std::min(int(width) - 1, x));
        const int cy = std::max(0, std::min(int(height) - 1, y));
        return values[size_t(cy) * width + size_t(cx)];
    }
};

//Odvojivo Gaussovo zagladjivanje nad plohom u pokretnom zarezu. Odvojivo jer je jezgra umnozak
//dvaju jednodimenzionalnih, pa je posao O(piksela * polumjer) umjesto O(piksela * polumjer^2)
Plane blur(const Plane& source, float sigma){
    Plane out;
    out.width = source.width; out.height = source.height;
    out.values.assign(size_t(out.width) * out.height, 0.0f);
    if(sigma <= 0.0f){ out.values = source.values; return out; }

    const int reach = std::max(1, int(std::ceil(3.0f * sigma)));
    std::vector<float> kernel(size_t(2 * reach + 1));
    float total = 0.0f;
    for(int d = -reach; d <= reach; ++d){
        kernel[size_t(d + reach)] = std::exp(-float(d * d) / (2.0f * sigma * sigma));
        total += kernel[size_t(d + reach)];
    }
    for(float& k : kernel) k /= total;

    std::vector<float> across(size_t(source.width) * source.height, 0.0f);
    inBands(0, int(source.height), [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            for(int x = 0; x < int(source.width); ++x){
                float sum = 0.0f;
                for(int d = -reach; d <= reach; ++d) sum += kernel[size_t(d + reach)] * source.at(x + d, y);
                across[size_t(y) * source.width + size_t(x)] = sum;
            }
        }
    });

    inBands(0, int(source.height), [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            for(int x = 0; x < int(source.width); ++x){
                float sum = 0.0f;
                for(int d = -reach; d <= reach; ++d){
                    const int row = std::max(0, std::min(int(source.height) - 1, y + d));
                    sum += kernel[size_t(d + reach)] * across[size_t(row) * source.width + size_t(x)];
                }
                out.values[size_t(y) * out.width + size_t(x)] = sum;
            }
        }
    });
    return out;
}

//Prepolovljenje uzimanjem svakog drugog piksela. Ne prosjekom: sljedeca oktava krece od vec
//zagladjene plohe, pa je prosjek preko nje drugo zagladjivanje kojeg u racunu mjerila nema
Plane halve(const Plane& source){
    Plane out;
    out.width = std::max(1u, source.width / 2);
    out.height = std::max(1u, source.height / 2);
    out.values.assign(size_t(out.width) * out.height, 0.0f);
    for(uint32_t y = 0; y < out.height; ++y){
        for(uint32_t x = 0; x < out.width; ++x){
            out.values[size_t(y) * out.width + x] = source.at(int(x * 2), int(y * 2));
        }
    }
    return out;
}

}

std::vector<Keypoint> detectScaleSpace(const GrayImage& image, const ScaleSpaceConfig& config){
    std::vector<Keypoint> found;
    if(!image.pixels || image.width < 8 || image.height < 8) return found;

    const uint32_t stride = image.stride ? image.stride : image.width;

    Plane current;
    current.width = image.width; current.height = image.height;
    current.values.assign(size_t(image.width) * image.height, 0.0f);
    for(uint32_t y = 0; y < image.height; ++y){
        for(uint32_t x = 0; x < image.width; ++x){
            current.values[size_t(y) * image.width + x] = float(image.pixels[size_t(y) * stride + x]) / 255.0f;
        }
    }

    //KOLIKO SE PUTA MJERILO MNOZI UNUTAR OKTAVE. Na kraju oktave mora biti tocno dvostruko, jer se
    //ondje slika prepolovi i racun se nastavlja kao da se nista nije dogodilo
    const float factor = std::pow(2.0f, 1.0f / float(std::max(1u, config.perOctave)));

    for(uint32_t octave = 0; octave < config.octaves; ++octave){
        if(current.width < 16 || current.height < 16) break;

        //Zagladjenja unutar oktave: treba ih perOctave + 3 da bi bilo perOctave razlika s punim
        //susjedstvom po mjerilu
        const uint32_t levels = config.perOctave + 3;
        std::vector<Plane> blurred;
        std::vector<float> sigmas;
        blurred.reserve(levels);

        Plane running = blur(current, config.baseSigma);
        blurred.push_back(running);
        sigmas.push_back(config.baseSigma);

        for(uint32_t level = 1; level < levels; ++level){
            //Dodatno zagladjivanje do sljedeceg mjerila: dvije uzastopne Gaussove se zbrajaju po
            //kvadratu, pa je korak sqrt(novi^2 - stari^2), a ne novi
            const float next = sigmas.back() * factor;
            const float step = std::sqrt(std::max(0.0f, next * next - sigmas.back() * sigmas.back()));
            running = blur(running, step);
            blurred.push_back(running);
            sigmas.push_back(next);
        }

        //Razlike susjednih zagladjenja
        std::vector<Plane> difference(levels - 1);
        for(uint32_t level = 0; level + 1 < levels; ++level){
            difference[level].width = current.width;
            difference[level].height = current.height;
            difference[level].values.assign(size_t(current.width) * current.height, 0.0f);
            for(size_t i = 0; i < difference[level].values.size(); ++i){
                difference[level].values[i] = blurred[level + 1].values[i] - blurred[level].values[i];
            }
        }

        const float atOctave = float(1u << octave);   //koliko je ova oktava manja od predane slike

        std::vector<std::vector<Keypoint>> perBand;
        for(uint32_t level = 1; level + 1 < uint32_t(difference.size()); ++level){
            const Plane& below = difference[level - 1];
            const Plane& here = difference[level];
            const Plane& above = difference[level + 1];

            std::vector<std::vector<Keypoint>> bands(32);
            inBands(1, int(current.height) - 1, [&](uint32_t band, int firstRow, int lastRow){
                std::vector<Keypoint>& mine = bands[band % bands.size()];
                for(int y = firstRow; y < lastRow; ++y){
                    for(int x = 1; x + 1 < int(current.width); ++x){
                        const float value = here.at(x, y);
                        if(std::fabs(value) < 0.5f * config.contrast) continue;

                        //EKSTREM U SVA TRI SMJERA: osam susjeda na istom mjerilu i po devet na
                        //susjednima. Znacajka koja je vrh samo u slici a ne i po mjerilu nije
                        //znacajka nego presjek s krivim zagladjivanjem
                        bool biggest = true, smallest = true;
                        for(int dy = -1; dy <= 1 && (biggest || smallest); ++dy){
                            for(int dx = -1; dx <= 1 && (biggest || smallest); ++dx){
                                for(int which = 0; which < 3; ++which){
                                    const Plane& plane = which == 0 ? below : (which == 1 ? here : above);
                                    if(which == 1 && dx == 0 && dy == 0) continue;
                                    const float other = plane.at(x + dx, y + dy);
                                    if(other >= value) biggest = false;
                                    if(other <= value) smallest = false;
                                }
                            }
                        }
                        if(!biggest && !smallest) continue;

                        //VRH ISPOD PIKSELA. Kroz susjedstvo se provuce kvadratna ploha u tri
                        //varijable i uzme njezino tjeme. Bez toga je polozaj tocan na piksel te
                        //oktave, a na cetvrtoj oktavi je to osam piksela prave slike
                        const float dx = 0.5f * (here.at(x + 1, y) - here.at(x - 1, y));
                        const float dy = 0.5f * (here.at(x, y + 1) - here.at(x, y - 1));
                        const float ds = 0.5f * (above.at(x, y) - below.at(x, y));

                        const float dxx = here.at(x + 1, y) - 2.0f * value + here.at(x - 1, y);
                        const float dyy = here.at(x, y + 1) - 2.0f * value + here.at(x, y - 1);
                        const float dss = above.at(x, y) - 2.0f * value + below.at(x, y);
                        const float dxy = 0.25f * (here.at(x + 1, y + 1) - here.at(x - 1, y + 1)
                                                 - here.at(x + 1, y - 1) + here.at(x - 1, y - 1));
                        const float dxs = 0.25f * (above.at(x + 1, y) - above.at(x - 1, y)
                                                 - below.at(x + 1, y) + below.at(x - 1, y));
                        const float dys = 0.25f * (above.at(x, y + 1) - above.at(x, y - 1)
                                                 - below.at(x, y + 1) + below.at(x, y - 1));

                        const double determinant =
                            double(dxx) * (double(dyy) * dss - double(dys) * dys)
                          - double(dxy) * (double(dxy) * dss - double(dys) * dxs)
                          + double(dxs) * (double(dxy) * dys - double(dyy) * dxs);
                        if(std::fabs(determinant) < 1e-12) continue;

                        //Rjesenje sustava H * pomak = -gradijent, Cramerovim pravilom
                        const double gx = -double(dx), gy = -double(dy), gs = -double(ds);
                        const double shiftX = (gx * (double(dyy) * dss - double(dys) * dys)
                                             - double(dxy) * (gy * dss - double(dys) * gs)
                                             + double(dxs) * (gy * dys - double(dyy) * gs)) / determinant;
                        const double shiftY = (double(dxx) * (gy * dss - double(dys) * gs)
                                             - gx * (double(dxy) * dss - double(dys) * dxs)
                                             + double(dxs) * (double(dxy) * gs - gy * dxs)) / determinant;
                        const double shiftS = (double(dxx) * (double(dyy) * gs - gy * dys)
                                             - double(dxy) * (double(dxy) * gs - gy * dxs)
                                             + gx * (double(dxy) * dys - double(dyy) * dxs)) / determinant;

                        //VRH KOJI POBJEGNE DALJE OD POLA PIKSELA pripada susjednom pikselu, ne
                        //ovome. Takav se odbija umjesto da se premjesta: premjestanjem bi se ista
                        //znacajka nasla dvaput
                        if(std::fabs(shiftX) > 0.6 || std::fabs(shiftY) > 0.6 || std::fabs(shiftS) > 0.6) continue;

                        const float peak = value + 0.5f * float(double(dx) * shiftX + double(dy) * shiftY
                                                              + double(ds) * shiftS);
                        if(std::fabs(peak) < config.contrast) continue;

                        //RUB NIJE ZNACAJKA - omjer svojstvenih brojeva Hessijana u slici
                        const double trace = double(dxx) + double(dyy);
                        const double area = double(dxx) * double(dyy) - double(dxy) * double(dxy);
                        if(area <= 0.0) continue;
                        const double limit = double(config.edgeRatio);
                        if(trace * trace / area >= (limit + 1.0) * (limit + 1.0) / limit) continue;

                        Keypoint one;
                        one.pixel = glm::vec2((float(x) + float(shiftX)) * atOctave,
                                              (float(y) + float(shiftY)) * atOctave);
                        one.scale = sigmas[level] * std::pow(factor, float(shiftS)) * atOctave;
                        one.strength = std::fabs(peak);
                        mine.push_back(one);
                    }
                }
            });

            for(std::vector<Keypoint>& band : bands) perBand.push_back(std::move(band));
        }

        for(const std::vector<Keypoint>& band : perBand){
            found.insert(found.end(), band.begin(), band.end());
        }

        current = halve(blurred[config.perOctave]);
    }

    //PO JACINI, pa po polozaju kod izjednacenja - izlaz ne smije ovisiti o tome koja je dretva
    //stigla prva
    std::sort(found.begin(), found.end(), [](const Keypoint& one, const Keypoint& two){
        if(one.strength != two.strength) return one.strength > two.strength;
        if(one.pixel.y != two.pixel.y) return one.pixel.y < two.pixel.y;
        if(one.pixel.x != two.pixel.x) return one.pixel.x < two.pixel.x;
        return one.scale < two.scale;
    });

    if(config.minDistance > 0.0f){
        //Mreza umjesto usporedbe svakog sa svakim: znacajki su desetine tisuca
        const float cell = config.minDistance;
        const uint32_t across = uint32_t(float(image.width) / cell) + 2;
        const uint32_t down = uint32_t(float(image.height) / cell) + 2;
        std::vector<std::vector<uint32_t>> grid(size_t(across) * down);

        std::vector<Keypoint> kept;
        kept.reserve(found.size());
        for(const Keypoint& one : found){
            const int cx = int(one.pixel.x / cell), cy = int(one.pixel.y / cell);
            bool tooClose = false;
            for(int dy = -1; dy <= 1 && !tooClose; ++dy){
                for(int dx = -1; dx <= 1 && !tooClose; ++dx){
                    const int gx = cx + dx, gy = cy + dy;
                    if(gx < 0 || gy < 0 || gx >= int(across) || gy >= int(down)) continue;
                    for(uint32_t index : grid[size_t(gy) * across + size_t(gx)]){
                        if(glm::length(kept[index].pixel - one.pixel) < config.minDistance){ tooClose = true; break; }
                    }
                }
            }
            if(tooClose) continue;
            if(cx >= 0 && cy >= 0 && cx < int(across) && cy < int(down)){
                grid[size_t(cy) * across + size_t(cx)].push_back(uint32_t(kept.size()));
            }
            kept.push_back(one);
        }
        found.swap(kept);
    }

    if(config.maxKeypoints > 0 && found.size() > config.maxKeypoints) found.resize(config.maxKeypoints);
    return found;
}

}
