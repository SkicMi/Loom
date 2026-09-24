#include "Engine/Bands.h"
#include "Engine/Track.h"

#include "Engine/Dense.h"

#include <thread>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>

namespace Engine{
namespace{

//Pojasevi za dretve stoje u Engine/Bands.h - trebaju i poklapanju potpisa, pa su izdvojeni

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

float estimateNoise(const GrayImage& image){
    if(!image.pixels || image.width < 4 || image.height < 4) return 0.0f;

    //Jezgra koja ponistava sve do drugog reda: na ravnoj plohi i na nagibu daje nulu, pa ostane
    //samo ono sto se mijenja od piksela do piksela. Faktor sqrt(pi/2)/6 pretvara srednje
    //apsolutno odstupanje te jezgre natrag u standardnu devijaciju piksela
    std::vector<float> response;
    response.reserve(size_t(image.width - 2) * size_t(image.height - 2));

    for(int y = 1; y < int(image.height) - 1; ++y){
        for(int x = 1; x < int(image.width) - 1; ++x){
            const float value =
                  1.0f * at(image, x - 1, y - 1) - 2.0f * at(image, x, y - 1) + 1.0f * at(image, x + 1, y - 1)
                - 2.0f * at(image, x - 1, y)     + 4.0f * at(image, x, y)     - 2.0f * at(image, x + 1, y)
                + 1.0f * at(image, x - 1, y + 1) - 2.0f * at(image, x, y + 1) + 1.0f * at(image, x + 1, y + 1);
            response.push_back(std::fabs(value));
        }
    }
    if(response.empty()) return 0.0f;

    //MEDIJAN, ne prosjek. Rubovi i uglovi kroz ovu jezgru daju velike vrijednosti i prosjek bi
    //napuhali - onda bi kontrastna slika izgledala sumnije od mutne, a mjeri se suprotno
    const size_t middle = response.size() / 2;
    std::nth_element(response.begin(), response.begin() + long(middle), response.end());
    const double median = double(response[middle]);

    //Medijan poluvrijednosti prema standardnoj devijaciji: |N(0,s)| ima medijan 0.6745*s
    const double kernelNorm = 6.0;          //sqrt(sum kvadrata koeficijenata) = sqrt(36)
    return float(median / (0.6745 * kernelNorm));
}

std::vector<glm::vec2> detectCorners(const GrayImage& image, const TrackConfig& config){
    std::vector<glm::vec2> corners;
    if(!image.pixels || image.width < 8 || image.height < 8) return corners;

    const int window = int(config.window);
    const int margin = window + 2;
    const int width = int(image.width);
    const int height = int(image.height);

    const int scoreX0 = margin, scoreY0 = margin;
    const int scoreX1 = width - margin, scoreY1 = height - margin;
    if(scoreX0 >= scoreX1 || scoreY0 >= scoreY1) return corners;

    const int workX0 = 0, workY0 = 0, workX1 = width, workY1 = height;

    struct Candidate{
        float score = 0.0f;
        uint32_t x = 0, y = 0;
    };
    std::vector<Candidate> candidates;

    //GAUSSOVO TEZINJENJE, a ne ravnomjerno. S ravnomjernim prozorom odziv ugla je PLATO, ne siljak:
    //kod sahovnice gradijent postoji samo na bridovima, pa prozor pomaknut uzduz brida sadrzi isti
    //krizni uzorak i daje isti odziv. Izmjereno: detektor je nalazio tocno 88 uglova (koliko ima
    //krizista) ali pomaknutih za polumjer prozora - 6 px pri prozoru 6, 11 px pri prozoru 10.
    //S tezinama sredina prozora nosi najvise i plato postaje siljak.
    //
    //SEPARABILNO, a ne prozor po pikselu. Prva verzija je za SVAKI piksel obilazila cijeli prozor
    //i racunala gradijente iznova - na 4K uz prozor 12 to je 8.3 M piksela puta 625 uzoraka, dakle
    //5.2 milijarde operacija, od kojih su gotovo sve ponovljene jer susjedni pikseli dijele 96
    //posto prozora. Izmjereno: 16.8 s po pozivu, i time 91 posto cijelog vremena praćenja.
    //
    //Gaussova jezgra je separabilna: exp(-(dx^2+dy^2)/2s^2) = exp(-dx^2/2s^2) * exp(-dy^2/2s^2).
    //Zato se gradijenti racunaju JEDNOM po pikselu, slozi se tri umnoska, i svaki se filtrira
    //vodoravno pa okomito. O(piksela * prozor^2) postaje O(piksela * prozor), uz isti rezultat
    const double sigma = std::max(1.0, double(window) / 2.0);
    std::vector<float> kernel(size_t(2 * window + 1));
    for(int d = -window; d <= window; ++d){
        kernel[size_t(d + window)] = float(std::exp(-(double(d) * d) / (2.0 * sigma * sigma)));
    }

    const size_t count = size_t(width) * size_t(height);
    std::vector<float> xx(count), xy(count), yy(count);

    //Gradijenti i njihovi umnosci, jednom po pikselu
    inBands(workY0, workY1, [&](uint32_t, int firstRow, int lastRow){
        for(int y = firstRow; y < lastRow; ++y){
            for(int x = workX0; x < workX1; ++x){
                const float ix = 0.5f * (at(image, x + 1, y) - at(image, x - 1, y));
                const float iy = 0.5f * (at(image, x, y + 1) - at(image, x, y - 1));
                const size_t index = size_t(y) * size_t(width) + size_t(x);
                xx[index] = ix * ix;
                xy[index] = ix * iy;
                yy[index] = iy * iy;
            }
        }
    });

    //Vodoravni pa okomiti prolaz. Rub se ponavlja, isto kao sto at() stezne koordinatu - inace bi
    //se rubni prozori tezinili drukcije nego unutarnji, a kandidati se ionako uzimaju od margine
    std::vector<float> scratch(count);
    auto blurHorizontal = [&](std::vector<float>& plane){
        inBands(workY0, workY1, [&](uint32_t, int firstRow, int lastRow){
            for(int y = firstRow; y < lastRow; ++y){
                const size_t row = size_t(y) * size_t(width);
                for(int x = workX0; x < workX1; ++x){
                    float sum = 0.0f;
                    for(int d = -window; d <= window; ++d){
                        const int sx = std::max(workX0, std::min(workX1 - 1, x + d));
                        sum += kernel[size_t(d + window)] * plane[row + size_t(sx)];
                    }
                    scratch[row + size_t(x)] = sum;
                }
            }
        });
        plane.swap(scratch);
    };
    auto blurVertical = [&](std::vector<float>& plane){
        inBands(workY0, workY1, [&](uint32_t, int firstRow, int lastRow){
            for(int y = firstRow; y < lastRow; ++y){
                const size_t row = size_t(y) * size_t(width);
                for(int x = 0; x < width; ++x){
                    float sum = 0.0f;
                    for(int d = -window; d <= window; ++d){
                        const int sy = std::max(0, std::min(height - 1, y + d));
                        sum += kernel[size_t(d + window)] * plane[size_t(sy) * size_t(width) + size_t(x)];
                    }
                    scratch[row + size_t(x)] = sum;
                }
            }
        });
        plane.swap(scratch);
    };

    for(std::vector<float>* plane : {&xx, &xy, &yy}){
        blurHorizontal(*plane);
        blurVertical(*plane);
    }

    {
        const int firstY = scoreY0, lastY = scoreY1;
        const uint32_t bands = bandCount(std::max(0, lastY - firstY));
        std::vector<std::vector<Candidate>> perBand(bands);

        inBands(firstY, lastY, [&](uint32_t band, int firstRow, int lastRow){
            std::vector<Candidate>& mineList = perBand[std::min(band, bands - 1)];
            mineList.reserve(size_t(lastRow - firstRow) * size_t(std::max(0, scoreX1 - scoreX0)));

            for(int y = firstRow; y < lastRow; ++y){
                for(int x = scoreX0; x < scoreX1; ++x){
                    const size_t index = size_t(y) * size_t(width) + size_t(x);
                    const double gxx = double(xx[index]), gxy = double(xy[index]), gyy = double(yy[index]);

                    //Shi-Tomasi: manja svojstvena vrijednost. Ugao je jak samo ako su OBA smjera jaka
                    const double trace = gxx + gyy;
                    const double determinant = gxx * gyy - gxy * gxy;
                    const double smaller = 0.5 * (trace - std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant)));
                    mineList.push_back(Candidate{float(smaller), uint32_t(x), uint32_t(y)});
                }
            }
        });

