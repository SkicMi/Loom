#pragma once
#include <functional>
#include "Engine/Bundle.h"
#include "Engine/TwoView.h"

namespace Engine{

//=============================================================================================
// The whole chain: from raw observations to poses and points, with not a single known pose.
//
//   1 initial pair   two cameras, relative pose via RANSAC (S4). The first camera becomes the origin
//   2 first points   triangulate what both frames see (S1)
//   3 new camera     PnP from the already-solved points (S2), then triangulate what that camera
//                    unlocks
//   4 bundle         poses and points together (S3), with Huber (S5)
//
// INITIAL PAIR: take the farthest frame that still shares enough points with the first, because a
// narrow baseline means poorly determined depth (S1 rejected a one-millimeter baseline).
//
// HOW MUCH THAT IS WORTH, MEASURED ON TWO SCENES - because the first alone is misleading:
//
//   80 deg arc    a neighboring pair gives THE SAME result to the last digit; only the scale
//                 differs (0.62 vs 0.097). A later bundle irons out the difference
//   6 deg arc     neighboring pair: rotation 3.29 deg, points 21.7 m, reprojection 1.28 px
//                 wide pair:   rotation 0.021 deg, points 0.079 m, reprojection 0.527 px
//
// So a wide pair is not decoration, but it only shows once the whole arc is narrow - when even
// the widest baseline is not wide. I tried to prove this on the first scene and could not; the
// second one finally did.
//
// THE WIDTH OF THE ARC IS IMPORTANT THOUGH, and for POINTS rather than cameras. Measured on the
// same scene:
//
//   camera arc    6 deg   11 deg   23 deg   46 deg   80 deg
//   points      0.079 m   0.082    0.012    0.0074   0.0056
//   cameras     0.021 deg  0.070    0.036    0.045    0.021
//
// All cameras get solved either way - a wealth of points holds them up - while point depth
// suffers, because the angle at which the rays meet is small.
//
// PNP NEEDS AN INITIAL POSE, and we do not have P3P yet. A new camera therefore starts from the
// pose of the nearest already-solved frame. For a drone shot that is reasonable - neighboring
// frames are close - and in S2 PnP was measured to arrive even from a meter and fifteen degrees
// of misalignment. When a real P3P is needed, this is where it goes in.
//
// THE SCALE STAYS FREE: the initial pair's translation is unit, so the whole reconstruction is
// correct up to one number. Same as in S3 and S5.
//=============================================================================================

//Forward-declared, because ReconstructConfig::onProgress speaks of it and it is described below
struct Reconstruction;

struct ReconstructConfig{
    RansacConfig ransac;
    double huberPixels = 2.0;

    //How many pixels a camera may miss to be accepted as solved
    double acceptPixels = 4.0;

    //Minimum number of already-solved points a new camera must see
    uint32_t minPointsForPose = 12;

    //How many pairs are checked when choosing the initial pair. The measure is not how many points
    //a pair shares but how many can actually be triangulated from it, and that demands RANSAC per
    //pair - so only the busiest are checked. See the comment next to the choice in Reconstruct.cpp
    uint32_t initialPairCandidates = 30;

    //FORCED INITIAL PAIR, for measurement. When both are equal, the pair is chosen as usual. It
    //exists because the question "is the initial pair choice wrong or everything else" would
    //otherwise be unmeasurable
    uint32_t forceInitialA = 0, forceInitialB = 0;

