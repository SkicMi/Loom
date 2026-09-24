// Treadle: suicelje provjereno bez ijednog piksela.
//
// Cijela je poanta granice to sto je ovo moguce. Treadle ne zna za Vulkan ni za prozor, pa se
// raspored, pogadjanje misem i racun klizaca daju provjeriti brojevima - a bas su to mjesta
// gdje UI tiho grijesi: gumb koji se okine dok se vuce klizac, klizac koji ne doseze kraj
// raspona, dva gumba istog imena koja su zapravo jedan.
//
// Sto se provjerava, i zasto bas to:
//
//   font              svako slovo iz raspona ima sliku, razmak je prazan a slovo nije. Font
//                     kojemu nedostaje znak daje rupu u natpisu, a nista ne pukne
//   sirina teksta     izmjerena sirina se mora slagati s onim sto crtanje stvarno zauzme,
//                     jer raspored racuna PRIJE crtanja
//   pravokutnik       cetiri vrha i sest indeksa, na zadanim koordinatama
//   gumb              okida samo unutar sebe i samo u kadru pritiska, ne dok se drzi
//   klizac            vucenje postavlja vrijednost razmjerno, i doseze OBA kraja raspona
//   vucenje van       mis koji izadje s klizaca dok je gumb dolje i dalje ga vuce
//   wantsMouse        istinit nad plohom, laz pokraj nje - to je jedina obrana kamere od UI-a
//   iste oznake       dva gumba istog imena na dvije plohe nisu isti widget
//   sadrzaj ostaje    ono sto je widget nacrtao mora BITI u popisu na kraju kadra. Prva
//                     verzija je zatvaranjem plohe odsijecala polje do obruba - a obrub je
//                     zapisan prije sadrzaja, pa je svaki put odletjelo sve. Ostajao je
//                     prazan okvir, nista nije puklo, i nijedna provjera iznad to nije
//                     primijetila jer sve mjere pogadjanje misem a ne ono sto se vidi
#include "TestHarness.h"

#include <Treadle/Draw.h>
#include <Treadle/Ui.h>

#include <cmath>

namespace{

//Jedan kadar suicelja s zadanim ulazom. Vraca ono sto su widgeti javili
struct Frame{
    bool clicked = false;
    float sliderValue = 0.0f;
    bool wantsMouse = false;
};

//Najveci x koji je crtanje stvarno dotaknulo, mjereno od ishodista
float rightmost(const Treadle::DrawList& list){
    float worst = 0.0f;
    for(const Treadle::Vertex& vertex : list.vertices) worst = std::max(worst, vertex.x);
    return worst;
}

}