        size_t total = 0;
        for(const std::vector<Candidate>& one : perBand) total += one.size();
        candidates.reserve(total);
        for(const std::vector<Candidate>& one : perBand){
            candidates.insert(candidates.end(), one.begin(), one.end());
        }
    }

    if(candidates.empty()) return corners;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){return a.score > b.score;});

    //PRAG IZ SUMA, uz relativni. Uzima se ono sto je strože - vidi TrackConfig::minStrengthOverNoise
    float threshold = candidates.front().score * config.quality;
    if(config.minStrengthOverNoise > 0.0f){
        double weightSum = 0.0;
        for(float w : kernel) weightSum += double(w);

        const double noise = double(estimateNoise(image));
        const double floorScore = 0.5 * noise * noise * weightSum * weightSum
                                * double(config.minStrengthOverNoise);
        threshold = std::max(threshold, float(floorScore));
    }
    const float minDistanceSquared = config.minDistance * config.minDistance;

    //RAZMAK SE PROVJERAVA PREKO MREZE, ne prolaskom kroz sve prihvacene.
    //
    //Prije je svaki kandidat usporedjivan sa svakim vec prihvacenim uglom - kvadratno u broju
    //uglova. Dok ih je bilo sesto to se nije vidjelo; s dvadeset tisuca je to dvjesto milijuna
    //usporedbi po kadru i 700 ms.
    //
    //Celija je velika tocno minDistance, pa ugao blizi od toga MORA biti u istoj ili susjednoj
    //celiji - provjera je time ista provjera, samo nad devet celija umjesto nad svime. Poredak
    //kandidata se ne dira, pa je i popis uglova isti do zadnjeg bita
    //=========================================================================================
    // SUBPIKSELNI POLOZAJ (TrackConfig::subpixel): Foerstnerov ugao (refineCorner) na ISTOJ slici
    // na kojoj je nadjen.
    //
    // Graf poklapanja trazi uglove na slici smanjenoj cetiri puta, pa je bez ovoga svaki ugao na
    // 4K tocan na cetiri piksela - izmjereno na C0257: dvije trecine koordinata lezi tocno na
    // mrezi od 4 px, a medijan ostatka je 1.5 px. I gore od kvantizacije: vrh Shi-Tomasijevog
    // odziva uz Gaussov prozor ne lezi na uglu nego je pomaknut prema unutra, pa se iz drugog
    // pogleda pomakne drukcije.
    //
    // Dotjerivanje NA PUNOJ SLICI je vec probano i stetilo je (vidi
    // MatchGraphConfig::refineAtFullResolution): unutar cetiri piksela 4K slike ima drugih, sitnijih
    // uglova, pa opazanja istog traga skoce na razlicite. Ovdje se ostaje na skali na kojoj je ugao
    // nadjen. Izmjereno na sintetici (test_subpixel_corners), medijan greske pomaka na punoj slici:
    //
    //   cijeli piksel         2.854 px
    //   parabola kroz odziv   1.436 px
    //   Foerstner, poluprozor 3 / 5 / 8     0.408 / 0.161 / 0.228 px
    //=========================================================================================
    std::vector<glm::vec2> refined;
    auto subpixelPosition = [&](const glm::vec2& start){
        return refineCorner(image, start, 5, 3.0f, 10);
    };

    const float cellSize = std::max(1.0f, config.minDistance);
    const int32_t cellsX = int32_t(float(image.width) / cellSize) + 2;
    const int32_t cellsY = int32_t(float(image.height) / cellSize) + 2;
    std::vector<std::vector<uint32_t>> grid(size_t(cellsX) * size_t(cellsY));

    auto cellOf = [&](const glm::vec2& at, int32_t& cx, int32_t& cy){
        cx = std::max(0, std::min(cellsX - 1, int32_t(at.x / cellSize)));
        cy = std::max(0, std::min(cellsY - 1, int32_t(at.y / cellSize)));
    };

    for(const Candidate& candidate : candidates){
        if(candidate.score < threshold) break;
        if(corners.size() >= config.maxCorners) break;

        const glm::vec2 position(float(candidate.x), float(candidate.y));

        int32_t cx = 0, cy = 0;
        cellOf(position, cx, cy);

        bool farEnough = true;
        for(int32_t dy = -1; dy <= 1 && farEnough; ++dy){
            for(int32_t dx = -1; dx <= 1 && farEnough; ++dx){
                const int32_t x = cx + dx, y = cy + dy;
                if(x < 0 || y < 0 || x >= cellsX || y >= cellsY) continue;

                for(uint32_t index : grid[size_t(y) * size_t(cellsX) + size_t(x)]){
                    const glm::vec2 difference = corners[index] - position;
                    if(glm::dot(difference, difference) < minDistanceSquared){
                        farEnough = false;
                        break;
                    }
                }
            }
        }

        if(farEnough){
            grid[size_t(cy) * size_t(cellsX) + size_t(cx)].push_back(uint32_t(corners.size()));
            corners.push_back(position);
            if(config.subpixel) refined.push_back(subpixelPosition(position));
        }
    }
    //Razmak se provjeravao na cijelim pikselima, pa je IZBOR uglova isti s dotjerivanjem i bez
    //njega; mijenja se samo polozaj
    return config.subpixel ? refined : corners;
}

