#pragma once
#include "Engine/Bundle.h"
#include "Engine/TwoView.h"

namespace Engine{

//=============================================================================================
// Cijeli lanac: iz samih opazanja do poza i tocaka, bez ijedne poznate poze.
//
//   1 pocetni par     dvije kamere, relativna poza RANSAC-om (S4). Prva kamera postaje ishodiste
//   2 prve tocke      triangulacija onoga sto oba kadra vide (S1)
//   3 nova kamera     PnP iz vec rijesenih tocaka (S2), pa triangulacija onoga sto ta kamera
//                     otkljucava
//   4 bundle          poze i tocke zajedno (S3), s Huberom (S5)
//
// POCETNI PAR: uzima se najudaljeniji kadar koji s prvim jos dijeli dovoljno tocaka, jer sitna
// baza znaci lose odredjenu dubinu (S1 je odbio bazu od milimetra).
//
// KOLIKO TO VRIJEDI, IZMJERENO NA DVIJE SCENE - jer prva sama po sebi zavarava:
//
//   luk 80 st    susjedni par daje ISTI rezultat do zadnje znamenke; razlikuje se samo mjerilo
//                (0.62 naspram 0.097). Kasniji bundle izravna razliku
//   luk 6 st     susjedni par: rotacija 3.29 st, tocke 21.7 m, reprojekcija 1.28 px
//                siroki par:   rotacija 0.021 st, tocke 0.079 m, reprojekcija 0.527 px
//
// Dakle sirok par nije ukras, ali se to vidi tek kad je cijeli luk uzak - kad ni najsira baza nije
// siroka. Na prvoj sceni sam to pokusao dokazati i nisam mogao; dokaz je dala tek druga.
//
// SIRINA LUKA MEDJUTIM JEST BITNA, i to za TOCKE a ne za kamere. Izmjereno na istoj sceni:
//
//   luk kamera     6 st     11 st    23 st    46 st    80 st
//   tocke        0.079 m   0.082    0.012    0.0074   0.0056
//   kamere       0.021 st  0.070    0.036    0.045    0.021
//
// Sve kamere se rijese u svakom slucaju - drzi ih mnostvo tocaka - a dubina tocaka trpi, jer je
// kut pod kojim se zraka sijeku malen.
//
// PNP TREBA POCETNU POZU, a P3P jos nemamo. Nova kamera zato krece od poze najblizeg vec
// rijesenog kadra. Za snimku iz drona je to razumno - susjedni kadrovi su blizu - i u S2 je
// izmjereno da PnP stize i s metra i petnaest stupnjeva promasaja. Kad zatreba pravi P3P, ovo je
// mjesto gdje ulazi.
//
// MJERILO OSTAJE SLOBODNO: pomak pocetnog para je jedinicni, pa je cijela rekonstrukcija tocna do
// jednog broja. Isto kao u S3 i S5.
//=============================================================================================

struct ReconstructConfig{
    RansacConfig ransac;
    double huberPixels = 2.0;

    //Koliko piksela smije promasiti kamera da bi se prihvatila kao rijesena
    double acceptPixels = 4.0;

    //Najmanje vec rijesenih tocaka koje nova kamera mora vidjeti
    uint32_t minPointsForPose = 12;

    //Koliko se parova provjeri pri izboru pocetnog para. Mjera nije koliko tocaka par dijeli nego
    //koliko ih se iz njega dade triangulirati, a to trazi RANSAC po paru - pa se provjerava samo
    //najprometnije. Vidi komentar uz izbor u Reconstruct.cpp
    uint32_t initialPairCandidates = 30;

    //ZADANI POCETNI PAR, za mjerenje. Kad su oba ista, par se bira kao i inace. Postoji zato sto je
    //pitanje "je li kriv izbor pocetnog para ili sve ostalo" inace nemjerljivo
    uint32_t forceInitialA = 0, forceInitialB = 0;

