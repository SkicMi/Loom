#include "Engine/TwoView.h"
#include "Engine/Dense.h"

#include <algorithm>
#include <cmath>

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
//samo nad dvije zrake i bez poza - ovdje poza jos ne postoji
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

}

TwoViewResult relativePose(const std::vector<glm::vec2>& pixelsA,
                           const std::vector<glm::vec2>& pixelsB,
                           const Intrinsics& intrinsics){
    TwoViewResult result;
    const size_t count = std::min(pixelsA.size(), pixelsB.size());
    if(count < 8){
        return result;   //osam tocaka je najmanje sto devet clanova E moze odrediti
    }
    result.used = uint32_t(count);

    std::vector<Vector3> a(count), b(count);
    for(size_t i = 0; i < count; ++i){
        a[i] = bearing(pixelsA[i], intrinsics);
        b[i] = bearing(pixelsB[i], intrinsics);
    }

    //Normalne jednadzbe osmotockovnog algoritma: b' E a = 0 je linearno u devet clanova E
    std::vector<double> normal(81, 0.0);
    for(size_t i = 0; i < count; ++i){
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
        return result;
    }

    double E[3][3];
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) E[i][j] = solution[size_t(i * 3 + j)];

    //SVD od E preko svojstvenog rastava E'E: V su svojstveni vektori, singularne vrijednosti
    //korijeni svojstvenih vrijednosti, a U se dobije kao E v / sigma
    double Et[3][3], EtE[3][3];
    transpose(E, Et);
    multiply(Et, E, EtE);

    double values[3], V[3][3];
    symmetricEigen3(EtE, values, V);
    if(values[1] <= 1e-18){
        return result;   //degenerirano: sve tocke na pravcu ili u jednoj tocki
    }

    double U[3][3];
    for(int column = 0; column < 2; ++column){
        const double sigma = std::sqrt(std::max(0.0, values[column]));
        const Vector3 v{V[0][column], V[1][column], V[2][column]};
        const Vector3 u = normalized(apply(E, v));
        U[0][column] = u.x; U[1][column] = u.y; U[2][column] = u.z;
        (void)sigma;
    }
    const Vector3 u1{U[0][0], U[1][0], U[2][0]};
    const Vector3 u2{U[0][1], U[1][1], U[2][1]};
    const Vector3 u3 = normalized(cross(u1, u2));
    U[0][2] = u3.x; U[1][2] = u3.y; U[2][2] = u3.z;

    //Desni sustav na obje strane; inace ispadne zrcaljenje umjesto rotacije
    if(determinant(U) < 0.0){ for(int i = 0; i < 3; ++i) U[i][2] = -U[i][2]; }
    if(determinant(V) < 0.0){ for(int i = 0; i < 3; ++i) V[i][2] = -V[i][2]; }

    const double W[3][3] = {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    double Wt[3][3];
    transpose(W, Wt);

    double Vt[3][3];
    transpose(V, Vt);

    double UW[3][3], UWt[3][3], R1[3][3], R2[3][3];
    multiply(U, W, UW);
    multiply(UW, Vt, R1);
    multiply(U, Wt, UWt);
    multiply(UWt, Vt, R2);

    if(determinant(R1) < 0.0) for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) R1[i][j] = -R1[i][j];
    if(determinant(R2) < 0.0) for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) R2[i][j] = -R2[i][j];

    const Vector3 translation{U[0][2], U[1][2], U[2][2]};

    //CETIRI KANDIDATA, i samo jedan ima tocke ispred obje kamere. To je jedini nacin da se odabere:
    //algebra ne razlikuje kameru koja gleda scenu od one koja joj je okrenuta ledjima
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

        //Centar druge kamere i smjerovi njezinih zraka, u sustavu prve
        double Rt[3][3];
        transpose(R, Rt);
        const Vector3 centre = apply(Rt, Vector3{-t.x, -t.y, -t.z});

        uint32_t inFront = 0;
        for(size_t i = 0; i < count; ++i){
            Vector3 point;
            if(!meet(Vector3{0.0, 0.0, 0.0}, a[i], centre, apply(Rt, b[i]), point)) continue;
            if(point.z <= 0.0) continue;   //ispred prve kamere: u klasicnoj konvenciji z raste naprijed

            const Vector3 inSecond = apply(R, point);
            if(inSecond.z + t.z > 0.0) ++inFront;   //i ispred druge
        }

        if(inFront > bestInFront){
            bestInFront = inFront;
            best = c;
        }
    }

    if(best < 0){
        return result;
    }

    const double (*R)[3] = candidates[best].rotation;
    const Vector3 t{translation.x * candidates[best].sign,
                    translation.y * candidates[best].sign,
                    translation.z * candidates[best].sign};

    //Iz klasicne natrag u nasu: x_B = R x_A + t vrijedi u klasicnoj, a nasa je zrcaljena po dvije
    //osi. Iz x_camB = R_p'(x_svijet - c) slijedi R_p = M R' M i c = -M R' t
    double poseRotation[3][3];
    double Rt[3][3];
    transpose(R, Rt);
    for(int i = 0; i < 3; ++i){
        for(int j = 0; j < 3; ++j) poseRotation[i][j] = mirror[i] * Rt[i][j] * mirror[j];
    }
    const Vector3 centre = apply(Rt, t);

    glm::mat3 rotation(1.0f);
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) rotation[j][i] = float(poseRotation[i][j]);

    result.pose.orientation = glm::normalize(glm::quat_cast(rotation));
    result.pose.position = glm::vec3(float(-mirror[0] * centre.x), float(-mirror[1] * centre.y), float(-mirror[2] * centre.z));
    result.inFront = bestInFront;
    result.solved = true;
    return result;
}

}