glm::vec2 refineCorner(const GrayImage& image, const glm::vec2& start,
                       uint32_t window, float maxShift, uint32_t iterations){
    if(window < 2 || image.width < 4 || image.height < 4) return start;

    glm::vec2 corner = start;

    for(uint32_t step = 0; step < iterations; ++step){
        //Sustav 2x2: suma g g' i suma g g' p. Tezina pada prema rubu okoline, jer piksel daleko od
        //procjene vjerojatnije pripada drugoj strukturi
        double gxx = 0.0, gxy = 0.0, gyy = 0.0, bx = 0.0, by = 0.0;

        const int centreX = int(std::lround(corner.x));
        const int centreY = int(std::lround(corner.y));
        const int reach = int(window);
        const double sigma = double(window) * 0.5;

        for(int dy = -reach; dy <= reach; ++dy){
            for(int dx = -reach; dx <= reach; ++dx){
                const int x = centreX + dx, y = centreY + dy;
                if(x < 1 || y < 1 || x + 1 >= int(image.width) || y + 1 >= int(image.height)) continue;

                const double gx = 0.5 * double(at(image, x + 1, y) - at(image, x - 1, y));
                const double gy = 0.5 * double(at(image, x, y + 1) - at(image, x, y - 1));

                const double away = double(dx * dx + dy * dy) / (2.0 * sigma * sigma);
                const double weight = std::exp(-away);

                const double wxx = weight * gx * gx;
                const double wxy = weight * gx * gy;
                const double wyy = weight * gy * gy;

                gxx += wxx; gxy += wxy; gyy += wyy;
                bx += wxx * double(x) + wxy * double(y);
                by += wxy * double(x) + wyy * double(y);
            }
        }

        const double determinant = gxx * gyy - gxy * gxy;
        //Slabo uvjetovan sustav znaci rub ili ravnu plohu, a ne kut - ondje se nema sto dotjerivati
        if(std::fabs(determinant) < 1e-9) return start;

        const glm::vec2 solved(float((gyy * bx - gxy * by) / determinant),
                               float((gxx * by - gxy * bx) / determinant));

        if(!std::isfinite(solved.x) || !std::isfinite(solved.y)) return start;
        if(glm::length(solved - start) > maxShift) return start;

        if(glm::length(solved - corner) < 1e-4f){ corner = solved; break; }
        corner = solved;
    }

    return glm::length(corner - start) > maxShift ? start : corner;
}