    //=========================================================================================
    // KOLIKO SE POCETNIH PAROVA ISPROBA DO KRAJA.
    //
    // Cijela rekonstrukcija visi o prvom paru, i izmjereno je koliko: na istom grafu par 86-89 daje
    // 4.69 st greske rotacije, a par 80-89 daje 119.94 st. Oba prolaze sve provjere koje izbor
    // para ima - dovoljno tocaka, dovoljan kut, dvoprizorna poza rijesena - pa se razlika NE VIDI
    // dok se ne izgradi cijela scena.
    //
    // Zato se ovdje ne bira nego POKUSAVA: prvih nekoliko kandidata se izgradi do kraja i zadrzi se
    // najbolji. Mjera je broj rijesenih kamera, pa medijan kuta pod kojim se zrake sijeku - vidi
    // Reconstruction::medianTriangulationAngle. Reprojekcija se NE koristi, jer ona krivo rjesenje
    // ne prijavi: ono se samo sa sobom slaze jednako dobro kao ispravno.
    //
    // ZADANO CETIRI. Izmjereno na cetiri razlicita grafa iste snimke, protiv COLMAP-ovog rjesenja:
    //
    //   graf                          jedan pokusaj   cetiri pokusaja
    //   binarni, bacanje                  6.60 st          6.60 st
    //   binarni, rastavljanje svj. 2      4.69 st          4.69 st
    //   SIFT                            163.80 st         10.71 st
    //   rastavljanje bez praga          119.08 st          4.65 st
    //
    // Gdje je prvi izbor bio dobar, ne mijenja nista - doslovno, jer izabere isti par. Gdje nije,
    // razlika je dvadeset do trideset puta.
    //
    // CETIRI NIJE UVIJEK DOSTA. Jedan od ta cetiri grafa (rastavljanje uz tri svjedoka) ostaje
    // kriv i nakon cetiri pokusaja - 126.48 st - a s OSAM padne na 5.75 st, uz bazu 4.67 i smjer
    // koraka 1.97. Kad rjesenje izgleda lose a medianTriangulationAngle je bitno uzi nego sto graf
    // dopusta, prvo sto vrijedi probati je vise pokusaja.
    //
    // CIJENA JE CETVEROSTRUKO VRIJEME: 325 s po pokusaju na 101 kadru 4K snimke. To je svjesna
    // razmjena - solver koji tiho vrati putanju krivu 119 stupnjeva nije upotrebljiv ni koliko god
    // brz bio.
    //
    // Jedan znaci kao prije - uzme se prvi izbor i s njim se ide do kraja
    //=========================================================================================
    uint32_t initialPairTrials = 4;

    //=========================================================================================
    // DRUGO MISLJENJE ZA KAMERU KOJA SE ZAGLAVILA.
    //
    // Kamera se registrira PnP-om nad tockama koje u tom trenutku postoje, i tada moze sjesti u
    // krivo rjesenje - obicno kad su te tocke bile lose triangulirane. Globalni bundle je poslije
    // ne izvlaci: on radi lokalne korake, a kriva poza je u drugom minimumu.
    //
    // Izmjereno: SIFT-ov graf daje zaokret iz kadra u kadar 0.101 st medijan, a NAJGORI korak
    // 26.072 st - dakle jedna jedina kamera nosi cijelu gresku.
    //
    // Ovdje se takva kamera prepozna po tome sto joj je vlastita reprojekcija visestruko veca od
    // opce, pa joj se poza racuna IZNOVA - i to polazeci od susjedne rijesene kamere, ne od
    // vlastite, jer bi se inace vratila u isti minimum. Zamjena se prihvaca samo ako je bolja.
    //
    // ZADANO ISKLJUCENO, JER NE OKIDA. Na SIFT-ovom grafu gdje jedna kamera nosi 26 st greske,
    // nijedna kamera nema reprojekciju trostruko iznad opce - zaglavljena kamera je SAMODOSLJEDNO
    // kriva, jer je registrirana nad tockama koje su i same krive. Ista pouka kao svugdje danas:
    // reprojekcija ne prijavi krivo rjesenje.
    //
    // Ostaje jer je detektor sam po sebi ispravan za drugu vrstu kvara - kameru koja je losa a to
    // se na njoj i vidi. Broj je koliko puta veca od opce reprojekcije smije biti
    //=========================================================================================
    double rescueFactor = 0.0;

