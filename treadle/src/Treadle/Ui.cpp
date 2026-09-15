#include "Treadle/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Treadle{

namespace{

//FNV-1a. Bilo koji rasprsivac bi posluzio - trazi se samo da dvije razlicite oznake gotovo
//sigurno daju razlicit broj, i da isti kod u sljedecem kadru da isti broj
uint64_t hashOf(const std::string& text, uint64_t seed){
    uint64_t hash = seed;
    for(char character : text){
        hash ^= uint64_t(uint8_t(character));
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string formatNumber(float value){
    char text[32];
    //Tri decimale za male brojeve, dvije za ostalo: klizac za velicinu kocke radi u stotinkama
    //polumjera scene, pa bi dvije decimale pokazivale isti broj kroz pola raspona
    std::snprintf(text, sizeof(text), std::fabs(value) < 10.0f ? "%.3f" : "%.2f", double(value));
    return text;
}

}

void Ui::begin(const Input& newInput, float width, float height){
    for(uint32_t button = 0; button < uint32_t(MouseButton::Count); ++button){
        pressed[button] = newInput.down[button] && !wasDown[button];
        released[button] = !newInput.down[button] && wasDown[button];
    }

    input = newInput;
    screenWidth = width;
    screenHeight = height;

    list.clear();
    panelOpen = false;
    panelIndex = 0;
    pointerOverUi = false;

    //Vucenje prestaje kad se gumb pusti, bez obzira gdje je mis tada. Bez ovoga bi klizac
    //kojemu je mis pobjegao izvan plohe ostao zalijepljen za pokazivac zauvijek
    if(released[uint32_t(MouseButton::Left)]) activeId = 0;
}

void Ui::end(){
    closePanel();
    for(uint32_t button = 0; button < uint32_t(MouseButton::Count); ++button){
        wasDown[button] = input.down[button];
    }
}

uint64_t Ui::idFor(const std::string& name) const{
    //Ploha ulazi u broj, pa dva gumba istog imena na dvije plohe nisu isti widget. Bez toga
    //bi vucenje jednog pomicalo drugi, a oznake se ponavljaju cim ploha ima X, Y i Z
    return hashOf(name, 1469598103934665603ull + panelIndex * 0x9E3779B97F4A7C15ull);
}

void Ui::panel(const std::string& title, float x, float y, float width){
    closePanel();

    panelOpen = true;
    ++panelIndex;
    panelBox = Rect{x, y, width, 0.0f};

    //Pozadina se crta PRIJE sadrzaja jer je iza njega, a visina joj se zna tek na kraju. Zato
    //se zapamti gdje su joj vrhovi i visina im se upise u closePanel(). Druga mogucnost je
    //crtati pozadinu na kraju i sortirati, sto bi za jedan pravokutnik bilo skuplje od ovoga
    //VISINA JEDAN, NE NULA. rect() pravokutnik nulte visine preskace - i to s razlogom, jer
    //je to trokut bez povrsine - pa je pozadina nastala kao NISTA, a panelVertexBase je onda
    //pokazivao na prvi pravokutnik koji je dosao poslije: prvi upaljeni niz tocaka naslova.
    //Zakrpa visine ga je zatim razvukla preko cijele plohe i dobila se bijela crta uz lijevi
    //rub teksta, dok prave pozadine nije bilo. Nista nije puklo i nijedan broj nije bio kriv
    panelVertexBase = list.vertices.size();
    list.rect(panelBox.x, panelBox.y, panelBox.width, 1.0f, theme.panel);

    //OBRUB IDE NA KRAJ, u closePanel(). Prvo je stajao ovdje, a closePanel ga je odsijecanjem
    //polja crtao iznova kad se visina sazna - i time odbacivao SVE sto je u medjuvremenu
    //nacrtano, dakle sve widgete plohe. Ostajao je prazan okvir, a nista nije puklo.
    //
    //Obrub je jedan piksel i crta se preko sadrzaja; da je deblji, trebalo bi ga zakrpati kao
    //pozadinu umjesto ovoga

    cursorY = y + theme.padding;

    const float titleHeight = textHeight(theme.textScale);
    list.text(x + theme.padding, cursorY, title, theme.title, theme.textScale);
    cursorY += titleHeight + theme.spacing;

    list.rect(x + theme.padding, cursorY, width - 2.0f * theme.padding, 1.0f, theme.panelEdge);
    cursorY += 1.0f + theme.spacing;
}

void Ui::closePanel(){
    if(!panelOpen) return;
    panelOpen = false;

    panelBox.height = cursorY - panelBox.y + theme.padding - theme.spacing;

    //Pozadina: cetiri vrha, donja dva dobiju pravu visinu
    list.vertices[panelVertexBase + 2].y = panelBox.y + panelBox.height;
    list.vertices[panelVertexBase + 3].y = panelBox.y + panelBox.height;

    //Tek sada se zna koliko je ploha visoka, pa se obrub moze nacrtati
    list.outline(panelBox, 1.0f, theme.panelEdge);

    if(panelBox.contains(input.mouseX, input.mouseY)) pointerOverUi = true;
}

Ui::Row Ui::nextRow(float height){
    Row row;
    row.box = Rect{panelBox.x + theme.padding, cursorY,
                   panelBox.width - 2.0f * theme.padding, height};
    cursorY += height + theme.spacing;

    row.hot = row.box.contains(input.mouseX, input.mouseY);
    return row;
}

void Ui::drawLabelIn(const Rect& box, const std::string& text, const Color& color){
    //Okomito po sredini reda, vodoravno s malim odmakom od ruba
    const float y = box.y + (box.height - textHeight(theme.textScale)) * 0.5f;
    list.text(box.x + theme.padding * 0.6f, y, text, color, theme.textScale);
}

void Ui::label(const std::string& text){
    const Row row = nextRow(textHeight(theme.textScale));
    list.text(row.box.x, row.box.y, text, theme.text, theme.textScale);
}

void Ui::value(const std::string& name, const std::string& reading){
    const Row row = nextRow(textHeight(theme.textScale));
    list.text(row.box.x, row.box.y, name, theme.dim, theme.textScale);

    const float width = textWidth(reading, theme.textScale);
    list.text(row.box.x + row.box.width - width, row.box.y, reading, theme.text, theme.textScale);
}

void Ui::separator(){
    const Row row = nextRow(1.0f);
    list.rect(row.box, theme.panelEdge);
}

bool Ui::button(const std::string& text){
    const Row row = nextRow(theme.rowHeight);
    const uint64_t id = idFor(text);

    const bool held = activeId == id;
    if(row.hot && pressed[uint32_t(MouseButton::Left)]) activeId = id;

    list.rect(row.box, held ? theme.active : (row.hot ? theme.hot : theme.control));
    drawLabelIn(row.box, text, theme.text);

    //Klik se javlja na PRITISAK, ne na otpustanje. Otpustanje je ono sto radi Blender, i
    //bolje je za gumb koji se moze predomisliti - ali ovdje je odziv vazniji, jer je gumb
    //"Obrisi" jedini koji nesto mijenja i on ionako trazi potvrdu izborom iznad sebe
    return row.hot && pressed[uint32_t(MouseButton::Left)];
}

int Ui::buttonRow(const std::vector<std::string>& labels){
    if(labels.empty()) return -1;

    const Row row = nextRow(theme.rowHeight);
    const float width = (row.box.width - theme.spacing * float(labels.size() - 1)) / float(labels.size());

    int clicked = -1;
    for(size_t index = 0; index < labels.size(); ++index){
        const Rect box{row.box.x + float(index) * (width + theme.spacing), row.box.y, width, row.box.height};
        const bool hot = box.contains(input.mouseX, input.mouseY);

        list.rect(box, hot ? theme.hot : theme.control);

        const float textLeft = box.x + (box.width - textWidth(labels[index], theme.textScale)) * 0.5f;
        list.text(textLeft, box.y + (box.height - textHeight(theme.textScale)) * 0.5f,
                  labels[index], theme.text, theme.textScale);

        if(hot && pressed[uint32_t(MouseButton::Left)]) clicked = int(index);
    }
    return clicked;
}

bool Ui::slider(const std::string& name, float* target, float low, float high,
                const std::string& unit){
    if(!target || high <= low) return false;

    const Row row = nextRow(theme.rowHeight);
    const uint64_t id = idFor(name);

    if(row.hot && pressed[uint32_t(MouseButton::Left)]) activeId = id;

    const float before = *target;
    if(activeId == id){
        //Vuce se po SIROKOM dijelu, a ne po sredini rucice. Rucica je siroka pa bi racun po
        //njezinom srednjem stupcu znacio da se krajevi raspona ne mogu dosegnuti
        const float share = (input.mouseX - row.box.x) / row.box.width;
        *target = low + (high - low) * std::min(1.0f, std::max(0.0f, share));
    }

    *target = std::min(high, std::max(low, *target));
    const float share = (*target - low) / (high - low);

    list.rect(row.box, theme.control);
    list.rect(row.box.x, row.box.y, row.box.width * share, row.box.height,
              activeId == id ? theme.active : theme.accent);

    drawLabelIn(row.box, name, theme.text);

    const std::string reading = formatNumber(*target) + unit;
    const float width = textWidth(reading, theme.textScale);
    list.text(row.box.x + row.box.width - width - theme.padding * 0.6f,
              row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              reading, theme.text, theme.textScale);

    return *target != before;
}

bool Ui::checkbox(const std::string& name, bool* target){
    if(!target) return false;

    const Row row = nextRow(theme.rowHeight);
    const float side = row.box.height * 0.6f;
    const Rect mark{row.box.x, row.box.y + (row.box.height - side) * 0.5f, side, side};

    list.rect(mark, *target ? theme.accent : theme.control);
    list.outline(mark, 1.0f, theme.panelEdge);
    list.text(row.box.x + side + theme.spacing,
              row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              name, theme.text, theme.textScale);

    //Cijeli red pogadja, ne samo kvadratic: kvadratic od cetrnaest piksela je premali cilj
    if(row.hot && pressed[uint32_t(MouseButton::Left)]){
        *target = !*target;
        return true;
    }
    return false;
}

bool Ui::choice(const std::string& name, const std::vector<std::string>& options, int* index){
    if(!index || options.empty()) return false;

    label(name);

    const Row row = nextRow(theme.rowHeight);
    const float width = (row.box.width - theme.spacing * float(options.size() - 1)) / float(options.size());

    bool changed = false;
    for(size_t option = 0; option < options.size(); ++option){
        const Rect box{row.box.x + float(option) * (width + theme.spacing), row.box.y, width, row.box.height};
        const bool hot = box.contains(input.mouseX, input.mouseY);
        const bool chosen = int(option) == *index;

        list.rect(box, chosen ? theme.accent : (hot ? theme.hot : theme.control));

        const float textLeft = box.x + (box.width - textWidth(options[option], theme.textScale)) * 0.5f;
        list.text(textLeft, box.y + (box.height - textHeight(theme.textScale)) * 0.5f,
                  options[option], theme.text, theme.textScale);

        if(hot && pressed[uint32_t(MouseButton::Left)] && !chosen){
            *index = int(option);
            changed = true;
        }
    }
    return changed;
}

}