Pyramid::Pyramid(const GrayImage& image, uint32_t levels){
    //buildPyramid vraca svoj Level iz anonimnog imenika; ovdje se prepisuje u nas
    for(const auto& source : buildPyramid(image, levels)){
        steps.push_back(Pyramid::Level{source.pixels, source.width, source.height});
    }
}

uint32_t Pyramid::levels() const {return uint32_t(steps.size());}
bool Pyramid::empty() const {return steps.empty();}

GrayImage Pyramid::level(uint32_t index) const {
    if(index >= steps.size()) return GrayImage{};
    const Level& step = steps[index];
    return GrayImage{step.pixels.data(), step.width, step.height, step.width};
}

bool refineToward(const GrayImage& reference, const GrayImage& image,
                  const glm::vec2& at, glm::vec2& position,
                  float maxShift, const TrackConfig& config){
    if(!reference.pixels || !image.pixels) return false;

    const float margin = float(config.window) + 2.0f;
    if(!insideWithMargin(reference, at, margin)) return false;
    if(!insideWithMargin(image, position, margin)) return false;

    //Pocetni pomak je ono sto vec znamo: razlika dvaju poznatih polozaja. Lucas-Kanade odavde
    //trazi samo ostatak, dakle kvantizaciju
    glm::vec2 shift = position - at;
    const glm::vec2 before = shift;

    float residual = 0.0f;
    if(!refine(reference, image, at, shift, config, residual)) return false;

    const glm::vec2 moved = shift - before;
    if(glm::length(moved) > maxShift) return false;

    const glm::vec2 found = at + shift;
    if(!insideWithMargin(image, found, margin)) return false;
    if(residual > config.maxResidual) return false;

    position = found;
    return true;
}

