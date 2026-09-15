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
    float textScale = 2.0f;     //slovo 5x7 pomnozeno ovoliko

    Color panel     {0.10f, 0.11f, 0.13f, 0.88f};
    Color panelEdge {0.30f, 0.32f, 0.36f, 1.00f};
    Color title     {0.95f, 0.96f, 0.98f, 1.00f};
    Color text      {0.80f, 0.82f, 0.86f, 1.00f};
    Color dim       {0.50f, 0.52f, 0.56f, 1.00f};
    Color control   {0.20f, 0.22f, 0.26f, 1.00f};
    Color hot       {0.28f, 0.31f, 0.36f, 1.00f};
    Color active    {0.35f, 0.55f, 0.85f, 1.00f};
    Color accent    {0.35f, 0.55f, 0.85f, 1.00f};
    Color warning   {0.75f, 0.30f, 0.20f, 1.00f};
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
};

}
