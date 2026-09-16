#pragma once
#include "Engine/Describe.h"
#include "Engine/Sift.h"
#include "Engine/TwoView.h"

#include <vector>

namespace Engine{

//=============================================================================================
// Korespondencije iz POKLAPANJA, ne iz pracenja.
//
// RAZLIKA U VRSTI. Tracker nosi ugao iz kadra u kadar i umire cim ugao izadje iz slike; sto je
// jednom izgubljeno, izgubljeno je. Ovdje se u svakom kljucnom kadru znacajke nadju NEOVISNO i
// zatim povezu s onima u drugim kadrovima. Trag je time posljedica poklapanja, a ne njegov uvjet.
//
// ZASTO JE TO VAZNO BAS OVDJE. Izmjereno na pravoj snimci: nas lanac je davao oko 300 opazanja po
// kljucnom kadru, COLMAP 3564, i njegov solver je rjesavao 65 kamera od 101 dok je nas rjesavao 7
// od 93. Spajanje vec pracenih tragova je popravilo reprojekciju s 1.553 na 0.924 px, ali broj
// kamera nije pomaknulo - jer spajanje popravlja tocke, a ne stvara nove veze. Ovo ih stvara.
//
// TRI SITA, ista kao kod spajanja i iz istog razloga - krivo poklapanje daje tocku koje nema, a ta
// tocka zatim povlaci pozu za sobom:
//
//   potpis       uzajamno najbolji uz prag omjera
//   polumjer     kandidat mora biti blizu u slici. Na snimci se kamera izmedju bliskih kadrova
//                pomakne ograniceno, pa je poklapanje na drugom kraju slike greska a ne nalaz
//   geometrija   RANSAC nad dvoprizornom pozom; prolaze samo parovi koji se slazu s JEDNIM rjesenjem
//
// TRAG JE POVEZANA KOMPONENTA poklapanja. Ako je A u kadru 1 isto sto i B u kadru 2, a B isto sto i
// C u kadru 3, onda su sva tri jedna tocka - i to bez ijednog pracenja kroz kadrove izmedju njih.
//=============================================================================================

struct MatchGraphConfig{
    TrackConfig detect;        //za detectCorners; maxCorners je ovdje bitno veci nego pri pracenju
    DescribeConfig describe;
    SiftConfig sift;

    //=========================================================================================
    // KOJI POTPIS. Binarni je 256 usporedbi intenziteta, SIFT-ov histogram gradijenata u 4x4
    // celije po 8 smjerova.
    //
    // Izmjereno na pravim kadrovima, koliko parova prezivi geometrijsku provjeru:
    //
    //   razmak kadrova      1     10     15     20
    //   binarni          4322     25     18     22
    //   SIFT             9424     61     29     40
    //
    // Vise nego dvostruko na svakom razmaku, a upravo veliki razmaci nose sirinu baze koja
    // odredjuje tocnost poza
    //=========================================================================================
    bool useSift = false;
    RansacConfig ransac;

    //Koliko kadrova unaprijed se usporedjuje. COLMAP za video koristi deset
    uint32_t window = 10;

    //Najveci pomak u slici koji se jos smatra mogucim, kao udio sirine
    float searchFraction = 0.25f;

    //Koliko se parova mora sloziti s dvoprizornom pozom da bi se paru kadrova vjerovalo
    uint32_t minInliers = 20;

    //Tocka mora biti vidjena iz barem toliko kadrova da udje u rezultat
    uint32_t minViews = 2;

    //Izvodi li se velicina zakrpe iz sirine slike. Vidi mjerenje u MatchGraph.cpp - razlika izmedju
    //zakrpe 16 i 96 na 4K je cetrnaest puta vise dobrih parova
    bool patchFromWidth = true;

