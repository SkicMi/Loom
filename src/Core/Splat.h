#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

//=============================================================================================
// Jedan 3D gaussian, i matematika koja od njega pravi mrlju na ekranu.
//
// Bez Vulkana i bez Spoola. Loom ne zna odakle su gaussiani dosli - datoteka je tudji posao,
// isto kao sto Mesh ne zna za .obj. Aplikacija je ta koja procitano prepise u Splat.
//
// OVDJE SU VRIJEDNOSTI VEC AKTIVIRANE. Zapis na disku nosi ih u obliku u kojem ih je trening
// optimizirao - logit, logaritam, nenormaliziran kvaternion - jer su tamo neogranicene, a
// optimizator koji smije predloziti bilo koji broj ne moze raditi s velicinom koja mora ostati
// pozitivna. Crta se ono sto je iz tog oblika izvedeno, i granica izmedju ta dva je ovaj file.
//=============================================================================================
struct Splat{
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};           //polumjeri u jedinicama scene, vec exp
    glm::quat rotation{1,0,0,0};     //vec jedinicne duljine
    float opacity = 1.0f;            //0..1, vec sigmoid
    glm::vec3 color{1.0f};           //iz stupnja 0 sfernih harmonika
};

namespace SplatMath{

//-- aktivacija -------------------------------------------------------------------------------
//Iz onoga sto u zapisu pise u ono sto se crta. Svaka od ove cetiri je jedan red koda i svaka
//se moze zaboraviti a da nista ne pukne - scena samo izgleda malo krivo

//Neprozirnost je zapisana kao logit jer sigmoid drzi rezultat u 0..1 sam od sebe
float activateOpacity(float logit);

//Polumjer je zapisan kao logaritam jer exp ne moze dati negativan broj
glm::vec3 activateScale(const glm::vec3& logScale);

//KVATERNION IZ ZAPISA NIJE JEDINICNE DULJINE. Izmjereno na pravoj sceni od 741883 gaussiana:
//duljine idu od 0.414 do 1.908, prosjek 0.954. Trening ih normalizira tek pri upotrebi, pa u
//fileu ostaju kakvi su ispali. Rotacija napravljena iz takvog kvaterniona nije rotacija nego
//rotacija plus skaliranje, i kovarijanca ispadne do dvostruko prevelika.
//
//Redoslijed je onaj iz zapisa: rot_0 je W, pa X, Y, Z
glm::quat activateRotation(const glm::vec4& stored);

//Stupanj 0 sfernih harmonika je ravna boja, ista iz svih smjerova. Konstanta je vrijednost
//nultog SH bazisa; polovica je pomak jer koeficijent smije biti negativan
glm::vec3 colorFromSH0(const glm::vec3& dc);

//-- kovarijanca ------------------------------------------------------------------------------
//Sto gaussian JEST: elipsoid opisan matricom 3x3. Iz polumjera i rotacije, kao R*S*S'*R'.
//
//Dvije stvari se ovdje daju provjeriti bez ijednog piksela, i test ih provjerava:
//   matrica je simetricna, uvijek
//   determinanta je (a*b*c)^2 za BILO KOJU rotaciju, jer rotacija ne mijenja volumen. To je
//   jedina provjera koja hvata nenormaliziran kvaternion, a on je najtiša greska ovdje
glm::mat3 covariance3D(const glm::vec3& scale, const glm::quat& rotation);

//Ista mrlja gledana kroz kameru: elipsa na ekranu, matrica 2x2 u pikselima.
//
//Perspektiva nije linearna, pa se elipsoid ne projicira u elipsu tocno. Uzima se linearna
//aproksimacija u tocki gdje gaussian jest - Jakobijan projekcije - i to je EWA splatting.
//Aproksimacija je to losija sto je gaussian dalje od osi, i zato se omjer prema osi ogranicava
//(vidi tangentLimit): bez toga Jakobijan na rubu kadra naraste i mrlja se razvuce preko pola
//ekrana umjesto da nestane.
//
//   view      matrica koja tocku scene stavlja u prostor kamere
//   focalX/Y  zariste u PIKSELIMA, isto sto nosi CameraIntrinsics
//   blur      dodaje se na dijagonalu, u pikselima na kvadrat. Gaussian manji od piksela inace
//             nestaje izmedju uzoraka i treperi kroz kadrove; 0.3 je vrijednost iz referentne
//             implementacije. Nula znaci cistu matematiku, i tako je testirana
//
// Vraca nulu kad je gaussian iza kamere - tamo projekcija nema smisla i pozivatelj ga preskace
glm::mat2 covariance2D(const glm::mat3& covariance,
                       const glm::vec3& position,
                       const glm::mat4& view,
                       float focalX, float focalY,
                       float tangentLimitX, float tangentLimitY,
                       float blur = 0.3f);

}
