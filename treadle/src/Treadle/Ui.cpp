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
        pressed[button] = newInput.pressedEvent[button] || (newInput.down[button] && !wasDown[button]);
        released[button] = newInput.releasedEvent[button] || (!newInput.down[button] && wasDown[button]);
        menuPressed[button] = false;
        anyPress = anyPress || pressed[button];
    }

    input = newInput;
    for(uint32_t button = 0; button < uint32_t(MouseButton::Count); ++button){
        if(!newInput.pressedEvent[button]) continue;
        input.mouseX = newInput.pressX[button];
        input.mouseY = newInput.pressY[button];
    }
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
    lastRowHoveredValue = false;

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

    //Kratki klik moze imati press i release izmedju dva kadra. Pocetak kadra tada vec
    //ocisti stari aktivni widget; ocisti ga i ovdje da ga pritisak istog kadra ne ostavi zalijepljenim.
    if(released[uint32_t(MouseButton::Left)]) activeId = 0;
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
    if(box.contains(input.mouseX, input.mouseY)) pointerOverUi = true;

    //Visina se zna unaprijed, pa pozadini ne treba zakrpa kao kod obicne plohe
    panelVertexBase = list.vertices.size();
    list.rect(panelBox, Color{theme.panel.r, theme.panel.g, theme.panel.b, 0.97f});

    list.marble(box, Color{0.57f, 0.68f, 0.49f, 0.13f}, Color{0.02f, 0.035f, 0.025f, 0.10f}, input.timeSeconds);
    const Color headerWash{theme.panelEdge.r, theme.panelEdge.g, theme.panelEdge.b, 0.10f};
    list.rect(box.x + 1.0f, box.y + 1.0f, std::max(0.0f, box.width - 2.0f),
              textHeight(theme.textScale) + theme.padding * 1.15f, headerWash);

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
    lastRowHoveredValue = row.hot;
    lastRowBox = row.box;
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