    //=========================================================================================
    // HOW MANY INITIAL PAIRS ARE TRIED TO COMPLETION.
    //
    // The whole reconstruction hangs on the first pair, and how much was measured: on the same
    // graph pair 86-89 gives 4.69 deg of rotation error while pair 80-89 gives 119.94 deg. Both
    // pass every check the pair choice has - enough points, enough angle, two-view pose
    // solved - so the difference is NOT SEEN until the whole scene is built.
    //
    // So this does not choose but TRIES: the first few candidates are built to completion and the
    // best is kept. The measure is the number of solved cameras, then the median angle at which
    // the rays meet - see Reconstruction::medianTriangulationAngle. Reprojection is NOT used,
    // because it does not report a wrong solution: it agrees with itself just as well as a
    // correct one.
    //
    // DEFAULT FOUR. Measured on four different graphs of the same shot, against COLMAP's
    // solution:
    //
    //   graph                        one trial     four trials
    //   binary, discard                 6.60 deg       6.60 deg
    //   binary, split witnesses 2       4.69 deg       4.69 deg
    //   SIFT                          163.80 deg      10.71 deg
    //   split without threshold       119.08 deg       4.65 deg
    //
    // Where the first choice was good it changes nothing - literally, because it picks the same
    // pair. Where it was not, the difference is twenty to thirty times.
    //
    // FOUR IS NOT ALWAYS ENOUGH. One of those four graphs (split with three witnesses) stays
    // wrong even after four trials - 126.48 deg - and with EIGHT drops to 5.75 deg, with a
    // baseline of 4.67 and direction of travel of 1.97. When a solution looks bad and
    // medianTriangulationAngle is noticeably narrower than the graph allows, the first thing
    // worth trying is more trials.
    //
    // THE COST IS FOUR TIMES THE TIME: 325 s per trial on 101 frames of a 4K shot. That is a
    // deliberate trade - a solver that silently returns a trajectory wrong by 119 degrees is
    // unusable no matter how fast it is.
    //
    // One means as before - take the first choice and go with it to completion
    //=========================================================================================
    uint32_t initialPairTrials = 4;

    //Full trials with already-chosen initial pairs share no mutable state and can be built
    //concurrently. False exists as a reference path for the bit-identical test and measurement;
    //the pair choice and winner rule must stay the same in both modes.
    bool parallelInitialPairTrials = true;


    //=========================================================================================
    // A SECOND OPINION FOR A STUCK CAMERA.
    //
    // A camera is registered via PnP over the points that exist at that moment, and can then land
    // in a wrong solution - usually when those points were poorly triangulated. A later global
    // bundle does not pull it out: it makes local steps, and a wrong pose sits in another minimum.
    //
    // Measured: the SIFT graph gives a frame-to-frame turn of 0.101 deg median, and the WORST
    // step 26.072 deg - so a single camera carries the whole error.
    //
    // Here such a camera is recognized by its own reprojection being several times larger than
    // the overall one, so its pose is recomputed - starting from a neighboring solved camera, not
    // from its own, because it would otherwise fall back into the same minimum. The replacement
    // is accepted only if better.
    //
    // DEFAULT OFF, BECAUSE IT DOES NOT FIRE. On the SIFT graph where one camera carries 26 deg of
    // error, no camera has a reprojection three times above the overall one - the stuck camera is
    // SELF-CONSISTENTLY wrong, because it was registered over points that are themselves wrong.
    // The same lesson as everywhere today: reprojection does not report a wrong solution.
    //
    // It stays because the detector is in itself right for another kind of fault - a camera that
    // is bad and it shows on it. The number is how many times above the overall reprojection it
    // may be
    //=========================================================================================
    double rescueFactor = 0.0;

    //=========================================================================================
    // THE DETECTOR THAT FIRES: A SUDDEN JUMP IN THE SEQUENCE.
    //
    // There is something about a shot that reprojection does not know - frames go in order, so a
    // camera moves little between two neighboring frames. A stuck camera is thereby recognized
    // right away: on the SIFT graph the frame-to-frame turn is 0.101 deg median, and the worst
    // step 26.072 deg - two hundred fifty times.
    //
    // A camera is suspect when the path THROUGH it is LONGER than the path ACROSS it: the sum of
    // the two adjacent turns versus the turn between its neighbors. For a correct camera these
    // two are nearly equal, because the turns add up around the same axis; for a rotated one the
    // difference is the double turn.
    //
    // The rule "both neighboring steps are large" does NOT work, and that was measured: a wrong
    // camera's turn adds to the motion on one side and subtracts on the other. A camera rotated
    // 25 deg with a step of 11.46 gives neighboring steps of 36.32 and 13.90 - one huge, the
    // other perfectly ordinary.
    //
    // Zero disables. The number is how many times above the median a step may be.
    //
    // ONLY VALID FOR A SEQUENCE. When the camera indices are not the shooting order, this makes
    // no sense and must stay disabled
    //=========================================================================================
    double stepOutlierFactor = 0.0;

