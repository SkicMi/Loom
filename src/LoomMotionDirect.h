#pragma once
// Editable SOMA30 constraint skeleton. Offsets are from Kimodo's neutral_joints.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Loom{
struct MotionDirectBone{ const char* name; int parent; glm::vec3 offset; };
inline const std::array<MotionDirectBone, 30>& motionDirectBones(){
    static const std::array<MotionDirectBone, 30> bones{{
    MotionDirectBone{"Hips", -1, glm::vec3(0.00000000f, 0.00000000f, 0.00000000f)},
    MotionDirectBone{"Spine1", 0, glm::vec3(-0.00013727f, 0.05003763f, -0.00053727f)},
    MotionDirectBone{"Spine2", 1, glm::vec3(-0.00000000f, 0.07125301f, -0.00029825f)},
    MotionDirectBone{"Chest", 2, glm::vec3(-0.00000001f, 0.07550063f, -0.00815971f)},
    MotionDirectBone{"Neck1", 3, glm::vec3(-0.00181677f, 0.26311295f, -0.00553348f)},
    MotionDirectBone{"Neck2", 4, glm::vec3(-0.00000003f, 0.07709397f, 0.02302585f)},
    MotionDirectBone{"Head", 5, glm::vec3(-0.00000005f, 0.06128916f, 0.01953709f)},
    MotionDirectBone{"Jaw", 6, glm::vec3(0.00002637f, 0.00475592f, 0.03094941f)},
    MotionDirectBone{"LeftEye", 6, glm::vec3(0.03206381f, 0.05380205f, 0.07586883f)},
    MotionDirectBone{"RightEye", 6, glm::vec3(-0.03222440f, 0.05361869f, 0.07558234f)},
    MotionDirectBone{"LeftShoulder", 3, glm::vec3(0.01621652f, 0.23237164f, 0.05113413f)},
    MotionDirectBone{"LeftArm", 10, glm::vec3(0.14919846f, 0.00000002f, -0.05502326f)},
    MotionDirectBone{"LeftForeArm", 11, glm::vec3(0.28739308f, 0.00000000f, -0.00002588f)},
    MotionDirectBone{"LeftHand", 12, glm::vec3(0.27093981f, -0.00000001f, 0.00002609f)},
    MotionDirectBone{"LeftHandThumbEnd", 13, glm::vec3(0.12268627f, -0.03220176f, 0.04833069f)},
    MotionDirectBone{"LeftHandMiddleEnd", 13, glm::vec3(0.19011960f, -0.00312878f, -0.00033957f)},
    MotionDirectBone{"RightShoulder", 3, glm::vec3(-0.01380118f, 0.23180309f, 0.05214158f)},
    MotionDirectBone{"RightArm", 16, glm::vec3(-0.15037196f, 0.00000012f, -0.05545604f)},
    MotionDirectBone{"RightForeArm", 17, glm::vec3(-0.28736639f, 0.00000002f, -0.00002597f)},
    MotionDirectBone{"RightHand", 18, glm::vec3(-0.27133620f, -0.00000000f, 0.00002613f)},
    MotionDirectBone{"RightHandThumbEnd", 19, glm::vec3(-0.12264248f, -0.03211454f, 0.04804039f)},
    MotionDirectBone{"RightHandMiddleEnd", 19, glm::vec3(-0.19000594f, -0.00306616f, -0.00031573f)},
    MotionDirectBone{"LeftLeg", 0, glm::vec3(0.10043214f, -0.08434527f, 0.02595655f)},
    MotionDirectBone{"LeftShin", 22, glm::vec3(-0.00000001f, -0.43221754f, -0.00802913f)},
    MotionDirectBone{"LeftFoot", 23, glm::vec3(0.00000001f, -0.42155096f, -0.03481523f)},
    MotionDirectBone{"LeftToeBase", 24, glm::vec3(0.00000000f, -0.05059472f, 0.13231529f)},
    MotionDirectBone{"RightLeg", 0, glm::vec3(-0.10047278f, -0.08295260f, 0.02620317f)},
    MotionDirectBone{"RightShin", 26, glm::vec3(0.00000001f, -0.43362206f, -0.00805556f)},
    MotionDirectBone{"RightFoot", 27, glm::vec3(0.00000002f, -0.42117394f, -0.03478398f)},
    MotionDirectBone{"RightToeBase", 28, glm::vec3(-0.00000000f, -0.05079609f, 0.13284196f)},
    }};
    return bones;
}
struct MotionPoseConstraint{
    int frame = 0;
    float rootHeight = 1.0f;
    std::array<glm::vec3, 30> rotationDegrees{};
    MotionPoseConstraint(){ rotationDegrees.fill(glm::vec3(0.0f)); }
};
inline int motionDirectMirrorBone(int index){
    static constexpr int pairs[] = {0,1,2,3,4,5,6,7,9,8,16,17,18,19,20,21,10,11,12,13,14,15,
                                    26,27,28,29,22,23,24,25};
    return index >= 0 && index < 30 ? pairs[index] : index;
}
inline std::array<glm::quat, 30> motionDirectGlobalRotations(const MotionPoseConstraint& pose){
    std::array<glm::quat, 30> rotations{};
    const auto& bones = motionDirectBones();
    for(size_t i = 0; i < bones.size(); ++i){
        const glm::quat local = glm::normalize(glm::quat(glm::radians(pose.rotationDegrees[i])));
        const int parent = bones[i].parent;
        rotations[i] = parent < 0 ? local : glm::normalize(rotations[size_t(parent)] * local);
    }
    return rotations;
}
inline std::array<glm::vec3, 30> motionDirectJointPositions(const MotionPoseConstraint& pose){
    std::array<glm::vec3, 30> positions{};
    const auto rotations = motionDirectGlobalRotations(pose);
    const auto& bones = motionDirectBones();
    for(size_t i = 0; i < bones.size(); ++i){
        const int parent = bones[i].parent;
        positions[i] = parent < 0 ? glm::vec3(0.0f, pose.rootHeight, 0.0f)
                                  : positions[size_t(parent)] + rotations[size_t(parent)] * bones[i].offset;
    }
    return positions;
}
inline glm::quat motionDirectRotationBetween(const glm::vec3& from, const glm::vec3& to){
    const float fromLength2 = glm::dot(from, from), toLength2 = glm::dot(to, to);
    if(fromLength2 < 1e-10f || toLength2 < 1e-10f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 a = from / std::sqrt(fromLength2), b = to / std::sqrt(toLength2);
    const float cosine = std::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if(cosine < -0.9999f){
        glm::vec3 axis = glm::cross(a, std::fabs(a.x) < 0.8f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        const float axisLength2 = glm::dot(axis, axis);
        if(axisLength2 < 1e-10f) axis = glm::cross(a, glm::vec3(0, 0, 1));
        return glm::angleAxis(3.14159265359f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}
inline bool motionDirectSolveTwoBoneIK(MotionPoseConstraint& pose, int upper, int middle, int end,
                                       const glm::vec3& target, const glm::vec3& pole){
    const auto& bones = motionDirectBones();
    if(upper < 0 || middle < 0 || end < 0 || upper >= int(bones.size()) ||
       middle >= int(bones.size()) || end >= int(bones.size()) ||
       bones[middle].parent != upper || bones[end].parent != middle) return false;
    const float upperLength = glm::length(bones[middle].offset);
    const float lowerLength = glm::length(bones[end].offset);
    if(upperLength < 1e-5f || lowerLength < 1e-5f) return false;

    const auto initialPositions = motionDirectJointPositions(pose);
    auto initialRotations = motionDirectGlobalRotations(pose);
    const glm::vec3 start = initialPositions[size_t(upper)];
    glm::vec3 toTarget = target - start;
    float distance = glm::length(toTarget);
    if(distance < 1e-5f){
        toTarget = initialPositions[size_t(end)] - start;
        distance = glm::length(toTarget);
    }
    if(distance < 1e-5f) return false;
    const glm::vec3 direction = toTarget / distance;
    distance = std::clamp(distance, std::fabs(upperLength - lowerLength) + 1e-4f,
                           upperLength + lowerLength - 1e-4f);

    glm::vec3 bend = pole - start;
    bend -= direction * glm::dot(bend, direction);
    if(glm::dot(bend, bend) < 1e-8f){
        bend = initialPositions[size_t(middle)] - start;
        bend -= direction * glm::dot(bend, direction);
    }
    if(glm::dot(bend, bend) < 1e-8f){
        bend = glm::cross(direction, std::fabs(direction.y) < 0.9f
            ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
    }
    bend = glm::normalize(bend);
    const float along = (upperLength * upperLength - lowerLength * lowerLength + distance * distance) /
                        (2.0f * distance);
    const float height = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
    const glm::vec3 elbowTarget = start + direction * along + bend * height;

    const int upperParent = bones[size_t(upper)].parent;
    const glm::quat upperDelta = motionDirectRotationBetween(
        initialRotations[size_t(upper)] * bones[size_t(middle)].offset, elbowTarget - start);
    const glm::quat desiredUpperGlobal = glm::normalize(upperDelta * initialRotations[size_t(upper)]);
    const glm::quat upperParentGlobal = upperParent < 0
        ? glm::quat(1, 0, 0, 0) : initialRotations[size_t(upperParent)];
    const glm::quat upperLocal = glm::normalize(glm::inverse(upperParentGlobal) * desiredUpperGlobal);
    pose.rotationDegrees[size_t(upper)] = glm::clamp(glm::degrees(glm::eulerAngles(upperLocal)),
                                                       glm::vec3(-180.0f), glm::vec3(180.0f));

    initialRotations = motionDirectGlobalRotations(pose);
    const glm::quat lowerDelta = motionDirectRotationBetween(
        initialRotations[size_t(middle)] * bones[size_t(end)].offset, target - elbowTarget);
    const glm::quat desiredLowerGlobal = glm::normalize(lowerDelta * initialRotations[size_t(middle)]);
    const glm::quat lowerLocal = glm::normalize(glm::inverse(initialRotations[size_t(upper)]) * desiredLowerGlobal);
    pose.rotationDegrees[size_t(middle)] = glm::clamp(glm::degrees(glm::eulerAngles(lowerLocal)),
                                                        glm::vec3(-180.0f), glm::vec3(180.0f));
    return true;
}

enum class MotionDirectControlKind{ PathRoot, RotateJoint, LimbTarget, Pole };
struct MotionDirectControl{
    const char* label;
    int joint;
    MotionDirectControlKind kind;
    int upper = -1, middle = -1, end = -1;
};
inline const std::array<MotionDirectControl, 11>& motionDirectControls(){
    static const std::array<MotionDirectControl, 11> controls{{
        {"ROOT", 0, MotionDirectControlKind::PathRoot},
        {"CHEST", 3, MotionDirectControlKind::RotateJoint},
        {"HEAD", 6, MotionDirectControlKind::RotateJoint},
        {"L HAND", 13, MotionDirectControlKind::LimbTarget, 11, 12, 13},
        {"R HAND", 19, MotionDirectControlKind::LimbTarget, 17, 18, 19},
        {"L ELBOW", 12, MotionDirectControlKind::Pole, 11, 12, 13},
        {"R ELBOW", 18, MotionDirectControlKind::Pole, 17, 18, 19},
        {"L FOOT", 24, MotionDirectControlKind::LimbTarget, 22, 23, 24},
        {"R FOOT", 28, MotionDirectControlKind::LimbTarget, 26, 27, 28},
        {"L KNEE", 23, MotionDirectControlKind::Pole, 22, 23, 24},
        {"R KNEE", 27, MotionDirectControlKind::Pole, 26, 27, 28},
    }};
    return controls;
}
inline std::string motionDirectPoseProblem(const std::vector<MotionPoseConstraint>& poses, int lastFrame){
    int previous = -1;
    if(poses.size() > 20) return "Use at most 20 sparse pose keys.";
    for(const auto& pose : poses){
        if(pose.frame <= previous || pose.frame < 0 || pose.frame > lastFrame)
            return "Pose frames must be unique, increasing and inside the clip.";
        if(!std::isfinite(pose.rootHeight) || pose.rootHeight < 0.2f || pose.rootHeight > 2.5f)
            return "Pose root height must be between 0.2 and 2.5 m.";
        for(const glm::vec3& angles : pose.rotationDegrees)
            for(int axis = 0; axis < 3; ++axis) if(!std::isfinite(angles[axis])) return "Bone angles must be finite.";
        previous = pose.frame;
    }
    return {};
}
}
