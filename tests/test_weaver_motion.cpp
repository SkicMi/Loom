#include "TestHarness.h"
#include <Engine/WeaverMotion.h>
#include "../src/LoomWeaverMotion.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace{

float angleDistance(const glm::quat& a, const glm::quat& b){
    const glm::quat target = glm::normalize(glm::dot(a, b) < 0.0f ? -b : b);
    return glm::angle(target * glm::inverse(glm::normalize(a)));
}

}

int main(){
    TestReport report("weaver_motion");

    report.check("identitet", std::string(Engine::WeaverMotion::poweredBy) == "NVIDIA Kimodo",
                 Engine::WeaverMotion::poweredBy);

    const std::string unsafePrompt = std::string("walk ") + char(39) + "now" + char(39) + "; $(touch /tmp/should-not-run)";
    const std::string expectedQuoted = std::string(1, char(39)) + "walk " + char(39) + char(92) + char(39) + char(39) +
                                       "now" + char(39) + char(92) + char(39) + char(39) +
                                       "; $(touch /tmp/should-not-run)" + char(39);
    report.check("shell argument quoting", Loom::shellQuoteArgument(unsafePrompt) == expectedQuoted,
                 Loom::shellQuoteArgument(unsafePrompt));

    const std::string shellCommand = "printf '%s' " + Loom::shellQuoteArgument(unsafePrompt);
    FILE* shell = popen(shellCommand.c_str(), "r");
    std::string roundTrip;
    if(shell){
        char buffer[128];
        while(std::fgets(buffer, sizeof(buffer), shell)) roundTrip += buffer;
    }
    const int shellStatus = shell ? pclose(shell) : -1;
    report.check("shell quote round-trip against real /bin/sh",
                 shellStatus == 0 && roundTrip == unsafePrompt, roundTrip);

    const std::string generationCommand = Loom::buildWeaverMotionCommand(
        "/tmp/Kimodo runner/bin/kimodo_gen", unsafePrompt, 120.0f, "/tmp/motion output/clip");
    report.check("Kimodo command quotes values, clamps duration, and enables BVH",
                 generationCommand.find(" " + Loom::shellQuoteArgument("/tmp/Kimodo runner/bin/kimodo_gen") + " ") != std::string::npos &&
                 generationCommand.find(" " + Loom::shellQuoteArgument(unsafePrompt) + " --model ") != std::string::npos &&
                 generationCommand.find("--output " + Loom::shellQuoteArgument("/tmp/motion output/clip")) != std::string::npos &&
                 generationCommand.find("--duration 10.00") != std::string::npos &&
                 generationCommand.find("--bvh --bvh_standard_tpose") != std::string::npos,
                 generationCommand);

    const std::string bvh =
        "HIERARCHY\n"
        "ROOT Pelvis\n"
        "{\n"
        "  OFFSET 0 100 0\n"
        "  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
        "  JOINT Chest\n"
        "  {\n"
        "    OFFSET 0 50 0\n"
        "    CHANNELS 3 Zrotation Xrotation Yrotation\n"
        "    End Site\n"
        "    {\n"
        "      OFFSET 0 10 0\n"
        "    }\n"
        "  }\n"
        "}\n"
        "MOTION\n"
        "Frames: 2\n"
        "Frame Time: 0.0333333333333\n"
        "10 20 30 90 0 0  0 45 0\n"
        "11 22 33 0 0 90  0 0 30\n";

    Engine::WeaverMotion::Clip clip;
    std::string error;
    const bool ok = Engine::WeaverMotion::parseKimodoBvh(bvh, clip, error);
    report.check("parse", ok && error.empty(), error.empty() ? fmt("%zu zgloba", clip.jointCount()) : error);
    report.check("hijerarhija", clip.jointCount() == 2 && clip.joints[0].name == "Pelvis" &&
                                clip.joints[1].parent == 0 && clip.joints[1].offset.y == 0.5f,
                 fmt("%zu zgloba, parent %d, offset %.3f", clip.jointCount(),
                     clip.jointCount() > 1 ? clip.joints[1].parent : -99,
                     clip.jointCount() > 1 ? clip.joints[1].offset.y : -1.0f));
    report.check("vrijeme", clip.frameCount() == 2 && std::fabs(clip.framesPerSecond - 30.0) < 1e-7,
                 fmt("%zu frameova, %.6f fps", clip.frameCount(), clip.framesPerSecond));
    const bool hasRootMotion = clip.frames.size() == 2 && clip.frames[0].translations.size() == clip.jointCount() &&
                               clip.frames[1].translations.size() == clip.jointCount();
    const glm::vec3 firstRoot = hasRootMotion ? clip.frames[0].translations[0] : glm::vec3(-1.0f);
    report.check("root pomak", hasRootMotion &&
                                glm::length(firstRoot - glm::vec3(0.1f, 0.2f, 0.3f)) < 1e-6f &&
                                glm::length(clip.frames[1].translations[0] - glm::vec3(0.11f, 0.22f, 0.33f)) < 1e-6f,
                 fmt("f0 %.3f %.3f %.3f", firstRoot.x, firstRoot.y, firstRoot.z));

    const glm::quat z90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat child45 = glm::angleAxis(glm::radians(45.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const bool hasRotations = clip.frames.size() == 2 && clip.frames[0].rotations.size() == clip.jointCount() &&
                              clip.jointCount() == 2;
    const float rootAngle = hasRotations ? angleDistance(clip.frames[0].rotations[0], z90) : 999.0f;
    const float childAngle = hasRotations ? angleDistance(clip.frames[0].rotations[1], child45) : 999.0f;
    report.check("rotacije", rootAngle < 1e-5f && childAngle < 1e-5f,
                 fmt("root %.6f child %.6f", rootAngle, childAngle));

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom_weaver_motion_test.bvh";
    {
        std::ofstream file(path);
        file << bvh;
    }
    Engine::WeaverMotion::Clip fromFile;
    const bool read = Engine::WeaverMotion::readKimodoBvh(path.string(), fromFile, error);
    report.check("datoteka", read && fromFile.frameCount() == clip.frameCount() &&
                             fromFile.jointCount() == clip.jointCount(),
                 read ? fmt("%zu/%zu", fromFile.frameCount(), fromFile.jointCount()) : error);

    Engine::WeaverMotion::Clip badClip;
    const bool bad = Engine::WeaverMotion::parseKimodoBvh("HIERARCHY ROOT Pelvis { OFFSET 0 0 0 }", badClip, error);
    report.check("los bvh pada", !bad && !error.empty(), error);

    return report.result();
}