bool trackPoint(const GrayImage& from, const GrayImage& to,
                const glm::vec2& start, glm::vec2& end, const TrackConfig& config){
    if(!from.pixels || !to.pixels) return false;
    return trackPoint(Pyramid(from, config.levels), Pyramid(to, config.levels), start, end, config);
}

bool trackPoint(const Pyramid& fromPyramid, const Pyramid& toPyramid,
                const glm::vec2& start, glm::vec2& end, const TrackConfig& config){
    if(fromPyramid.empty() || toPyramid.empty()) return false;
    if(fromPyramid.levels() != toPyramid.levels()) return false;

    glm::vec2 shift(0.0f);
    float residual = 0.0f;

    //Od najgrubljeg nivoa prema najfinijem: pomak nadjen gore je pretpostavka dolje
    for(int level = int(fromPyramid.levels()) - 1; level >= 0; --level){
        const float factor = float(1u << uint32_t(level));
        const glm::vec2 point = start / factor;

        const GrayImage levelFrom = fromPyramid.level(uint32_t(level));
        const GrayImage levelTo = toPyramid.level(uint32_t(level));

        if(!insideWithMargin(levelFrom, point, float(config.window) + 2.0f)) return false;
        if(!refine(levelFrom, levelTo, point, shift, config, residual)) return false;

        if(level > 0) shift *= 2.0f;   //spust na dvostruko vecu sliku
    }

    end = start + shift;
    if(!insideWithMargin(toPyramid.level(0), end, float(config.window) + 2.0f)) return false;
    return residual <= config.maxResidual;
}

TrackTemplate::TrackTemplate(const Pyramid& pyramid, const glm::vec2& point, const TrackConfig& config){
    if(pyramid.empty()) return;

    const int window = int(config.window);
    const size_t count = size_t((2 * window + 1) * (2 * window + 1));

    std::vector<Level> built;
    for(uint32_t level = 0; level < pyramid.levels(); ++level){
        const float factor = float(1u << level);
        const glm::vec2 centre = point / factor;
        const GrayImage image = pyramid.level(level);

        //Od najfinijeg prema grubljem: cim jedan nivo ne stane, ne stane ni nijedan grublji, jer
        //je slika manja a margina ista. Trag zato smije imati manje nivoa nego piramida
        if(!insideWithMargin(image, centre, float(window) + 2.0f)) break;

        Level step;
        step.centre = centre;
        step.values.reserve(count);
        step.gradientX.reserve(count);
        step.gradientY.reserve(count);
        std::vector<double> hessian(36, 0.0);

        for(int dy = -window; dy <= window; ++dy){
            for(int dx = -window; dx <= window; ++dx){
                const float x = centre.x + float(dx);
                const float y = centre.y + float(dy);
                const float gx = 0.5f * (sample(image, x + 1.0f, y) - sample(image, x - 1.0f, y));
                const float gy = 0.5f * (sample(image, x, y + 1.0f) - sample(image, x, y - 1.0f));

                step.values.push_back(sample(image, x, y));
                step.gradientX.push_back(gx);
                step.gradientY.push_back(gy);

                //Steepest descent: gradijent puta izvod warpa po parametrima. Redoslijed parametara
                //je (linear00, linear10, linear01, linear11, shiftX, shiftY)
                const double sd[6] = {double(gx) * dx, double(gy) * dx,
                                      double(gx) * dy, double(gy) * dy,
                                      double(gx),      double(gy)};
                for(int r = 0; r < 6; ++r){
                    for(int c = 0; c < 6; ++c) hessian[size_t(r) * 6 + size_t(c)] += sd[r] * sd[c];
                }
            }
        }

        step.hessian = std::move(hessian);
        built.push_back(std::move(step));
    }

    if(built.empty()) return;
    steps = std::move(built);
    birth = point;
}

