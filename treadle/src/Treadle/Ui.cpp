#include "Treadle/Ui.h"

#include <algorithm>
#include <cctype>
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
    fieldClaimedPress = false;
    fieldSeen = false;

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
    list.cornerRadius = theme.widgetRadius;
    overlay.cornerRadius = theme.widgetRadius;
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

    //Klik izvan svakog polja skida fokus; polje koje ovaj kadar nije nacrtano (panel zatvoren)
    //ne smije i dalje gutati tipke
    if(focusedField && ((pressed[uint32_t(MouseButton::Left)] && !fieldClaimedPress) || !fieldSeen)){
        focusedField = 0;
        selectingWithMouse = false;
    }

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
    list.rectFlat(panelBox.x, panelBox.y, panelBox.width, 1.0f, theme.panel);

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
    drawLabelIn(row.box, text, held ? theme.textOnAccent : theme.text);

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

    //Oznaka i vrijednost leze NA zlatnoj ispuni kad ih ispuna pokrije: tada se pisu dark
    //tamnom kaduljom da ostanu citljive, a inace svijetlom. Prozirno ili polovicno preklapanje
    //ne mijenja boju - tu tekst ionako prelazi preko ruba pa je svijetli citljiviji
    const float labelX = row.box.x + theme.padding * 0.6f;
    const float labelWidth = textWidth(name, theme.textScale);
    const float fillRight = row.box.x + row.box.width * share;
    const bool labelOnGold = fillRight >= labelX + labelWidth;
    list.text(labelX, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              name, labelOnGold ? theme.textOnAccent : theme.text, theme.textScale);

    const std::string reading = formatNumber(*target) + unit;
    const float width = textWidth(reading, theme.textScale);
    const float readX = row.box.x + row.box.width - width - theme.padding * 0.6f;
    const bool readOnGold = fillRight > readX + width * 0.5f;
    list.text(readX, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              reading, readOnGold ? theme.textOnAccent : theme.text, theme.textScale);

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
                  options[option], chosen ? theme.textOnAccent : theme.text, theme.textScale);

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
    drawLabelIn(row.box, fitText(text, row.box.width - theme.padding * 1.2f, theme.textScale),
                selected ? theme.textOnAccent : theme.text);
    return row.hot && pressed[uint32_t(MouseButton::Left)];
}

bool Ui::assetRow(const std::string& name, const std::string& badge, bool selected, const Color& accent){
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;

    const Color tint{accent.r, accent.g, accent.b, selected ? 0.92f : 0.14f};
    list.rect(row.box, selected ? tint : (row.hot ? theme.hot : theme.control));
    list.outline(row.box, selected ? 2.0f : 1.0f, accent);

    const bool videoBadge = badge == "MP4" || badge == "MOV" || badge == "MKV" || badge == "AVI" || badge == "VID";
    const float badgeWidth = std::max(30.0f, textWidth(badge, theme.textScale) + (videoBadge ? 19.0f : 10.0f));
    const float badgeHeight = row.box.height - 8.0f;
    const Rect badgeBox{row.box.x + 4.0f, row.box.y + 4.0f, badgeWidth, badgeHeight};
    Color badgeFill{accent.r, accent.g, accent.b, selected ? 0.95f : 0.22f};
    list.rect(badgeBox, badgeFill);
    list.outline(badgeBox, 1.0f, accent);
    const Color badgeInk = selected ? theme.textOnAccent : accent;
    float badgeX = badgeBox.x + (badgeBox.width - textWidth(badge, theme.textScale)) * 0.5f;
    if(videoBadge){
        const float cy = badgeBox.y + badgeBox.height * 0.5f;
        list.triangle(badgeBox.x + 5.0f, cy - 4.5f, badgeBox.x + 5.0f, cy + 4.5f,
                      badgeBox.x + 12.0f, cy, badgeInk);
        badgeX = badgeBox.x + 14.0f;
    }
    list.text(badgeX, badgeBox.y + (badgeBox.height - textHeight(theme.textScale)) * 0.5f,
              badge, selected ? theme.textOnAccent : accent, theme.textScale);

    const float textLeft = badgeBox.x + badgeBox.width + 7.0f;
    const float room = row.box.x + row.box.width - textLeft - 5.0f;
    list.text(textLeft, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              fitText(name, room, theme.textScale), selected ? theme.textOnAccent : theme.text, theme.textScale);
    return row.hot && pressed[uint32_t(MouseButton::Left)];
}

