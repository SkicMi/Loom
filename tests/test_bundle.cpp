// S3: bundle adjustment - poses and points are fixed TOGETHER, through the Schur complement.
//
// PnP (S2) fixes the pose with motionless points, triangulation (S1) the points with
// motionless poses. Each trusts what it was given, so the error moves from one side to the
// other. Here both are unknowns: 6 per camera and 3 per point, for this scene 42 + 900.
//
// What is defended:
//
//   point Jacobian    against numeric derivative, per-row scale - the same rule that in
//                     S2 exposed a flipped sign
//   together is better   BA must give points notably closer to the truth than triangulation
//                     with the same (wrong) poses. That is why BA exists at all, so it is
//                     measured
//   without noise     everything must return to the truth up to rounding
//   with noise        reprojection falls to the noise level, and poses and points stay close
//   anchor rests      the fixed camera MUST NOT move by even a bit; without it the whole
//                     solution may slide through space and comparison with the truth loses
//                     meaning
//   without iterations   control that the steps do the work, not the initial guess itself
#include "TestHarness.h"

#include <Engine/Bundle.h>
#include <Engine/SyntheticScene.h>
#include <Engine/Triangulate.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double medianPointError(const std::vector<glm::vec3>& got, const std::vector<glm::vec3>& truth){
    std::vector<double> errors;
    for(size_t i = 0; i < truth.size() && i < got.size(); ++i) errors.push_back(double(glm::length(got[i] - truth[i])));
    return medianOf(errors);
}

double medianPoseError(const std::vector<Engine::Pose>& got, const std::vector<Engine::Pose>& truth){
    std::vector<double> errors;
    for(size_t i = 0; i < truth.size() && i < got.size(); ++i) errors.push_back(double(glm::length(got[i].position - truth[i].position)));
    return medianOf(errors);
}

//SCALE IS FREE. With camera 0 fixed one undetermined degree of freedom remains: scale every
//distance around it and no observation changes. Reprojection cannot tell those two scenes
//apart, so SHAPE is compared with the truth - scale is measured from the baseline toward the
//second camera and cancelled. In real work scale comes from outside (a known baseline or
//sensor), not from images
double scaleOf(const std::vector<Engine::Pose>& got, const std::vector<Engine::Pose>& truth){
    const double mine = double(glm::length(got[1].position - got[0].position));
    const double real = double(glm::length(truth[1].position - truth[0].position));
    return real > 0.0 ? mine / real : 1.0;
}

glm::vec3 unscale(const glm::vec3& value, const glm::vec3& anchor, double scale){
    return anchor + (value - anchor) / float(scale);
}

//A disturbance that is always the same: cameras 1.. off to the side and slightly rotated,
//points scattered around the truth. Camera 0 is NOT touched - it is the anchor, and had it
//been moved and then fixed, the whole solution would legitimately settle into its wrong
//frame
void disturb(std::vector<Engine::Pose>& poses, std::vector<glm::vec3>& points){
    for(size_t i = 1; i < poses.size(); ++i){
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        poses[i].position += poses[i].orientation * glm::vec3(0.05f * sign, -0.03f, 0.04f * sign);
        poses[i].orientation = glm::normalize(poses[i].orientation *
            glm::angleAxis(glm::radians(1.0f * sign), glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f))));
    }
    for(size_t i = 0; i < points.size(); ++i){
        const float a = float((i * 37) % 19) / 19.0f - 0.5f;
        const float b = float((i * 53) % 23) / 23.0f - 0.5f;
        const float c = float((i * 71) % 29) / 29.0f - 0.5f;
        points[i] += 0.06f * glm::vec3(a, b, c);
    }
}

uint64_t bundleHash(const Engine::BundleResult& result){
    uint64_t hash = 1469598103934665603ull;
    auto addFloat = [&](float value){
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for(unsigned shift = 0; shift < 32; shift += 8){
            hash ^= uint8_t(bits >> shift);
            hash *= 1099511628211ull;
        }
    };
    for(const Engine::Pose& pose : result.poses){
        addFloat(pose.position.x); addFloat(pose.position.y); addFloat(pose.position.z);
        addFloat(pose.orientation.w); addFloat(pose.orientation.x);
        addFloat(pose.orientation.y); addFloat(pose.orientation.z);
    }
    for(const glm::vec3& point : result.points){
        addFloat(point.x); addFloat(point.y); addFloat(point.z);
    }
    return hash;
}

}

