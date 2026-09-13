#include "Engine/TwoView.h"
#include "Engine/Dense.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace Engine{
namespace{

//Nasa kamera gleda niz -Z s +Y gore; klasicna epipolarna geometrija gleda +Z s +Y dolje. Prijelaz
//je zrcaljenje po dvije osi, i samo sebi je inverz
const double mirror[3] = {1.0, -1.0, -1.0};

struct Vector3{
    double x = 0.0, y = 0.0, z = 0.0;
};

double dot(const Vector3& a, const Vector3& b){return a.x * b.x + a.y * b.y + a.z * b.z;}

Vector3 cross(const Vector3& a, const Vector3& b){
    return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vector3 normalized(const Vector3& v){
    const double length = std::sqrt(dot(v, v));
    return length > 0.0 ? Vector3{v.x / length, v.y / length, v.z / length} : v;
}

//Piksel -> smjer u klasicnoj konvenciji, na ravnini z = 1
Vector3 bearing(const glm::vec2& pixel, const Intrinsics& intrinsics){
    return Vector3{(double(pixel.x) - double(intrinsics.cx)) / double(intrinsics.fx),
                   (double(pixel.y) - double(intrinsics.cy)) / double(intrinsics.fy),
                   1.0};
}

void multiply(const double a[3][3], const double b[3][3], double out[3][3]){
    for(int i = 0; i < 3; ++i){
        for(int j = 0; j < 3; ++j){
            double sum = 0.0;
            for(int k = 0; k < 3; ++k) sum += a[i][k] * b[k][j];
            out[i][j] = sum;
        }
    }
}

void transpose(const double a[3][3], double out[3][3]){
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) out[i][j] = a[j][i];
}

double determinant(const double m[3][3]){
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

Vector3 apply(const double m[3][3], const Vector3& v){
    return Vector3{m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                   m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                   m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

//Tocka najbliza dvjema zrakama, u klasicnom sustavu prve kamere. Ista matematika kao Triangulate,
//samo bez poza - ovdje poza jos ne postoji
bool meet(const Vector3& originA, const Vector3& directionA,
          const Vector3& originB, const Vector3& directionB, Vector3& point){
    double A[3][3] = {};
    double b[3] = {};
    const Vector3 rays[2] = {directionA, directionB};
    const Vector3 origins[2] = {originA, originB};

    for(int r = 0; r < 2; ++r){
        const Vector3 d = normalized(rays[r]);
        const double p[3][3] = {
            {1.0 - d.x * d.x,      -d.x * d.y,      -d.x * d.z},
            {     -d.y * d.x, 1.0 - d.y * d.y,      -d.y * d.z},
            {     -d.z * d.x,      -d.z * d.y, 1.0 - d.z * d.z}
        };
        const Vector3 o = origins[r];
        for(int i = 0; i < 3; ++i){
            for(int j = 0; j < 3; ++j) A[i][j] += p[i][j];
            b[i] += p[i][0] * o.x + p[i][1] * o.y + p[i][2] * o.z;
        }
    }

    double inverse[3][3];
    if(!invert3(A, inverse)) return false;
    point = Vector3{inverse[0][0] * b[0] + inverse[0][1] * b[1] + inverse[0][2] * b[2],
                    inverse[1][0] * b[0] + inverse[1][1] * b[1] + inverse[1][2] * b[2],
                    inverse[2][0] * b[0] + inverse[2][1] * b[1] + inverse[2][2] * b[2]};
    return true;
}

//Esencijalna matrica iz zadanog PODSKUPA parova: RANSAC racuna iz osam nasumicnih, a zavrsni
//prolaz iz svih koji su se s njima slozili
bool essentialFrom(const std::vector<Vector3>& a, const std::vector<Vector3>& b,
                   const std::vector<uint32_t>& indices, double E[3][3]){
    if(indices.size() < 8){
        return false;   //osam parova je najmanje sto devet clanova E moze odrediti
    }

    std::vector<double> normal(81, 0.0);
    for(uint32_t i : indices){
        const double row[9] = {
            b[i].x * a[i].x, b[i].x * a[i].y, b[i].x,
            b[i].y * a[i].x, b[i].y * a[i].y, b[i].y,
            a[i].x,          a[i].y,          1.0
        };
        for(int p = 0; p < 9; ++p){
            for(int q = 0; q < 9; ++q) normal[size_t(p * 9 + q)] += row[p] * row[q];
        }
    }

    std::vector<double> solution;
    if(!smallestEigenvector(normal, 9, solution)){
        return false;
    }
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) E[i][j] = solution[size_t(i * 3 + j)];
    return true;
}

//Poza iz esencijalne matrice. Cetiri kandidata, a bira se onaj kojemu tocke leze ISPRED obje
//kamere: algebra ne razlikuje kameru koja gleda scenu od one koja joj je okrenuta ledjima
bool poseFromEssential(const double E[3][3], const std::vector<Vector3>& a, const std::vector<Vector3>& b,
                       const std::vector<uint32_t>& indices, Pose& pose, uint32_t& inFront){
    //SVD od E preko svojstvenog rastava E'E: V su svojstveni vektori, a U se dobije kao E v
    double Et[3][3], EtE[3][3];
    transpose(E, Et);
    multiply(Et, E, EtE);

    double values[3], V[3][3];
    symmetricEigen3(EtE, values, V);
    if(values[1] <= 1e-18){
        return false;   //degenerirano: sve tocke na pravcu ili u jednoj tocki
    }

    double U[3][3];
    for(int column = 0; column < 2; ++column){
        const Vector3 v{V[0][column], V[1][column], V[2][column]};
        const Vector3 u = normalized(apply(E, v));
        U[0][column] = u.x; U[1][column] = u.y; U[2][column] = u.z;
    }
    const Vector3 u1{U[0][0], U[1][0], U[2][0]};
    const Vector3 u2{U[0][1], U[1][1], U[2][1]};
    const Vector3 u3 = normalized(cross(u1, u2));
    U[0][2] = u3.x; U[1][2] = u3.y; U[2][2] = u3.z;

    //Desni sustav na obje strane; inace ispadne zrcaljenje umjesto rotacije
    if(determinant(U) < 0.0){ for(int i = 0; i < 3; ++i) U[i][2] = -U[i][2]; }
    if(determinant(V) < 0.0){ for(int i = 0; i < 3; ++i) V[i][2] = -V[i][2]; }

    const double W[3][3] = {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    double Wt[3][3], Vt[3][3], UW[3][3], UWt[3][3], R1[3][3], R2[3][3];
    transpose(W, Wt);
    transpose(V, Vt);
    multiply(U, W, UW);
    multiply(UW, Vt, R1);
    multiply(U, Wt, UWt);
    multiply(UWt, Vt, R2);

    if(determinant(R1) < 0.0) for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) R1[i][j] = -R1[i][j];
    if(determinant(R2) < 0.0) for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) R2[i][j] = -R2[i][j];

    const Vector3 translation{U[0][2], U[1][2], U[2][2]};

    struct Candidate{
        const double (*rotation)[3];
        double sign;
    };
    const Candidate candidates[4] = {{R1, 1.0}, {R1, -1.0}, {R2, 1.0}, {R2, -1.0}};

    int best = -1;
    uint32_t bestInFront = 0;

    for(int c = 0; c < 4; ++c){
        const double (*R)[3] = candidates[c].rotation;
        const Vector3 t{translation.x * candidates[c].sign,
                        translation.y * candidates[c].sign,
                        translation.z * candidates[c].sign};

        double Rt[3][3];
        transpose(R, Rt);
        const Vector3 centre = apply(Rt, Vector3{-t.x, -t.y, -t.z});

        uint32_t frontCount = 0;
        for(uint32_t i : indices){
            Vector3 point;
            if(!meet(Vector3{0.0, 0.0, 0.0}, a[i], centre, apply(Rt, b[i]), point)) continue;
            if(point.z <= 0.0) continue;   //ispred prve kamere: u klasicnoj konvenciji z raste naprijed

            const Vector3 inSecond = apply(R, point);
            if(inSecond.z + t.z > 0.0) ++frontCount;   //i ispred druge
        }

        if(frontCount > bestInFront){
            bestInFront = frontCount;
            best = c;
        }
    }

    if(best < 0){
        return false;
    }

    const double (*R)[3] = candidates[best].rotation;
    const Vector3 t{translation.x * candidates[best].sign,
                    translation.y * candidates[best].sign,
                    translation.z * candidates[best].sign};

    //Iz klasicne natrag u nasu: x_B = R x_A + t vrijedi u klasicnoj, a nasa je zrcaljena po dvije
    //osi. Iz x_camB = R_p'(x_svijet - c) slijedi R_p = M R' M i c = -M R' t
    double Rt[3][3], poseRotation[3][3];
    transpose(R, Rt);
    for(int i = 0; i < 3; ++i){
        for(int j = 0; j < 3; ++j) poseRotation[i][j] = mirror[i] * Rt[i][j] * mirror[j];
    }
    const Vector3 centre = apply(Rt, t);

    glm::mat3 rotation(1.0f);
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) rotation[j][i] = float(poseRotation[i][j]);

    pose.orientation = glm::normalize(glm::quat_cast(rotation));
    pose.position = glm::vec3(float(-mirror[0] * centre.x), float(-mirror[1] * centre.y), float(-mirror[2] * centre.z));
    inFront = bestInFront;
    return true;
}

//Sampsonova udaljenost: koliko par promasuje epipolarni uvjet, u pikselima. Prava mjera bila bi
//udaljenost do ispravljenog para, a ovo je njezina prva aproksimacija - standardna, jer se racuna
//bez ijedne iteracije
double sampsonPixels(const double E[3][3], const Vector3& a, const Vector3& b, double focal){
    const Vector3 Ea = apply(E, a);
    double Et[3][3];
    transpose(E, Et);
    const Vector3 Etb = apply(Et, b);

    const double numerator = dot(b, Ea);
    const double denominator = Ea.x * Ea.x + Ea.y * Ea.y + Etb.x * Etb.x + Etb.y * Etb.y;
    if(denominator <= 0.0) return 1e9;
    return focal * std::fabs(numerator) / std::sqrt(denominator);
}

}