    //=========================================================================================
    // HOW OFTEN AN OBSERVATION IS HELD OUT OF THE COMPUTATION, to serve as a check.
    //
    // Zero disables and the solution is then bit-for-bit the same as before. Ten means every
    // tenth observation does not enter the computation but is instead used at the end to CHECK
    // the solution.
    //
    // Only that observation is held out whose point still stays seen from at least three frames
    // without it - otherwise instead of a check you would get a shorter track, and that is
    // changing the input, not measuring
    //=========================================================================================
    uint32_t holdOutEvery = 0;

    //=========================================================================================
    // SEAM: THE PLACE WHERE THE CHAIN RESEEDED.
    //
    // Measured on the SIFT graph: our frame-to-frame turn is 0.101 deg median, but at frame 37 it
    // is 22.977 deg and at frame 65 a further 24.093 - while the real one is about 2 deg. Between
    // those places everything agrees. The shot is thereby broken into three parts, each tidy in
    // itself, joined by two turns of about twenty degrees each.
    //
    // It is NOT an out-of-place camera - that would give two bad steps in a row, while here only
    // one is bad and everything after it continues neatly. It is not a narrow bridge either: 2026
    // points cross frame 65, of which 1039 survive cleaning. It is not the initial pair choice
    // either: eight trials give the same.
    //
    // What remains is that the cameras behind the seam were registered while the points ahead
    // were still bad, so they sat down wrong and pulled their points along. Here this is
    // dismantled: the poses from the seam onward are discarded, the points are rebuilt from just
    // the head of the sequence, and the tail is registered again - now over points that are
    // considerably better than when it first tried.
    //
    // It is kept only if the outcome is better, measured the same way as for initial pairs.
    //
    // Zero disables. The number is how many times above the median OUR OWN step may be; this
    // check does not ask for any external truth.
    //
    // DEFAULT TEN, and the cost is zero when there is no seam: the search runs on the solution
    // that would be built anyway, so the extra computation is paid only when a seam is actually
    // found. Measured on the main path of a real shot: zero seams and a result bit-for-bit
    // identical to without this. On the SIFT path, where there are seams: rotation without
    // alignment 56.900 -> 27.900 deg.
    //
    // A wrongly detected seam costs time, not quality, because the fix is kept only if there are
    // FEWER seams.
    //
    // ONLY VALID FOR A SEQUENCE. When the camera indices are not the shooting order, this should
    // be switched off
    //=========================================================================================
    double seamFactor = 10.0;

    //=========================================================================================
    // WHICH PART OF THE SEQUENCE IS KEPT, and everything outside it is rebuilt. The seam fix
    // fills it itself; the caller does not touch it. A keepTo of zero means no limit.
    //
    // WHY A RANGE AND NOT JUST "DISCARD THE TAIL". First I discarded cameras from the seam
    // onward, and that fixed the second seam but not the first - because the ANCHOR was in the
    // discarded part. The initial pair of this solution is 81-83, and the seams are at 37 and 65;
    // discarding the tail from 37 leaves only the head of the sequence [0..36], and that is
    // exactly the dark part of the shot that COLMAP failed to register at all - so the tail
    // reseeds weakly again.
    //
    // The correct thing is to keep the part that CONTAINS THE INITIAL PAIR, because it is the
    // only one known to be built from good seed, and register everything else relative to it
    //=========================================================================================
    uint32_t keepFrom = 0, keepTo = 0;

    //Whether the tail is rebuilt after the segment is kept. The seam fix builds it - that is its
    //purpose; trimming to a healthy segment does NOT build it, because that would bring back
    //exactly what was just cut off
    bool reAddAfterTrim = true;

