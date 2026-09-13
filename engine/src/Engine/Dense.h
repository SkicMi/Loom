#pragma once
#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// Sitna linearna algebra koju faza S treba: gust sustav, inverz 3x3, svojstvene vrijednosti
// simetricne 3x3 i najmanji svojstveni vektor.
//
// Bez vanjske biblioteke, jer su svi sustavi ovdje mali: 6x6 u PnP-u, 42x42 poslije Schura,
// 9x9 kod esencijalne matrice. Ono sto NIJE malo - velik rijedak sustav - ovdje se ni ne
// pojavljuje, upravo zato sto Schur tocke izbaci prije rjesavanja.
//
// Zivi u zasebnom fileu jer ga treba vise koraka: prvo je stajalo u Bundle.cpp, a onda ga je
// zatrazila i esencijalna matrica. Drugi primjerak iste eliminacije bio bi tocno ona vrsta
// udvostrucenja koju ovaj projekt inace lovi.
//=============================================================================================

//A x = b, Gaussova eliminacija s biranjem stozera. False kad je sustav singularan
bool solveDense(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x);

//Inverz 3x3 preko adjunkte. False kad je determinanta prakticki nula
bool invert3(const double m[3][3], double out[3][3]);

//Svojstvene vrijednosti i vektori SIMETRICNE 3x3, Jacobijevim rotacijama. Vrijednosti silazno,
//vektori u stupcima. Preko ovoga ide rastav esencijalne matrice: SVD od E dobije se iz
//svojstvenog rastava E'E
void symmetricEigen3(const double m[3][3], double values[3], double vectors[3][3]);

//Svojstveni vektor NAJMANJE svojstvene vrijednosti simetricne n x n, inverznom iteracijom.
//Osmotockovni algoritam trazi bas to: rjesenje homogenog sustava je smjer u kojem se A'A gotovo
//ne opire
bool smallestEigenvector(const std::vector<double>& A, int n, std::vector<double>& vector);

}