    //=========================================================================================
    // DETEKTOR KOJI OKIDA: NAGLI SKOK U NIZU.
    //
    // Za snimku vrijedi nesto sto reprojekcija ne zna - kadrovi idu redom, pa se kamera izmedju dva
    // susjedna kadra pomakne malo. Zaglavljena kamera se time prepozna odmah: na SIFT-ovom grafu je
    // zaokret iz kadra u kadar 0.101 st medijan, a najgori korak 26.072 st - dvjesto pedeset puta.
    //
    // Sumnjiva je kamera kroz koju je put DULJI nego preko nje: zbroj dvaju susjednih zaokreta
    // naspram zaokreta izmedju njezinih susjeda. Za ispravnu kameru su ta dva gotovo jednaka, jer
    // se zaokreti zbrajaju oko iste osi; za zaokrenutu je razlika dvostruki zaokret.
    //
    // Pravilo "oba susjedna koraka su velika" NE radi, i to je izmjereno: zaokret krive kamere se s
    // jedne strane zbraja s gibanjem a s druge oduzima. Kamera zaokrenuta 25 st uz korak od 11.46
    // daje susjedne korake 36.32 i 13.90 - jedan golem, drugi posve obican.
    //
    // Nula iskljucuje. Broj je koliko puta veci od medijana korak smije biti.
    //
    // VRIJEDI SAMO ZA NIZ. Kad redni brojevi kamera nisu redoslijed snimanja, ovo nema smisla i
    // mora ostati iskljuceno
    //=========================================================================================
    double stepOutlierFactor = 0.0;

    //=========================================================================================
    // SAV: MJESTO NA KOJEM SE LANAC PRESIDRAO.
    //
    // Izmjereno na SIFT-ovom grafu: nas zaokret iz kadra u kadar je 0.101 st medijan, ali kod
    // kadra 37 iznosi 22.977 st a kod kadra 65 jos 24.093 - dok je stvarni oko 2 st. Izmedju tih
    // mjesta se sve slaze. Snimka je time razlomljena na tri dijela, svaki uredan u sebi, spojena
    // dvama zaokretima od po dvadesetak stupnjeva.
    //
    // NIJE ISKOCENA KAMERA - ta bi dala dva losa koraka zaredom, a ovdje je los samo jedan pa se
    // sve iza njega nastavlja uredno. Nije ni uzak most: preko kadra 65 prelazi 2026 tocaka, od
    // kojih 1039 prezivi ciscenje. Nije ni izbor pocetnog para: osam pokusaja daje isto.
    //
    // Ono sto ostaje jest da su kamere iza sava registrirane dok su tocke ispred jos bile lose, pa
    // su sjele krivo i povukle svoje tocke za sobom. Ovdje se to rastavlja: pozе od sava nadalje se
    // odbacuju, tocke se slozu iznova samo iz glave niza, i rep se registrira ponovno - sada nad
    // tockama koje su bitno bolje nego kad je prvi put pokusao.
    //
    // Zadrzava se samo ako je ishod bolji, istom mjerom kao kod pocetnih parova.
    //
    // Nula iskljucuje. Broj je koliko puta veci od medijana NAS VLASTITI korak smije biti; ova
    // provjera ne trazi nikakvu istinu izvana. Vrijedi samo kad su redni brojevi kamera redoslijed
    // snimanja
    //=========================================================================================
    double seamFactor = 0.0;

    //=========================================================================================
    // KOJI SE DIO NIZA ZADRZAVA, a sve izvan njega gradi iznova. Puni ga popravak sava sam;
    // pozivatelj ga ne dira. keepTo nula znaci bez ogranicenja.
    //
    // ZASTO RASPON A NE SAMO "ODBACI REP". Prvo sam odbacivao kamere od sava nadalje, i to je
    // popravilo drugi sav ali ne i prvi - jer je SIDRO bilo u odbacenom dijelu. Pocetni par ovog
    // rjesenja je 81-83, a savovi su kod 37 i 65; odbacivanjem repa od 37 ostane samo glava niza
    // [0..36], a to je bas onaj tamni dio snimke koji COLMAP nije uspio registrirati uopce - pa se
    // rep ponovno presidri na slabo.
    //
    // Ispravno je zadrzati dio koji SADRZI POCETNI PAR, jer je on jedini za koji se zna da je
    // gradjen iz dobrog sjemena, i sve ostalo registrirati prema njemu
    //=========================================================================================
    uint32_t keepFrom = 0, keepTo = 0;