    //=========================================================================================
    // THE LARGEST HEALTHY SEGMENT INSTEAD OF ALL-OR-NOTHING.
    //
    // When the chain reseeds and the fix fails, the solution stretches across a break: two parts
    // of the shot that disagree with each other by about twenty degrees, while each is tidy in
    // itself. Delivering such a solution means delivering something silently wrong.
    //
    // Here, instead, the LONGEST run of frames with no seam inside it is kept, and that is
    // clearly reported. Fifty correct cameras are usable; eighty cameras across a break are not.
    //
    // It does not turn on by itself because it changes what the tool delivers - whoever turns it
    // on must know that the output may have fewer cameras than the shot had frames
    //=========================================================================================
    bool keepLargestHealthySegment = false;

    //Pairs that should not be tried again. The multi-trial fills it itself; the caller does not
    //touch it
    std::vector<std::pair<uint32_t, uint32_t>> skipInitialPairs;

    //=========================================================================================
    // HOW FAR THE NEXT TRIAL MUST BE from the ones already tried, in frames.
    //
    // The idea was that not all trials end up in the same part of the shot: candidates are sorted
    // by the number of shared points, and that is a property of the REGION - where texture is
    // rich, all pairs share a lot. On 101 frames all four chosen pairs were between frames 80 and
    // 99.
    //
    // DEFAULT ZERO, THAT IS, DISABLED - and that was measured. With a spread of 12 frames (101
    // divided into eight):
    //
    //                          without spread   with spread
    //   split without threshold     4.65 deg       119.08 deg
    //   split, witnesses 3         126.48 deg       142.98 deg
    //   SIFT                      10.71 deg          9.71 deg
    //
    // The reason is simple once seen: good pairs live RIGHT in that neighborhood. On this shot
    // the best pair is 90-92, and the first choice 82-85 - their centers are seven frames apart,
    // so a spread of twelve throws it out. A rich region is not a trap but the place where the
    // scene can actually be solved.
    //
    // The spread of a pair's center is measured
    //=========================================================================================
    uint32_t initialPairSpread = 0;

    //PARALLAX. How much depth is to be trusted is decided not by the angle by itself but by the
    //angle together with the focal length and noise, so the threshold is not given but DERIVED:
    //
    //    a noise of s pixels at focal length f gives a relative depth error of
    //                                                         s / (f * angle in radians)
    //    so the smallest meaningful angle is                       s / (f * allowed error)
    //
    //That is why what is actually demanded stands here - how much depth error is accepted - and
    //not the angle. The same number then holds for a phone, a drone, and a GoPro, because the
    //focal length differs and the requirement does not. Zero disables the check and restores the
    //previous S10 behavior.
    //
    //Measured on a drone shot (f = 649 px, 24 frames): without the check the p90 of point
    //distance is 362304 ranges of the trajectory - pure garbage that reprojection does not punish
    //because a distant point reprojects cleanly wherever it is along its ray. With 0.15 the tail
    //disappears (p99 = 0.87), 218 points remain, all 24 cameras remain, and the reprojection does
    //not move (0.191 px)
    double maxRelativeDepthError = 0.15;

    //ABSOLUTE FLOOR FOR PARALLAX, in degrees. The derived threshold above is geometrically
    //correct - longer optics resolve angles more finely, so they need a smaller angle for the
    //same depth precision - but depth is not the only thing demanded of a point. A point seen at
    //three hundredths of a degree is practically degenerate for placing the NEXT CAMERA however
    //well its depth turns out.
    //
    //So this is a floor, not a replacement: the stricter of the two is taken. Without it the
    //same number 0.15 on a drone shot (f = 649) means 0.294 degrees, and on a 4K shot (f = 5285)
    //only 0.036 - eight times looser, on the same setting.
    //
    //Measured on COLMAP's correspondences (65 cameras), acceptance threshold 4 px:
    //
    //   floor   cameras   points   reprojection
    //   0.0   45 of 65     15984     1.742 px
    //   0.5   65 of 65     26498     1.146 px
    //
    //COLMAP on the same data filters below 1.5 degrees (filter_min_tri_angle).
    //
    //DEFAULT 1.0, and that was decided by the SECOND shot, not the one above. On COLMAP's
    //correspondences it makes no difference - 0.5, 1.0 and 1.5 give the same result to the last
    //digit (0.735 px, 65 of 65 cameras, position 0.2 percent, rotation 0.51 deg). On ours it
    //does:
    //
    //   floor   cameras   position   rotation
    //   0.5    74/101     21.2 %    176.81 deg
    //   1.0    96/101      4.4 %      7.09 deg
    //   1.5    86/101      6.3 %     10.56 deg
    //
    //So a number that a good input does not feel, a bad input feels strongly - and that is why it
    //sits where it is best for bad input and indifferent to good
    double minParallaxDegrees = 1.0;

