// Trakasto rjesavanje: isti odgovor kao gusto, samo bez posla koji je dokazano nula.
//
// ZASTO JE OVO VRIJEDNO. Schurova dopuna u bundleu rjesava se gusto, O(n^3), i to je zid prema
// vecim scenama: izmjereno, osam puta vise kamera znaci 548 puta skuplje rjesavanje. A matrica je
// na pravoj snimci 84.2 posto tocno nula, i to vrpcasto rasporedjeno - nijedan par kamera
// udaljeniji od 39 ne dijeli nijednu tocku.
//
// TVRDNJA KOJU OVAJ TEST BRANI nije "priblizno isto" nego BIT PO BIT ISTO. Preskacu se iskljucivo
// clanovi koji su tocno nula, a dodavanje nule ne mijenja nijedan bit. Da se popusti na "blizu
// je", ovo bi bila druga metoda s drugim zaokruzivanjem i zlatni hash bundlea bi pao - a nitko ne
// bi znao je li pao zbog greske ili zbog dozvoljene razlike.
//
// Uz to ide NEGATIVNA KONTROLA: ista matrica koja NIJE vrpcasta, rjesena kao da jest, mora dati
// ocito kriv odgovor. Bez nje test ne bi razlikovao "radi" od "slucajno prolazi".
#include "TestHarness.h"

#include <Engine/Dense.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace{

//Jednostavan ponovljiv generator - ne treba nam kvaliteta, treba nam isti niz svaki put
struct Rng{
    uint64_t state = 0x9E3779B97F4A7C15ull;
    double next(){
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return double(state >> 11) / double(1ull << 53);
    }
};

//Vrpcasta simetricna pozitivno definitna matrica: A = L L', gdje je L donja trokutasta unutar
//vrpce. Time je vrpca tocno onakva kakvu tvrdimo, a sustav sigurno rjesiv
std::vector<double> bandedSpd(int n, int halfWidth, Rng& rng){
    std::vector<double> lower(size_t(n) * size_t(n), 0.0);
    for(int row = 0; row < n; ++row){
        const int from = std::max(0, row - halfWidth);
        for(int column = from; column < row; ++column){
            lower[size_t(row * n + column)] = rng.next() * 2.0 - 1.0;
        }
        lower[size_t(row * n + row)] = 1.0 + rng.next() * 2.0;   //pozitivna dijagonala
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

    //-- bit po bit isto, na vise velicina i sirina ------------------------------------------
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

    //-- negativna kontrola: prekratka vrpca MORA promasiti ----------------------------------
    //
    //Bez ovoga test ne bi razlikovao ispravan rjesavac od onoga koji slucajno prolazi jer su
    //matrice bile lake
    {
        const int n = 80;
        const std::vector<double> matrix = bandedSpd(n, 20, rng);   //stvarna polusirina 20
        std::vector<double> right(size_t(n), 0.0);
        for(int i = 0; i < n; ++i) right[size_t(i)] = rng.next() * 2.0 - 1.0;

        std::vector<double> dense, tooNarrow;
        Engine::solveDense(matrix, right, n, dense);
        const bool solved = Engine::solveBanded(matrix, right, n, 3, tooNarrow);   //laz: 3

        double worst = 0.0;
        if(solved){
            for(int i = 0; i < n; ++i){
                worst = std::max(worst, std::fabs(dense[size_t(i)] - tooNarrow[size_t(i)]));
            }
        }
        report.check("prekratka vrpca daje ocito kriv odgovor", !solved || worst > 1e-6,
            fmt("najveca razlika %.3e", worst));
    }

    //-- rjesenje stvarno rjesava sustav ------------------------------------------------------
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

    //-- i da se isplati ----------------------------------------------------------------------
    //
    //Brzina nije tvrdnja o ispravnosti, ali cijela svrha ovoga je usteda - pa ako je nema, nesto
    //je krivo shvaceno
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