    //Parovi koje ne treba ponovno probati. Puni ga visestruki pokusaj sam; pozivatelj ga ne dira
    std::vector<std::pair<uint32_t, uint32_t>> skipInitialPairs;

    //=========================================================================================
    // KOLIKO DALEKO MORA BITI SLJEDECI POKUSAJ od onih koji su vec probani, u kadrovima.
    //
    // Zamisao je bila da svi pokusaji ne zavrse u istom dijelu snimke: kandidati su poredani po
    // broju zajednickih tocaka, a to je svojstvo PODRUCJA - gdje je tekstura bogata, ondje svi
    // parovi dijele mnogo. Na 101 kadru su sva cetiri izabrana para bila izmedju kadra 80 i 99.
    //
    // ZADANO NULA, DAKLE ISKLJUCENO - i to je izmjereno. Uz razmak od 12 kadrova (101 podijeljeno
    // na osam):
    //
    //                          bez razmicanja   s razmicanjem
    //   rastavljanje bez praga     4.65 st        119.08 st
    //   rastavljanje, svjedoka 3 126.48 st        142.98 st
    //   SIFT                      10.71 st          9.71 st
    //
    // Razlog je jednostavan kad se vidi: dobri parovi zive BAS u tom susjedstvu. Na ovoj snimci je
    // najbolji par 90-92, a prvi izbor 82-85 - sredista su im sedam kadrova razmaknuta, dakle
    // razmicanje od dvanaest ga izbaci. Bogato podrucje nije zamka nego mjesto gdje se scena
    // stvarno dade rijesiti.
    //
    // Mjeri se razmak sredista para
    //=========================================================================================
    uint32_t initialPairSpread = 0;

    //PARALAKSA. Koliko se dubini smije vjerovati ne odlucuje kut sam po sebi nego kut zajedno sa
    //zaristem i sumom, pa se prag ne zadaje nego IZVODI:
    //
    //    sum od s piksela na zaristu f daje relativnu gresku dubine  s / (f * kut u radijanima)
    //    pa je najmanji smisleni kut                                 s / (f * dopustena greska)
    //
    //Zato ovdje stoji ono sto se stvarno trazi - kolika se greska dubine prihvaca - a ne kut.
    //Isti broj onda vrijedi i za mobitel i za dron i za GoPro, jer se zariste razlikuje a
    //zahtjev ne. Nula gasi provjeru i vraca ponasanje otprije S10.
    //
    //Izmjereno na dronskoj snimci (f = 649 px, 24 kadra): bez provjere p90 udaljenosti tocaka je
    //362304 dosega putanje - cisto smece koje reprojekcija ne kaznjava jer daleka tocka uredno
    //reprojicira ma gdje po svojoj zraki bila. Uz 0.15 rep nestane (p99 = 0.87), 218 tocaka
    //ostane, svih 24 kamera ostane, a reprojekcija se ne pomakne (0.191 px)
    double maxRelativeDepthError = 0.15;