    //=========================================================================================
    // NA KOJOJ SE SIRINI TRAZI I POKLAPA. Nula znaci na izvornoj.
    //
    // Na 4K detektor hvata sum senzora i najfiniju teksturu, a to se izmedju dva kadra ne
    // ponavlja - pa potpis opisuje nesto cega u drugom kadru nema. Izmjereno na dva prava kadra
    // (0050 i 0051), koliko parova prezivi geometrijsku provjeru i koliki im je udio:
    //
    //   sirina   zakrpa   zagladjivanje   poklopljeno   provjereno   prezivi
    //    3840      96          8              633          230        36 %
    //    1920      48          4              967          565        58 %
    //     960      24          2              729          598        82 %
    //
    // Dvije trecine poklapanja na 4K su kriva, na 960 ih je krivo osamnaest posto. Uz gusce
    // uglove (razmak 3 px umjesto 8) na 960 se dobije 4322 provjerena para po paru kadrova -
    // dvadeset puta vise nego s cime smo poceli, i u redu velicine COLMAP-ovih 3564 opazanja po
    // kadru.
    //
    // Opazanja se vracaju u KOORDINATAMA SLIKE KOJU JE POZIVATELJ DAO, ne u smanjenima: tko ovo
    // ukljuci ne smije morati mijenjati intrinsics
    //=========================================================================================
    //ZADANO 960, i to je mjereno na cijelom lancu a ne na paru kadrova. Slika uza od toga se ne
    //dira, pa ista postavka vrijedi i za 480x360 sintetiku i za 4K snimku
    uint32_t workingWidth = 960;

    //=========================================================================================
    // STO S KOMPONENTOM KOJA DVA PUTA DODIRNE ISTI KADAR.
    //
    // Trag nastaje kao prijelazno zatvorenje poklapanja: ako je A isto sto i B, a B isto sto i C,
    // onda su sva tri jedna tocka. Jedno krivo poklapanje time slijepi dva NEOVISNA traga u jedan,
    // i to se prepozna po tome sto takva komponenta jedan kadar dodirne dvaput - jedna tocka ne
    // moze biti na dva mjesta u istoj slici.
    //
    // Dosad se zadrzavalo prvo vidjeno po kadru. To ne popravlja nista: komponenta i dalje daje
    // JEDAN trag, samo pomijesan iz dvije tocke - a takav trag triangulira negdje izmedju njih i
    // povlaci poze za sobom. Reprojekcija toga ne prijavi, jer se poze slozu oko izmisljene tocke.
    //
    // true baca cijelu takvu komponentu. Gubi se i ono sto je u njoj bilo tocno, ali ne ulazi
    // nista sto je sigurno krivo.
    //
    // ZADANO UKLJUCENO, i cijena i dobitak su izmjereni. Na 101 kadru prave snimke sukobljenih je
    // komponenti 11361 i nose 92848 od 343161 opazanja - dakle dvadeset sedam posto svega. Kad se
    // bace, uz pod paralakse 1 stupanj:
    //
    //                          kamere    polozaj    rotacija
    //   krate se (prije)      101/101     15.7 %     26.84 st
    //   bacaju se (sada)       96/101      4.4 %      7.09 st
    //
    // Polozaj i rotacija su mjereni protiv COLMAP-ovog rjesenja iste snimke, nakon poravnanja
    // slicnoscu. Pet kamera manje, a greska cetiri puta manja - jer rekonstrukcija koja se sama sa
    // sobom slaze na 1.2 px svejedno moze biti kriva, i s pomijesanim tragovima je bila
    //=========================================================================================
    bool dropConflicting = true;