bool trackAffine(const TrackTemplate& templ, const Pyramid& to, AffineWarp& warp,
                 const TrackConfig& config, float noise){
    if(templ.empty() || to.empty()) return false;

    const int window = int(config.window);
    const uint32_t usable = std::min(uint32_t(templ.steps.size()), to.levels());
    if(usable == 0) return false;

    for(int level = int(usable) - 1; level >= 0; --level){
        const TrackTemplate::Level& step = templ.steps[size_t(level)];
        const GrayImage image = to.level(uint32_t(level));
        const float factor = float(1u << uint32_t(level));

        //Linearni dio je bez mjerila i ide kroz nivoe nepromijenjen; pomak se dijeli
        glm::vec2 shift = warp.shift / factor;

        for(uint32_t iteration = 0; iteration < config.iterations; ++iteration){
            std::vector<double> b(6, 0.0);
            size_t index = 0;
            bool outside = false;

            for(int dy = -window; dy <= window && !outside; ++dy){
                for(int dx = -window; dx <= window; ++dx, ++index){
                    const glm::vec2 local{float(dx), float(dy)};
                    const glm::vec2 place = step.centre + warp.linear * local + shift;

                    if(place.x < 1.0f || place.y < 1.0f ||
                       place.x >= float(image.width) - 2.0f || place.y >= float(image.height) - 2.0f){
                        outside = true;
                        break;
                    }

                    //OBRNUTO od obicnog LK: razlika je slika minus predlozak, jer se korak trazi
                    //nad predloskom pa se primjenjuje kao INVERZ na warp
                    const float difference = sample(image, place.x, place.y) - step.values[index];
                    const float gx = step.gradientX[index];
                    const float gy = step.gradientY[index];

                    b[0] += double(gx) * dx * difference;
                    b[1] += double(gy) * dx * difference;
                    b[2] += double(gx) * dy * difference;
                    b[3] += double(gy) * dy * difference;
                    b[4] += double(gx) * difference;
                    b[5] += double(gy) * difference;
                }
            }
            if(outside) return false;

            //SAMO POMAK NA GRUBIM NIVOIMA, svih sest tek na najfinijem. Na grubom nivou je zakrpa
            //mala a pomak jos velik, pa linearni dio ode u divljinu prije nego se pomak slegne - i
            //onda ga ograda na rastezanje ubije, iako stvarnog izoblicenja jos nema. Isto stoji i
            //u izvornom Shi-Tomasi radu: afino je presumno za usporedbu susjednih kadrova, a na
            //mjestu je za usporedbu preko duge baze.
            //
            //Izmjereno na pravoj snimci, 40 kadrova iz dijela gdje se kamera giba: uspjesnih
            //pracenja 3488 -> 10200, odbijenih rastezanjem 24203 -> 19629. Na sintetici rotacija
            //0.0196 -> 0.0143 st. Dakle bolje na oba, sto je jedini razlog zasto je ovdje
            std::vector<double> delta;
            if(level > 0){
                const std::vector<double> pair = {
                    step.hessian[4 * 6 + 4], step.hessian[4 * 6 + 5],
                    step.hessian[5 * 6 + 4], step.hessian[5 * 6 + 5]};
                std::vector<double> shiftOnly;
                if(!solveDense(pair, {b[4], b[5]}, 2, shiftOnly)) return false;
                delta = {0.0, 0.0, 0.0, 0.0, shiftOnly[0], shiftOnly[1]};
            }else{
                //REGULARIZACIJA IZ SUMA, ne iz konstante. Kod bijelog suma s standardnom
                //devijacijom sigma kovarijanca desne strane je sigma^2 * H, pa je kovarijanca
                //parametara sigma^2 * H^-1. Tihonovljev clan lambda odgovara apriornoj razdiobi
                //sirine sigma/sqrt(lambda), pa je lambda = sigma^2 / s^2 gdje je s koliko se
                //linearni dio SMIJE pomaknuti - a to vec stoji zapisano kao maxStretch.
                //
                //Time nema nijednog novog broja: na slici bez suma lambda ispadne nula i racuna se
                //tocno ono sto bi se racunalo bez ovoga, a na snimci sa senzorskim sumom raste sam
                //od sebe. Pomak se NE prigusuje - njega zakrpa uvijek odredjuje
                std::vector<double> normal = step.hessian;
                const double room = std::max(1e-3, double(config.maxStretch) - 1.0);
                const double fromNoise = double(noise) * double(noise) / (room * room);

                static const double eps = std::getenv("LOOM_EPS") ? std::atof(std::getenv("LOOM_EPS")) : 0.0;
                double strongest = 0.0;
                for(int i = 0; i < 4; ++i) strongest = std::max(strongest, normal[size_t(i) * 6 + size_t(i)]);
                const double fromRank = eps * strongest;

                const double lambda = std::max(fromNoise, fromRank);
                for(int i = 0; i < 4; ++i) normal[size_t(i) * 6 + size_t(i)] += lambda;

                if(!solveDense(normal, b, 6, delta)) return false;
            }

            const glm::mat2 taken(1.0f + float(delta[0]), float(delta[1]),
                                  float(delta[2]),        1.0f + float(delta[3]));
            if(std::fabs(glm::determinant(taken)) < 1e-6f) return false;
            const glm::mat2 undone = glm::inverse(taken);

            //warp_novi = warp_stari slozen s INVERZOM koraka, pa zatim pomak istim inverzom
            warp.linear = warp.linear * undone;
            shift = shift - warp.linear * glm::vec2{float(delta[4]), float(delta[5])};

            if(double(delta[4]) * delta[4] + double(delta[5]) * delta[5] < 1e-6) break;
        }

        warp.shift = shift * factor;
    }

    //IZRODJEN PROZOR. Afini warp bez granice rado stanji prozor u crtu i onda se "savrseno"
    //poklopi s bilo cime. Determinanta hvata skupljanje i sirenje, duljine stupaca hvataju
    //rastezanje u jednom smjeru koje determinanta propusti jer ga drugi smjer ponisti
    const float determinant = std::fabs(glm::determinant(warp.linear));
    const float limit = config.maxStretch;
    if(determinant < 1.0f / (limit * limit) || determinant > limit * limit) return false;
    for(int column = 0; column < 2; ++column){
        const float length = glm::length(warp.linear[column]);
        if(length < 1.0f / limit || length > limit) return false;
    }

    const GrayImage finest = to.level(0);
    if(!insideWithMargin(finest, templ.origin() + warp.shift, float(window) + 2.0f)) return false;

    //Koliko se okolina razlikuje od predloska nakon warpa - ista mjera i isti prag kao prije
    const TrackTemplate::Level& fine = templ.steps[0];
    double sum = 0.0;
    size_t index = 0;
    for(int dy = -window; dy <= window; ++dy){
        for(int dx = -window; dx <= window; ++dx, ++index){
            const glm::vec2 place = fine.centre + warp.linear * glm::vec2{float(dx), float(dy)} + warp.shift;
            sum += std::fabs(double(sample(finest, place.x, place.y)) - double(fine.values[index]));
        }
    }
    return float(sum / double(fine.values.size())) <= config.maxResidual;
}

