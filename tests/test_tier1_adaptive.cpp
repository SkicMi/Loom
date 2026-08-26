// tier 1 adaptive shading: stepenica 1 dohvaca depth prepass i stopu sjencanja iz dubine.
//
// Ovo je bio zadnji dug arhitekture triju stepenica. Stopa iz dubine trazi tri stvari koje
// korisnik stepenice 1 ne bi smio ni vidjeti: dubinu koja se moze semplirati, prolaz koji
// pise samo nju, i compute izmedu ta dva prolaza. Scena vec vodi svoje prolaze - zato je
// odgovor bio dati joj i ove, a ne zaobici scenu.
//
// Mjeri se ono sto se vidi: blok piksela koji je BEZ karte imao detalj, a S kartom ga je
// izgubio, znaci da su njegova cetiri piksela izasla iz jednog poziva shadera. Broji se
// odvojeno u daljini i u blizini, jer tvrdnja nije "slika se promijenila" nego "grubo je
// daleko, a blizu je netaknuto".
#include "TestHarness.h"
#include "TestScene.h"

#include <Loom/Loom.h>

#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 512;
const uint32_t sceneHeight = 384;

bool blockIsUniform(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y){
    const size_t first = (size_t(y) * sceneWidth + x) * 4;
    for(uint32_t by = 0; by < 2; ++by){
        for(uint32_t bx = 0; bx < 2; ++bx){
            const size_t here = (size_t(y + by) * sceneWidth + (x + bx)) * 4;
            for(int c = 0; c < 3; ++c){
                if(pixels[here + c] != pixels[first + c]) return false;
            }
        }
    }
    return true;
}

//Blokovi koji su imali detalj bez karte, a s njom ga nemaju. Brojati uniformne blokove
//izravno bi brojalo i nebo, koje je jedne boje kakva god stopa bila
size_t coarsened(const std::vector<uint8_t>& without, const std::vector<uint8_t>& with,
                 uint32_t fromRow, uint32_t toRow){
    size_t count = 0;
    for(uint32_t y = fromRow; y + 2 <= toRow; y += 2){
        for(uint32_t x = 0; x + 2 <= sceneWidth; x += 2){
            if(!blockIsUniform(without, x, y) && blockIsUniform(with, x, y)) ++count;
        }
    }
    return count;
}

//Podloga koja bjezi do horizonta, plus stup blizu kamere. Blizina i daljina u istoj slici,
//pa se jedno mjerenje moze usporediti s drugim bez druge scene
std::vector<uint8_t> render(bool adaptive, bool* activeOut){
    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(sceneWidth, sceneHeight);

    if(adaptive){
        Loom::AdaptiveShading settings;
        settings.enabled = true;
        settings.quarterDistance = 12.0f;
        settings.sixteenthDistance = 45.0f;
        scene.setAdaptiveShading(settings);
    }

    //Sunce ne treba: sjena bi po podlozi napravila svoje rubove, a rub je detalj koji se
    //ne bi razlikovao od teksture
    scene.setShadows(false);

    Loom::TextureHandle checker = [&](){
        const std::vector<uint8_t> pixels = makeCheckerboard(64, 2);
        return scene.createTexture(pixels.data(), 64, 64);
    }();

    scene.camera().setPosition({0.0f, 0.9f, 6.0f});
    scene.camera().lookAt({0.0f, 0.6f, -40.0f});
    scene.camera().setClipPlanes(0.1f, 300.0f);

    //Ravno bijelo okruzenje: sto se vidi je tekstura, a ne kut prema svjetlu
    scene.environment().setAmbient({1.0f, 1.0f, 1.0f});

    if(activeOut) *activeOut = scene.adaptiveShadingActive();

    scene.startRendering();
    scene.drawPlane(checker, glm::scale(glm::mat4(1.0f), glm::vec3(160.0f)));
    scene.drawCube(checker, glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 3.5f}));
    scene.endRendering();

    return scene.readPixels();
}

}