int main(){
    TestReport report("S3 bundle adjustment");

    Engine::SyntheticConfig config;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    // -------------------------------------------------------------------------------
    // Point Jacobian against numeric derivative
    // -------------------------------------------------------------------------------

    {
        const double h = 1e-3;
        double worst = 0.0;
        size_t compared = 0;

        for(size_t i = 0; i < clean.observations.size(); i += 91){
            const Engine::Observation& observation = clean.observations[i];
            const Engine::Pose& pose = clean.poses[observation.camera];
            //Point deliberately moved off the truth, so residuals are not zero
            const glm::vec3 point = clean.points[observation.point] + glm::vec3(0.04f, -0.03f, 0.05f);

            double residual[2], jacobian[2][3];
            if(!Engine::pointJacobian(pose, clean.intrinsics, point, observation.pixel, residual, jacobian)) continue;

            double rowScale[2] = {1.0, 1.0};
            for(int row = 0; row < 2; ++row){
                for(int column = 0; column < 3; ++column) rowScale[row] = std::max(rowScale[row], std::fabs(jacobian[row][column]));
            }

            for(int parameter = 0; parameter < 3; ++parameter){
                glm::vec3 step(0.0f);
                step[parameter] = float(h);

                double plus[2], minus[2], ignored[2][3];
                if(!Engine::pointJacobian(pose, clean.intrinsics, point + step, observation.pixel, plus, ignored)) continue;
                if(!Engine::pointJacobian(pose, clean.intrinsics, point - step, observation.pixel, minus, ignored)) continue;

                for(int row = 0; row < 2; ++row){
                    const double numeric = (plus[row] - minus[row]) / (2.0 * h);
                    worst = std::max(worst, std::fabs(numeric - jacobian[row][parameter]) / rowScale[row]);
                    ++compared;
                }
            }
        }

        report.check("jakobijan po tocki se slaze s numerickom derivacijom", compared > 30 && worst < 1e-3,
            fmt("najveca relativna razlika %.2e kroz %zu clanova", worst, compared));
    }

    // -------------------------------------------------------------------------------
    // Without noise: everything returns to the truth
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        const double poseBefore = medianPoseError(poses, clean.poses);
        const double pointBefore = medianPointError(points, clean.points);

        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics);


        //Scale is measured and cancelled before comparison - see scaleOf
        const double scale = scaleOf(result.poses, clean.poses);
        std::vector<Engine::Pose> aligned = result.poses;
        std::vector<glm::vec3> alignedPoints = result.points;
        for(Engine::Pose& pose : aligned) pose.position = unscale(pose.position, result.poses[0].position, scale);
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);

        const double poseAfter = medianPoseError(aligned, clean.poses);
        const double pointAfter = medianPointError(alignedPoints, clean.points);

        report.check("bez suma se oblik vraca na istinu",
            result.solved && result.endMedian < 1e-2 && poseAfter < 1e-4 && pointAfter < 1e-4,
            fmt("poze %.4f -> %.2e m, tocke %.4f -> %.2e m, mjerilo %.6f, reprojekcija %.2f -> %.2e px kroz %u koraka",
                poseBefore, poseAfter, pointBefore, pointAfter, scale, result.startMedian, result.endMedian, result.iterations));

        //Anchor: bit for bit, because a fixed camera has no unknowns
        report.check("sidro miruje",
            result.poses[0].position == clean.poses[0].position && result.poses[0].orientation == clean.poses[0].orientation,
            "prva kamera je ostala tocno gdje je bila");

        const double classified = result.timing.costSeconds + result.timing.linearizeSeconds +
            result.timing.schurSeconds + result.timing.denseSolveSeconds +
            result.timing.backSubstituteSeconds;
        report.check("telemetrija pokriva stvarni bundle",
            result.iterations > 0 && result.timing.totalSeconds > 0.0 &&
            result.timing.linearizeSeconds > 0.0 && result.timing.schurSeconds > 0.0 &&
            result.timing.denseSolveSeconds > 0.0 && classified <= result.timing.totalSeconds * 1.01,
            fmt("ukupno %.6f s, klasificirano %.6f s kroz %u koraka",
                result.timing.totalSeconds, classified, result.iterations));

        report.check("bundle je bit-identican",
            bundleHash(result) == 4084565953268247014ull,
            fmt("hash %llu", static_cast<unsigned long long>(bundleHash(result))));

    }

    // -------------------------------------------------------------------------------
    // Together is better than each step alone
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        //Triangulation with those same (wrong) poses - the best S1 alone can do
        std::vector<uint8_t> solved;
        const std::vector<glm::vec3> triangulated = Engine::triangulateAll(clean.observations, poses,
                                                                           clean.intrinsics, clean.points.size(), solved);
        const double fromTriangulation = medianPointError(triangulated, clean.points);

        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics);
        const double scale = scaleOf(result.poses, clean.poses);
        std::vector<glm::vec3> alignedPoints = result.points;
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);
        const double fromBundle = medianPointError(alignedPoints, clean.points);

        report.check("zajedno je bolje nego samo triangulacija",
            fromBundle < 0.1 * fromTriangulation,
            fmt("tocke %.4f m s krivim pozama, %.2e m kad se poze popravljaju zajedno", fromTriangulation, fromBundle));
    }

    // -------------------------------------------------------------------------------
    // With noise
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticConfig noisyConfig = config;
        noisyConfig.noisePixels = 0.5f;
        const Engine::SyntheticScene noisy = Engine::makeSyntheticScene(noisyConfig);

        std::vector<Engine::Pose> poses = noisy.poses;
        std::vector<glm::vec3> points = noisy.points;
        disturb(poses, points);

        const Engine::BundleResult result = Engine::bundleAdjust(noisy.observations, poses, points, noisy.intrinsics);

        const double scale = scaleOf(result.poses, noisy.poses);
        std::vector<Engine::Pose> aligned = result.poses;
        std::vector<glm::vec3> alignedPoints = result.points;
        for(Engine::Pose& pose : aligned) pose.position = unscale(pose.position, result.poses[0].position, scale);
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);

        const double poseAfter = medianPoseError(aligned, noisy.poses);
        const double pointAfter = medianPointError(alignedPoints, noisy.points);

        //Reprojection must not fall below the noise: below it there is nothing to be, and a solver
        //claiming it actually bent the scene around the noise
        report.check("sa sumom: reprojekcija na razini suma, poze i tocke blizu",
            result.solved && result.endMedian > 0.2 && result.endMedian < 1.0 && poseAfter < 0.01 && pointAfter < 0.05,
            fmt("poze %.4f m, tocke %.4f m, reprojekcija %.2f -> %.3f px", poseAfter, pointAfter, result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
// Speed: point substitution must know about the camera moves
// -------------------------------------------------------------------------------
    //
    //A MUTATION ASKED FOR THIS CHECK TOO. When the E' dc term is dropped from the substitution
    //- that is, when points are fixed with no regard for how the cameras moved - the solver
    //still reaches the same answer, because damping only accepts steps that reduce the error.
    //What differs is SPEED:
    //
    //   steps     exact          regardless of cameras
    //     1     2.78e-02 px            2.48 px
    //     2     1.17e-04               0.46
    //     3     2.40e-05               0.27
    //     6     2.40e-05               0.044
    //
    //Threshold 1e-3 is about forty times above the exact and two hundred times below the
    //mutated

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        Engine::BundleConfig three;
        three.maxIterations = 3;
        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics, three);

        report.check("tri koraka su dosta", result.endMedian < 1e-3,
            fmt("reprojekcija %.2f -> %.2e px u tri koraka", result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Control: without iterations nothing changes
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        Engine::BundleConfig none;
        none.maxIterations = 0;
        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics, none);

        report.check("bez iteracija nema popravka",
            result.endMedian == result.startMedian && result.points == points && result.startMedian > 1.0,
            fmt("reprojekcija ostala %.2f px", result.endMedian));
    }

    return report.result();
}
