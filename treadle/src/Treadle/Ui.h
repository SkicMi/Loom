#pragma once
#include "Treadle/Draw.h"
#include "Treadle/Input.h"

#include <functional>
#include <string>
#include <vector>

namespace Treadle{

//Boje i mjere suicelja. Jedan zapis, da se izgled mijenja na jednom mjestu
struct Theme{
    float padding = 11.0f;      //od ruba plohe do sadrzaja
    float spacing = 7.0f;       //izmedju dva reda
    float rowHeight = 27.0f;    //visina gumba i klizaca
    float textScale = 3.0f;     //mjerilo fonta iz atlasa; redak je 7 logickih jedinica, pa je
                                //ovdje 22 piksela. Atlas je podebljan i dvostruko ostar, pa tekst
                                //na toj velicini izgleda odtisnut, ne svijetlo i otrcano
    float widgetRadius = 5.0f;  //zaobljenje gumba, ploha i obruba; nula je ravno

    //Sage-green work surfaces, warm gold controls, and vivid neon reserved for data accents.
    Color panel     {0.055f, 0.075f, 0.060f, 0.97f};
    Color panelEdge {0.34f, 0.43f, 0.31f, 0.92f};
    Color title     {0.92f, 0.94f, 0.80f, 1.00f};
    Color text      {0.82f, 0.86f, 0.76f, 1.00f};
    Color dim       {0.52f, 0.60f, 0.47f, 1.00f};
    Color control   {0.105f, 0.145f, 0.115f, 1.00f};
    Color hot       {0.19f, 0.255f, 0.18f, 1.00f};
    Color active    {0.91f, 0.72f, 0.29f, 1.00f};
    Color accent    {0.78f, 0.61f, 0.24f, 1.00f};
    Color textOnAccent {0.09f, 0.12f, 0.075f, 1.00f};
    Color warning   {1.00f, 0.24f, 0.30f, 1.00f};
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

    //Kompaktni naglaseni chipovi za povratak na nedavne odabire; vraca kliknuti indeks ili -1
    int chipRow(const std::vector<std::string>& labels, const Color& accent);

    //-- hijerarhija -------------------------------------------------------------------------
    //
    //Plocha u kojoj je sve gumb iste boje i tekst iste velicine ne kaze sto je glavno. Ovi
    //widgeti postoje da bi se razina vidjela: jedan glavni gumb, kartice za nacin rada, tihe
    //sekcije za ono sto se rijetko dira, i sitan sivi tekst za objasnjenja

    //Mali sivi naslov odjeljka s razmakom iznad - dijeli skupine bez okvira
    void caption(const std::string& text);

    //Objasnjenje: sitnije, sivo, prelama se po rijecima u koliko god redaka treba
    void hint(const std::string& text);

    //Stanje u jednom retku: obojena tocka pa tekst (prelama se kao hint)
    void status(const std::string& text, const Color& dot);

    //Glavna radnja plohe: visi, pun zlatni. Iskljucen se vidi, ali ne okida
    bool primaryButton(const std::string& text, bool enabled = true);

    //Kartice za nacin rada: tekst s podvlakom ispod odabrane. true kad se izbor promijenio
    bool tabs(const std::vector<std::string>& options, int* index);

    //Pilule od kojih je jedna odabrana (ili nijedna, -1); vraca kliknuti indeks ili -1
    int pills(const std::vector<std::string>& labels, int selected);

    //Tiha sekcija koja se otvara: strelica, naslov i sivi sazetak desno, bez punog okvira
    bool disclosure(const std::string& title, const std::string& summary, bool* expanded);

    //Prazan razmak zadane visine
    void space(float height);

    //Podnozje usidrene plohe: bez naslova i bez pomicanja, za ono sto mora ostati vidljivo
    //i kad se sadrzaj iznad odscrolla (glavni gumb). Zatvara prethodnu plohu
    void footer(const Rect& box);

