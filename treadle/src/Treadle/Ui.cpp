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
    ++frameNumber;
    bool anyPress = false;
    for(uint32_t button = 0; button < uint32_t(MouseButton::Count); ++button){
        pressed[button] = newInput.down[button] && !wasDown[button];
        released[button] = !newInput.down[button] && wasDown[button];
        menuPressed[button] = false;
        anyPress = anyPress || pressed[button];
    }

    input = newInput;
    screenWidth = width;
    screenHeight = height;

    list.clear();
    overlay.clear();
    panelOpen = false;
    panelDocked = false;
    scrollTarget = nullptr;
    panelIndex = 0;
    pointerOverUi = false;
    lastRowRightPressed = false;

    //OTVOREN IZBORNIK UZIMA KLIK prije svih widgeta ovog kadra. Klik u njemu ide samo njegovim
    //stavkama; klik pokraj njega ga zatvori i nestane - ne smije usput kliknuti ono ispod
    if(openMenuId != 0 && menuOpenedFrame != frameNumber){
        const bool inside = menuBox.contains(input.mouseX, input.mouseY);
        if(inside) pointerOverUi = true;
        if(anyPress){
            for(uint32_t button = 0; button < uint32_t(MouseButton::Count); ++button){
                if(inside) menuPressed[button] = pressed[button];
                pressed[button] = false;
            }
            if(!inside) openMenuId = 0;
        }
    }

    //Vucenje prestaje kad se gumb pusti, bez obzira gdje je mis tada. Bez ovoga bi klizac
    //kojemu je mis pobjegao izvan plohe ostao zalijepljen za pokazivac zauvijek
    if(released[uint32_t(MouseButton::Left)]) activeId = 0;
}

