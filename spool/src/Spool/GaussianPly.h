#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Spool{

//Oblak 3D gaussiana iz .ply datoteke - zapis kojim se gaussian splatting razmjenjuje.
//
//SPOOL VRACA ONO STO U FILEU PISE, NE ONO STO SE CRTA. To nije sitnica nego jedina obrana
//od tihe greske: brojevi u fileu su zapisani u OBLIKU PRIJE AKTIVACIJE, pa citac koji ih
//"popravi" i potrosac koji ih popravi jos jednom daju scenu koja izgleda skoro dobro.
//
//   opacity   logit. Ono sto se crta je sigmoid(opacity)
//   scale     logaritam. Ono sto se crta je exp(scale)
//   rotation  kvaternion koji NIJE nuzno jedinicne duljine
//
//Aktivacija je posao sloja koji zna sto crta, i tamo joj je i test.
struct Gaussian{
    float position[3] = {0,0,0};
    float normal[3]   = {0,0,0};   //u fileu stoji, i gotovo uvijek je nula
    float dc[3]       = {0,0,0};   //SH stupanj 0, po jedan po kanalu
    float opacity     = 0.0f;      //logit
    float scale[3]    = {0,0,0};   //logaritam
    float rotation[4] = {1,0,0,0}; //redoslijedom kojim file nabraja rot_0..rot_3
};

//Sferni harmonici viseg reda, ako ih file nosi.
//
//REDOSLIJED JE ONAJ IZ FILEA, nedirnut. Zapis koji je uveo referentni 3DGS nabraja f_rest
//po KANALIMA: prvo svih 15 koeficijenata za crveni, pa 15 za zeleni, pa 15 za plavi. Tko to
//procita kao 3 broja po koeficijentu dobije boje koje se mijenjaju s kutom na krivi nacin -
//greska koja se ne vidi na jednom kadru nego tek kad se kamera pomakne
struct GaussianCloud{
    std::vector<Gaussian> gaussians;

    //Koeficijenti stupnjeva 1 i vise, restStride po gaussianu, prazno za stupanj 0
    std::vector<float> shRest;

    uint32_t shDegree = 0;    //0, 1, 2 ili 3
    uint32_t restStride = 0;  //0, 9, 24 ili 45 floatova po gaussianu

    size_t count() const {return gaussians.size();}
    bool isValid() const {return !gaussians.empty() && shRest.size() == gaussians.size() * restStride;}

    //Koeficijenti jednog gaussiana, ili nullptr za stupanj 0
    const float* restFor(size_t index) const{
        return restStride == 0 ? nullptr : shRest.data() + index * restStride;
    }
};

//Iz datoteke.
//
//SVOJSTVA SE TRAZE PO IMENU, ne po mjestu. Redoslijed u kojem ih glava nabraja je stvar
//alata koji je file napisao i mijenja se izmedju njih; citac koji pretpostavi redoslijed
//radi tocno dok ne naidje na file iz drugog alata, a onda daje brojeve koji su i dalje
//brojevi - samo krivi. Nepoznata svojstva se preskacu po svojoj velicini.
//
//Trazi binary_little_endian. Baca s putanjom i razlogom: oblak koji se tiho ucitao kao
//nista je prazna scena, i to se vidi tek tri sloja dalje
GaussianCloud loadGaussianPly(const std::string& path);

}