    //APSOLUTNI POD ZA PARALAKSU, u stupnjevima. Izveden prag iznad je geometrijski tocan - duza
    //optika razlucuje kutove finije, pa joj za istu preciznost dubine treba manji kut - ali
    //dubina nije jedino sto se od tocke trazi. Tocka koja se vidi pod tri stotinke stupnja je za
    //postavljanje SLJEDECE KAMERE prakticki degenerirana ma kako dobro joj dubina ispala.
    //
    //Zato je ovo pod, a ne zamjena: uzima se ono sto je strože. Bez njega isti broj 0.15 na
    //dronskoj snimci (f = 649) znaci 0.294 stupnja, a na 4K snimci (f = 5285) samo 0.036 -
    //osam puta labavije, na istoj postavci.
    //
    //Izmjereno na COLMAP-ovim korespondencijama (65 kamera), prag prihvacanja 4 px:
    //
    //   pod   kamere      tocke   reprojekcija
    //   0.0  45 od 65     15984     1.742 px
    //   0.5  65 od 65     26498     1.146 px
    //
    //COLMAP na istim podacima filtrira ispod 1.5 stupnja (filter_min_tri_angle).
    //
    //ZADANO 1.0, i to je odlucila DRUGA snimka od one gore. Na COLMAP-ovim korespondencijama je
    //svejedno - 0.5, 1.0 i 1.5 daju isti rezultat do zadnje znamenke (0.735 px, 65 od 65 kamera,
    //polozaj 0.2 posto, rotacija 0.51 st). Na nasima nije:
    //
    //   pod   kamere    polozaj   rotacija
    //   0.5   74/101     21.2 %   176.81 st
    //   1.0   96/101      4.4 %     7.09 st
    //   1.5   86/101      6.3 %    10.56 st
    //
    //Dakle broj koji dobar ulaz ne osjeti, los ulaz osjeti jako - i zato stoji ondje gdje je
    //losem ulazu najbolje, a dobrom svejedno
    double minParallaxDegrees = 1.0;

    //Sum u pikselima koji se pripisuje pracenju uglova. Nije mjerenje nego pretpostavka, i zato
    //stoji ovdje gdje se vidi. Mjerena reprojekcija bi bila kriva zamjena: bundle je namjesti na
    //podatke pa ispadne manja od pravog suma, i prag bi izasao prenizak
    double assumedPixelNoise = 0.5;

    uint32_t bundleIterations = 15;

    //=========================================================================================
    // CISCENJE I PONOVNA TRIANGULACIJA, u krug, nakon sto se kamere iscrpe.
    //
    // Postoji zato sto je izmjereno da bundle nije kriv: pusten na COLMAP-ovo gotovo rjesenje
    // iste snimke on ga drzi i jos neznatno popravi (0.7461 -> 0.7380 px), a na nasoj
    // rekonstrukciji sjedne na 3.34 px i ne mice se koliko god iteracija dobio. Rjesenje dakle
    // nije lose zato sto bundle ne zna sici nego zato sto ga put dovede u drugi minimum - a iz
    // njega se izlazi samo mijenjanjem onoga sto bundle dobije.
    //=========================================================================================

    //Koliko krugova. Nula iskljucuje i vraca ponasanje otprije
    uint32_t refineRounds = 5;

    //Donja granica praga za izbacivanje opazanja, u pikselima
    double filterPixels = 4.0;

    //Prag je visekratnik TRENUTNOG medijana dok je on iznad filterPixels. Fiksni prag nad
    //rjesenjem koje je tek na tri i pol piksela odbacio bi u prvom krugu pola scene; ovako je
    //ciscenje u pocetku blago i steze se samo od sebe
    double filterMedians = 2.5;

    //KOLIKO CESTO USRED GRADNJE, kao visekratnik broja vec rijesenih kamera. 1.25 znaci: ocisti
    //kad ih naraste za cetvrtinu.
    //
    //Ciscenje samo na kraju lijeci posljedicu umjesto uzroka - do tada je put vec zasao u losiji
    //minimum, a lokalni korak iz njega ne izlazi. Usput se u njega uopce ne ulazi.
    //
    //Nula znaci samo na kraju. Cijena je linearna u broju ciscenja, a svako je jedan prolaz kroz
    //sve tocke plus bundle
    double refineGrowth = 1.25;