bool Ui::folderRow(const std::string& name, bool selected){
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;
    if(selected) list.rect(row.box, theme.accent);
    else if(row.hot) list.rect(row.box, theme.hot);

    //Zlatni znak mape lijevo, pa ime: znak zamjenjuje tekstni predznak ">" koji je prije rekao
    //da je ovo mapa, i prvi pogled odmah vidi sto je folder a sto snimka
    const float icon = 15.0f;
    const float iconX = row.box.x + theme.padding * 0.6f;
    const float iconY = row.box.y + (row.box.height - icon) * 0.5f;
    list.folderIcon(iconX, iconY, icon, selected ? theme.title : theme.accent);

    const float textLeft = iconX + icon + theme.spacing * 0.7f;
    const float room = row.box.x + row.box.width - textLeft - theme.padding * 0.5f;
    list.text(textLeft, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              fitText(name, room, theme.textScale),
              selected ? theme.textOnAccent : theme.text, theme.textScale);

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
              fitText(text, room, theme.textScale),
              selected ? theme.textOnAccent : theme.text, theme.textScale);

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

void Ui::openMenuAt(const std::string& id, float x, float y){
    openMenu(id);
    menuX = x;
    menuY = y;
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
    list.rectFlat(x, y, width, 1.0f, Color{theme.panel.r, theme.panel.g, theme.panel.b, 1.0f});
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

bool Ui::dragField(uint64_t id, const Rect& box, float* target, float speed){
    const bool hot = box.contains(input.mouseX, input.mouseY);
    if(hot && pressed[uint32_t(MouseButton::Left)]){
        activeId = id;
        dragLastX = input.mouseX;
    }
    const float before = *target;
    if(activeId == id){
        const float moved = input.mouseX - dragLastX;
        *target += moved * speed * (input.shift ? 0.1f : 1.0f);
        dragLastX = input.mouseX;
    }
    list.rect(box, activeId == id ? theme.active : (hot ? theme.hot : theme.control));
    const std::string reading = fitText(formatNumber(*target), box.width - 6.0f, theme.textScale);
    const Color ink = (activeId == id) ? theme.textOnAccent : theme.text;
    list.text(box.x + (box.width - textWidth(reading, theme.textScale)) * 0.5f,
              box.y + (box.height - textHeight(theme.textScale)) * 0.5f, reading, ink, theme.textScale);
    return *target != before;
}

bool Ui::dragFloat(const std::string& name, float* target, float speed){
    if(!target) return false;
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;
    const float labelWidth = row.box.width * 0.4f;
    list.text(row.box.x, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              fitText(name, labelWidth - 4.0f, theme.textScale), theme.dim, theme.textScale);
    const Rect field{row.box.x + labelWidth, row.box.y, row.box.width - labelWidth, row.box.height};
    return dragField(idFor(name), field, target, speed);
}

bool Ui::dragVector(const std::string& name, float* xyz, float speed){
    if(!xyz) return false;
    label(name);
    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return false;
    const float width = (row.box.width - 2.0f * theme.spacing) / 3.0f;
    bool changed = false;
    const Color axes[3] = {{1.0f, 0.18f, 0.28f, 1.0f}, {0.20f, 1.0f, 0.42f, 1.0f}, {0.24f, 0.55f, 1.0f, 1.0f}};
    for(int axis = 0; axis < 3; ++axis){
        const Rect field{row.box.x + float(axis) * (width + theme.spacing), row.box.y, width, row.box.height};
        if(dragField(idFor(name + char('x' + axis)), field, &xyz[axis], speed)) changed = true;
        list.rect(field.x, field.y + field.height - 2.0f, field.width, 2.0f, axes[axis]);
    }
    return changed;
}

//=============================================================================================
// POLJE ZA TEKST
//=============================================================================================
namespace{

bool continuation(char c){ return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

size_t previousCodepoint(const std::string& s, size_t i){
    if(i == 0) return 0;
    --i;
    while(i > 0 && continuation(s[i])) --i;
    return i;
}

size_t nextCodepoint(const std::string& s, size_t i){
    if(i >= s.size()) return s.size();
    ++i;
    while(i < s.size() && continuation(s[i])) ++i;
    return i;
}

bool wordChar(char c){
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

size_t previousWord(const std::string& s, size_t i){
    while(i > 0 && !wordChar(s[i - 1])) --i;
    while(i > 0 && wordChar(s[i - 1])) --i;
    return i;
}

size_t nextWord(const std::string& s, size_t i){
    while(i < s.size() && wordChar(s[i])) ++i;
    while(i < s.size() && !wordChar(s[i])) ++i;
    return i;
}

//Ono sto font zna nacrtati: ASCII ostaje, visebajtni znak postaje jedan '?'
std::string displayed(const std::string& s, size_t from, size_t to){
    std::string out;
    for(size_t i = from; i < to; i = nextCodepoint(s, i)){
        const unsigned char c = static_cast<unsigned char>(s[i]);
        out += c < 0x80 ? s[i] : '?';
    }
    return out;
}

struct Wrapped{ size_t start, end; };         //end bez '\n'

//Prelamanje po rijecima u zadanu sirinu; predugacka rijec se prelomi gdje stane
std::vector<Wrapped> wrapLines(const std::string& s, float width, float scale){
    std::vector<Wrapped> lines;
    size_t start = 0;
    while(true){
        size_t lastSpace = std::string::npos;
        size_t i = start;
        size_t end = s.size();
        bool broke = false;
        while(i < s.size()){
            if(s[i] == '\n'){ end = i; broke = true; break; }
            const size_t next = nextCodepoint(s, i);
            if(textWidth(displayed(s, start, next), scale) > width && i > start){
                if(lastSpace != std::string::npos && lastSpace > start){ end = lastSpace; broke = true; }
                else{ end = i; broke = true; }
                break;
            }
            if(s[i] == ' ') lastSpace = i;
            i = next;
        }
        if(!broke){ lines.push_back({start, s.size()}); break; }
        lines.push_back({start, end});
        //Iza prijeloma: preskoci '\n' ili razmak na kojem je prelomljeno
        start = (end < s.size() && (s[end] == '\n' || s[end] == ' ')) ? end + 1 : end;
        if(start > s.size()) start = s.size();
        if(start == s.size() && end < s.size() && s[end] == '\n'){ lines.push_back({start, start}); break; }
        if(start == s.size() && !(end < s.size() && s[end] == '\n')) break;
    }
    if(lines.empty()) lines.push_back({0, 0});
    return lines;
}

size_t lineOf(const std::vector<Wrapped>& lines, size_t caret){
    for(size_t l = 0; l < lines.size(); ++l){
        const size_t nextStart = l + 1 < lines.size() ? lines[l + 1].start : std::string::npos;
        if(caret < nextStart || l + 1 == lines.size()) return l;
        if(caret == lines[l].end && caret < nextStart) return l;
    }
    return lines.size() - 1;
}

//Bajt u retku najblizi zadanom x-u
size_t offsetAt(const std::string& s, const Wrapped& line, float x, float scale){
    size_t best = line.start;
    float bestDistance = std::fabs(x);
    for(size_t i = line.start; i < line.end;){
        i = nextCodepoint(s, i);
        const float distance = std::fabs(textWidth(displayed(s, line.start, i), scale) - x);
        if(distance < bestDistance){ bestDistance = distance; best = i; }
    }
    return best;
}

}

Ui::TextFieldResult Ui::textField(const std::string& id, std::string* text){
    return textField(id, text, TextFieldConfig());
}

void Ui::focusTextField(const std::string& id){
    pendingFocus = id;
}

Ui::TextFieldResult Ui::textField(const std::string& id, std::string* text, const TextFieldConfig& config){
    TextFieldResult result;
    if(!text) return result;
    const float scale = theme.textScale;
    const float lineHeight = textHeight(scale) + 4.0f;
    const int visibleLines = std::max(1, config.lines);
    const float height = float(visibleLines) * lineHeight + theme.padding;
    const Row row = nextRow(height);
    const uint64_t fieldId = idFor("text:" + id);
    //Fokus trazen iz koda: kursor na kraj teksta
    if(!pendingFocus.empty() && pendingFocus == id){
        focusedField = fieldId;
        caret = anchor = text->size();
        scrollLine = 0;
        pendingFocus.clear();
        fieldClaimedPress = true;       //i ako je fokus trazen klikom na gumb, taj klik ga ne skida
    }
    if(!row.visible){
        if(focusedField == fieldId) fieldSeen = true;
        return result;
    }

    const Rect box = row.box;
    const float innerX = box.x + theme.padding * 0.7f;
    const float innerY = box.y + theme.padding * 0.5f;
    const float innerWidth = std::max(10.0f, box.width - theme.padding * 1.4f);
    std::string& s = *text;
    std::vector<Wrapped> lines = wrapLines(s, innerWidth, scale);
    auto hit = [&](float mx, float my){
        int l = scrollLine + int(std::floor((my - innerY) / lineHeight));
        l = std::clamp(l, 0, int(lines.size()) - 1);
        return offsetAt(s, lines[size_t(l)], mx - innerX, scale);
    };

    //-- mis: fokus, kursor, odabir vucenjem ----------------------------------------------------
    if(row.hot && pressed[uint32_t(MouseButton::Left)]){
        if(focusedField != fieldId){ scrollLine = 0; }
        focusedField = fieldId;
        fieldClaimedPress = true;
        caret = hit(input.mouseX, input.mouseY);
        if(!input.shift) anchor = caret;
        selectingWithMouse = true;
        preferredX = -1.0f;
    }
    const bool focused = focusedField == fieldId;
    if(focused){
        fieldSeen = true;
        if(caret == std::string::npos || caret > s.size()) caret = anchor = s.size();
        if(anchor > s.size()) anchor = caret;
        if(selectingWithMouse){
            if(input.down[uint32_t(MouseButton::Left)]) caret = hit(input.mouseX, input.mouseY);
            else selectingWithMouse = false;
        }
    }

    //-- tipkovnica ------------------------------------------------------------------------------
    if(focused){
        const std::string before = s;
        auto hasSelection = [&]{ return caret != anchor; };
        auto eraseSelection = [&]{
            const size_t a = std::min(caret, anchor), b = std::max(caret, anchor);
            s.erase(a, b - a);
            caret = anchor = a;
        };
        auto insert = [&](const std::string& raw){
            std::string clean;
            for(char c : raw){
                if(c == '\r') continue;
                if(c == '\t') c = ' ';
                if(c == '\n' && visibleLines == 1) c = ' ';
                if(static_cast<unsigned char>(c) < 0x20 && c != '\n') continue;
                clean += c;
            }
            if(hasSelection()) eraseSelection();
            size_t room = config.maxLength > s.size() ? config.maxLength - s.size() : 0;
            if(clean.size() > room){
                clean.resize(room);
                while(!clean.empty() && continuation(clean.back())) clean.pop_back();   //ne pola znaka
                if(!clean.empty() && static_cast<unsigned char>(clean.back()) >= 0xC0) clean.pop_back();
            }
            s.insert(caret, clean);
            caret += clean.size();
            anchor = caret;
        };

        if(!input.text.empty()) insert(input.text);
        for(const KeyEvent& event : input.keys){
            lines = wrapLines(s, innerWidth, scale);
            const size_t line = lineOf(lines, caret);
            const bool keepColumn = event.key == Key::Up || event.key == Key::Down;
            if(!keepColumn) preferredX = -1.0f;
            switch(event.key){
                case Key::Left:
                    if(hasSelection() && !event.shift) caret = std::min(caret, anchor);
                    else caret = event.ctrl ? previousWord(s, caret) : previousCodepoint(s, caret);
                    if(!event.shift) anchor = caret;
                    break;
                case Key::Right:
                    if(hasSelection() && !event.shift) caret = std::max(caret, anchor);
                    else caret = event.ctrl ? nextWord(s, caret) : nextCodepoint(s, caret);
                    if(!event.shift) anchor = caret;
                    break;
                case Key::Up: case Key::Down:{
                    if(preferredX < 0.0f) preferredX = textWidth(displayed(s, lines[line].start, caret), scale);
                    const int target = int(line) + (event.key == Key::Up ? -1 : 1);
                    if(target < 0) caret = 0;
                    else if(target >= int(lines.size())) caret = s.size();
                    else caret = offsetAt(s, lines[size_t(target)], preferredX, scale);
                    if(!event.shift) anchor = caret;
                    break;
                }
                case Key::Home:
                    caret = event.ctrl ? 0 : lines[line].start;
                    if(!event.shift) anchor = caret;
                    break;
                case Key::End:
                    caret = event.ctrl ? s.size() : lines[line].end;
                    if(!event.shift) anchor = caret;
                    break;
                case Key::Backspace:
                    if(hasSelection()) eraseSelection();
                    else if(caret > 0){
                        const size_t from = event.ctrl ? previousWord(s, caret) : previousCodepoint(s, caret);
                        s.erase(from, caret - from);
                        caret = anchor = from;
                    }
                    break;
                case Key::Delete:
                    if(hasSelection()) eraseSelection();
                    else if(caret < s.size()){
                        const size_t to = event.ctrl ? nextWord(s, caret) : nextCodepoint(s, caret);
                        s.erase(caret, to - caret);
                        anchor = caret;
                    }
                    break;
                case Key::Enter:
                    if(config.enterSubmits && !event.shift) result.submitted = true;
                    else if(visibleLines > 1) insert("\n");
                    else result.submitted = true;
                    break;
                case Key::Escape:
                    focusedField = 0;
                    break;
                case Key::Tab:
                    break;
                case Key::A:
                    if(event.ctrl){ anchor = 0; caret = s.size(); }
                    break;
                case Key::C: case Key::X:
                    if(event.ctrl && hasSelection()){
                        const size_t a = std::min(caret, anchor), b = std::max(caret, anchor);
                        const std::string copied = s.substr(a, b - a);
                        if(setClipboard) setClipboard(copied);
                        localClipboard = copied;
                        if(event.key == Key::X) eraseSelection();
                    }
                    break;
                case Key::V:
                    if(event.ctrl) insert(getClipboard ? getClipboard() : localClipboard);
                    break;
            }
        }
        result.changed = s != before;
        lines = wrapLines(s, innerWidth, scale);
        //Kursor ostaje vidljiv: redak s kursorom se pomakne u prozor polja
        const int caretLine = int(lineOf(lines, caret));
        if(caretLine < scrollLine) scrollLine = caretLine;
        if(caretLine >= scrollLine + visibleLines) scrollLine = caretLine - visibleLines + 1;
        scrollLine = std::clamp(scrollLine, 0, std::max(0, int(lines.size()) - visibleLines));
    }
    result.focused = focusedField == fieldId;

    //-- crtanje ----------------------------------------------------------------------------------
    list.rect(box, row.hot && !result.focused ? theme.hot : theme.control);
    list.outline(box, result.focused ? 2.0f : 1.0f, result.focused ? theme.accent : theme.panelEdge);
    const int first = result.focused ? scrollLine : 0;
    if(s.empty() && !config.placeholder.empty()){
        const std::vector<Wrapped> hint = wrapLines(config.placeholder, innerWidth, scale);
        for(int l = 0; l < visibleLines && size_t(l) < hint.size(); ++l){
            list.text(innerX, innerY + float(l) * lineHeight, displayed(config.placeholder, hint[size_t(l)].start, hint[size_t(l)].end),
                      theme.dim, scale);
        }
    }
    const size_t selectionFrom = std::min(caret, anchor), selectionTo = std::max(caret, anchor);
    for(int l = first; l < first + visibleLines && size_t(l) < lines.size(); ++l){
        const Wrapped& line = lines[size_t(l)];
        const float y = innerY + float(l - first) * lineHeight;
        if(result.focused && selectionFrom != selectionTo){
            const size_t a = std::max(selectionFrom, line.start), b = std::min(selectionTo, line.end);
            const bool newlineSelected = selectionTo > line.end && selectionFrom <= line.end;
            if(a < b || newlineSelected){
                const float x0 = textWidth(displayed(s, line.start, std::min(a, line.end)), scale);
                const float x1 = textWidth(displayed(s, line.start, std::max(a, b)), scale) + (newlineSelected ? 6.0f : 0.0f);
                list.rectFlat(innerX + x0, y - 1.0f, std::max(2.0f, x1 - x0), lineHeight, Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.45f});
            }
        }
        list.text(innerX, y, displayed(s, line.start, line.end), theme.text, scale);
    }
    //Kursor treperi pola sekunde (u kadrovima: 60 fps pretpostavljeno)
    if(result.focused && (frameNumber / 30) % 2 == 0){
        const int l = int(lineOf(lines, caret));
        if(l >= first && l < first + visibleLines){
            const float x = textWidth(displayed(s, lines[size_t(l)].start, std::min(caret, lines[size_t(l)].end)), scale);
            list.rectFlat(innerX + x, innerY + float(l - first) * lineHeight - 1.0f, 2.0f, lineHeight, theme.title);
        }
    }
    //Traka kad ima vise redaka nego stane
    if(lines.size() > size_t(visibleLines)){
        const float share = float(visibleLines) / float(lines.size());
        const float offset = float(first) / float(lines.size());
        list.rectFlat(box.x + box.width - 5.0f, box.y + 3.0f + offset * (box.height - 6.0f), 3.0f,
                      std::max(8.0f, share * (box.height - 6.0f)), theme.dim);
    }
    return result;
}

}
