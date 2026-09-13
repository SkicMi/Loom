#include "Engine/Dense.h"

#include <algorithm>
#include <cmath>

namespace Engine{

bool solveDense(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x){
    x.assign(size_t(n), 0.0);
    for(int column = 0; column < n; ++column){
        int pivot = column;
        for(int row = column + 1; row < n; ++row){
            if(std::fabs(A[size_t(row * n + column)]) > std::fabs(A[size_t(pivot * n + column)])) pivot = row;
        }
        if(std::fabs(A[size_t(pivot * n + column)]) < 1e-14){
            return false;
        }
        if(pivot != column){
            for(int k = 0; k < n; ++k) std::swap(A[size_t(column * n + k)], A[size_t(pivot * n + k)]);
            std::swap(b[size_t(column)], b[size_t(pivot)]);
        }
        for(int row = column + 1; row < n; ++row){
            const double factor = A[size_t(row * n + column)] / A[size_t(column * n + column)];
            if(factor == 0.0) continue;
            for(int k = column; k < n; ++k) A[size_t(row * n + k)] -= factor * A[size_t(column * n + k)];
            b[size_t(row)] -= factor * b[size_t(column)];
        }
    }
    for(int row = n - 1; row >= 0; --row){
        double sum = b[size_t(row)];
        for(int k = row + 1; k < n; ++k) sum -= A[size_t(row * n + k)] * x[size_t(k)];
        x[size_t(row)] = sum / A[size_t(row * n + row)];
    }
    return true;
}

bool invert3(const double m[3][3], double out[3][3]){
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if(std::fabs(det) < 1e-18){
        return false;
    }
    const double inverse = 1.0 / det;
    out[0][0] =  (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inverse;
    out[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) * inverse;
    out[0][2] =  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inverse;
    out[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) * inverse;
    out[1][1] =  (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inverse;
    out[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) * inverse;
    out[2][0] =  (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inverse;
    out[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) * inverse;
    out[2][2] =  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inverse;
    return true;
}

void symmetricEigen3(const double m[3][3], double values[3], double vectors[3][3]){
    double a[3][3];
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) a[i][j] = m[i][j];
    for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) vectors[i][j] = (i == j) ? 1.0 : 0.0;

    //Jacobi: svaka rotacija ponisti jedan izvandijagonalni clan. Za 3x3 je dvadesetak prolaza
    //vise nego dovoljno, a svaki je nekoliko mnozenja
    for(int sweep = 0; sweep < 24; ++sweep){
        double off = 0.0;
        for(int i = 0; i < 3; ++i) for(int j = i + 1; j < 3; ++j) off += a[i][j] * a[i][j];
        if(off < 1e-30) break;

        for(int p = 0; p < 2; ++p){
            for(int q = p + 1; q < 3; ++q){
                if(std::fabs(a[p][q]) < 1e-300) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double sign = theta >= 0.0 ? 1.0 : -1.0;
                const double t = sign / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;

                for(int k = 0; k < 3; ++k){
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for(int k = 0; k < 3; ++k){
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for(int k = 0; k < 3; ++k){
                    const double vkp = vectors[k][p], vkq = vectors[k][q];
                    vectors[k][p] = c * vkp - s * vkq;
                    vectors[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }

    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int left, int right){return a[left][left] > a[right][right];});

    double sortedValues[3];
    double sortedVectors[3][3];
    for(int i = 0; i < 3; ++i){
        sortedValues[i] = a[order[i]][order[i]];
        for(int k = 0; k < 3; ++k) sortedVectors[k][i] = vectors[k][order[i]];
    }
    for(int i = 0; i < 3; ++i){
        values[i] = sortedValues[i];
        for(int k = 0; k < 3; ++k) vectors[k][i] = sortedVectors[k][i];
    }
}

bool smallestEigenvector(const std::vector<double>& A, int n, std::vector<double>& vector){
    //Inverzna iteracija: rjesavanje (A + eI) x = prethodni gura vektor prema najmanjoj svojstvenoj
    //vrijednosti. Pomak e je sitan, samo da eliminacija ne stane na tocnoj nuli
    double trace = 0.0;
    for(int i = 0; i < n; ++i) trace += A[size_t(i * n + i)];
    const double shift = std::max(1e-12, 1e-10 * std::fabs(trace) / double(n));

    std::vector<double> shifted = A;
    for(int i = 0; i < n; ++i) shifted[size_t(i * n + i)] += shift;

    vector.assign(size_t(n), 0.0);
    for(int i = 0; i < n; ++i) vector[size_t(i)] = 1.0 / std::sqrt(double(n));

    for(int iteration = 0; iteration < 60; ++iteration){
        std::vector<double> next;
        if(!solveDense(shifted, vector, n, next)){
            return false;
        }
        double length = 0.0;
        for(double value : next) length += value * value;
        length = std::sqrt(length);
        if(length < 1e-300){
            return false;
        }
        for(double& value : next) value /= length;

        double change = 0.0;
        for(int i = 0; i < n; ++i) change += std::fabs(std::fabs(next[size_t(i)]) - std::fabs(vector[size_t(i)]));
        vector = next;
        if(change < 1e-15) break;
    }
    return true;
}

}
