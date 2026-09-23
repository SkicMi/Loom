#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Engine::WeaverMotion{

constexpr const char* poweredBy = "NVIDIA Kimodo";

struct Joint{
    std::string name;
    int parent = -1;
    glm::vec3 offset{0.0f};
};

struct Pose{
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
};

struct Clip{
    std::vector<Joint> joints;
    std::vector<Pose> frames;
    double framesPerSecond = 30.0;

    bool empty() const {return joints.empty() || frames.empty();}
    size_t jointCount() const {return joints.size();}
    size_t frameCount() const {return frames.size();}
};

struct BvhImportOptions{
    // Kimodo's SOMA BVH exporter writes offsets and root motion in centimeters.
    float lengthScale = 0.01f;
};

bool parseKimodoBvh(const std::string& text, Clip& clip, std::string& error,
                    BvhImportOptions options = {});
bool readKimodoBvh(const std::string& path, Clip& clip, std::string& error,
                   BvhImportOptions options = {});

}
