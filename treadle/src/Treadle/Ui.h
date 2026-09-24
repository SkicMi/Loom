#pragma once
#include "Treadle/Draw.h"
#include "Treadle/Input.h"

#include <string>
#include <vector>

namespace Treadle{

//Boje i mjere suicelja. Jedan zapis, da se izgled mijenja na jednom mjestu
struct Theme{
    float padding = 10.0f;      //od ruba plohe do sadrzaja
    float spacing = 6.0f;       //izmedju dva reda
    float rowHeight = 24.0f;    //visina gumba i klizaca
    float textScale = 3.0f;     //mjerilo fonta iz atlasa; redak je 7 logickih jedinica, pa je
                                //ovdje 22 piksela. Atlas je podebljan i dvostruko ostar, pa tekst
                                //na toj velicini izgleda odtisnut, ne svijetlo i otrcano
    float widgetRadius = 6.0f;  //zaobljenje gumba, ploha i obruba; nula je ravno

    //PALETA SAGE GREEN / GOLD: plohe i podloga u prigusenoj zelenoj-kadulji, a svjetlo i bitno
    //(odabir, klizac, mapa) u zlatu. Tko lezi NA zlatu pise se textOnAccent - tamnom kaduljom
    Color panel     {0.13f, 0.18f, 0.16f, 0.92f};
    Color panelEdge {0.38f, 0.47f, 0.40f, 1.00f};
    Color title     {0.96f, 0.94f, 0.84f, 1.00f};
    Color text      {0.83f, 0.87f, 0.80f, 1.00f};
    Color dim       {0.58f, 0.64f, 0.55f, 1.00f};
    Color control   {0.21f, 0.28f, 0.24f, 1.00f};
    Color hot       {0.29f, 0.38f, 0.32f, 1.00f};
    Color active    {0.78f, 0.65f, 0.28f, 1.00f};
    Color accent    {0.70f, 0.58f, 0.22f, 1.00f};
    Color textOnAccent {0.08f, 0.12f, 0.10f, 1.00f};
    Color warning   {0.76f, 0.36f, 0.22f, 1.00f};
};

//=============================================================================================
// Suicelje koje se opisuje svaki kadar iznova.
//
// Nema stabla widgeta i nema objekata koji zive izmedju kadrova. Gumb postoji dok traje red
// koda koji ga spominje, i vraca je li upravo kliknut; klizac dobije pokazivac na broj koji
// mijenja. Stanje o kojem UI mora brinuti su tri broja - koji je widget pod misem, koji se
// upravo vuce, i gdje je red - a sve ostalo drzi pozivatelj, ondje gdje mu ionako treba.
//
// Redoslijed u kadru je uvijek isti i vazan je:
//
//     ui.begin(ulaz, sirina, visina);
//     ui.panel("Kocka", 20, 20, 260);
//     if(ui.button("Obrisi")) ...
//     ui.end();
//     if(!ui.wantsMouse()) ...pomicanje kamere...
//
// Kamera se pomice POSLIJE, i to nije stvar ukusa: dok se vuce klizac, mis ne smije okretati
// pogled. Odgovor na to pitanje postoji tek kad su svi widgeti tog kadra vidjeli mis.
//=============================================================================================
class Ui{
    public:
    Ui() = default;
    explicit Ui(const Theme& theme) : theme(theme){}

    void begin(const Input& input, float screenWidth, float screenHeight);
    void end();

    //Nova ploha. Zatvara prethodnu ako je otvorena
    void panel(const std::string& title, float x, float y, float width);

    //USIDRENA PLOHA: zadan cijeli pravokutnik, kao stupac editora. Sadrzaj koji ne stane se ne
    //crta i ne pogadja misem - redak je ili cijeli vidljiv ili ga nema, jer crtac ne reze. Kad
    //je dan scroll, kotacic nad plohom pomice sadrzaj, a na kraju kadra se odsijece na ono sto
    //sadrzaj stvarno zauzme
    void dock(const std::string& title, const Rect& box, float* scroll = nullptr);

    //-- widgeti -----------------------------------------------------------------------------
    void label(const std::string& text);

    //Oznaka lijevo, vrijednost desno u istom redu. Za ono sto se samo gleda
    void value(const std::string& name, const std::string& reading);

    void separator();

    //Vraca true u kadru u kojem je kliknut
    bool button(const std::string& text);

    //Vodoravno poredani gumbi, jedan red. Vraca indeks kliknutog ili -1
    int buttonRow(const std::vector<std::string>& labels);

    //Vraca true kad se vrijednost promijenila. Vrijednost se odsijeca na raspon
    bool slider(const std::string& name, float* target, float low, float high,
                const std::string& unit = std::string());

    bool checkbox(const std::string& name, bool* target);

    //Jedan od ponudjenih, kao red gumba s oznacenim izborom. true kad se izbor promijenio
    bool choice(const std::string& name, const std::vector<std::string>& options, int* index);

    //Red koji se da odabrati, kao u popisu datoteka. true u kadru lijevog pritiska
    bool selectable(const std::string& text, bool selected);

    //Red popisa datoteka za MAPU: mali zlatni znak mape na pocetku, pa ime. Isti kilk i
    //odabir kao selectable, samo sto prvi pogled odmah kaze da je ovo mapa
    bool folderRow(const std::string& name, bool selected);

    //BROJ KOJI SE VUCE, kao u Blenderu: pritisni na polje i vuci vodoravno. speed je promjena po
    //pikselu; sa shiftom deset puta sporije, za fino namjestanje. Nema raspona - pomak kocke u
    //sceni bez metara nema prirodne granice, a klizac bi ju morao izmisliti
    bool dragFloat(const std::string& name, float* target, float speed);