int main(){
    TestReport report("tier 1 adaptive shading");

    bool active = false;
    const std::vector<uint8_t> withoutMap = render(false, nullptr);
    const std::vector<uint8_t> withMap = render(true, &active);

    if(!active){
        //Kartica bez attachmentFragmentShadingRate crta jednako, samo bez ustede. To nije
        //greska nego dogovor - ali onda se nema sto mjeriti, pa se to i kaze
        report.check("uredaj podrzava sliku stope", false,
            "ne podrzava - scena je nacrtana punom stopom, mjerenje preskoceno");
        report.check("bez podrske slika je ista", diffBytes(withMap, withoutMap).different == 0,
            fmt("%zu bajtova razlike", diffBytes(withMap, withoutMap).different));
        return report.result();
    }

    // -------------------------------------------------------------------------------
    // Daljina je gruba
    // -------------------------------------------------------------------------------

    //Gdje je daljina, ne pogadamo - broji se po cijeloj slici. Nabrojati pojas redova i
    //nazvati ga "daljinom" znaci mjeriti nebo, sto je vec jednom prevarilo mjerenje ovdje
    const size_t coarseAll = coarsened(withoutMap, withMap, 0, sceneHeight);

    //Donja cetvrtina je pod metar ispred kamere. Ovdje je udaljenost manja od quarterDistance
    //po konstrukciji scene, a ne po oku
    const uint32_t nearFrom = sceneHeight - sceneHeight / 4;
    const size_t coarseNear = coarsened(withoutMap, withMap, nearFrom, sceneHeight);

    report.check("nesto se ogrubilo", coarseAll > 0,
        fmt("%zu blokova izgubilo detalj kad je karta ukljucena", coarseAll));

    //Tvrdnja nije "slika se promijenila" nego "daleko da, blizu ne". Bez ovoga bi prosla i
    //karta koja ogrubi cijelu sliku
    report.check("blizina je netaknuta", coarseNear * 20 < coarseAll,
        fmt("%zu ogrubjelih blokova u zadnjih %u redova, %zu ukupno",
            coarseNear, sceneHeight - nearFrom, coarseAll));

    // -------------------------------------------------------------------------------
    // Prepass nije promijenio sto se vidi
    // -------------------------------------------------------------------------------

    //eEqual i ucitana dubina smiju stediti, ali ne smiju izgubiti povrsinu. Nacrtano je
    //isto onoliko piksela podloge koliko i bez prepassa - jedini se detalj mijenja
    auto covered = [](const std::vector<uint8_t>& pixels){
        size_t count = 0;
        for(size_t i = 0; i + 3 < pixels.size(); i += 4){
            //Cisto pozadinsko plavo je clearColor; sve ostalo je nacrtana geometrija
            if(!(pixels[i] > 8 && pixels[i+1] < 24 && pixels[i+2] < 24)) ++count;
        }
        return count;
    };

    const size_t coveredWithout = covered(withoutMap);
    const size_t coveredWith = covered(withMap);
    const double drift = 100.0 * std::abs(double(coveredWith) - double(coveredWithout))
                       / double(coveredWithout);

    report.check("pokrivenost je ista", drift < 1.0,
        fmt("%zu piksela geometrije bez karte, %zu s njom, razlika %.2f%%",
            coveredWithout, coveredWith, drift));

    // -------------------------------------------------------------------------------
    // I da se to iz stepenice 1 uopce moze ugasiti
    // -------------------------------------------------------------------------------

    {
        Loom::Scene off(Loom::Preset::Offscreen);
        off.setSize(64, 64);
        report.check("iskljuceno je iskljuceno", !off.adaptiveShadingActive(),
            "scena koja to nije trazila nema ni prepass ni kartu");

        bool threw = false;
        off.startRendering();
        off.endRendering();
        try{
            Loom::AdaptiveShading late;
            late.enabled = true;
            off.setAdaptiveShading(late);
        }
        catch(const std::exception&){ threw = true; }

        report.check("kasno ukljucivanje se odbija", threw,
            "setAdaptiveShading nakon prvog framea baca - dubina i cjevovodi su vec sagradeni");
    }

    report.checkNoValidationMessages();
    return report.result();
}
