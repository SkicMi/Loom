// Banded solving: the same answer as dense, just without the work that is provably zero.
//
// WHY THIS MATTERS. The Schur complement in bundle is solved dense, O(n^3), and that is a wall
// against bigger scenes: measured, eight times more cameras means 548 times more expensive
// solving. And on a real shot the matrix is 84.2 percent exactly zero, in bands - no pair of
// cameras more than 39 apart shares a single point.
//
// WHAT THIS TEST DEFENDS is not "roughly the same" but BIT FOR BIT THE SAME. Only terms that
// are exactly zero are skipped, and adding zero changes no bit. If "close enough" were
// allowed this would be a different method with different rounding and the bundle golden hash
// would fail - and nobody would know whether it failed from an error or from an allowed
// difference.
//
// On top come the NEGATIVE CONTROL: the same matrix that is NOT banded, solved as if it were,
// must give an obviously wrong answer. Without it the test could not tell "works" from
// "passes by luck".
#include "TestHarness.h"

#include <Engine/Dense.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace{

//Simple repeatable generator - we don't need quality, we need the same sequence every time
struct Rng{
    uint64_t state = 0x9E3779B97F4A7C15ull;
    double next(){
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return double(state >> 11) / double(1ull << 53);
    }
};

//Banded symmetric positive definite matrix: A = L L' where L is lower triangular within the
//band. That makes the band exactly what we claim, and the system certainly solvable
std::vector<double> bandedSpd(int n, int halfWidth, Rng& rng){
    std::vector<double> lower(size_t(n) * size_t(n), 0.0);
    for(int row = 0; row < n; ++row){
        const int from = std::max(0, row - halfWidth);
        for(int column = from; column < row; ++column){
            lower[size_t(row * n + column)] = rng.next() * 2.0 - 1.0;
        }
        lower[size_t(row * n + row)] = 1.0 + rng.next() * 2.0;   //positive diagonal
    }

    std::vector<double> full(size_t(n) * size_t(n), 0.0);
    for(int row = 0; row < n; ++row){
        for(int column = 0; column < n; ++column){
            double sum = 0.0;
            for(int k = 0; k < n; ++k) sum += lower[size_t(row * n + k)] * lower[size_t(column * n + k)];
            full[size_t(row * n + column)] = sum;
        }
    }
    return full;
}

}

int main(){
    TestReport report("S8 trakasto rjesavanje");

    Rng rng;

    //-- bit for bit the same, across sizes and widths -----------------------------------------
    bool allIdentical = true;
    double worstDifference = 0.0;
    int checked = 0;

    for(int n : {12, 60, 180}){
        for(int halfWidth : {2, 6, 18}){
            if(halfWidth >= n) continue;

            const std::vector<double> matrix = bandedSpd(n, halfWidth, rng);
            std::vector<double> right(size_t(n), 0.0);
            for(int i = 0; i < n; ++i) right[size_t(i)] = rng.next() * 2.0 - 1.0;

            std::vector<double> dense, banded;
            const bool okDense = Engine::solveDense(matrix, right, n, dense);
            const bool okBanded = Engine::solveBanded(matrix, right, n, halfWidth, banded);
            if(!okDense || !okBanded){ allIdentical = false; continue; }

            for(int i = 0; i < n; ++i){
                if(dense[size_t(i)] != banded[size_t(i)]) allIdentical = false;
                worstDifference = std::max(worstDifference,
                                           std::fabs(dense[size_t(i)] - banded[size_t(i)]));
            }
            ++checked;
        }
    }

    report.check("bit po bit isto kao gusto", allIdentical && checked >= 6,
        fmt("%d sustava, najveca razlika %.3e", checked, worstDifference));

    //-- negative control: a too-short band MUST miss ------------------------------------------
    //
    //Without this the test could not tell a correct solver from one that passes by luck
    //because the matrices were easy
    {
        const int n = 80;
        const std::vector<double> matrix = bandedSpd(n, 20, rng);   //actual half-width 20
        std::vector<double> right(size_t(n), 0.0);
        for(int i = 0; i < n; ++i) right[size_t(i)] = rng.next() * 2.0 - 1.0;

        std::vector<double> dense, tooNarrow;
        Engine::solveDense(matrix, right, n, dense);
        const bool solved = Engine::solveBanded(matrix, right, n, 3, tooNarrow);   //lie: 3

        double worst = 0.0;
        if(solved){
            for(int i = 0; i < n; ++i){
                worst = std::max(worst, std::fabs(dense[size_t(i)] - tooNarrow[size_t(i)]));
            }
        }
        report.check("prekratka vrpca daje ocito kriv odgovor", !solved || worst > 1e-6,
            fmt("najveca razlika %.3e", worst));
    }

    //-- the solution actually solves the system ------------------------------------------------
    {
        const int n = 120, halfWidth = 8;
        const std::vector<double> matrix = bandedSpd(n, halfWidth, rng);
        std::vector<double> right(size_t(n), 0.0);
        for(int i = 0; i < n; ++i) right[size_t(i)] = rng.next() * 2.0 - 1.0;

        std::vector<double> x;
        Engine::solveBanded(matrix, right, n, halfWidth, x);

        double worst = 0.0;
        for(int row = 0; row < n; ++row){
            double sum = 0.0;
            for(int k = 0; k < n; ++k) sum += matrix[size_t(row * n + k)] * x[size_t(k)];
            worst = std::max(worst, std::fabs(sum - right[size_t(row)]));
        }
        report.check("A x = b stvarno vrijedi", worst < 1e-9, fmt("najgori ostatak %.3e", worst));
    }

    //-- and that it pays off -------------------------------------------------------------------
    //
    //Speed is not a claim about correctness, but the whole point of this is the saving - so
    //if it isn't there, something was misunderstood
    {
        const int n = 600, halfWidth = 12;
        const std::vector<double> matrix = bandedSpd(n, halfWidth, rng);
        std::vector<double> right(size_t(n), 0.0);
        for(int i = 0; i < n; ++i) right[size_t(i)] = rng.next() * 2.0 - 1.0;

        std::vector<double> out;
        const auto denseStarted = std::chrono::steady_clock::now();
        Engine::solveDense(matrix, right, n, out);
        const double denseSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - denseStarted).count();

        const auto bandedStarted = std::chrono::steady_clock::now();
        Engine::solveBanded(matrix, right, n, halfWidth, out);
        const double bandedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - bandedStarted).count();

        report.check("trakasto je bitno jeftinije", bandedSeconds * 3.0 < denseSeconds,
            fmt("%.1f ms naspram %.1f ms na n=%d, polusirina %d",
                bandedSeconds * 1000.0, denseSeconds * 1000.0, n, halfWidth));
    }

    return report.result();
}