    //Tri broja u jednom redu (x, y, z), svaki se vuce zasebno. true kad se bilo koji promijenio
    bool dragVector(const std::string& name, float* xyz, float speed);

    //Red stabla. depth uvlaci, strelica lijevo otvara i zatvara djecu kad ih ima
    enum class TreeClick{ None, Select, Toggle };
    TreeClick treeRow(const std::string& text, int depth, bool hasChildren, bool expanded, bool selected);

    //Je li ZADNJI widget upravo dobio desni klik - za izbornik na desni klik
    bool rightClicked() const {return lastRowRightPressed;}

    //-- izbornik na desni klik ----------------------------------------------------------------
    //
    //    if(ui.selectable(ime, odabran)) ...;
    //    if(ui.rightClicked()) ui.openMenu("media");
    //    ...
    //    if(ui.beginMenu("media")){
    //        if(ui.menuItem("Solve kamere")) ...;
    //        ui.endMenu();
    //    }
    //
    //Izbornik se crta IZNAD svega sto je u kadru nacrtano, i prije i poslije njega. Dok je
    //otvoren, klik ispod njega ne stize do widgeta ispod; klik pokraj njega ga zatvori i takodjer
    //ne stize nikamo - inace bi zatvaranje izbornika usput kliknulo gumb iza njega
    void openMenu(const std::string& id);
    //Isto, ali na zadanom mjestu - za pitanje koje ne dolazi od klika (izlaz s nespremljenim)
    void openMenuAt(const std::string& id, float x, float y);
    bool menuOpen(const std::string& id) const;
    bool beginMenu(const std::string& id);
    bool menuItem(const std::string& text, bool enabled = true);
    void menuSeparator();
    void endMenu();
    void closeMenu(){openMenuId = 0;}

    //-- slobodna povrsina ---------------------------------------------------------------------
    //
    //Za ono sto nije redak: timeline, graf, pogled. Treadle kaze sto mis radi nad pravokutnikom,
    //a pozivatelj crta sam u canvas(). Vucenje koje krene u povrsini ostaje njezino i kad mis
    //izadje iz nje, isto kao kod klizaca
    struct Region{
        Rect box;
        bool hot = false;           //mis je nad povrsinom
        bool pressed = false;       //lijevi pritisak u ovom kadru
        bool held = false;          //vuce se, pocelo je u ovoj povrsini
        bool rightPressed = false;
        float mouseX = 0.0f, mouseY = 0.0f;
        float wheel = 0.0f;         //kotacic nad povrsinom
    };
    Region region(const std::string& id, const Rect& box);
    DrawList& canvas(){return list;}

    //-- rezultat ----------------------------------------------------------------------------
    const DrawList& drawn() const {return list;}

    //Pripada li mis suicelju. Aplikacija koja ovo ne provjeri okrece pogled dok se vuce
    //klizac - i to je najcesca greska s ovakvim suiceljem, pa je odgovor ovdje gotov
    bool wantsMouse() const {return pointerOverUi || activeId != 0;}

    Theme& style(){return theme;}
    const Theme& style() const {return theme;}

    private:
    struct Row{
        Rect box;
        bool hot = false;
        bool visible = true;      //u usidrenoj plohi: stane li redak u nju
    };

    uint64_t idFor(const std::string& name) const;
    Row nextRow(float height);
    void closePanel();
    void drawLabelIn(const Rect& box, const std::string& text, const Color& color);

    Theme theme;
    DrawList list;

    Input input;
    bool wasDown[uint32_t(MouseButton::Count)] = {false, false, false};
    bool pressed[uint32_t(MouseButton::Count)] = {false, false, false};
    bool released[uint32_t(MouseButton::Count)] = {false, false, false};

    float screenWidth = 0.0f;
    float screenHeight = 0.0f;

    bool panelOpen = false;
    uint32_t panelIndex = 0;
    Rect panelBox;
    float cursorY = 0.0f;
    size_t panelVertexBase = 0;   //gdje u polju vrhova lezi pozadina, da joj se naknadno upise visina

    //Widget koji se upravo vuce. Zivi izmedju kadrova jer vucenje po definiciji traje dulje
    //od jednog kadra - i zato mora biti broj a ne pokazivac: widget koji ga je stvorio u
    //sljedecem kadru nastane iznova, na drugoj adresi
    uint64_t activeId = 0;
    bool pointerOverUi = false;

    //Usidrena ploha
    bool panelDocked = false;
    float contentTop = 0.0f;      //prvi piksel ispod naslova
    float* scrollTarget = nullptr;
    float scrollOffset = 0.0f;

    bool lastRowRightPressed = false;
    float dragLastX = 0.0f;       //gdje je mis bio prosli kadar, dok se broj vuce
    bool dragField(uint64_t id, const Rect& box, float* target, float speed);

    //Izbornik. Velicina je iz PROSLOG kadra: pozadina se crta prije stavki, a klik se mora znati
    //odbiti prije nego sto ijedan widget ovog kadra pita za njega
    uint64_t openMenuId = 0;
    uint64_t menuOpenedFrame = 0;
    uint64_t frameNumber = 0;
    Rect menuBox;                 //prosli kadar
    Rect menuBuilding;            //ovaj kadar
    float menuWidestText = 0.0f;
    float menuX = 0.0f, menuY = 0.0f;
    bool inMenu = false;
    bool menuItemClicked = false;
    DrawList overlay;             //izbornik, spojen na kraj popisa u end()
    size_t menuVertexBase = 0;
    bool menuPressed[uint32_t(MouseButton::Count)] = {false, false, false};
};

}