Tracker::Tracker(const TrackConfig& config)
: config(config){
}

void Tracker::addFrame(const GrayImage& image){
    if(!image.pixels || image.width == 0 || image.height == 0) return;

    const uint32_t frame = frames;

    //Piramida jednom po kadru, pa je svi tragovi dijele. Prije se gradila po tragu i to je bio
    //cijeli trosak: 934 ms po kadru naspram 4 ms za dekodiranje. Gradi se OVDJE, na jednom mjestu:
    //prije je ista piramida nastajala dvaput u svakom kadru, jednom za pracenje i jednom za
    //sljedeci kadar
    Pyramid current(image, config.levels);

    //Jednom po kadru, ne po tragu - isto pravilo kao za piramidu
    const float noise = config.affine ? estimateNoise(image) : 0.0f;
    if(std::getenv("LOOM_SHOWNOISE") && frames == 0) std::fprintf(stderr, "[SUM] %.3f\n", noise);

    //Novi trag: sidro se uzima iz kadra u kojem je ugao nadjen, i to je jedini kadar s kojim ce
    //se taj trag ikad usporedjivati
    auto startTrack = [&](const glm::vec2& corner){
        Active track;
        track.position = corner;
        track.track = nextTrack;
        if(config.affine){
            track.anchor = TrackTemplate(current, corner, config);
            if(track.anchor.empty()) return false;   //ugao preblizu rubu da bi nosio prozor
        }
        active.push_back(std::move(track));
        collected.push_back(Observation{frame, nextTrack, corner});
        ++nextTrack;
        return true;
    };

    if(frames == 0){
        for(const glm::vec2& corner : detectCorners(image, config)) startTrack(corner);
    }else{
        //Svaki trag se racuna ne gledajuci nijedan drugi, pa se posao dijeli po dretvama. Izlaz
        //se NE pise iz dretvi nego u vlastite pretince, a red se slaze poslije - opazanja time
        //izlaze istim redom kao da je racunato jednom dretvom, i testovi mjere racun a ne raspored
        const size_t liveCount = active.size();
        std::vector<char> kept(liveCount, 0);
        std::vector<glm::vec2> places(liveCount);

        inBands(0, int(liveCount), [&](uint32_t, int firstTrack, int lastTrack){
            for(int i = firstTrack; i < lastTrack; ++i){
                Active& track = active[size_t(i)];
                glm::vec2 moved;

                if(config.affine){
                    //Pretpostavka je warp iz proslog kadra, pa je za popraviti ostao jedan kadar
                    //gibanja - ali se MJERI od sidra, ne od proslog kadra
                    if(!trackAffine(track.anchor, current, track.warp, config, noise)) continue;
                    moved = track.anchor.origin() + track.warp.shift;
                }else{
                    if(!trackPoint(previousPyramid, current, track.position, moved, config)) continue;
                }

                kept[size_t(i)] = 1;
                places[size_t(i)] = moved;
            }
        });

        std::vector<Active> survived;
        survived.reserve(liveCount);
        for(size_t i = 0; i < liveCount; ++i){
            if(!kept[i]) continue;
            Active& track = active[i];
            track.position = places[i];
            collected.push_back(Observation{frame, track.track, places[i]});
            survived.push_back(std::move(track));
        }
        active = std::move(survived);

        //Dopuna: tragovi se gube, a rekonstrukciji trebaju tocke razasute po slici.
        //
        //TRAZI SE PO CIJELOJ SLICI, i to je mjereno a ne lijenost. Ocito je bilo traziti samo u
        //celijama koje su ostale prazne - na 4K se inace pretrazuje 8.3 milijuna piksela da bi se
        //uzelo pedeset tocaka. Izmjereno na pravoj snimci, 200 kadrova, uz celije po strani:
        //
        //   celija    tragova   opazanja   medijan duljine   vrijeme
        //     1        2871      114055        40 kadrova     18.8 s
        //     2        3680      123313        23 kadrova     20.2 s
        //     4        4161      117538        18 kadrova     26.2 s
        //     8        5081      108109        10 kadrova     52.9 s
        //
        //Gore na svakoj osi. Svaka celija mora izracunati i rubni pojas sirok prozor, pa se taj
        //posao ponavlja, a vlastiti prag po celiji propusta slabije uglove koji onda brze umru -
        //vise tragova, kraci tragovi, vise posla. Cijela slika odjednom je i najbrza i najbolja
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

                startTrack(corner);
            }
        }
    }

    //Samo stari, ulancani nacin treba prosli kadar. Sidrenom pracenju prosli kadar ne treba
    //uopce - ono gleda iskljucivo kadar rodjenja
    if(!config.affine) previousPyramid = std::move(current);

    ++frames;
}

}
