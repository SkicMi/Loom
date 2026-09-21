#include "Engine/CameraHints.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <cstdio>
#include <cstdlib>

namespace Engine{
namespace{

std::string lowercase(std::string text){
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){return char(std::tolower(c));});
    return text;
}

bool contains(const std::string& haystack, const std::string& needle){
    return lowercase(haystack).find(lowercase(needle)) != std::string::npos;
}

std::string valueOf(const SourceFacts& facts, const std::string& key){
    for(const MetadataEntry& entry : facts.metadata){
        if(lowercase(entry.key) == lowercase(key)) return entry.value;
    }
    return {};
}

//Bilo koji kljuc koji sadrzi ovaj niz - kontejneri ista imena pisu na pet nacina
std::string valueContaining(const SourceFacts& facts, const std::string& part){
    for(const MetadataEntry& entry : facts.metadata){
        if(contains(entry.key, part)) return entry.value;
    }
    return {};
}

//ISO 6709: "+58.6539+25.9721/" ili "+58.6539+25.9721+123.456/"
bool parseLocation(const std::string& text, double& latitude, double& longitude, double& altitude){
    std::vector<double> numbers;
    size_t i = 0;
    while(i < text.size()){
        if(text[i] != '+' && text[i] != '-'){ ++i; continue; }
        size_t end = i + 1;
        while(end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) ++end;
        if(end > i + 1) numbers.push_back(std::atof(text.substr(i, end - i).c_str()));
        i = end;
    }
    if(numbers.size() < 2) return false;
    latitude = numbers[0];
    longitude = numbers[1];
    altitude = numbers.size() > 2 ? numbers[2] : 0.0;
    return true;
}

//Ime zapisa koje ne govori nista o kameri. Kontejner ga uvijek nekako popuni, pa prazno polje
//izgleda isto kao popunjeno - a razlika je bas ono sto citatelja zanima
bool isGenericHandler(const std::string& handler){
    static const char* generic[] = {
        "videohandler", "soundhandler", "sound", "video media handler", "sound media handler",
        "timed metadata media handler", "core media video", "core media audio", "core media data",
        "subtitle", "alias data handler", "gpac", "isomedia", "bento4", "mainconcept", "l-smash"
    };
    for(const char* one : generic){
        if(contains(handler, one)) return true;
    }
    return false;
}

//SIRINA SENZORA po modelu, u milimetrima. Samo ono sto se dade tvrditi: format, ne tocna brojka
//za svaki nacin snimanja. U videu se senzor obicno jos i izreze, pa je ovo gornja granica
double sensorWidthFor(const std::string& both){
    if(contains(both, "zv-e10") || contains(both, "a6") || contains(both, "fx30") ||
       contains(both, "a6700") || contains(both, "zv-e1")) return 23.5;     //Sony APS-C
    if(contains(both, "a7") || contains(both, "fx3") || contains(both, "fx6") ||
       contains(both, "a1") || contains(both, "a9")) return 35.9;           //Sony puni kadar
    return 0.0;
}

//ZARISNI RASPON IZ IMENA OBJEKTIVA. "18-50mm F2.8 DC DN" -> 18 i 50; "35mm F1.8" -> 35 i 35.
//Bez regexa: trazi se "mm", pa se unatrag citaju brojevi i eventualna crtica
void focalRangeFrom(const std::string& lens, double& shortest, double& longest){
    shortest = 0.0; longest = 0.0;
    const size_t at = lens.find("mm");
    if(at == std::string::npos) return;

    size_t from = at;
    while(from > 0){
        const char c = lens[from - 1];
        if((c >= '0' && c <= '9') || c == '-' || c == '.') --from;
        else break;
    }
    if(from >= at) return;

    const std::string piece = lens.substr(from, at - from);
    const size_t dash = piece.find('-');
    try{
        if(dash == std::string::npos){
            shortest = longest = std::stod(piece);
        }else{
            shortest = std::stod(piece.substr(0, dash));
            longest  = std::stod(piece.substr(dash + 1));
        }
    }catch(...){
        shortest = longest = 0.0;
    }
    if(shortest <= 0.0 || longest < shortest){ shortest = 0.0; longest = 0.0; }
}