TwoViewResult relativePose(const std::vector<glm::vec2>& pixelsA,
                           const std::vector<glm::vec2>& pixelsB,
                           const Intrinsics& intrinsics){
    TwoViewResult result;
    const size_t count = std::min(pixelsA.size(), pixelsB.size());
    if(count < 8) return result;

    std::vector<Vector3> a(count), b(count);
    std::vector<uint32_t> all(count);
    for(size_t i = 0; i < count; ++i){
        a[i] = bearing(pixelsA[i], intrinsics);
        b[i] = bearing(pixelsB[i], intrinsics);
        all[i] = uint32_t(i);
    }
    result.used = uint32_t(count);

    double E[3][3];
    if(!essentialFrom(a, b, all, E)) return result;
    if(!poseFromEssential(E, a, b, all, result.pose, result.inFront)) return result;

    result.solved = true;
    return result;
}

TwoViewResult relativePoseRobust(const std::vector<glm::vec2>& pixelsA,
                                 const std::vector<glm::vec2>& pixelsB,
                                 const Intrinsics& intrinsics,
                                 const RansacConfig& config){
    TwoViewResult result;
    const size_t count = std::min(pixelsA.size(), pixelsB.size());
    if(count < 8) return result;

    std::vector<Vector3> a(count), b(count);
    for(size_t i = 0; i < count; ++i){
        a[i] = bearing(pixelsA[i], intrinsics);
        b[i] = bearing(pixelsB[i], intrinsics);
    }
    result.used = uint32_t(count);

    const double focal = 0.5 * (double(intrinsics.fx) + double(intrinsics.fy));

    std::mt19937 random(config.seed);
    std::vector<uint32_t> sample(8);
    std::vector<uint8_t> bestInliers;
    uint32_t bestCount = 0;

    for(uint32_t iteration = 0; iteration < config.iterations; ++iteration){
        //Osam RAZLICITIH parova: ponavljanje bi dalo degeneriran uzorak, a ne gresku
        for(int k = 0; k < 8; ++k){
            bool fresh = false;
            while(!fresh){
                sample[size_t(k)] = uint32_t(random() % count);
                fresh = true;
                for(int j = 0; j < k; ++j) if(sample[size_t(j)] == sample[size_t(k)]) fresh = false;
            }
        }

        double E[3][3];
        if(!essentialFrom(a, b, sample, E)) continue;

        std::vector<uint8_t> inliers(count, 0);
        uint32_t inlierCount = 0;
        for(size_t i = 0; i < count; ++i){
            if(sampsonPixels(E, a[i], b[i], focal) <= config.thresholdPixels){
                inliers[i] = 1;
                ++inlierCount;
            }
        }

        if(inlierCount > bestCount){
            bestCount = inlierCount;
            bestInliers = inliers;
        }
    }

    if(bestCount < 8){
        return result;
    }

    //Zavrsni racun samo nad onima koji se slazu: uzorak od osam sluzio je samo za odabir
    std::vector<uint32_t> kept;
    kept.reserve(bestCount);
    for(size_t i = 0; i < count; ++i) if(bestInliers[i]) kept.push_back(uint32_t(i));

    double E[3][3];
    if(!essentialFrom(a, b, kept, E)) return result;
    if(!poseFromEssential(E, a, b, kept, result.pose, result.inFront)) return result;

    result.inliers = bestInliers;
    result.inlierCount = bestCount;
    result.solved = true;
    return result;
}

}