    //Vraca true kad se vrijednost promijenila. Vrijednost se odsijeca na raspon
    bool slider(const std::string& name, float* target, float low, float high,
                const std::string& unit = std::string());

    bool checkbox(const std::string& name, bool* target);

    //Jedan od ponudjenih, kao red gumba s oznacenim izborom. true kad se izbor promijenio
    bool choice(const std::string& name, const std::vector<std::string>& options, int* index);

    //Red koji se da odabrati, kao u popisu datoteka. true u kadru lijevog pritiska
    bool selectable(const std::string& text, bool selected);

    //Medijski red s formatnom značkom i neon obrubom po tipu datoteke.
    bool assetRow(const std::string& name, const std::string& badge, bool selected, const Color& accent);

    //Collapsible instrument header for component-style property groups.
    bool componentHeader(const std::string& title, const Color& accent, bool* expanded, bool active = true);

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
    TreeClick atlasRow(const std::string& name, const std::string& kind, int depth, bool hasChildren,
                       bool expanded, bool selected, const Color& accent, bool visible);
    void selectionCard(const std::string& name, const std::string& kind, const Color& accent, bool visible);
    void linkedPreview(const std::string& name, const std::string& kind, const Color& accent);
    int breadcrumb(const std::vector<std::string>& labels);
    bool lastRowHovered() const {return lastRowHoveredValue;}
    Rect lastRowRect() const {return lastRowBox;}

    //POLJE ZA TEKST, vise redaka s prelamanjem po rijecima. Klik postavi kursor, vucenje odabire;
    //strelice, Home/End, Ctrl+strelice po rijecima, Shift za odabir, Backspace/Delete (s Ctrl po
    //rijecima), Ctrl+A/C/X/V. Enter javlja submitted, Shift+Enter je novi red (kad je enterSubmits).
    //Esc ili klik izvan polja skida fokus. Tekst je UTF-8; font crta ASCII, ostalo kao '?'
    struct TextFieldConfig{
        int lines = 1;                  //koliko redaka se vidi; dulji tekst se pomice
        std::string placeholder;        //sivo, dok je polje prazno
        size_t maxLength = 4096;        //u bajtovima
        bool enterSubmits = true;
    };
    struct TextFieldResult{
        bool changed = false;
        bool submitted = false;
        bool focused = false;
    };
    TextFieldResult textField(const std::string& id, std::string* text, const TextFieldConfig& config);
    TextFieldResult textField(const std::string& id, std::string* text);

    //Fokus na polje iz koda (npr. kad se panel otvori)
    void focusTextField(const std::string& id);

    //Pripada li tipkovnica polju za tekst. Aplikacija tada ne smije tipke tumaciti kao precace -
    //inace "W" u opisu pokreta prebaci alat na pomicanje
    bool wantsKeyboard() const {return focusedField != 0;}

    //Medjuspremnik sustava; aplikacija ga spoji na GLFW. Bez njih Ctrl+C/V radi unutar Treadlea
    std::function<std::string()> getClipboard;
    std::function<void(const std::string&)> setClipboard;

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
    //Radial context menu positioned at the pointer; returns the item index selected this frame.
    int orbitMenu(const std::string& id, const std::vector<std::string>& labels,
                  const std::vector<bool>& enabled = {}, const std::vector<std::string>& shortcuts = {},
                  std::vector<bool>* favorites = nullptr);
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
    bool lastRowHoveredValue = false;
    Rect lastRowBox;

    //Polje za tekst u fokusu: kursor i sidro odabira su bajtovi u UTF-8 tekstu
    uint64_t focusedField = 0;
    std::string pendingFocus;          //ime polja koje dobiva fokus kad se nacrta
    size_t caret = 0, anchor = 0;
    int scrollLine = 0;
    bool fieldClaimedPress = false;
    bool fieldSeen = false;
    bool selectingWithMouse = false;
    float preferredX = -1.0f;           //gore/dolje zadrzavaju stupac
    std::string localClipboard;
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