    //The noise in pixels attributed to corner tracking. It is not a measurement but an
    //assumption, and that is why it stands here where it can be seen. A measured reprojection
    //would be a wrong substitute: bundle fits it to the data so it turns out smaller than the
    //real noise, and the threshold would come out too low
    double assumedPixelNoise = 0.5;

    uint32_t bundleIterations = 15;

    //GLOBALNI BUNDLE NAKON DODAVANJA KAMERE. Vrijednost 1.0 cuva stari put: bundle nakon svake
    //kamere. Vrijednost veca od jedan pokrece ga kad broj rijesenih kamera naraste za taj faktor;
    //npr. 1.25 kod 3, 4, 6, 8... kamera. Zavrsni refine i njegovi bundleovi ostaju netaknuti.
    //To je bitno na velikom grafu gdje je svaki globalni bundle skuplji, a 76 uzastopnih poziva
    //rjesava gotovo isti problem. Pozivatelj smije ukljuciti rjedju kadencu tek uz vlastitu mjeru.
    double incrementalBundleGrowth = 1.0;

    //=========================================================================================
    // LOKALNI BUNDLE: koliko se kamera oko zadnje dodane smije micati. Nula znaci sve.
    //
    // ZASTO. Inkrementalni rast zove bundle nakon svake kamere i svaki put optimizira CIJELU
    // rekonstrukciju - i onih dvjesto kamera koje su odavno konvergirale. Izmjereno na kamenom
    // zidu: 229 poziva, 3107 s, dakle 13.6 s po pozivu, dok jedan poziv na kompletan graf traje
    // 14.55 s. Svaki poziv placa cijeli graf.
    //
    // Rjedja kadenca (incrementalBundleGrowth) to ne rjesava - izmjereno i odbaceno, vidi tamo.
    //
    // Prozor je drugo: nove kamere se micu, daleke stoje ALI I DALJE DRZE tocke koje vide, pa se
    // tocnost ne gubi kao kod preskakanja. Sto je izvan prozora, ovaj put se ne dira
    //=========================================================================================
    uint32_t localBundleWindow = 0;

    //=========================================================================================
    // JAVLJANJE NAPRETKA tijekom rasta. Nula znaci bez javljanja.
    //
    // ZASTO. Rekonstrukcija na pravoj snimci traje minutama i dosad se o njoj nije znalo nista dok
    // ne zavrsi. Suicelju treba ono sto vec postoji u ovoj petlji: koje su kamere postavljene i
    // gdje su tocke - da se vidi kako scena nastaje umjesto da se ceka.
    //
    // ENGINE I DALJE NE DIRA DISK. Ovdje se samo pozove ono sto je pozivatelj dao; hoce li on to
    // zapisati, nacrtati ili baciti, nije stvar ovog sloja
    //=========================================================================================
    std::function<void(const Reconstruction&)> onProgress;
    uint32_t progressEvery = 0;      //nakon koliko novih kamera se javlja

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

struct ReconstructTiming{
    //Samo telemetrija: nijedna vrijednost ne ulazi u odluku solvera. totalSeconds je stvarno
    //zidno vrijeme poziva, ukljucujuci odbacene pocetne parove i popravak sava. Ostale faze
    //opisuju rjesenje koje je vraceno; kod jednog pokusaja njihov zbroj objasnjava cijeli poziv.
    double totalSeconds = 0.0;
    double initialPairSeconds = 0.0;
    double poseSeconds = 0.0;
    double triangulationSeconds = 0.0;
    double bundleSeconds = 0.0;
    double bundleCostSeconds = 0.0;
    double bundleLinearizeSeconds = 0.0;
    double bundleSchurSeconds = 0.0;
    double bundleDenseSolveSeconds = 0.0;
    double bundleBackSubstituteSeconds = 0.0;
    double filteringSeconds = 0.0;
    double diagnosticsSeconds = 0.0;
    uint32_t poseCalls = 0;
    uint32_t triangulationCalls = 0;
    uint32_t bundleCalls = 0;
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