void Ui::end(){
    closePanel();

    //Izbornik ide na kraj: crtac crta redom, pa je zadnje nacrtano na vrhu
    const uint32_t base = uint32_t(list.vertices.size());
    list.vertices.insert(list.vertices.end(), overlay.vertices.begin(), overlay.vertices.end());
    for(uint32_t index : overlay.indices) list.indices.push_back(base + index);

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

void Ui::dock(const std::string& title, const Rect& box, float* scroll){
    closePanel();

    panelOpen = true;
    panelDocked = true;
    ++panelIndex;
    panelBox = box;

    //Visina se zna unaprijed, pa pozadini ne treba zakrpa kao kod obicne plohe
    panelVertexBase = list.vertices.size();
    list.rect(panelBox, Color{theme.panel.r, theme.panel.g, theme.panel.b, 1.0f});

    cursorY = box.y + theme.padding;
    list.text(box.x + theme.padding, cursorY, fitText(title, box.width - 2.0f * theme.padding, theme.textScale),
              theme.title, theme.textScale);
    cursorY += textHeight(theme.textScale) + theme.spacing;
    list.rect(box.x + theme.padding, cursorY, box.width - 2.0f * theme.padding, 1.0f, theme.panelEdge);
    cursorY += 1.0f + theme.spacing;
    contentTop = cursorY;

    scrollTarget = scroll;
    if(scroll){
        if(box.contains(input.mouseX, input.mouseY) && input.wheel != 0.0f){
            *scroll -= input.wheel * theme.rowHeight * 2.0f;
        }
        *scroll = std::max(0.0f, *scroll);
        scrollOffset = *scroll;
    }else{
        scrollOffset = 0.0f;
    }
    cursorY -= scrollOffset;
}

void Ui::closePanel(){
    if(!panelOpen) return;
    panelOpen = false;

    if(panelDocked){
        panelDocked = false;
        const float content = cursorY + scrollOffset - contentTop;
        const float visible = panelBox.y + panelBox.height - theme.padding * 0.5f - contentTop;
        if(scrollTarget){
            *scrollTarget = std::min(*scrollTarget, std::max(0.0f, content - visible));
        }
        //Traka koja kaze da ima jos: sirina tri piksela uz desni rub, duljina razmjerna
        if(content > visible && visible > 0.0f){
            const float share = visible / content;
            const float offset = (scrollTarget ? *scrollTarget : 0.0f) / content;
            list.rect(panelBox.x + panelBox.width - 5.0f, contentTop + offset * visible, 3.0f,
                      std::max(12.0f, share * visible), theme.dim);
        }
        scrollTarget = nullptr;
        list.outline(panelBox, 1.0f, theme.panelEdge);
        if(panelBox.contains(input.mouseX, input.mouseY)) pointerOverUi = true;
        return;
    }

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

    if(panelDocked){
        row.visible = row.box.y >= contentTop - 0.5f &&
                      row.box.y + height <= panelBox.y + panelBox.height - theme.padding * 0.5f + 0.5f;
    }
    row.hot = row.visible && row.box.contains(input.mouseX, input.mouseY);
    lastRowRightPressed = row.hot && pressed[uint32_t(MouseButton::Right)];
    return row;
}

void Ui::drawLabelIn(const Rect& box, const std::string& text, const Color& color){
    //Okomito po sredini reda, vodoravno s malim odmakom od ruba
    const float y = box.y + (box.height - textHeight(theme.textScale)) * 0.5f;
    list.text(box.x + theme.padding * 0.6f, y, text, color, theme.textScale);
}

void Ui::label(const std::string& text){
    const Row row = nextRow(textHeight(theme.textScale));
    if(!row.visible) return;
    list.text(row.box.x, row.box.y, text, theme.text, theme.textScale);
}

void Ui::value(const std::string& name, const std::string& reading){
    const Row row = nextRow(textHeight(theme.textScale));
    if(!row.visible) return;
    list.text(row.box.x, row.box.y, name, theme.dim, theme.textScale);

    const float width = textWidth(reading, theme.textScale);
    list.text(row.box.x + row.box.width - width, row.box.y, reading, theme.text, theme.textScale);
}

void Ui::separator(){
    const Row row = nextRow(1.0f);
    if(!row.visible) return;
    list.rect(row.box, theme.panelEdge);
}

bool Ui::button(const std::string& text){
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;
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
    if(!row.visible) return -1;
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
    if(!row.visible) return false;
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
    if(!row.visible) return false;
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
    if(!row.visible) return false;
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

bool Ui::selectable(const std::string& text, bool selected){
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;
    if(selected) list.rect(row.box, theme.accent);
    else if(row.hot) list.rect(row.box, theme.hot);
    drawLabelIn(row.box, fitText(text, row.box.width - theme.padding * 1.2f, theme.textScale), theme.text);
    return row.hot && pressed[uint32_t(MouseButton::Left)];
}

Ui::TreeClick Ui::treeRow(const std::string& text, int depth, bool hasChildren, bool expanded, bool selected){
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return TreeClick::None;
    if(selected) list.rect(row.box, theme.accent);
    else if(row.hot) list.rect(row.box, theme.hot);

    const float indent = float(depth) * theme.rowHeight * 0.7f;
    const Rect arrow{row.box.x + indent, row.box.y, theme.rowHeight, row.box.height};
    if(hasChildren){
        //Trokut od pravokutnika, jer crtac ne zna drugo: tri pruge sve krace
        const float cx = arrow.x + arrow.width * 0.5f, cy = arrow.y + arrow.height * 0.5f;
        for(int i = 0; i < 4; ++i){
            const float half = 5.0f - float(i) * 1.5f;
            if(expanded) list.rect(cx - half, cy - 3.0f + float(i) * 2.0f, 2.0f * half, 2.0f, theme.text);
            else list.rect(cx - 3.0f + float(i) * 2.0f, cy - half, 2.0f, 2.0f * half, theme.text);
        }
    }
    const float textLeft = arrow.x + arrow.width;
    const float room = row.box.x + row.box.width - textLeft - theme.padding * 0.5f;
    list.text(textLeft, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              fitText(text, room, theme.textScale), theme.text, theme.textScale);

    if(!(row.hot && pressed[uint32_t(MouseButton::Left)])) return TreeClick::None;
    if(hasChildren && arrow.contains(input.mouseX, input.mouseY)) return TreeClick::Toggle;
    return TreeClick::Select;
}

namespace{
uint64_t menuHash(const std::string& id){return hashOf(id, 0xC6A4A7935BD1E995ull);}
}

void Ui::openMenu(const std::string& id){
    openMenuId = menuHash(id);
    menuOpenedFrame = frameNumber;
    menuX = input.mouseX;
    menuY = input.mouseY;
    menuBox = Rect{};
}

bool Ui::menuOpen(const std::string& id) const{
    return openMenuId != 0 && openMenuId == menuHash(id);
}

bool Ui::beginMenu(const std::string& id){
    if(!menuOpen(id)) return false;
    closePanel();
    std::swap(list, overlay);
    inMenu = true;
    menuItemClicked = false;

    //Sirina i visina iz proslog kadra; prvi kadar ih ne zna pa uzima razumnu sirinu. Izbornik
    //otvoren uz desni ili donji rub se pomakne da stane
    const float width = std::max(200.0f, menuWidestText + 3.0f * theme.padding);
    float x = menuX, y = menuY;
    if(x + width > screenWidth) x = std::max(0.0f, screenWidth - width);
    if(menuBox.height > 0.0f && y + menuBox.height > screenHeight) y = std::max(0.0f, screenHeight - menuBox.height);
    menuBuilding = Rect{x, y, width, 0.0f};
    menuWidestText = 0.0f;

    menuVertexBase = list.vertices.size();
    list.rect(x, y, width, 1.0f, Color{theme.panel.r, theme.panel.g, theme.panel.b, 1.0f});
    cursorY = y + theme.padding * 0.4f;
    return true;
}

bool Ui::menuItem(const std::string& text, bool enabled){
    if(!inMenu) return false;
    const Rect box{menuBuilding.x + 2.0f, cursorY, menuBuilding.width - 4.0f, theme.rowHeight};
    cursorY += theme.rowHeight;
    const bool hot = enabled && box.contains(input.mouseX, input.mouseY);
    if(hot) list.rect(box, theme.hot);
    drawLabelIn(box, text, enabled ? theme.text : theme.dim);
    menuWidestText = std::max(menuWidestText, textWidth(text, theme.textScale));

    const bool clicked = hot && menuPressed[uint32_t(MouseButton::Left)];
    if(clicked) menuItemClicked = true;
    return clicked;
}

void Ui::menuSeparator(){
    if(!inMenu) return;
    cursorY += theme.spacing * 0.5f;
    list.rect(menuBuilding.x + theme.padding, cursorY, menuBuilding.width - 2.0f * theme.padding, 1.0f, theme.panelEdge);
    cursorY += theme.spacing * 0.5f + 1.0f;
}

void Ui::endMenu(){
    if(!inMenu) return;
    inMenu = false;
    menuBuilding.height = cursorY - menuBuilding.y + theme.padding * 0.4f;
    list.vertices[menuVertexBase + 2].y = menuBuilding.y + menuBuilding.height;
    list.vertices[menuVertexBase + 3].y = menuBuilding.y + menuBuilding.height;
    list.outline(menuBuilding, 1.0f, theme.panelEdge);
    menuBox = menuBuilding;
    if(menuBox.contains(input.mouseX, input.mouseY)) pointerOverUi = true;
    std::swap(list, overlay);
    if(menuItemClicked) openMenuId = 0;
}

Ui::Region Ui::region(const std::string& id, const Rect& box){
    Region region;
    region.box = box;
    region.mouseX = input.mouseX;
    region.mouseY = input.mouseY;
    region.hot = box.contains(input.mouseX, input.mouseY);
    const uint64_t rid = idFor("region:" + id);
    if(region.hot && pressed[uint32_t(MouseButton::Left)]){
        activeId = rid;
        region.pressed = true;
    }
    region.held = activeId == rid && input.down[uint32_t(MouseButton::Left)];
    region.rightPressed = region.hot && pressed[uint32_t(MouseButton::Right)];
    region.wheel = region.hot ? input.wheel : 0.0f;
    if(region.hot || region.held) pointerOverUi = true;
    return region;
}

}