    //=========================================================================================
    // KAKO SE BIRA SLJEDECA KAMERA.
    //
    // Dosad: ona koja vidi NAJVISE vec rijesenih tocaka. To je razumno i pogresno iz istog
    // razloga - broj ne kaze nista o tome GDJE su te tocke u slici. Kamera koja ih vidi tisucu,
    // sve zbijene u jedan kut, daje lose uvjetovanu pozu: rotacija i pomak se ondje mijesaju i
    // PnP ih ne razlucuje. Takva kamera se postavi malo krivo, tocke koje ona otkljuca nastanu
    // malo krivo, i pogreska ostane u tom dijelu snimke.
    //
    // A upravo to nam je i izmjereno: greska po kameri kroz snimku ima doline i grebene - 0.8 px
    // na jednim kadrovima, 5.5 px na drugima - dakle vezana je uz dijelove snimke, a ne uz
    // udaljenost od pocetnog para.
    //
    // Umjesto broja: PIRAMIDA VIDLJIVOSTI. Slika se dijeli na 2x2, pa 4x4, sve do 64x64. Celija
    // koja prvi put dobije tocku donese tezinu jednaku BROJU CELIJA na svojoj razini, i vise
    // nikad. Time broj tocaka odlucuje dok ih je malo, a njihov RASPORED cim ih ima dovoljno -
    // jer zbijene tocke pune malo celija koliko god ih bilo.
    //
    // Sest razina i tezina jednaka broju celija su ono sto COLMAP koristi (Schoenberger i Frahm,
    // Structure-from-Motion Revisited, 4.2), a njihova je usporedba pokazala da bas taj izbor
    // nadmasuje biranje po broju tocaka.
    //
    // ZADANO ISKLJUCENO, i to je mjereno a ne pretpostavka. Na COLMAP-ovim korespondencijama (65
    // kamera, 3564 opazanja po kameri) razlike NEMA: 1.146 px i s piramidom i bez nje, najgora
    // kamera 4.20 naspram 4.29 px. Razlog je vidljiv iz same mjere - kad svaka kamera vidi tri i
    // pol tisuce tocaka rasutih po cijeloj slici, sve kandidate piramida ocijeni jednako i
    // raspored nema sto razluciti. COLMAP je gradi za neuredjene zbirke fotografija, gdje se
    // pokrivenost izmedju slika razlikuje u redovima velicine.
    //
    // Ostaje jer NASE korespondencije nisu takve: oko 300 opazanja po kadru i cesto zbijene ondje
    // gdje je teksture. Tamo bi mogla nesto znaciti, i tada se ukljucuje jednim poljem umjesto da
    // se pise iznova
    //=========================================================================================
    bool visibilityScore = false;

    //=========================================================================================
    // ODBACIVANJE PROMASAJA PRI POSTAVLJANJU KAMERE.
    //
    // Nova kamera se postavlja iz tocaka koje su vec rijesene - a one nisu sve dobre: nastale su
    // iz dosad postavljenih poza, pa je dio njih na krivoj dubini. Dosad su sve ulazile s punom
    // tezinom, a Huber je promasaj samo pritegnuo umjesto da ga izbaci; kamera se zatim odbijala
    // po medijanu preko SVIH tocaka, pa je dobra poza s petinom losih tocaka ispadala kao losa
    // kamera.
    //
    // Sada se trazi najveci skup opazanja koji se slaze, poza se dotjera SAMO na njemu, i odluka
    // se donosi po tom skupu. Vidi solvePoseRansac
    //=========================================================================================
    bool poseRansac = true;

    //Koliko piksela smije promasiti opazanje da bi se racunalo kao slaganje. Siroko namjerno -
    //ovo razlucuje promasaj od suma, ne dobru pozu od lose. Isti broj koji drzi COLMAP
    double poseMaxError = 12.0;

    //Najmanji udio opazanja koja se slazu. Ispod toga poza nije nadjena nego pogodjena
    double poseMinInlierRatio = 0.25;
};

struct Reconstruction{
    std::vector<Pose> poses;
    std::vector<uint8_t> posed;          //1 za kameru koja je rijesena

    std::vector<glm::vec3> points;
    std::vector<uint8_t> solved;         //1 za tocku koja je triangulirana

    uint32_t posedCameras = 0;
    uint32_t solvedPoints = 0;

    //Koliko je opazanja zavrsilo U RJESENJU, i koliko ih je ciscenje izbacilo kao promasaje.
    //Zbroj nije nuzno broj ulaznih opazanja: ona koja pripadaju nerijesenoj kameri ili
    //netrianguliranoj tocki nisu ni jedno ni drugo
    uint32_t usedObservations = 0;
    uint32_t filteredObservations = 0;