    //=========================================================================================
    // KOLIKO SE RJESENJU SMIJE VJEROVATI, mjereno na opazanjima koja ga NISU gradila.
    //
    // Reprojekcija nad opazanjima koja su sudjelovala kaze koliko se rjesenje slaze samo sa sobom,
    // a to je danas sest puta zaredom bila laz: krivo rjesenje se sa sobom slaze jednako dobro kao
    // ispravno. Bundle k tome ta ista opazanja i minimizira, pa ih je duzan objasniti.
    //
    // Izdvojena opazanja nisu usla ni u triangulaciju, ni u bundle, ni u ciscenje. Ako je rjesenje
    // pogodilo scenu, ona se reprojiciraju jednako dobro kao ostala; ako je bundle upio sum, ona to
    // pokazu. Omjer to dvoje je referentni broj: blizu jedan znaci da rjesenje vrijedi i izvan
    // onoga sto je vidjelo.
    //
    // Nula u brojacu znaci da se nije izdvajalo - vidi ReconstructConfig::holdOutEvery
    //=========================================================================================
    uint32_t heldOutObservations = 0;
    double heldOutReprojection = 0.0;

    //Koliko je kamera dobilo drugo misljenje - vidi ReconstructConfig::rescueFactor
    uint32_t rescuedCameras = 0;

    //Kod koje je kamere nadjen sav i je li rastavljanje pomoglo - vidi ReconstructConfig::seamFactor
    uint32_t seamAt = 0;
    uint32_t seamsFound = 0;
    uint8_t seamRepaired = 0;

    //Koji je odsjecak zadrzan kad se rjesenje odrezalo na zdravi dio - vidi
    //ReconstructConfig::keepLargestHealthySegment. Jednaki kad se nista nije rezalo
    uint32_t healthyFrom = 0, healthyTo = 0;
    uint32_t camerasDroppedBySeam = 0;

    uint32_t initialA = 0, initialB = 0;
    double initialAngle = 0.0;       //medijan kuta pod kojim se zrake tog para sijeku
    uint32_t initialPoints = 0;      //koliko se iz njega dalo triangulirati

    //Svi pokusaji pocetnog para kad ih je bilo vise (ReconstructConfig::initialPairTrials), redom
    //kojim su probani - da se vidi zasto je pobijedio bas ovaj. Samo za ispis
    struct Trial{
        uint32_t initialA = 0, initialB = 0;
        uint32_t posedCameras = 0, solvedPoints = 0;
        double medianTriangulationAngle = 0.0, medianReprojection = 0.0;
    };
    std::vector<Trial> trials;

    ReconstructTiming timing;

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

//Ponovno racuna dijagnostiku nad POSTOJECOM geometrijom i maskama. Ne mijenja poze, tocke ni
//odluku koja su opazanja usla u rjesenje. Potrebno je nakon vanjskog bundlea (npr. zajednicke
//samokalibracije), jer su tada spremljene reprojekcije i kutovi iz stare geometrije zastarjeli.
void refreshReconstructionDiagnostics(const std::vector<Observation>& observations,
                                      const Intrinsics& intrinsics,
                                      const ReconstructConfig& config,
                                      Reconstruction& state);

}