    //=========================================================================================
    // SPAJANJE KOJE ODBIJA SUKOB, umjesto da ga poslije lijeci.
    //
    // dropConflicting baca komponentu koja je nastala krivo - ali s njom i sve sto je u njoj bilo
    // tocno, a to je bilo dvadeset sedam posto svih opazanja. Ovdje se sukob ne dopusta da nastane:
    // spajanje dvaju tragova koji bi zajedno dodirnuli isti kadar dvaput jednostavno se NE izvede.
    // Odbaci se jedan brid, ne cijela komponenta.
    //
    // POREDAK ODLUCUJE, pa je zadan: bridovi se obilaze po rastucoj udaljenosti potpisa, dakle
    // najpouzdaniji prvi. Krivo poklapanje tada zatekne mjesto zauzeto i otpadne, umjesto da ono
    // slijepi dva traga prije nego dobri stignu.
    //
    // ZADANO ISKLJUCENO, i to je izmjereno. Radi ono sto obecava - sukobljenih komponenti ostane
    // NULA, tragovi se produze s 3.72 na 5.88 kadra, opazanja s 343 na 946 tisuca - a poze su
    // svejedno losije, protiv COLMAP-ovog rjesenja iste snimke:
    //
    //                                     kamere    polozaj   rotacija
    //   slijepo + bacanje sukobljenih     96/101      4.4 %     7.09 st
    //   bez sukoba, tragovi >= 2          68/101     33.5 %   171.28 st
    //   bez sukoba, tragovi >= 4          76/101     11.5 %    26.18 st
    //
    // Zasto: "prvi stigao pobjedjuje" nije dovoljno dobar sudac. Ako krivo poklapanje ima manju
    // udaljenost potpisa od pravog puta do istog kadra, ono zauzme mjesto i pravo se ODBIJE - pa
    // trag zadrzi krivog clana umjesto da se, kao dosad, cijela takva komponenta prepozna i baci.
    //
    // Ostaje u kodu jer sama ideja stoji; fali joj bolji sudac od udaljenosti potpisa. Ocit
    // kandidat je suglasnost trojki: brid koji nema zajednickog susjeda nije potvrdjen nicim
    //=========================================================================================
    bool conflictFreeMerge = false;

    //=========================================================================================
    // POLOZAJ DOTJERAN NA PUNOJ SLICI.
    //
    // Trazenje i poklapanje idu na smanjenoj slici jer ondje potpis mjeri strukturu a ne sum.
    // Cijena je kvantizacija: pri smanjenju cetiri puta svaka je znacajka tocna na cetiri piksela,
    // a COLMAP-ove su subpikselne. Ta razlika ide ravno u tocnost poza, dakle u ostrinu splata.
    //
    // Ovdje se to placa samo jednom po opazanju koje je PREZIVJELO sva sita: polozaj se vrati u
    // punu sliku i ondje dotjera Foerstnerovim racunom (vidi refineCorner). Poklapanje ostaje
    // grubo, mjerenje postaje fino.
    //
    // ZADANO ISKLJUCENO, I TO ŠTETI - izmjereno, protiv COLMAP-ovog rjesenja iste snimke:
    //
    //   najveci pomak   kamere    polozaj   rotacija
    //   bez dotjerivanja 96/101     4.4 %     7.09 st
    //   1 px             91/101     7.2 %    11.94 st
    //   2 px             66/101    28.1 %   120.54 st
    //   4 px             90/101    33.0 %    57.78 st
    //
    // Monotono: sto se vise dopusti, to gore - i vec na jednom pikselu je losije nego bez ikakvog
    // dotjerivanja. Sam refineCorner je tocan (test ga s 1.54 px promasaja vraca na 0.13), pa
    // greska nije u njemu nego u tome STO SE DOTJERUJE NEOVISNO: na 4K je unutar cetiri piksela
    // vise uglova, pa se dva opazanja istog traga zalijepe na RAZLICITE - i trag koji je bio
    // kvantiziran ali dosljedan postane tocan ali nedosljedan. Triangulaciji treba ovo drugo.
    //
    // Prava inacica trazi dosljednost: jedno opazanje traga je referentno, a ostala se dotjeruju
    // Lucas-Kanadeom PREMA NJEGOVOJ ZAKRPI, pa svi opisuju istu fizicku tocku. To je jos posao
    //=========================================================================================
    bool refineAtFullResolution = false;

    //Koliko se polozaj smije pomaknuti pri dotjerivanju, u pikselima pune slike. Nula znaci
    //onoliko koliko je slika smanjena - dakle koliko kvantizacija najvise i moze promasiti
    float refineMaxShift = 0.0f;