    //KOJE JE OPAZANJE PREZIVJELO, usporedno s ulaznim nizom - 1 za ono koje je u rjesenju.
    //
    //Postoji zbog dijagnoze koju bez njega nije bilo moguce napraviti: ciscenje izbacuje cetvrtinu
    //do trecine svih opazanja, a ako medju njima budu bas ona koja povezuju dva dijela snimke, veza
    //puca i sve iza nje se zaokrene - a reprojekcija to ne prijavi jer se svaka polovica slaze sama
    //sa sobom. Tko hoce znati je li se to dogodilo, mora moci prebrojati sto je ostalo
    std::vector<uint8_t> observationUsed;

    //PO OPAZANJIMA KOJA SU U RJESENJU, dakle bez onih koja je ciscenje izbacilo.
    //
    //Dugo je ovdje stajao medijan preko SVIH opazanja rijesenih kamera i tocaka, ukljucujuci
    //promasaje koje je rekonstrukcija namjerno odbacila - a to je mjerilo koliko je ulaz los, ne
    //koliko je rjesenje dobro. Usporedba s COLMAP-om je time bila neposteno na nasu stetu: on
    //svoje odbacene ni ne zapise u model, pa mu se mjeri samo ono sto je zadrzao
    double medianReprojection = 0.0;
    double parallaxLimitDegrees = 0.0;   //kut izveden iz zarista i suma, onaj koji je stvarno vrijedio

    //POCETNI PAR, ONAKO KAKO JE ZAVRSIO. Cijela rekonstrukcija visi o njemu - iz njega nastaju prve
    //tocke na koje se zatim oslanja svaka sljedeca kamera - pa kad rjesenje ispadne krivo, prvo
    //pitanje je odakle je krenulo. Bez ovoga se na to nije dalo odgovoriti bez prekapanja po kodu
    //MEDIJAN KUTA POD KOJIM SE ZRAKE SIJEKU, nad konacnim pozama. Dubina iz uske baze ne postoji
    //koliko god tocaka bilo, pa je ovo jedina mjera kakvoce koja ne trazi poznatu istinu - i jedina
    //koja razlikuje dobru rekonstrukciju od one koja se sama sa sobom slaze a kriva je.
    //Izmjereno na istoj snimci: rjesenja s bazom 4.7-4.9 st daju 4.7-6.8 st greske rotacije protiv
    //COLMAP-a, a ona s 2.9-3.1 st daju 119 st. Reprojekcija ih ne razlikuje - obje su oko 1.65 px
    double medianTriangulationAngle = 0.0;

    //Koliko je kamera dobilo drugo misljenje - vidi ReconstructConfig::rescueFactor
    uint32_t rescuedCameras = 0;

    //Kod koje je kamere nadjen sav i je li rastavljanje pomoglo - vidi ReconstructConfig::seamFactor
    uint32_t seamAt = 0;
    uint32_t seamsFound = 0;
    uint8_t seamRepaired = 0;

    uint32_t initialA = 0, initialB = 0;
    double initialAngle = 0.0;       //medijan kuta pod kojim se zrake tog para sijeku
    uint32_t initialPoints = 0;      //koliko se iz njega dalo triangulirati

    bool ok = false;
};

//OPAZANJA MORAJU BITI VEC ISPRAVLJENA ZA DISTORZIJU, a intrinsics predan ovamo mora biti cisti
//pinhole (k1 = k2 = 0).
//
//Razlog je u tome sto jakobijani - i pozin i bundleov - racunaju derivaciju PINHOLE projekcije.
//Ako intrinsics nosi k1, reprojekcija se mjeri s distorzijom a korak se racuna bez nje, pa
//optimizacija ide u smjeru koji ne smanjuje ono sto mjeri. Izmjereno na COLMAP-ovim
//korespondencijama: ispravljena opazanja uz pinhole daju 0.735 px, a neispravljena uz model s k1
//daju 1.091 px i sedam posto odbacenih opazanja umjesto tri desetinke posto.
//
//VideoSolve to radi tocno - ispravi opazanja pa izricito nulira k1 i k2. Ovdje pise zato sto je
//greska tiha: sve se prevede, sve se izvrti, i rezultat je samo losiji
Reconstruction reconstruct(const std::vector<Observation>& observations,
                           size_t cameraCount,
                           size_t pointCount,
                           const Intrinsics& intrinsics,
                           const ReconstructConfig& config = {});

}