//Tvornicka vodoravna vidna polja, po modelu. Priblizna i to je tako i receno u biljesci - GoPro
//mijenja kadar s nacinom snimanja (Wide/Linear/SuperView) i sa stabilizacijom, a dron ima svoje
double fieldOfViewFor(const std::string& make, const std::string& model, std::string& note){
    const std::string both = make + " " + model;

    if(contains(both, "gopro")){
        note = "GoPro, tvornicki Wide: oko 92 st vodoravno. Linear je oko 75, SuperView preko 100 "
               "i jako izoblicen. Nacin snimanja se iz metapodataka obicno ne vidi";
        return 92.0;
    }
    if(contains(both, "dji") || contains(both, "mavic") || contains(both, "mini") || contains(both, "phantom")){
        note = "DJI, tipicno 84 st dijagonalno = oko 73 st vodoravno na 16:9";
        return 73.0;
    }
    if(contains(both, "apple") || contains(both, "iphone")){
        note = "iPhone, glavna kamera je oko 69 st vodoravno; ultrasiroka oko 106";
        return 69.0;
    }
    if(contains(both, "insta360")){
        note = "Insta360: vidno polje ovisi o izrezu, 90 st je samo gruba sredina";
        return 90.0;
    }
    if(contains(both, "sony") || contains(both, "canon") || contains(both, "nikon") || contains(both, "panasonic")){
        note = "Fotoaparat: vidno polje ovisi o objektivu i nije u snimci. 60 st je puka sredina";
        return 60.0;
    }
    return 0.0;
}

}

const char* sourceName(HintSource source){
    switch(source){
        case HintSource::Metadata:   return "procitano iz datoteke";
        case HintSource::ModelTable: return "iz tablice modela";
        case HintSource::Assumed:    return "pretpostavka";
        default:                     return "nepoznato";
    }
}