int main(){
    TestReport report("treadle_ui");

    //-- font ---------------------------------------------------------------------------------
    {
        bool everyGlyphDrawn = true;
        bool uvInRange = true;
        for(char character = '!'; character <= '~'; ++character){
            const Treadle::GlyphMetrics& glyph = Treadle::glyphMetrics(character);
            if(glyph.width <= 0.0f || glyph.height <= 0.0f) everyGlyphDrawn = false;
            if(glyph.u0 < 0.0f || glyph.u1 > 1.0f || glyph.v0 < 0.0f || glyph.v1 > 1.0f ||
               glyph.u1 < glyph.u0 || glyph.v1 < glyph.v0) uvInRange = false;
        }

        const Treadle::GlyphMetrics& space = Treadle::glyphMetrics(' ');
        const Treadle::GlyphMetrics& unknown = Treadle::glyphMetrics('\n');

        report.check("font pun", everyGlyphDrawn, "svaki znak 33..126 ima tinte u atlasu");
        report.check("UV u granicama", uvInRange, "ni jedan okvir tinte ne izlazi iz atlasa");
        report.check("razmak prazan", space.width == 0.0f && space.height == 0.0f && space.advance > 0.0f,
                     "' ' ima korak olovke ali nema tinte");
        report.check("izvan raspona", unknown.width == 0.0f && unknown.advance == space.advance,
                     "nepoznat znak se crta kao razmak");
    }

    //-- sirina teksta ------------------------------------------------------------------------
    {
        Treadle::DrawList list;
        const float scale = 2.0f;
        const std::string text = "Obrisi";
        const float promised = Treadle::textWidth(text, scale);
        list.text(0.0f, 0.0f, text, Treadle::Color{1,1,1,1}, scale);

        //Izmjerena sirina mora biti TOcNA, ne samo manja od obecane: raspored racuna PRIJE
        //crtanja i s tim brojem pomice dalje widgete, pa odstupanje odgadja samo udarac.
        //Tocan racun je isti koji crtanje radi: koraci svih slova osim zadnjeg, pa okvir
        //zadnjeg (bearing + sirina)
        float pen = 0.0f;
        for(size_t i = 0; i + 1 < text.size(); ++i) pen += Treadle::glyphMetrics(text[i]).advance;
        const Treadle::GlyphMetrics& last = Treadle::glyphMetrics(text.back());
        const float expected = (pen + last.bearing) * scale + last.width * scale;

        const float drawn = rightmost(list);
        report.check("sirina teksta", std::fabs(drawn - expected) < 0.001f && drawn <= promised + 0.001f,
                     fmt("obecano %.1f, tocno %.1f, nacrtano do %.1f", double(promised), double(expected), double(drawn)));

        report.check("prazan tekst", Treadle::textWidth("", scale) == 0.0f, "nema sirine");
    }

    //-- pravokutnik --------------------------------------------------------------------------
    {
        Treadle::DrawList list;
        list.rect(10.0f, 20.0f, 30.0f, 40.0f, Treadle::Color{1,0,0,1});

        const bool shape = list.vertices.size() == 4 && list.indices.size() == 6;
        const bool corners = shape
            && list.vertices[0].x == 10.0f && list.vertices[0].y == 20.0f
            && list.vertices[2].x == 40.0f && list.vertices[2].y == 60.0f;

        report.check("pravokutnik", shape && corners, "4 vrha, 6 indeksa, kutovi na mjestu");

        list.clear();
        list.rect(10.0f, 20.0f, 0.0f, 40.0f, Treadle::Color{1,0,0,1});
        report.check("nulta sirina", list.vertices.empty(), "ne proizvodi trokute");
    }

    //-- mapa ---------------------------------------------------------------------------------
    {
        Treadle::DrawList list;
        list.folderIcon(10.0f, 20.0f, 20.0f, Treadle::Color{1,1,0,1});

        //Znak mape su DVA ravna pravokutnika: traka (uza) i tijelo (sira). Osam vrhova i
        //dvanaest indeksa, sve u svom okviru, a donji rub trake dotice gornji rub tijela
        const bool shape = list.vertices.size() == 8 && list.indices.size() == 12;
        const bool inBox = shape && list.vertices[0].x == 10.0f && list.vertices[0].y == 20.0f
            && list.vertices[6].x == 30.0f && list.vertices[6].y == 40.0f
            && list.vertices[2].y == list.vertices[4].y
            && list.vertices[6].x > list.vertices[2].x;
        report.check("znak mape", shape && inBox,
                     "traka pa tijelo, bez razmaka, u okviru sirine size");
    }

    //-- gumb ---------------------------------------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;

        //Ploha na (20,20), sirine 200. Prvi red sadrzaja je ispod naslova i crte
        auto oneFrame = [&](Treadle::Input state){
            Frame result;
            ui.begin(state, 800.0f, 600.0f);
            ui.panel("Kocka", 20.0f, 20.0f, 200.0f);
            result.clicked = ui.button("Obrisi");
            ui.end();
            result.wantsMouse = ui.wantsMouse();
            return result;
        };

        //Mis izvan plohe, gumb dolje: nista
        input.mouseX = 600.0f; input.mouseY = 500.0f;
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        report.check("van plohe", !oneFrame(input).clicked, "pritisak izvan gumba ne okida");

        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        oneFrame(input);

        //Gdje gumb stvarno lezi: naslov + razmak + crta + razmak, pa red visine rowHeight
        const Treadle::Theme& theme = ui.style();
        const float buttonY = 20.0f + theme.padding + Treadle::textHeight(theme.textScale)
                            + theme.spacing + 1.0f + theme.spacing + theme.rowHeight * 0.5f;
        input.mouseX = 120.0f; input.mouseY = buttonY;

        report.check("bez pritiska", !oneFrame(input).clicked, "mis nad gumbom, gumb gore");

        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        report.check("pritisak", oneFrame(input).clicked, "okida u kadru pritiska");

        //Isti ulaz jos jednom: gumb je i dalje dolje, ali pritisak je bio prosli kadar
        report.check("drzanje", !oneFrame(input).clicked, "drzanje ne okida drugi put");
    }

    //-- red mape --------------------------------------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        bool clicked = false;

        auto oneFrame = [&](Treadle::Input state, bool selected){
            clicked = false;
            ui.begin(state, 800.0f, 600.0f);
            ui.panel("Projekt", 20.0f, 20.0f, 220.0f);
            clicked = ui.folderRow("snimke", selected);
            ui.end();
            return ui.drawn();
        };

        const Treadle::Theme& theme = ui.style();
        const float rowY = 20.0f + theme.padding + Treadle::textHeight(theme.textScale)
                         + theme.spacing + 1.0f + theme.spacing + theme.rowHeight * 0.5f;
        input.mouseX = 60.0f; input.mouseY = rowY;

        //Znak mape je dvostruki ravni pravokutnik: uza traka i siri oblik. Ime stoji pokraj
        const Treadle::DrawList& drawn = oneFrame(input, false);
        bool hasTab = false, hasBody = false;
        for(size_t i = 0; i + 3 < drawn.vertices.size(); i += 4){
            const float width = drawn.vertices[i + 2].x - drawn.vertices[i].x;
            if(drawn.vertices[i].mode < 0.5f){
                if(width > 7.0f && width < 10.0f) hasTab = true;
                if(width > 14.0f && width < 16.0f) hasBody = true;
            }
        }
        report.check("red mape", !clicked && hasTab && hasBody,
                     "znak mape (traka + tijelo) pored imena, bez klika");

        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        oneFrame(input, false);
        report.check("ulaz u mapu", clicked, "pritisak na red mape ulazi u nju");

        //Odabrana mapa zadrzi znak: crta se i sa zlatnom podlogom odabira
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        const Treadle::DrawList& selected = oneFrame(input, true);
        bool iconSurvives = false;
        for(size_t i = 0; i + 3 < selected.vertices.size(); i += 4){
            const float width = selected.vertices[i + 2].x - selected.vertices[i].x;
            if(width > 14.0f && width < 16.0f) iconSurvives = true;
        }
        report.check("mapa u odabiru", iconSurvives, "znak ne nestane kad se red odabere");
    }

    //-- klizac -------------------------------------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        float size = 0.05f;

        auto oneFrame = [&](Treadle::Input state){
            ui.begin(state, 800.0f, 600.0f);
            ui.panel("Kocka", 20.0f, 20.0f, 200.0f);
            ui.slider("velicina", &size, 0.0f, 1.0f);
            ui.end();
            return ui.wantsMouse();
        };

        const Treadle::Theme& theme = ui.style();
        const float rowY = 20.0f + theme.padding + Treadle::textHeight(theme.textScale)
                         + theme.spacing + 1.0f + theme.spacing + theme.rowHeight * 0.5f;
        const float trackLeft = 20.0f + theme.padding;
        const float trackWidth = 200.0f - 2.0f * theme.padding;

        input.mouseY = rowY;
        input.mouseX = trackLeft + trackWidth * 0.5f;
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        oneFrame(input);
        report.check("klizac sredina", std::fabs(size - 0.5f) < 0.001f, fmt("%.3f", double(size)));

        //Mis pobjegne s plohe dok je gumb jos dolje - vucenje se nastavlja
        input.mouseX = trackLeft + trackWidth * 0.25f;
        input.mouseY = 590.0f;
        oneFrame(input);
        report.check("vucenje van", std::fabs(size - 0.25f) < 0.001f, fmt("%.3f", double(size)));

        //Oba kraja se moraju dosegnuti. Klizac koji staje na 0.98 je klizac koji laze
        input.mouseX = trackLeft + trackWidth + 50.0f;
        oneFrame(input);
        report.check("gornji kraj", size == 1.0f, fmt("%.3f", double(size)));

        input.mouseX = trackLeft - 50.0f;
        oneFrame(input);
        report.check("donji kraj", size == 0.0f, fmt("%.3f", double(size)));

        //Gumb gore: vucenje prestaje i mis vise ne mijenja nista
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        oneFrame(input);
        input.mouseX = trackLeft + trackWidth * 0.75f;
        input.mouseY = rowY;
        oneFrame(input);
        report.check("otpusteno", size == 0.0f, fmt("%.3f nakon prelaska preko klizaca", double(size)));
    }

    //-- wantsMouse ---------------------------------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;

        auto oneFrame = [&](float x, float y){
            input.mouseX = x; input.mouseY = y;
            ui.begin(input, 800.0f, 600.0f);
            ui.panel("Kocka", 20.0f, 20.0f, 200.0f);
            ui.button("Obrisi");
            ui.end();
            return ui.wantsMouse();
        };

        report.check("mis na plohi", oneFrame(100.0f, 40.0f), "wantsMouse istinit");
        report.check("mis pokraj", !oneFrame(600.0f, 400.0f), "wantsMouse laz");
    }

    //-- iste oznake na dvije plohe -----------------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        float left = 0.5f, right = 0.5f;

        const Treadle::Theme& theme = ui.style();
        const float rowY = 20.0f + theme.padding + Treadle::textHeight(theme.textScale)
                         + theme.spacing + 1.0f + theme.spacing + theme.rowHeight * 0.5f;

        //Mis na prvom klizacu, obje plohe imaju klizac iste oznake
        input.mouseX = 30.0f + 10.0f; input.mouseY = rowY;
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;

        ui.begin(input, 800.0f, 600.0f);
        ui.panel("Lijevo", 20.0f, 20.0f, 200.0f);
        ui.slider("velicina", &left, 0.0f, 1.0f);
        ui.panel("Desno", 400.0f, 20.0f, 200.0f);
        ui.slider("velicina", &right, 0.0f, 1.0f);
        ui.end();

        report.check("razdvojene plohe", left != 0.5f && right == 0.5f,
                     fmt("lijevo %.3f, desno %.3f", double(left), double(right)));
    }

    //-- sadrzaj plohe stvarno zavrsi u popisu -------------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        input.mouseX = 600.0f; input.mouseY = 500.0f;

        auto verticesOf = [&](bool withWidgets){
            ui.begin(input, 800.0f, 600.0f);
            ui.panel("Kocka", 20.0f, 20.0f, 200.0f);
            if(withWidgets){
                ui.label("velicina");
                ui.button("Obrisi");
                float size = 0.5f;
                ui.slider("polumjer", &size, 0.0f, 1.0f);
            }
            ui.end();
            return ui.drawn().vertices.size();
        };

        const size_t bare = verticesOf(false);
        const size_t full = verticesOf(true);

        //Sam naslov "Kocka" nosi desetke pravokutnika, pa prazna ploha nije prazna - ali tri
        //widgeta s tekstom moraju je visestruko nadmasiti
        report.check("sadrzaj plohe ostaje", full > bare * 2,
            fmt("prazna ploha %zu vrhova, s widgetima %zu", bare, full));

        //I da indeksi odgovaraju vrhovima: sest na svaka cetiri, jer je sve pravokutnik
        report.check("indeksi prate vrhove", ui.drawn().indices.size() == full / 4 * 6,
            fmt("%zu vrhova, %zu indeksa", full, ui.drawn().indices.size()));

        //Nijedan indeks ne smije pokazivati izvan polja vrhova. Odsijecanje polja je upravo
        //ono sto bi ovo slomilo
        bool inRange = true;
        for(uint32_t index : ui.drawn().indices){
            if(index >= ui.drawn().vertices.size()) inRange = false;
        }
        report.check("indeksi u granicama", inRange, "nijedan ne pokazuje izvan polja vrhova");

        //POZADINA PLOHE POSTOJI I POKRIVA JE CIJELU. Prva verzija ju je crtala visine nula,
        //sto rect() preskace, pa je zakrpa visine razvukla prvi pravokutnik NASLOVA preko
        //cijele plohe: bijela crta uz tekst umjesto pozadine. Nijedna provjera to nije
        //primijetila jer su sve mjerile gdje se klikne, a ne sto se vidi
        const Treadle::DrawList& drawn = ui.drawn();
        bool hasBackground = false;
        float tallest = 0.0f;
        for(size_t i = 0; i + 3 < drawn.vertices.size(); i += 4){
            const float width = drawn.vertices[i + 2].x - drawn.vertices[i].x;
            const float height = drawn.vertices[i + 2].y - drawn.vertices[i].y;
            if(width >= 199.0f && height > 40.0f) hasBackground = true;

            //I nista visoko ne smije stajati UNUTAR plohe: uspravna crta preko cijele plohe
            //je upravo ono kako se ta greska pokazala. Slova su uski pravokutnici (znamenka
            //i polumjer visine 42), a zaobljeni gumb je squaret s radiusom - ni jedno ni
            //drugo nije greska koju ovaj test trazi, pa se gledaju samo ravni (mode 0)
            //cetverokuti
            const float mode = drawn.vertices[i].mode;
            if(width < 4.0f && mode < 0.5f) tallest = std::max(tallest, height);
        }

        report.check("pozadina plohe", hasBackground, "pun pravokutnik preko cijele plohe");
        report.check("nema razvucenih", tallest < 30.0f,
            fmt("najvisi uski pravokutnik %.0f px (slovo je 14)", double(tallest)));
    }

    //-- USIDRENA PLOHA: ono sto ne stane se ne crta i ne klika --------------------------------
    //
    //Crtac ne reze, pa redak koji ne stane mora nestati CIJELI. Da se samo ne crta a i dalje
    //prima klik, klik na pogled ispod stupca bi kliknuo nevidljivi gumb
    {
        Treadle::Ui ui;
        Treadle::Input input;
        float scroll = 0.0f;
        const Treadle::Rect box{0.0f, 0.0f, 240.0f, 200.0f};
        int clicked = -1;

        auto oneFrame = [&](Treadle::Input state){
            clicked = -1;
            ui.begin(state, 1000.0f, 800.0f);
            ui.dock("Media", box, &scroll);
            for(int i = 0; i < 20; ++i){
                if(ui.button("snimka " + std::to_string(i))) clicked = i;
            }
            ui.end();
        };

        oneFrame(input);
        float lowest = 0.0f;
        for(const Treadle::Vertex& v : ui.drawn().vertices) lowest = std::max(lowest, v.y);
        report.check("usidrena ploha ne curi", lowest <= box.height + 0.01f,
            fmt("najnizi vrh %.1f, ploha do %.0f", double(lowest), double(box.height)));

        //Gdje bi deseti redak bio da ploha nije usidrena: ispod nje
        const Treadle::Theme& theme = ui.style();
        const float firstRow = theme.padding + Treadle::textHeight(theme.textScale) + theme.spacing + 1.0f + theme.spacing;
        const float pitch = theme.rowHeight + theme.spacing;
        input.mouseX = 100.0f;
        input.mouseY = firstRow + pitch * 10.0f + theme.rowHeight * 0.5f;
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        oneFrame(input);
        report.check("redak izvan plohe ne prima klik", clicked == -1, fmt("kliknut %d", clicked));
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        oneFrame(input);

        //Kotacic do kraja: odsijece se na sadrzaj, pa je zadnji redak na dnu i klika se
        input.mouseY = 100.0f;
        input.wheel = -100.0f;
        oneFrame(input);
        input.wheel = 0.0f;
        const float visible = box.height - theme.padding * 0.5f - firstRow;
        const float content = pitch * 20.0f;
        report.check("kotacic staje na kraju sadrzaja", std::fabs(scroll - (content - visible)) < 0.01f,
            fmt("pomak %.1f, ocekivano %.1f", double(scroll), double(content - visible)));

        const float lastRowY = firstRow - scroll + pitch * 19.0f + theme.rowHeight * 0.5f;
        input.mouseY = lastRowY;
        oneFrame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        oneFrame(input);
        report.check("zadnji redak se klika nakon pomaka", clicked == 19, fmt("kliknut %d na y %.0f", clicked, double(lastRowY)));
    }

    //-- IZBORNIK NA DESNI KLIK --------------------------------------------------------------
    //
    //Dvije stvari se ne smiju dogoditi: klik na stavku izbornika klikne i gumb ispod njega, i
    //klik kojim se izbornik zatvara klikne ono na sto je pao
    {
        Treadle::Ui ui;
        Treadle::Input input;
        bool solveChosen = false, buttonUnder = false, rowSelected = false;
        size_t vertexCountBeforeMenu = 0;


        //Stvarni kadar: redak, gumbi, pa izbornik na kraju, kao u editoru
        auto frame = [&](Treadle::Input state){
            solveChosen = buttonUnder = rowSelected = false;
            ui.begin(state, 1000.0f, 800.0f);
            ui.dock("Media", Treadle::Rect{0.0f, 0.0f, 300.0f, 600.0f});
            if(ui.selectable("C0256.MP4", false)) rowSelected = true;
            if(ui.rightClicked()) ui.openMenu("media");
            for(int i = 0; i < 8; ++i) if(ui.button("ispod " + std::to_string(i))) buttonUnder = true;
            vertexCountBeforeMenu = ui.drawn().vertices.size();
            if(ui.beginMenu("media")){
                if(ui.menuItem("Solve kamere")) solveChosen = true;
                ui.menuItem("Solve + splat");
                ui.endMenu();
            }
            ui.end();
        };

        const Treadle::Theme& theme = ui.style();
        const float firstRow = theme.padding + Treadle::textHeight(theme.textScale) + theme.spacing + 1.0f + theme.spacing;
        input.mouseX = 60.0f;
        input.mouseY = firstRow + theme.rowHeight * 0.5f;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Right)] = true;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Right)] = false;
        const bool opened = ui.menuOpen("media");
        frame(input);

        //Izbornik je nacrtan POSLIJE svega ostalog: njegova pozadina pocinje na mjestu klika, a
        //lezi iza svega sto je stupac nacrtao
        bool onTop = false;
        const std::vector<Treadle::Vertex>& vertices = ui.drawn().vertices;
        for(size_t i = vertexCountBeforeMenu; i + 3 < vertices.size(); i += 4){
            if(vertices[i].x == input.mouseX && vertices[i].y == input.mouseY &&
               vertices[i + 2].x - vertices[i].x >= 200.0f) onTop = true;
        }
        report.check("desni klik otvara izbornik, crta se na vrhu", opened && onTop,
            fmt("%zu vrhova prije izbornika, %zu ukupno", vertexCountBeforeMenu, ui.drawn().vertices.size()));

        //Prva stavka lezi odmah ispod mjesta klika (od +16 do +40 ispod prvog retka), a gumb
        //"ispod 0" od +30 do +54: na +35 mis je nad OBOJE
        input.mouseY = firstRow + 35.0f;
        input.mouseX = 120.0f;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        report.check("stavka izbornika okida, gumb ispod ne", solveChosen && !buttonUnder && !rowSelected,
            fmt("stavka %d, gumb ispod %d, redak %d", solveChosen, buttonUnder, rowSelected));
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        frame(input);
        report.check("izbornik se zatvori nakon izbora", !ui.menuOpen("media"), "zatvoren");

        //Opet otvoren, pa klik pokraj njega - na gumb: izbornik se zatvori, gumb ne okine
        input.mouseX = 60.0f;
        input.mouseY = firstRow + theme.rowHeight * 0.5f;
        input.down[uint32_t(Treadle::MouseButton::Right)] = true;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Right)] = false;
        frame(input);
        const float buttonRow7 = firstRow + (theme.rowHeight + theme.spacing) * 8.0f + theme.rowHeight * 0.5f;
        input.mouseY = buttonRow7;
        input.mouseX = 20.0f;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        report.check("klik pokraj izbornika ga zatvara i ne klika dalje", !ui.menuOpen("media") && !buttonUnder,
            fmt("otvoren %d, gumb %d", ui.menuOpen("media"), buttonUnder));
    }

    //-- RED STABLA: strelica otvara, ostatak bira ---------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        Treadle::Ui::TreeClick result = Treadle::Ui::TreeClick::None;
        auto frame = [&](Treadle::Input state){
            ui.begin(state, 1000.0f, 800.0f);
            ui.dock("Scena", Treadle::Rect{0.0f, 0.0f, 300.0f, 400.0f});
            result = ui.treeRow("Kamera", 1, true, false, false);
            ui.end();
        };
        const Treadle::Theme& theme = ui.style();
        const float firstRow = theme.padding + Treadle::textHeight(theme.textScale) + theme.spacing + 1.0f + theme.spacing;
        const float arrowX = theme.padding + theme.rowHeight * 0.7f + theme.rowHeight * 0.5f;
        input.mouseY = firstRow + theme.rowHeight * 0.5f;
        input.mouseX = arrowX;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        const Treadle::Ui::TreeClick onArrow = result;
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        frame(input);
        input.mouseX = 200.0f;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        report.check("strelica otvara, ime bira",
            onArrow == Treadle::Ui::TreeClick::Toggle && result == Treadle::Ui::TreeClick::Select, "uvucen redak dubine 1");
    }

    //-- SLOBODNA POVRSINA: vucenje ostaje njezino ---------------------------------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        Treadle::Ui::Region region;
        auto frame = [&](Treadle::Input state){
            ui.begin(state, 1000.0f, 800.0f);
            region = ui.region("timeline", Treadle::Rect{0.0f, 600.0f, 1000.0f, 200.0f});
            ui.end();
        };
        input.mouseX = 500.0f; input.mouseY = 700.0f;
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        const bool started = region.pressed && region.held && ui.wantsMouse();
        input.mouseY = 100.0f;                              //mis pobjegne gore, u pogled
        frame(input);
        const bool stillHeld = region.held && !region.hot && ui.wantsMouse();
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        frame(input);
        report.check("vucenje po timelineu ostaje njegovo i kad mis izadje", started && stillHeld && !region.held &&
                                                                             !ui.wantsMouse(),
            fmt("pocelo %d, drzano vani %d", started, stillHeld));
    }

    //-- skraceni tekst --------------------------------------------------------------------------
    {
        const std::string longName = "C0256_jako_dugo_ime_snimke_koje_ne_stane.MP4";
        const std::string fitted = Treadle::fitText(longName, 200.0f, 2.0f);
        report.check("predugo ime se skrati da stane", Treadle::textWidth(fitted, 2.0f) <= 200.0f &&
                                                       fitted.size() > 4 && fitted.substr(fitted.size() - 2) == "..",
            fitted);
    }

    //-- BROJ KOJI SE VUCE: pomak misa u pomak broja, svako polje za sebe ---------------------
    {
        Treadle::Ui ui;
        Treadle::Input input;
        float position[3] = {1.0f, 2.0f, 3.0f};
        auto frame = [&](Treadle::Input state){
            ui.begin(state, 1000.0f, 800.0f);
            ui.dock("Svojstva", Treadle::Rect{0.0f, 0.0f, 320.0f, 400.0f});
            ui.dragVector("pomak", position, 0.01f);
            ui.end();
        };
        const Treadle::Theme& theme = ui.style();
        const float firstRow = theme.padding + Treadle::textHeight(theme.textScale) + theme.spacing + 1.0f + theme.spacing;
        const float fieldRow = firstRow + Treadle::textHeight(theme.textScale) + theme.spacing + theme.rowHeight * 0.5f;
        const float width = (320.0f - 2.0f * theme.padding - 2.0f * theme.spacing) / 3.0f;
        //Srednje polje (y): pritisni, vuci 50 desno pa jos 20 desno; mis smije izaci iz polja
        input.mouseX = theme.padding + width * 1.5f + theme.spacing;
        input.mouseY = fieldRow;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = true;
        frame(input);
        input.mouseX += 50.0f;
        frame(input);
        input.mouseX += 120.0f;                       //van polja, u trece - i dalje vuce y
        input.mouseY += 200.0f;
        frame(input);
        input.down[uint32_t(Treadle::MouseButton::Left)] = false;
        frame(input);
        input.mouseX += 100.0f;
        frame(input);
        report.check("vucenje broja: pomak misa puta brzina, samo svoje polje",
            std::fabs(position[1] - 3.7f) < 1e-4f && position[0] == 1.0f && position[2] == 3.0f,
            fmt("x %.3f, y %.3f (ocekivano 3.700), z %.3f", position[0], position[1], position[2]));
    }

    return report.result();
}
