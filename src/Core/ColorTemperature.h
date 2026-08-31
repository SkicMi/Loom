#pragma once
#include <glm/glm.hpp>

//BOJA SVJETLA, RECENA KAO TEMPERATURA.
//
//Svjetlo je i dosad imalo boju - tri broja koja mnoze i difuz i zrcalni clan. Problem nije
//bio u tome sto se boja ne da postaviti, nego u tome sto se ne da postaviti a da se pritom
//ne promijeni i KOLICINA svjetla. Trojac {1, 0.82, 0.55}, koji je stajao zakucan u LoomAppu,
//ima Rec.709 luminanciju 0.839: taj "topli ton" je ujedno bio svjetlo slabije za sesnaest
//posto. Dvije takve boje se onda ne daju usporediti, jer razlika u tonu nosi i razliku u
//svjetlini - ista zamka zbog koje se zrnatost polusjene morala mjeriti na faktoru
//vidljivosti a ne na zatamnjenju.
//
//Temperatura to rjesava sama od sebe, i to je jedini razlog zasto je ovdje ima. Crno tijelo
//se racuna preko kromatičnosti (x, y), a Y - koji JEST luminancija - se pritom zada kao 1.
//Zato boja iz Kelvina po konstrukciji nosi tocno jedinicnu luminanciju, pa mijenja iskljucivo
//ton. Izmjereno na nizu 2000-25000 K: luminancija ostaje 1.0000 do na 5e-5.

//Rec.709 luminancija linearne boje. To je doslovno red za Y iz matrice XYZ -> linearni sRGB,
//pa ovo nije "neka formula za svjetlinu" nego ista definicija koju koristi i pretvorba ispod
inline float luminance(const glm::vec3& linear){
    return 0.2126f * linear.r + 0.7152f * linear.g + 0.0722f * linear.b;
}

//Ista boja, s luminancijom tocno 1. Time intensity ostaje jedina stvar koja mijenja kolicinu
//svjetla, a boja jedina koja mijenja ton
inline glm::vec3 normalizeLuminance(const glm::vec3& linear){
    const float y = luminance(linear);
    return (y > 1e-6f) ? linear / y : linear;
}

//Boja crnog tijela na zadanoj temperaturi, u LINEARNOM sRGB-u.
//
//Cetiri koraka, i svaki ima svoj razlog:
//
//  1. T -> (x, y) na Planckovom lokusu. Kubna aproksimacija (Kim et al.), jer je tocan racun
//     integral Planckovog zracenja preko tri CIE krivulje - a to je tablica koja bi ovdje
//     stajala samo da bi dala istih pet decimala
//  2. (x, y) + Y = 1 -> XYZ. Ovdje se odlucuje ono glavno: zadaje se JEDINICNA luminancija,
//     pa temperatura mijenja samo kromatičnost
//  3. XYZ -> linearni sRGB, matricom za Rec.709 primare i D65 bijelu
//  4. odrezivanje negativnog. Ispod otprilike 1900 K crveno-narancasta boja crnog tijela
//     izlazi iz sRGB trokuta i plavi kanal ispadne negativan; tamo se boja vise ne moze
//     prikazati nego samo primaknuti rubu gamuta
//
//POSTENO O TOME STO OVO NIJE: 6500 K nije sRGB bijelo. Kanali se izjednace tek na 6532 K, i
//ni tamo nisu jednaki - zeleni je 5.6% nizi od druga dva. Nije greska aproksimacije nego
//stanje stvari: D65 je dnevno svjetlo, a dnevno svjetlo nije crno tijelo, pa Planckov lokus
//kroz bijelu tocku sRGB-a uopce ne prolazi
inline glm::vec3 colorFromKelvin(float kelvin){
    //Izvan ovog raspona aproksimacija ne vrijedi, a i nema sto ponuditi: ispod je boja davno
    //izasla iz gamuta, iznad se vise ne mijenja
    const double t = double(glm::clamp(kelvin, 1500.0f, 25000.0f));

    double x;
    if(t <= 4000.0){
        x = -0.2661239e9 / (t*t*t) - 0.2343589e6 / (t*t) + 0.8776956e3 / t + 0.179910;
    }
    else{
        x = -3.0258469e9 / (t*t*t) + 2.1070379e6 / (t*t) + 0.2226347e3 / t + 0.240390;
    }

    //Tri komada krivulje, jer je lokus na donjem kraju previse zakrivljen da bi ga jedan
    //polinom pratio na pet decimala
    double y;
    if(t <= 2222.0){
        y = -1.1063814*x*x*x - 1.34811020*x*x + 2.18555832*x - 0.20219683;
    }
    else if(t <= 4000.0){
        y = -0.9549476*x*x*x - 1.37418593*x*x + 2.09137015*x - 0.16748867;
    }
    else{
        y =  3.0817580*x*x*x - 5.87338670*x*x + 3.75112997*x - 0.37001483;
    }

    //Y = 1: svjetlo nosi jedinicnu luminanciju, a x i y kazu samo kamo je obojeno
    const double X = x / y;
    const double Y = 1.0;
    const double Z = (1.0 - x - y) / y;

    const double r =  3.2404542*X - 1.5371385*Y - 0.4985314*Z;
    const double g = -0.9692660*X + 1.8760108*Y + 0.0415560*Z;
    const double b =  0.0556434*X - 0.2040259*Y + 1.0572252*Z;

    return glm::vec3(float(r < 0.0 ? 0.0 : r),
                     float(g < 0.0 ? 0.0 : g),
                     float(b < 0.0 ? 0.0 : b));
}