int Ui::chipRow(const std::vector<std::string>& labels, const Color& accent){
    if(labels.empty()) return -1;

    const Row row = nextRow(theme.rowHeight);
    if(!row.visible) return -1;
    const float gap = std::max(4.0f, theme.spacing * 0.6f);
    const float width = (row.box.width - gap * float(labels.size() - 1)) / float(labels.size());
    const float scale = theme.textScale * 0.78f;
    int clicked = -1;
    for(size_t index = 0; index < labels.size(); ++index){
        const Rect box{row.box.x + float(index) * (width + gap), row.box.y, width, row.box.height};
        const bool hot = box.contains(input.mouseX, input.mouseY);
        const Color edge{accent.r, accent.g, accent.b, hot ? 0.88f : 0.48f};
        list.rect(box, Color{accent.r, accent.g, accent.b, hot ? 0.20f : 0.08f});
        list.outline(box, hot ? 1.4f : 1.0f, edge);
        const std::string label = fitText(labels[index], box.width - 10.0f, scale);
        list.text(box.x + (box.width - textWidth(label, scale)) * 0.5f,
                  box.y + (box.height - textHeight(scale)) * 0.5f,
                  label, hot ? theme.title : theme.text, scale);
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

        const std::string fitted = fitText(options[option],
                                            std::max(0.0f, box.width - theme.padding * 1.2f),
                                            theme.textScale);
        const float textLeft = box.x + (box.width - textWidth(fitted, theme.textScale)) * 0.5f;
        list.text(textLeft, box.y + (box.height - textHeight(theme.textScale)) * 0.5f,
                  fitted, chosen ? theme.textOnAccent : theme.text, theme.textScale);

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
    const Row row = nextRow(theme.rowHeight + 27.0f);
    if(!row.visible) return false;

    const Color cardFill = selected ? Color{accent.r, accent.g, accent.b, 0.18f}
                                    : (row.hot ? theme.hot : theme.control);
    Color edge{accent.r, accent.g, accent.b, selected ? 0.92f : (row.hot ? 0.78f : 0.48f)};
    list.rect(row.box, cardFill);
    list.outline(row.box, selected ? 1.8f : 1.0f, edge);

    const Rect thumb{row.box.x + 6.0f, row.box.y + 6.0f, 44.0f, row.box.height - 12.0f};
    list.rect(thumb, Color{accent.r, accent.g, accent.b, 0.09f});
    list.outline(thumb, 1.0f, Color{accent.r, accent.g, accent.b, 0.45f});
    const float cx = thumb.x + thumb.width * 0.5f;
    const float cy = thumb.y + thumb.height * 0.5f;
    const std::string type = badge;
    const bool video = type == "MP4" || type == "MOV" || type == "MKV" || type == "AVI" || type == "VID";
    const bool image = type == "IMG" || type == "JPG" || type == "JPEG" || type == "PNG" ||
                       type == "WEBP" || type == "EXR" || type == "TIF" || type == "TIFF";
    const bool model = type == "GLTF" || type == "GLB" || type == "OBJ" || type == "FBX" ||
                       type == "USD" || type == "USDZ" || type == "3D";
    const bool motion = type == "BVH" || type == "MOTION";
    const bool splat = type == "SPLAT";
    const Color ink{accent.r, accent.g, accent.b, 0.95f};
    if(video){
        const Rect screen{thumb.x + 9.0f, thumb.y + 7.0f, thumb.width - 18.0f, thumb.height - 14.0f};
        list.rect(screen, Color{accent.r, accent.g, accent.b, 0.12f});
        list.outline(screen, 1.0f, ink);
        list.line(screen.x + 2.0f, screen.y + 3.0f, screen.x + screen.width - 2.0f, screen.y + 3.0f, 1.0f, ink);
        list.line(screen.x + 2.0f, screen.y + screen.height - 3.0f, screen.x + screen.width - 2.0f, screen.y + screen.height - 3.0f, 1.0f, ink);
        list.triangle(cx - 2.0f, cy - 5.0f, cx - 2.0f, cy + 5.0f, cx + 6.0f, cy, ink);
    }else if(image){
        list.rect(cx - 11.0f, cy - 9.0f, 22.0f, 18.0f, Color{accent.r, accent.g, accent.b, 0.10f});
        list.outline(Rect{cx - 11.0f, cy - 9.0f, 22.0f, 18.0f}, 1.0f, ink);
        list.rect(cx + 4.0f, cy - 6.0f, 3.0f, 3.0f, ink);
        list.line(cx - 9.0f, cy + 6.0f, cx - 2.0f, cy - 1.0f, 1.5f, ink);
        list.line(cx - 2.0f, cy - 1.0f, cx + 2.0f, cy + 3.0f, 1.5f, ink);
        list.line(cx + 2.0f, cy + 3.0f, cx + 7.0f, cy - 2.0f, 1.5f, ink);
    }else if(model){
        list.line(cx, cy - 11.0f, cx + 10.0f, cy - 5.0f, 1.3f, ink);
        list.line(cx + 10.0f, cy - 5.0f, cx + 10.0f, cy + 6.0f, 1.3f, ink);
        list.line(cx + 10.0f, cy + 6.0f, cx, cy + 12.0f, 1.3f, ink);
        list.line(cx, cy + 12.0f, cx - 10.0f, cy + 6.0f, 1.3f, ink);
        list.line(cx - 10.0f, cy + 6.0f, cx - 10.0f, cy - 5.0f, 1.3f, ink);
        list.line(cx - 10.0f, cy - 5.0f, cx, cy - 11.0f, 1.3f, ink);
        list.line(cx, cy - 11.0f, cx, cy + 1.0f, 1.3f, ink);
        list.line(cx - 10.0f, cy - 5.0f, cx, cy + 1.0f, 1.3f, ink);
        list.line(cx + 10.0f, cy - 5.0f, cx, cy + 1.0f, 1.3f, ink);
        list.line(cx, cy + 1.0f, cx, cy + 12.0f, 1.3f, ink);
    }else if(motion){
        list.line(cx, cy - 11.0f, cx, cy - 2.0f, 1.6f, ink);
        list.line(cx, cy - 2.0f, cx - 8.0f, cy + 5.0f, 1.6f, ink);
        list.line(cx, cy - 2.0f, cx + 8.0f, cy + 5.0f, 1.6f, ink);
        list.line(cx, cy - 2.0f, cx - 6.0f, cy + 12.0f, 1.6f, ink);
        list.line(cx, cy - 2.0f, cx + 6.0f, cy + 12.0f, 1.6f, ink);
        list.rect(cx - 2.0f, cy - 13.0f, 4.0f, 4.0f, ink);
        list.rect(cx - 10.0f, cy + 4.0f, 4.0f, 4.0f, ink);
        list.rect(cx + 6.0f, cy + 4.0f, 4.0f, 4.0f, ink);
    }else if(splat){
        list.rect(cx - 10.0f, cy - 7.0f, 3.0f, 3.0f, ink);
        list.rect(cx - 2.0f, cy - 11.0f, 3.0f, 3.0f, ink);
        list.rect(cx + 6.0f, cy - 5.0f, 3.0f, 3.0f, ink);
        list.rect(cx - 7.0f, cy + 1.0f, 3.0f, 3.0f, ink);
        list.rect(cx + 2.0f, cy + 5.0f, 3.0f, 3.0f, ink);
        list.rect(cx + 8.0f, cy + 9.0f, 3.0f, 3.0f, ink);
        list.line(cx - 8.0f, cy - 5.0f, cx - 1.0f, cy - 8.0f, 1.0f, ink);
        list.line(cx, cy - 7.0f, cx + 7.0f, cy - 3.0f, 1.0f, ink);
    }else{
        list.line(cx - 8.0f, cy - 10.0f, cx + 5.0f, cy - 10.0f, 1.3f, ink);
        list.line(cx - 8.0f, cy - 10.0f, cx - 8.0f, cy + 10.0f, 1.3f, ink);
        list.line(cx - 8.0f, cy + 10.0f, cx + 8.0f, cy + 10.0f, 1.3f, ink);
        list.line(cx + 8.0f, cy + 10.0f, cx + 8.0f, cy - 7.0f, 1.3f, ink);
        list.line(cx + 5.0f, cy - 10.0f, cx + 8.0f, cy - 7.0f, 1.3f, ink);
        list.line(cx - 4.0f, cy - 4.0f, cx + 4.0f, cy - 4.0f, 1.0f, ink);
        list.line(cx - 4.0f, cy + 1.0f, cx + 4.0f, cy + 1.0f, 1.0f, ink);
    }

    const float textLeft = thumb.x + thumb.width + 9.0f;
    const float smallScale = std::max(2.0f, theme.textScale - 0.6f);
    const float badgeWidth = std::max(30.0f, std::min(55.0f, textWidth(badge, smallScale) + 10.0f));
    const Rect badgeBox{row.box.x + row.box.width - badgeWidth - 7.0f, row.box.y + row.box.height - 22.0f,
                        badgeWidth, 16.0f};
    const float nameRoom = std::max(12.0f, badgeBox.x - textLeft - 6.0f);
    list.text(textLeft, row.box.y + 7.0f, fitText(name, nameRoom, theme.textScale),
              selected ? theme.title : theme.text, theme.textScale);
    list.rect(badgeBox, Color{accent.r, accent.g, accent.b, selected ? 0.27f : 0.13f});
    list.outline(badgeBox, 1.0f, Color{accent.r, accent.g, accent.b, 0.66f});
    const float badgeTextWidth = textWidth(badge, smallScale);
    list.text(badgeBox.x + (badgeBox.width - badgeTextWidth) * 0.5f,
              badgeBox.y + (badgeBox.height - textHeight(smallScale)) * 0.5f,
              fitText(badge, badgeBox.width - 4.0f, smallScale), accent, smallScale);
    const Color signal{accent.r, accent.g, accent.b, 0.9f};
    list.rect(textLeft, row.box.y + row.box.height - 13.0f, 3.0f, 3.0f, signal);
    return row.hot && pressed[uint32_t(MouseButton::Left)];
}

bool Ui::componentHeader(const std::string& title, const Color& accent, bool* expanded, bool active){
    if(!expanded) return false;
    const Row row = nextRow(theme.rowHeight + 2.0f);
    if(!row.visible) return *expanded;
    if(row.hot && pressed[uint32_t(MouseButton::Left)]) *expanded = !*expanded;

    list.rect(row.box, row.hot ? theme.hot : theme.control);
    Color edge{accent.r, accent.g, accent.b, row.hot ? 0.72f : 0.30f};
    list.outline(row.box, 1.0f, edge);
    const Color signal = active ? accent : theme.dim;
    list.rect(row.box.x + 7.0f, row.box.y + row.box.height * 0.5f - 2.5f, 5.0f, 5.0f, signal);
    const float labelY = row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f;
    list.text(row.box.x + 19.0f, labelY, fitText(title, row.box.width - 48.0f, theme.textScale),
              active ? theme.title : theme.dim, theme.textScale);
    list.text(row.box.x + row.box.width - 18.0f, labelY, *expanded ? "v" : ">",
              accent, theme.textScale);
    return *expanded;
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


Ui::TreeClick Ui::atlasRow(const std::string& name, const std::string& kind, int depth, bool hasChildren,
                           bool expanded, bool selected, const Color& accent, bool visible){
    const Row row = nextRow(theme.rowHeight + 3.0f);
    if(!row.visible) return TreeClick::None;

    list.rect(row.box, selected ? Color{accent.r, accent.g, accent.b, 0.15f} : (row.hot ? theme.hot : theme.control));
    list.outline(row.box, selected ? 1.5f : 1.0f,
                 Color{accent.r, accent.g, accent.b, selected ? 0.88f : 0.20f});
    if(selected) list.rect(row.box.x, row.box.y + 4.0f, 2.0f, row.box.height - 8.0f, accent);

    const float indent = float(std::min(depth, 7)) * 14.0f;
    const float branchX = row.box.x + 9.0f + indent;
    const Color branch{theme.panelEdge.r, theme.panelEdge.g, theme.panelEdge.b, 0.55f};
    if(depth > 0){
        list.line(branchX, row.box.y, branchX, row.box.y + row.box.height, 1.0f, branch);
        list.line(branchX, row.box.y + row.box.height * 0.5f, branchX + 6.0f,
                  row.box.y + row.box.height * 0.5f, 1.0f, branch);
    }

    const Rect arrow{branchX + (depth > 0 ? 6.0f : 0.0f), row.box.y, 17.0f, row.box.height};
    const float cx = arrow.x + arrow.width * 0.5f;
    const float cy = row.box.y + row.box.height * 0.5f;
    if(hasChildren){
        if(expanded) list.triangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.5f, theme.dim);
        else list.triangle(cx - 2.0f, cy - 4.0f, cx - 2.0f, cy + 4.0f, cx + 3.5f, cy, theme.dim);
    }

    const float iconX = arrow.x + arrow.width + 3.0f;
    const Color icon{accent.r, accent.g, accent.b, visible ? 0.96f : 0.35f};
    list.rect(iconX, cy - 4.0f, 8.0f, 8.0f, Color{accent.r, accent.g, accent.b, 0.16f});
    list.outline(Rect{iconX, cy - 4.0f, 8.0f, 8.0f}, 1.0f, icon);
    list.rect(iconX + 2.0f, cy - 2.0f, 4.0f, 4.0f, icon);

    const float smallScale = 2.15f;
    const float pillWidth = std::max(34.0f, std::min(66.0f, textWidth(kind, smallScale) + 10.0f));
    const Rect pill{row.box.x + row.box.width - pillWidth - 7.0f, row.box.y + 7.0f, pillWidth, row.box.height - 14.0f};
    const float nameX = iconX + 14.0f;
    const float nameRoom = std::max(10.0f, pill.x - nameX - 15.0f);
    list.text(nameX, row.box.y + (row.box.height - textHeight(theme.textScale)) * 0.5f,
              fitText(name, nameRoom, theme.textScale), selected ? theme.title : (visible ? theme.text : theme.dim),
              theme.textScale);
    list.rect(pill, Color{accent.r, accent.g, accent.b, 0.10f});
    list.outline(pill, 1.0f, Color{accent.r, accent.g, accent.b, 0.26f});
    list.text(pill.x + (pill.width - textWidth(kind, smallScale)) * 0.5f,
              pill.y + (pill.height - textHeight(smallScale)) * 0.5f,
              fitText(kind, pill.width - 4.0f, smallScale), accent, smallScale);

    if(!(row.hot && pressed[uint32_t(MouseButton::Left)])) return TreeClick::None;
    if(hasChildren && arrow.contains(input.mouseX, input.mouseY)) return TreeClick::Toggle;
    return TreeClick::Select;
}

void Ui::selectionCard(const std::string& name, const std::string& kind, const Color& accent, bool visible){
    const Row row = nextRow(theme.rowHeight + 20.0f);
    if(!row.visible) return;
    list.rect(row.box, Color{accent.r, accent.g, accent.b, 0.11f});
    list.outline(row.box, 1.0f, Color{accent.r, accent.g, accent.b, 0.42f});
    list.rect(row.box.x, row.box.y + 4.0f, 3.0f, row.box.height - 8.0f, accent);

    const float left = row.box.x + 12.0f;
    const float titleRoom = row.box.width - 24.0f;
    list.text(left, row.box.y + 4.0f, fitText(name, titleRoom, theme.textScale), theme.title, theme.textScale);

    const float smallScale = 2.1f;
    const float pillWidth = std::max(50.0f, std::min(row.box.width - 100.0f, textWidth(kind, smallScale) + 12.0f));
    const Rect pill{left, row.box.y + row.box.height - 19.0f, pillWidth, 14.0f};
    list.rect(pill, Color{accent.r, accent.g, accent.b, 0.15f});
    list.text(pill.x + (pill.width - textWidth(kind, smallScale)) * 0.5f,
              pill.y + (pill.height - textHeight(smallScale)) * 0.5f,
              fitText(kind, pill.width - 4.0f, smallScale), accent, smallScale);

    const Color stateColor = visible ? Color{0.36f, 0.95f, 0.61f, 0.95f} : theme.dim;
    const float statusX = row.box.x + row.box.width - 78.0f;
    list.rect(statusX, pill.y + 4.0f, 5.0f, 5.0f, stateColor);
    const std::string status = visible ? "VISIBLE" : "HIDDEN";
    list.text(statusX + 10.0f, pill.y + (pill.height - textHeight(smallScale)) * 0.5f,
              status, stateColor, smallScale);
}

void Ui::linkedPreview(const std::string& name, const std::string& kind, const Color& accent){
    const Row row = nextRow(theme.rowHeight + 10.0f);
    if(!row.visible) return;
    list.rect(row.box, Color{accent.r, accent.g, accent.b, 0.075f});
    list.outline(row.box, 1.0f, Color{accent.r, accent.g, accent.b, 0.30f});
    list.rect(row.box.x, row.box.y + 4.0f, 2.0f, row.box.height - 8.0f,
              Color{accent.r, accent.g, accent.b, 0.72f});
    const float left = row.box.x + 10.0f;
    const float micro = 1.8f;
    list.text(left, row.box.y + 3.0f, "ATLAS LINK / " + kind, accent, micro);
    list.text(left, row.box.y + 18.0f,
              fitText(name, row.box.width - 20.0f, theme.textScale * 0.82f),
              theme.title, theme.textScale * 0.82f);
}

int Ui::breadcrumb(const std::vector<std::string>& labels){
    if(labels.empty()) return -1;
    std::vector<int> visible;
    if(labels.size() <= 4){
        for(size_t i = 0; i < labels.size(); ++i) visible.push_back(int(i));
    }else{
        visible = {0, -1, int(labels.size()) - 2, int(labels.size()) - 1};
    }

    const Row row = nextRow(theme.rowHeight + 2.0f);
    if(!row.visible) return -1;
    const float gap = 4.0f;
    const float slot = std::max(24.0f, (row.box.width - gap * float(visible.size() - 1)) /
                                           float(visible.size()));
    float x = row.box.x;
    int clicked = -1;
    for(size_t i = 0; i < visible.size(); ++i){
        const int index = visible[i];
        const Rect chip{x, row.box.y + 2.0f, slot, row.box.height - 4.0f};
        const bool hot = index >= 0 && chip.contains(input.mouseX, input.mouseY);
        const bool current = index == int(labels.size()) - 1;
        const Color fill = current ? Color{theme.active.r, theme.active.g, theme.active.b, 0.17f}
                                   : hot ? theme.hot : theme.control;
        const Color edge = current ? theme.active : Color{theme.panelEdge.r, theme.panelEdge.g, theme.panelEdge.b, 0.58f};
        list.rect(chip, fill);
        list.outline(chip, current || hot ? 1.4f : 0.8f, edge);
        const float scale = theme.textScale * 0.76f;
        const std::string text = index < 0 ? "..." : labels[size_t(index)];
        const Color ink = current ? theme.title : (hot ? theme.text : theme.dim);
        const std::string display = fitText(text, chip.width - 8.0f, scale);
        list.text(chip.x + (chip.width - textWidth(display, scale)) * 0.5f,
                  chip.y + (chip.height - textHeight(scale)) * 0.5f, display, ink, scale);
        if(hot && pressed[uint32_t(MouseButton::Left)]) clicked = index;
        if(i + 1 < visible.size()){
            const float midY = chip.y + chip.height * 0.5f;
            const float arrowX = chip.x + chip.width + gap * 0.5f;
            list.line(arrowX - 1.5f, midY - 3.0f, arrowX + 1.5f, midY, 1.0f, theme.dim);
            list.line(arrowX + 1.5f, midY, arrowX - 1.5f, midY + 3.0f, 1.0f, theme.dim);
        }
        x += slot + gap;
    }
    if(row.hot) pointerOverUi = true;
    return clicked;
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


int Ui::orbitMenu(const std::string& id, const std::vector<std::string>& labels,
                  const std::vector<bool>& enabled, const std::vector<std::string>& shortcuts,
                  std::vector<bool>* favorites){
    if(!menuOpen(id) || labels.empty()) return -1;
    closePanel();
    std::swap(list, overlay);

    if(favorites && favorites->size() < labels.size()) favorites->resize(labels.size(), false);
    std::vector<int> order;
    order.reserve(labels.size());
    for(size_t i = 0; i < labels.size(); ++i) order.push_back(int(i));
    if(favorites){
        std::stable_sort(order.begin(), order.end(), [&](int a, int b){
            return (*favorites)[size_t(a)] && !(*favorites)[size_t(b)];
        });
    }

    const float scale = theme.textScale * 0.76f;
    const float keyScale = scale * 0.68f;
    const float cardHeight = theme.rowHeight + 8.0f;
    const float maxLabelWidth = std::clamp(screenWidth * 0.24f, 148.0f, 218.0f);
    float widest = 0.0f, widestKey = 0.0f;
    for(size_t i = 0; i < labels.size(); ++i){
        widest = std::max(widest, textWidth(fitText(labels[i], maxLabelWidth, scale), scale));
        if(i < shortcuts.size() && !shortcuts[i].empty())
            widestKey = std::max(widestKey, textWidth(shortcuts[i], keyScale) + 10.0f);
    }
    const float maxCardWidth = std::clamp(screenWidth * 0.24f, 164.0f, 216.0f);
    const float cardWidth = std::clamp(widest + widestKey + 2.0f * theme.padding + 22.0f,
                                       150.0f, maxCardWidth);
    const float radiusLimit = std::max(48.0f, std::min(screenWidth, screenHeight) * 0.29f);
    const float requestedRadius = std::max(58.0f + 7.5f * float(labels.size()), cardWidth * 0.60f + 4.0f);
    const float orbitRadius = std::min(requestedRadius, radiusLimit);
    const float reach = orbitRadius + cardWidth * 0.5f + 16.0f;
    const float cx = screenWidth >= reach * 2.0f ? std::clamp(menuX, reach, screenWidth - reach) : screenWidth * 0.5f;
    const float cy = screenHeight >= reach * 2.0f ? std::clamp(menuY, reach, screenHeight - reach) : screenHeight * 0.5f;
    menuBox = Rect{cx - reach, cy - reach, reach * 2.0f, reach * 2.0f};
    if(menuBox.contains(input.mouseX, input.mouseY)) pointerOverUi = true;

    const Color ring{theme.accent.r, theme.accent.g, theme.accent.b, 0.30f};
    constexpr int segments = 48;
    for(int segment = 0; segment < segments; ++segment){
        const float a = 6.28318530718f * float(segment) / float(segments);
        const float b = 6.28318530718f * float(segment + 1) / float(segments);
        list.line(cx + std::cos(a) * orbitRadius, cy + std::sin(a) * orbitRadius,
                  cx + std::cos(b) * orbitRadius, cy + std::sin(b) * orbitRadius, 1.0f, ring);
    }

    int selected = -1;
    bool pinnedThisFrame = false;
    for(size_t slot = 0; slot < order.size(); ++slot){
        const int action = order[slot];
        const float angle = -1.57079632679f + 6.28318530718f * float(slot) / float(order.size());
        const float ix = cx + std::cos(angle) * orbitRadius;
        const float iy = cy + std::sin(angle) * orbitRadius;
        const Rect box{ix - cardWidth * 0.5f, iy - cardHeight * 0.5f, cardWidth, cardHeight};
        const bool hot = box.contains(input.mouseX, input.mouseY);
        const bool active = enabled.empty() || (size_t(action) < enabled.size() && enabled[size_t(action)]);
        const bool favorite = favorites && (*favorites)[size_t(action)];
        if(hot) pointerOverUi = true;

        list.rect(box, hot && active ? theme.hot : theme.panel);
        const bool destructive = labels[size_t(action)] == "Delete";
        const Color edge = destructive && active ? theme.warning
                         : favorite ? Color{theme.active.r, theme.active.g, theme.active.b, 0.82f}
                         : (hot && active ? theme.active : theme.panelEdge);
        list.outline(box, hot ? 1.8f : 1.0f, edge);

        const float labelLeft = box.x + (favorite ? 22.0f : 11.0f);
        const bool hasShortcut = active && size_t(action) < shortcuts.size() && !shortcuts[size_t(action)].empty();
        const float keyWidth = hasShortcut ? textWidth(shortcuts[size_t(action)], keyScale) + 10.0f : 0.0f;
        const float labelRight = hasShortcut ? box.x + box.width - keyWidth - 12.0f : box.x + box.width - 10.0f;
        const float labelRoom = std::max(20.0f, labelRight - labelLeft);
        const std::string display = fitText(labels[size_t(action)], labelRoom, scale);
        const Color ink = !active ? theme.dim : hot ? theme.title : theme.text;
        list.text(labelLeft, box.y + (box.height - textHeight(scale)) * 0.5f, display, ink, scale);
        if(favorite){
            const float fx = box.x + 10.0f, fy = box.y + box.height * 0.5f;
            const Color gold{theme.active.r, theme.active.g, theme.active.b, 0.96f};
            list.line(fx, fy - 4.0f, fx + 4.0f, fy, 1.2f, gold);
            list.line(fx + 4.0f, fy, fx, fy + 4.0f, 1.2f, gold);
            list.line(fx, fy + 4.0f, fx - 4.0f, fy, 1.2f, gold);
            list.line(fx - 4.0f, fy, fx, fy - 4.0f, 1.2f, gold);
        }
        if(hasShortcut){
            const float keyHeight = 16.0f;
            const Rect key{box.x + box.width - keyWidth - 7.0f,
                           box.y + (box.height - keyHeight) * 0.5f, keyWidth, keyHeight};
            list.rect(key, Color{theme.active.r, theme.active.g, theme.active.b, 0.10f});
            list.outline(key, 0.8f, Color{theme.active.r, theme.active.g, theme.active.b, 0.42f});
            list.text(key.x + (key.width - textWidth(shortcuts[size_t(action)], keyScale)) * 0.5f,
                      key.y + (key.height - textHeight(keyScale)) * 0.5f,
                      shortcuts[size_t(action)], theme.active, keyScale);
        }
        if(active && hot && menuPressed[uint32_t(MouseButton::Left)]){
            if(input.shift && favorites){
                (*favorites)[size_t(action)] = !(*favorites)[size_t(action)];
                pinnedThisFrame = true;
            }else{
                selected = action;
            }
        }
    }

    const Rect hub{cx - 31.0f, cy - 20.0f, 62.0f, 36.0f};
    list.rect(hub, theme.control);
    list.outline(hub, 1.5f, theme.accent);
    const std::string brand = "LOOM";
    const float markScale = scale * 0.70f;
    list.text(cx - textWidth(brand, markScale) * 0.5f,
              cy - textHeight(markScale) * 0.5f, brand, theme.title, markScale);
    const std::string hint = pinnedThisFrame ? "PINNED" : "SHIFT-CLICK TO PIN";
    const float hintScale = 1.45f;
    const float hintY = cy + orbitRadius + cardHeight * 0.5f + 8.0f;
    list.text(cx - textWidth(hint, hintScale) * 0.5f, hintY, hint,
              pinnedThisFrame ? theme.active : theme.dim, hintScale);

    std::swap(list, overlay);
    if(selected >= 0) openMenuId = 0;
    return selected;
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
