#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Warp{

//=============================================================================================
// SCENA ZA FILM: stablo entiteta s komponentama, kroz vrijeme.
//
// ZASTO NE CISTI ECS. ECS je odgovor na pitanje igre: deset tisuca istih stvari koje se svaki
// kadar azuriraju, pa podaci moraju lezati gusto u memoriji. Snimanje filma pita drugo:
//
//   - IMENOVANA HIJERARHIJA. Umjetnik trazi "/Solve/Kamera", pomice roditelja i djeca idu s njim.
//     ECS hijerarhiju nema i dodaje je kao jos jednu komponentu, uz sve njezine zamke
//   - VRIJEME. Sve sto se krece ima kljuceve po kadru, a vrijednost izmedju kljuceva je racun
//     (lerp, slerp). U igri se stanje prepisuje; ovdje se ono PITA za kadar
//   - IZLAZ JE USD. Nuke, Houdini i Blender citaju prim s djecom, atribute s timeSamples. Scena
//     istog oblika se zapise bez prevodjenja
//
// Zato je ovo USD-ov oblik: ENTITET = prim (ime, roditelj, djeca, transformacija), KOMPONENTA =
// ono sto prim jest (kamera, oblak tocaka, mesh, splat). Entitet moze nositi vise komponenti;
// prazan entitet je grupa. Entiteta ce biti desetci do stotine, ne milijuni - gustoca u memoriji
// ovdje ne kupuje nista.
//
// ID JE STABILAN I NE PONAVLJA SE. Obrisani entitet ne vraca svoj broj, pa odabir u suceljima
// koji drzi stari id ne moze tiho pokazati na novi entitet
//=============================================================================================

using Id = uint32_t;
constexpr Id None = 0;

//Kljucevi po kadru. Kadar je broj s pomicnim zarezom, kao USD-ov timeCode: timeline se da
//zaustaviti i izmedju kadrova, a kamera iz solvea ima kljuc na svakom kadru snimke
template<class T>
struct Track{
    std::vector<double> times;           //uzlazno, bez ponavljanja
    std::vector<T> values;

    bool empty() const {return times.empty();}
    size_t size() const {return times.size();}

    //Postavlja kljuc; postojeci na istom kadru se zamijeni, ne udvostruci
    void set(double time, const T& value);
    bool erase(double time);

    //Vrijednost u zadanom kadru. Prije prvog i poslije zadnjeg kljuca drzi se rubna vrijednost -
    //kamera ne odleti u beskonacnost cim playhead izadje iz raspona snimke
    T at(double time) const;
};

//Transformacija kao USD-ov xformOp redoslijed: prvo mjerilo, pa rotacija, pa pomak
struct Transform{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;
};

//---------------------------------------------------------------------------------------------
// KOMPONENTE
//---------------------------------------------------------------------------------------------

//Kamera gleda niz -Z lokalnog sustava s +Y gore - isto kao Engineova Pose i USD kamera, pa se
//poza iz solvea prepise bez ijednog zrcaljenja. Objektiv je u PIKSELIMA SNIMKE, jer je to jedino
//sto solve stvarno zna; milimetri trebaju sirinu senzora koju snimka ne kaze
struct Camera{
    float focalPixels = 1000.0f;
    float centreX = 960.0f, centreY = 540.0f;
    uint32_t width = 1920, height = 1080;

    //Snimka kroz koju je ova kamera rijesena, i koji kadar snimke (od nule) stoji na kadru 1
    //timelinea. VideoSolve pise kadar snimke k kao timeCode k + 1, pa je to zadano 0. Prazno kod
    //kamere koju je netko dodao rukom
    std::string plate;
    int plateFirstFrame = 0;
};

//Oblak tocaka iz solvea. Boje su prazne kad ih solve nije zapisao
struct Points{
    std::vector<glm::vec3> positions;
    std::vector<glm::u8vec3> colours;
};

enum class Shape{ Cube, Plane };

//Jednostavno tijelo za provjeru matchmovea: kocka jedinicne velicine oko ishodista, ravnina u XZ
struct Mesh{
    Shape shape = Shape::Cube;
    glm::vec3 colour{0.95f, 0.55f, 0.15f};
};

//Istrenirani gaussian splat, kao put do .ply
struct Splat{
    std::string path;
};

struct Entity{
    Id id = None;
    std::string name;
    Id parent = None;
    std::vector<Id> children;           //redoslijed je onaj u hijerarhiji
    bool visible = true;

    //Mirna transformacija. Kad neka os ima kljuceve, kljucevi imaju prednost za tu os
    Transform local;
    Track<glm::vec3> translationKeys;
    Track<glm::quat> rotationKeys;
    Track<glm::vec3> scaleKeys;

    std::optional<Camera> camera;
    std::optional<Points> points;
    std::optional<Mesh> mesh;
    std::optional<Splat> splat;

    bool animated() const {return !translationKeys.empty() || !rotationKeys.empty() || !scaleKeys.empty();}
};

//Snimka ili slika u projektu. Stoji u sceni jer kamera iz solvea na nju pokazuje, a timeline iz
//nje uzima fps
struct Media{
    std::string path;
    uint32_t frames = 0;
    double framesPerSecond = 25.0;
    uint32_t width = 0, height = 0;
    std::string result;                 //mapa rezultata solvea, kad postoji
};

class Stage{
public:
    //Novi entitet pod zadanim roditeljem (None = na vrhu). Ime se napravi jedinstvenim medju
    //bracom: "Kocka", "Kocka1", "Kocka2" - jer put mora pokazivati na tocno jedan entitet
    Id create(const std::string& name, Id parent = None);