CameraHints hintsFrom(const SourceFacts& facts){
    CameraHints hints;
    hints.rotation = facts.rotation;

    //Proizvodjac i model: svaki kontejner ih pise drugdje
    hints.make = valueOf(facts, "make");
    if(hints.make.empty()) hints.make = valueContaining(facts, "quicktime.make");
    hints.model = valueOf(facts, "model");
    if(hints.model.empty()) hints.model = valueContaining(facts, "quicktime.model");

    //Kad nema izravnog kljuca, ime handlera zna odati proizvodjaca: "DJI.AVC", "GoPro AVC encoder".
    //Ali vecina kamera tu pise generican naziv koji ne znaci nista - "Video Media Handler",
    //"Core Media Video", "VideoHandler". Takvo ime nije podatak nego popuna, i upisati ga kao
    //proizvodjaca je gore nego ne upisati nista: Sony ZV-E10 II je tako izlazio kao kamera
    //imena "Video Media Handler", a citatelj nema kako znati da to nije procitano nego izmisljeno
    if(hints.make.empty()){
        const std::string handler = valueContaining(facts, "handler_name");
        if(!handler.empty() && !isGenericHandler(handler)){
            hints.make = handler;
            hints.notes.push_back("proizvodjac procitan iz imena zapisa (handler): \"" + handler + "\"");
        }
    }
    for(const std::string& stream : facts.streams){
        if(contains(stream, "gopro") || contains(stream, "gpmd")){
            hints.hasTelemetry = true;
            hints.telemetryNote = stream;
        }

        //Svaki zapis s vremenskim oznakama koji nije ni slika ni zvuk je telemetrija - GoPro je
        //samo jedan od njih. Sony ga zove "Timed Metadata Media Handler" i u njemu su zirokop,
        //akcelerometar i podaci o objektivu; prije se prijavljivalo da telemetrije nema, dok je
        //na ovoj snimci bilo 66 MB
        if(!hints.hasTelemetry && contains(stream, "data") &&
           (contains(stream, "metadata") || contains(stream, "rtmd") || contains(stream, "timed"))){
            hints.hasTelemetry = true;
            hints.telemetryNote = stream;
        }

        if(hints.make.empty() && contains(stream, "dji")) hints.make = "DJI";
    }

    //DJI serijski broj stoji u komentaru: "DE=None,SN=1SFLH1M0AB0NV5, Type=Normal..."
    const std::string comment = valueOf(facts, "comment");
    const size_t serialAt = comment.find("SN=");
    if(serialAt != std::string::npos){
        const size_t end = comment.find_first_of(", ", serialAt + 3);
        hints.serial = comment.substr(serialAt + 3, end == std::string::npos ? std::string::npos : end - serialAt - 3);
    }

    //Polozaj
    std::string location = valueOf(facts, "location");
    if(location.empty()) location = valueContaining(facts, "location");
    if(!location.empty() && parseLocation(location, hints.latitude, hints.longitude, hints.altitude)){
        hints.hasPosition = true;
        hints.notes.push_back("snimka nosi polozaj; jedna tocka daje mjesto, a cijeli zapis bi dao i MJERILO");
    }

    //Zarisna duljina: iz modela, ili opca pretpostavka
    std::string modelNote;
    double fov = fieldOfViewFor(hints.make, hints.model, modelNote);
    if(fov > 0.0){
        hints.focalSource = HintSource::ModelTable;
        hints.notes.push_back(modelNote);
    }else{
        fov = 70.0;
        hints.focalSource = HintSource::Assumed;
        hints.notes.push_back("kamera nije prepoznata; uzeto 70 st vodoravno kao opca pretpostavka");
    }

    hints.horizontalFieldOfView = fov;
    hints.intrinsics.width = facts.width;
    hints.intrinsics.height = facts.height;
    hints.intrinsics.cx = 0.5f * float(facts.width);
    hints.intrinsics.cy = 0.5f * float(facts.height);

    const double focal = (0.5 * double(facts.width)) / std::tan(0.5 * fov * 3.14159265358979 / 180.0);
    hints.intrinsics.fx = float(focal);
    hints.intrinsics.fy = float(focal * facts.pixelAspect);

    if(std::fabs(facts.pixelAspect - 1.0) > 1e-3){
        hints.notes.push_back("piksel nije kvadratan (omjer " + std::to_string(facts.pixelAspect) +
                              "), zarisna po okomici je prilagodjena");
    }
    if(facts.rotation != 0){
        hints.notes.push_back("snimka je zapisana zakrenuto za " + std::to_string(facts.rotation) +
                              " st; Spool je vec okrece, pa su sirina i visina one koje se vide");
    }
    if(hints.hasTelemetry){
        hints.notes.push_back("postoji telemetrijski zapis (" + hints.telemetryNote +
                              "): u njemu su zirokop i GPS, dakle pocetne rotacije i mjerilo. Jos ga ne citamo");
    }

    hints.notes.push_back("glavna tocka je pretpostavljena u sredini, distorzija se ne modelira - "
                          "oboje se vidi kao reprojekcija koja ne ide ispod otprilike jednog piksela");

    //=====================================================================================
    // OGRADA OKO ZARISTA IZ SENZORA I OBJEKTIVA - vidi CameraHints::hasFieldOfViewRange
    //=====================================================================================
    {
        const std::string sidecarModel = valueOf(facts, "sidecar.device.model");
        const std::string sidecarMake  = valueOf(facts, "sidecar.device.manufacturer");
        hints.lens = valueOf(facts, "sidecar.lens.model");

        if(!sidecarModel.empty()){
            if(hints.model.empty()) hints.model = sidecarModel;
            if(hints.make.empty()) hints.make = sidecarMake;
            hints.notes.push_back("kamera i objektiv procitani iz pratece datoteke: " +
                                  sidecarMake + " " + sidecarModel +
                                  (hints.lens.empty() ? std::string() : ", " + hints.lens));
        }

        const double sensor = sensorWidthFor(sidecarMake + " " + sidecarModel);
        hints.sensorWidthMillimetres = sensor;
        double shortest = 0.0, longest = 0.0;
        focalRangeFrom(hints.lens, shortest, longest);

        if(sensor > 0.0 && shortest > 0.0 && longest >= shortest){
            const auto fovOf = [&](double focal){
                return glm::degrees(2.0 * std::atan(0.5 * sensor / focal));
            };
            hints.widestFieldOfView = fovOf(shortest);
            hints.narrowestFieldOfView = fovOf(longest);
            hints.hasFieldOfViewRange = true;

            char text[220];
            std::snprintf(text, sizeof(text),
                          "objektiv %.0f-%.0f mm na senzoru sirine %.1f mm: vodoravno vidno polje "
                          "moze biti samo izmedju %.0f i %.0f st, sve izvan toga je izvan fizike",
                          shortest, longest, sensor, hints.narrowestFieldOfView, hints.widestFieldOfView);
            hints.notes.push_back(text);
            hints.notes.push_back("ograda je namjerno siroka: video obicno izrezuje senzor, a zoom "
                                  "se tijekom kadra moze pomaknuti");
        }
    }

    return hints;
}

}
