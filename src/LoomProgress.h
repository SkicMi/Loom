#pragma once
#include <Treadle/Draw.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Loom{

//=============================================================================================
// ZIVI SNIMAK: sto je solver dosad nasao.
//
// VideoSolve povremeno zapise kamere i tocke u napredak.bin (atomski, pa se nikad ne uhvati
// polovica). Ovdje se to cita i crta - bez ijednog novog shadera, jer Treadleov DrawList ima
// pravokutnik, a tocka na ekranu je mali pravokutnik.
//
// SREDISTE I MJERILO IDU PO POSTOTCIMA, ne po min/max. Rekonstrukcija redovito ima pokoju tocku
// trianguliranu iz gotovo paralelnih zraka; jedna takva na 1e5 pomakne min/max za pet redova
// velicine i scena se skupi u jedan piksel
//=============================================================================================
struct Snapshot{
    std::vector<glm::vec3> cameras;
    std::vector<glm::vec3> points;
};

bool readSnapshot(const std::string& path, Snapshot& out){
    std::ifstream file(path, std::ios::binary);
    if(!file) return false;

    char magic[8] = {0};
    file.read(magic, 8);
    if(std::string(magic, 8) != "LOOMPRG1") return false;

    uint32_t cameraCount = 0, pointCount = 0;
    file.read(reinterpret_cast<char*>(&cameraCount), 4);
    file.read(reinterpret_cast<char*>(&pointCount), 4);
    if(!file || cameraCount > 100000 || pointCount > 20000000) return false;

    out.cameras.resize(cameraCount);
    out.points.resize(pointCount);
    if(cameraCount) file.read(reinterpret_cast<char*>(out.cameras.data()), cameraCount * 12);
    if(pointCount) file.read(reinterpret_cast<char*>(out.points.data()), pointCount * 12);
    return bool(file);
}

//Crta oblak i kamere u zadani DrawList. Vraca koliko je tocaka stvarno palo u kadar
uint32_t paintSnapshot(const Snapshot& snapshot, Treadle::DrawList& list,
                       float width, float height, float angle){
    if(snapshot.points.size() < 8) return 0;

    std::vector<float> xs, ys, zs;
    xs.reserve(snapshot.points.size());
    for(const glm::vec3& point : snapshot.points){ xs.push_back(point.x); ys.push_back(point.y); zs.push_back(point.z); }
    auto at = [](std::vector<float>& values, double fraction){
        const size_t index = size_t(fraction * double(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + long(index), values.end());
        return values[index];
    };
    const glm::vec3 low(at(xs, 0.05), at(ys, 0.05), at(zs, 0.05));
    const glm::vec3 high(at(xs, 0.95), at(ys, 0.95), at(zs, 0.95));
    const glm::vec3 centre = (low + high) * 0.5f;
    const float radius = std::max(0.001f, glm::length(high - low) * 0.5f);

    //Pogled kruzi oko scene. Nista se ne moze kliknuti niti pomaknuti - ovo je prozor u posao koji
    //traje, a ne preglednik; preglednik je SplatViewer i otvara se na kraju
    const float distance = radius * 3.2f;
    const glm::vec3 eye = centre + glm::vec3(std::cos(angle) * distance,
                                             radius * 0.7f,
                                             std::sin(angle) * distance);
    const glm::vec3 forward = glm::normalize(centre - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);

    const float focal = height * 0.9f;
    uint32_t drawn = 0;

    auto place = [&](const glm::vec3& world, float& x, float& y)->bool{
        const glm::vec3 offset = world - eye;
        const float depth = glm::dot(offset, forward);
        if(depth <= 0.01f) return false;
        x = width * 0.5f + focal * glm::dot(offset, right) / depth;
        y = height * 0.5f - focal * glm::dot(offset, up) / depth;
        return x > -50.0f && x < width + 50.0f && y > -50.0f && y < height + 50.0f;
    };

    for(const glm::vec3& point : snapshot.points){
        float x = 0.0f, y = 0.0f;
        if(!place(point, x, y)) continue;
        list.rect(x, y, 1.6f, 1.6f, Treadle::Color{0.62f, 0.66f, 0.72f, 0.55f});
        ++drawn;
    }

    //Kamere preko tocaka, i vece - one su ono sto se prati dok raste
    for(const glm::vec3& camera : snapshot.cameras){
        float x = 0.0f, y = 0.0f;
        if(!place(camera, x, y)) continue;
        list.rect(x - 2.0f, y - 2.0f, 4.5f, 4.5f, Treadle::Color{1.0f, 0.72f, 0.25f, 0.95f});
    }
    return drawn;
}

}
