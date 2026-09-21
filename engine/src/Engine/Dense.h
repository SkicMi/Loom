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

//=============================================================================================
// Isti sustav, ali kad se ZNA da je matrica vrpcasta: sve izvan vrpce je tocno nula.
//
// ZASTO POSTOJI. Schurova dopuna u bundleu je n x n gdje je n sest puta broj kamera, i rjesava se
// gusto - O(n^3). Izmjereno na sintetici: osam puta vise kamera znaci 548 puta skuplje rjesavanje
// (0.20 s na pedeset kamera, 109.61 s na cetiristo). Za stan od tisucu kadrova to bi bilo oko
// pola sata PO JEDNOM POZIVU.
//
// A matrica je gotovo prazna. Izmjereno na pravoj snimci (kameni zid, 229 kamera): samo 15.8 posto
// parova kamera uopce dijeli neku tocku, dakle 84.2 posto matrice je tocno nula. I nije nasumicno
// rasporedjeno nego VRPCASTO - nijedan par udaljeniji od 39 kamera ne dijeli nijednu tocku:
//
//     50% veza unutar 10 kamera, 99% unutar 32, 100% unutar 39
//
// ARITMETIKA OSTAJE ISTA, ne samo priblizna. Preskacu se iskljucivo clanovi koji su TOCNO nula, a
// dodavanje nule ne mijenja nijedan bit. Zato ovo ne mijenja rezultat nego samo izostavlja posao -
// i zlatni hash bundlea to brani.
//
// GRANICE SU SIRE NEGO SAMA VRPCA jer pivotiranje unosi ispunu. Za matricu s donjom i gornjom
// polusirinom b, L ostaje unutar b ispod dijagonale a U naraste do 2b iznad - to je ista granica
// koju koristi i LAPACK-ov trakasti LU. Sve izvan toga je i dalje dokazano nula.
//
// halfWidth je polusirina u REDCIMA matrice (dakle vec pomnozena sa sest ako se broji po kamerama)
//=============================================================================================
bool solveBanded(std::vector<double> A, std::vector<double> b, int n, int halfWidth,
                 std::vector<double>& x);

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