    Entity* get(Id id);
    const Entity* get(Id id) const;
    bool contains(Id id) const {return entities.count(id) != 0;}
    size_t size() const {return entities.size();}

    //Brise entitet I SVU NJEGOVU DJECU. Vraca koliko je entiteta nestalo
    size_t remove(Id id);

    //Premjesta entitet pod novog roditelja. Odbija roditelja koji je sam entitet ili njegov
    //potomak - takvo stablo bi bilo petlja. Mirni entitet zadrzava svoje mjesto U SVIJETU, kao u
    //Mayi i Blenderu; animiranom se kljucevi ne diraju, jer bi preracun svakog kljuca promijenio
    //putanju izmedju njih
    bool reparent(Id id, Id newParent);

    bool rename(Id id, const std::string& name);

    //USD-ov put: "/Solve/Kamera". Prazan za nepostojeci entitet
    std::string path(Id id) const;
    Id find(const std::string& path) const;

    const std::vector<Id>& roots() const {return rootIds;}

    //Obilazak u redoslijedu hijerarhije, s dubinom - bas ono sto panel hijerarhije crta
    void walk(const std::function<void(const Entity&, int depth)>& visit) const;

    //Transformacija u kadru: lokalna (roditelj -> entitet) i svjetska (entitet -> svijet)
    Transform localAt(Id id, double frame) const;

    //UREDJIVANJE KROZ VRIJEME. Os koja vec ima kljuceve dobiva kljuc u ovom kadru - inace bi
    //promjena nestala cim se timeline pomakne, jer kljucevi imaju prednost. Os bez kljuceva mijenja
    //mirnu vrijednost, pa kocka koja nije animirana ne postane animirana slucajno
    void setLocalAt(Id id, double frame, const Transform& transform);

    //Kljuc na svim trima osima u ovom kadru, s vrijednoscu koju entitet u njemu upravo ima
    void keyAll(Id id, double frame);

    //Brise kljuceve u tom kadru na svim osima. Vraca koliko ih je bilo
    size_t eraseKeysAt(Id id, double frame);

    //Susjedni kljuc bilo koje osi: direction +1 sljedeci, -1 prethodni. false kad ga nema
    bool neighbourKey(Id id, double frame, int direction, double& found) const;
    glm::mat4 localMatrix(Id id, double frame) const;
    glm::mat4 worldMatrix(Id id, double frame) const;

    //OTISAK SCENE: broj koji se promijeni kad se promijeni bilo sto sto se sprema - stablo, imena,
    //transformacije, kljucevi, komponente, snimke, raspon. Editor iz njega zna ima li nespremljenog.
    //
    //Otisak a ne brojac izmjena: brojac bi svako mjesto koje dira entitet moralo pozvati, a
    //get() vraca obican pokazivac - jedno zaboravljeno mjesto i promjena bi tiho nestala pri
    //izlasku. Otisak se ne da zaboraviti. Polozaji tocaka ulaze samo brojem i rubnim tockama:
    //editor ih ne mijenja, a sto tisuca tocaka svaki kadar bi bilo skupo
    uint64_t fingerprint() const;

    //Raspon timelinea, u kadrovima, i koliko kadrova ide u sekundi
    double startFrame = 1.0;
    double endFrame = 100.0;
    double framesPerSecond = 25.0;

    std::vector<Media> media;

private:
    std::string uniqueName(const std::string& wanted, Id parent, Id except) const;
    std::vector<Id>& siblingsOf(Id parent);

    std::unordered_map<Id, Entity> entities;
    std::vector<Id> rootIds;
    Id nextId = 1;
};

//---------------------------------------------------------------------------------------------
// Kljucevi - u headeru jer je Track predlozak
//---------------------------------------------------------------------------------------------

inline float interpolate(float a, float b, float t){return a + (b - a) * t;}
inline glm::vec3 interpolate(const glm::vec3& a, const glm::vec3& b, float t){return a + (b - a) * t;}

//NAJKRACI PUT. q i -q su ista rotacija, a solve ih zna dati naizmjence u susjednim kadrovima.
//Slerp izmedju njih bez okretanja predznaka vrti kameru za cijeli krug izmedju dva kadra koji
//su zapravo isti - test to brani
inline glm::quat interpolate(const glm::quat& a, glm::quat b, float t){
    if(glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::slerp(a, b, t));
}

template<class T>
void Track<T>::set(double time, const T& value){
    size_t at = 0;
    while(at < times.size() && times[at] < time) ++at;
    if(at < times.size() && times[at] == time){ values[at] = value; return; }
    times.insert(times.begin() + std::ptrdiff_t(at), time);
    values.insert(values.begin() + std::ptrdiff_t(at), value);
}

template<class T>
bool Track<T>::erase(double time){
    for(size_t i = 0; i < times.size(); ++i){
        if(times[i] != time) continue;
        times.erase(times.begin() + std::ptrdiff_t(i));
        values.erase(values.begin() + std::ptrdiff_t(i));
        return true;
    }
    return false;
}

template<class T>
T Track<T>::at(double time) const{
    if(times.empty()) return T{};
    if(time <= times.front()) return values.front();
    if(time >= times.back()) return values.back();
    //Binarna pretraga: kamera iz solvea ima kljuc na svakom od tisuca kadrova
    size_t low = 0, high = times.size() - 1;
    while(high - low > 1){
        const size_t middle = (low + high) / 2;
        (times[middle] <= time ? low : high) = middle;
    }
    const double span = times[high] - times[low];
    const float t = span > 0.0 ? float((time - times[low]) / span) : 0.0f;
    return interpolate(values[low], values[high], t);
}

}