    //=========================================================================================
    // SUGLASNOST TROJKI: koliko trecih kadrova mora potvrditi jedan brid.
    //
    // Poklapanje A-B provjerava dvoprizorna geometrija, a ona propusta sve sto se slaze s JEDNIM
    // rjesenjem - ukljucujuci krivo poklapanje na ponavljajucoj teksturi, jer i ono lezi na
    // epipolarnoj crti. Takav brid zatim slijepi dva neovisna traga, a to je nosilo dvadeset sedam
    // posto svih opazanja.
    //
    // Trojka je jaca provjera i ne trazi poze: ako postoji znacajka C koja se poklapa i s A i s B,
    // onda se TRI kadra slazu oko iste tocke. C je nuzno u trecem kadru, jer se poklapa samo
    // izmedju razlicitih kadrova.
    //
    // Nula iskljucuje. Jedan znaci da svaki brid treba bar jednog svjedoka.
    //
    // NE PRIMJENJUJE SE kad trojka ne moze ni nastati - dakle ispod tri kadra ili uz prozor 1, gdje
    // postoje samo bridovi susjednih kadrova. Bez te ograde bi na dva kadra pobrisala sve.
    //
    // ZADANO JEDAN, i to je izmjereno protiv COLMAP-ovog rjesenja iste snimke:
    //
    //   svjedoka   kamere    polozaj   rotacija   duljina traga
    //     0        96/101      4.4 %     7.09 st      3.09
    //     1       101/101      1.6 %     6.60 st      5.16
    //     2       101/101     24.9 %    86.06 st      5.55
    //
    // Jedan svjedok otklanja gotovo tri cetvrtine greske polozaja i produzi tragove za dvije
    // trecine; sukobljenih komponenti ostane 5762 umjesto 11361. Dva svjedoka ruse sve - odbace
    // 639 tisuca bridova umjesto 338, i s njima i ono sto je scenu drzalo na okupu.
    //
    // Tragovi su duzi iako se bridovi BACAJU, i to nije proturjecje: bacaju se oni koji su tragove
    // krivo spajali, pa ono sto ostane prezivi ciscenje sukoba umjesto da padne s njim
    //=========================================================================================
    uint32_t minTriangleSupport = 1;
};

struct MatchGraphResult{
    std::vector<Observation> observations;
    uint32_t pointCount = 0;

    uint32_t comparedFrames = 0;
    uint32_t acceptedFrames = 0;
    uint32_t featuresTotal = 0;
    double medianMatchesPerPair = 0.0;

    //KOLIKO JE POLOZAJ ZNACAJKE TOCAN, u pikselima slike koju je pozivatelj dao. Jedan kad se
    //radilo na izvornoj sirini; inace onoliko koliko je slika smanjena.
    //
    //Postoji zato sto reconstruct ima prag prihvacanja kamere u pikselima, a taj prag mora biti
    //veci od ove nepreciznosti - inace se odbijaju kamere koje su tocne koliko podatak dopusta.
    //Izmjereno na 30 kadrova prave snimke, radna sirina 960 (dakle tocnost 4 px):
    //
    //   prag  4 px    5 od 30 kamera
    //   prag  8 px   30 od 30 kamera, reprojekcija 1.199 px
    //   prag 16 px   isto sto i 8
    //
    //Dakle dvostruko od ovoga je dovoljno, a vise ne mijenja nista
    float localizationPixels = 1.0f;

    //Koliko je komponenti dva puta dotaknulo isti kadar, i koliko su opazanja nosile. Jedna tocka
    //ne moze biti na dva mjesta u istoj slici, pa je takva komponenta spoj dvije stvarne tocke -
    //vidi MatchGraphConfig::dropConflicting
    uint32_t conflictingPoints = 0;
    uint32_t conflictingObservations = 0;

    //Koliko je bridova odbijeno jer bi spojio dva traga u isti kadar - vidi conflictFreeMerge
    uint32_t refusedEdges = 0;

    //Koliko ih je odbaceno jer ih nijedan treci kadar nije potvrdio - vidi minTriangleSupport
    uint32_t unwitnessedEdges = 0;
};

MatchGraphResult buildMatchGraph(const std::vector<GrayImage>& images,
                                 const Intrinsics& intrinsics,
                                 const MatchGraphConfig& config = {});

}
