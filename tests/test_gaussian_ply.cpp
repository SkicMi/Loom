// G0: .ply s gaussianima ulazi u Spool.
//
// Prvi korak gaussian splattinga, i namjerno onaj bez ijednog piksela: dok se ne zna da
// brojevi ulaze tocno onakvi kakvi u fileu pisu, svaka slika dalje je pogadjanje.
//
// SVE BAJTOVE OVDJE SLAZE TEST, RUCNO. Glava se sastavlja kao tekst, zapisi se pakiraju
// memcpyjem po zadanom rasporedu, i tek to ide na disk. Da file pise Spool, mjerila bi se
// samo njegova suglasnost sa samim sobom - a upravo takav test prezivi citac koji cijeli
// format razumije naopako.
//
// Cetiri tvrdnje, i svaka pada na drugu vrstu greske:
//
//   sto je zapisano to se cita     svako polje bit-identicno, ne "priblizno"
//   redoslijed nije pretpostavka   ista glava drugim redom mora dati isti oblak. Citac koji
//                                  ide po mjestu umjesto po imenu ovdje pada, a na jednom
//                                  fileu iz jednog alata izgledao bi ispravno zauvijek
//   tudja svojstva se preskacu     fileovi nose i sto Spool ne cita; preko toga se mora
//                                  prijeci po velicini tipa, ne stati
//   stupanj se prepoznaje          0, 9, 24 i 45 f_rest svojstava su jedini ispravni brojevi
//
// I peta, koja cuva prve cetiri: usporedba mora GRISTI. Jedan bit u jednoj poziciji i
// provjera mora pasti, inace "bit-identicno" ne znaci nista.
#include "TestHarness.h"

#include <Spool/GaussianPly.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace{

//Sto se u zapis pise, po imenu svojstva. Ovo je istina koju test drzi u ruci; sve nize je
//samo pitanje kojim ce redom ta imena zavrsiti u glavi
struct Field{
    std::string name;
    float value = 0.0f;
};

//Jedan gaussian s brojevima koji se ne daju zamijeniti jedan za drugi: svaki je drugaciji,
//nijedan nije nula i nijedan nije okrugao, pa zamjena dva polja odmah pukne
std::vector<Field> makeFields(int seed, uint32_t restCount){
    const float base = 100.0f * float(seed);
    std::vector<Field> fields = {
        {"x", base + 1.25f},      {"y", base + 2.5f},       {"z", base + 3.75f},
        {"nx", base + 4.125f},    {"ny", base + 5.25f},     {"nz", base + 6.375f},
        {"f_dc_0", base + 7.5f},  {"f_dc_1", base + 8.625f},{"f_dc_2", base + 9.75f},
        {"opacity", base + 10.875f},
        {"scale_0", base + 11.5f},{"scale_1", base + 12.25f},{"scale_2", base + 13.125f},
        {"rot_0", base + 14.0625f},{"rot_1", base + 15.03125f},
        {"rot_2", base + 16.5f},  {"rot_3", base + 17.25f},
    };
    for(uint32_t i = 0; i < restCount; ++i){
        //Rampa: vrijednost nosi i svoj redni broj, pa se premjesten koeficijent vidi odmah
        fields.push_back({"f_rest_" + std::to_string(i), base + 1000.0f + float(i)});
    }
    return fields;
}

//Dodatno svojstvo koje Spool ne cita. Nosi tip i bajtove, jer se preko njega mora preskociti
struct Extra{
    std::string name;
    std::string type;
    size_t size = 0;
};

void appendFloat(std::vector<uint8_t>& bytes, float value){
    uint8_t raw[4];
    std::memcpy(raw, &value, 4);
    bytes.insert(bytes.end(), raw, raw + 4);
}

//Glava i zapisi, tocno onim redom kojim su imena dana. Extra svojstva se ubacuju svakih
//`everyN` polja, da ne lece samo na kraju gdje ih je lako preskociti slucajno
std::string writePly(const std::filesystem::path& path,
                     const std::vector<std::vector<Field>>& gaussians,
                     const std::vector<size_t>& order,
                     const std::vector<Extra>& extras,
                     size_t everyN,
                     const std::string& format = "binary_little_endian"){
    std::string header = "ply\nformat " + format + " 1.0\n";
    header += "comment napisao test, rucno\n";
    header += "element vertex " + std::to_string(gaussians.size()) + "\n";

    size_t extraIndex = 0;
    for(size_t i = 0; i < order.size(); ++i){
        header += "property float " + gaussians[0][order[i]].name + "\n";
        if(everyN > 0 && (i + 1) % everyN == 0 && extraIndex < extras.size()){
            header += "property " + extras[extraIndex].type + " " + extras[extraIndex].name + "\n";
            ++extraIndex;
        }
    }
    //Jos jedan element iza vertexa: njegova svojstva ne smiju upasti u raspored zapisa
    header += "element camera 1\nproperty float view_px\n";
    header += "end_header\n";

    std::vector<uint8_t> bytes(header.begin(), header.end());

    for(const std::vector<Field>& gaussian : gaussians){
        extraIndex = 0;
        for(size_t i = 0; i < order.size(); ++i){
            appendFloat(bytes, gaussian[order[i]].value);
            if(everyN > 0 && (i + 1) % everyN == 0 && extraIndex < extras.size()){
                bytes.insert(bytes.end(), extras[extraIndex].size, uint8_t(0xAB));
                ++extraIndex;
            }
        }
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    return path.string();
}

//Redom kojim su polja i nabrojana
std::vector<size_t> straightOrder(size_t count){
    std::vector<size_t> order(count);
    for(size_t i = 0; i < count; ++i) order[i] = i;
    return order;
}

//Naopako, pa jos i izmijesano: nijedno polje ne ostaje na svom mjestu
std::vector<size_t> shuffledOrder(size_t count){
    std::vector<size_t> order = straightOrder(count);
    for(size_t i = 0; i + 1 < count; i += 2) std::swap(order[i], order[i + 1]);
    std::reverse(order.begin(), order.end());
    return order;
}

//Bit po bit, ne "priblizno". Float koji je prosao kroz citac smije biti samo isti float
bool sameFloat(float a, float b){
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

//Koliko se polja jednog gaussiana razlikuje od onoga sto je zapisano
size_t countDifferences(const Spool::GaussianCloud& cloud, size_t index,
                        const std::vector<Field>& written){
    const Spool::Gaussian& got = cloud.gaussians[index];
    size_t wrong = 0;

    auto valueOf = [&](const std::string& name) -> float {
        for(const Field& field : written) if(field.name == name) return field.value;
        return 0.0f;
    };
    const char* fixedNames[17] = {"x","y","z","nx","ny","nz","f_dc_0","f_dc_1","f_dc_2",
                                  "opacity","scale_0","scale_1","scale_2",
                                  "rot_0","rot_1","rot_2","rot_3"};
    const float fixedValues[17] = {
        got.position[0], got.position[1], got.position[2],
        got.normal[0],   got.normal[1],   got.normal[2],
        got.dc[0],       got.dc[1],       got.dc[2],
        got.opacity,
        got.scale[0],    got.scale[1],    got.scale[2],
        got.rotation[0], got.rotation[1], got.rotation[2], got.rotation[3],
    };
    for(int i = 0; i < 17; ++i){
        if(!sameFloat(fixedValues[i], valueOf(fixedNames[i]))) ++wrong;
    }

    for(uint32_t c = 0; c < cloud.restStride; ++c){
        if(!sameFloat(cloud.restFor(index)[c], valueOf("f_rest_" + std::to_string(c)))) ++wrong;
    }
    return wrong;
}

}

int main(){
    TestReport report("G0 gaussian ply");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_gaussian_ply";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    // -------------------------------------------------------------------------------
    // Sto je zapisano, to se i cita
    // -------------------------------------------------------------------------------

    const uint32_t restCount = 45;   //stupanj 3, ono sto pravi 3DGS file nosi
    const std::vector<std::vector<Field>> three = {
        makeFields(1, restCount), makeFields(2, restCount), makeFields(3, restCount)
    };
    const size_t fieldCount = three[0].size();

    const std::string plain = writePly(work / "plain.ply", three, straightOrder(fieldCount), {}, 0);
    const Spool::GaussianCloud cloud = Spool::loadGaussianPly(plain);

    size_t wrong = 0;
    for(size_t i = 0; i < three.size(); ++i) wrong += countDifferences(cloud, i, three[i]);

    report.check("sto je zapisano to se cita",
        cloud.count() == 3 && cloud.isValid() && wrong == 0,
        fmt("%zu gaussiana, %u koeficijenata po gaussianu, %zu polja odstupa",
            cloud.count(), cloud.restStride, wrong));

    // -------------------------------------------------------------------------------
    // Kontrola: usporedba gore mora gristi
    // -------------------------------------------------------------------------------

    //Jedan jedini bit, u jednom jedinom polju. Ako ovo prodje kao "isto", gornja provjera ne
    //mjeri nista
    std::vector<std::vector<Field>> nudged = three;
    for(Field& field : nudged[1]){
        if(field.name == "scale_1"){
            uint32_t bits;
            std::memcpy(&bits, &field.value, 4);
            bits += 1;
            std::memcpy(&field.value, &bits, 4);
        }
    }
    const std::string nudgedPath = writePly(work / "nudged.ply", nudged, straightOrder(fieldCount), {}, 0);
    const Spool::GaussianCloud nudgedCloud = Spool::loadGaussianPly(nudgedPath);

    size_t nudgedWrong = 0;
    for(size_t i = 0; i < three.size(); ++i) nudgedWrong += countDifferences(nudgedCloud, i, three[i]);

    report.check("usporedba grize", nudgedWrong == 1,
        fmt("jedan bit u scale_1 dao je %zu polja razlike (treba tocno 1)", nudgedWrong));

    // -------------------------------------------------------------------------------
    // Redoslijed svojstava nije pretpostavka
    // -------------------------------------------------------------------------------

    const std::string shuffled = writePly(work / "shuffled.ply", three, shuffledOrder(fieldCount), {}, 0);
    const Spool::GaussianCloud shuffledCloud = Spool::loadGaussianPly(shuffled);

    size_t shuffledWrong = 0;
    for(size_t i = 0; i < three.size(); ++i) shuffledWrong += countDifferences(shuffledCloud, i, three[i]);

    report.check("redoslijed nije pretpostavka",
        shuffledCloud.count() == 3 && shuffledWrong == 0,
        fmt("ista glava izmijesanim redom: %zu polja odstupa", shuffledWrong));

    // -------------------------------------------------------------------------------
    // Tudja svojstva se preskacu, po svojoj velicini
    // -------------------------------------------------------------------------------

    const std::vector<Extra> extras = {
        {"tudje_a", "uchar",  1},
        {"tudje_b", "double", 8},
        {"tudje_c", "ushort", 2},
        {"tudje_d", "int",    4},
    };
    const std::string withExtras = writePly(work / "extras.ply", three, straightOrder(fieldCount), extras, 7);
    const Spool::GaussianCloud extrasCloud = Spool::loadGaussianPly(withExtras);

    size_t extrasWrong = 0;
    for(size_t i = 0; i < three.size(); ++i) extrasWrong += countDifferences(extrasCloud, i, three[i]);

    report.check("tudja svojstva se preskacu",
        extrasCloud.count() == 3 && extrasWrong == 0,
        fmt("uchar, double, ushort i int izmedju nasih: %zu polja odstupa", extrasWrong));

    // -------------------------------------------------------------------------------
    // Stupanj sfernih harmonika
    // -------------------------------------------------------------------------------

    std::string degrees;
    bool degreesHold = true;
    const uint32_t counts[4] = {0, 9, 24, 45};
    for(uint32_t degree = 0; degree < 4; ++degree){
        const std::vector<std::vector<Field>> one = {makeFields(1, counts[degree])};
        const std::string path = writePly(work / ("degree" + std::to_string(degree) + ".ply"),
                                          one, straightOrder(one[0].size()), {}, 0);
        const Spool::GaussianCloud got = Spool::loadGaussianPly(path);
        degrees += fmt("%u->%u ", counts[degree], got.shDegree);
        if(got.shDegree != degree || got.restStride != counts[degree]) degreesHold = false;
        if(countDifferences(got, 0, one[0]) != 0) degreesHold = false;
    }

    report.check("stupanj se prepoznaje", degreesHold, fmt("f_rest svojstava -> stupanj: %s", degrees.c_str()));

    // -------------------------------------------------------------------------------
    // I sto mora puknuti
    // -------------------------------------------------------------------------------

    auto throwsOn = [&](const std::string& path){
        try{
            Spool::loadGaussianPly(path);
            return false;
        }
        catch(const std::runtime_error&){
            return true;
        }
    };

    //Broj f_rest svojstava koji ne odgovara nijednom stupnju
    const std::vector<std::vector<Field>> twelve = {makeFields(1, 12)};
    const std::string badDegree = writePly(work / "bad_degree.ply", twelve,
                                           straightOrder(twelve[0].size()), {}, 0);

    //Zapis koji citac ne zna
    const std::string ascii = writePly(work / "ascii.ply", three, straightOrder(fieldCount), {}, 0, "ascii");

    //Glava obecava tri gaussiana, a file nosi jedan i pol
    const std::string truncatedPath = (work / "truncated.ply").string();
    {
        std::ifstream in(plain, std::ios::binary);
        std::vector<char> all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream out(truncatedPath, std::ios::binary);
        out.write(all.data(), std::streamsize(all.size() - 200));
    }

    //Svojstvo koje mora postojati, a ne postoji
    std::vector<std::vector<Field>> missing = three;
    for(std::vector<Field>& gaussian : missing){
        gaussian.erase(gaussian.begin() + 11);   //scale_1
    }
    const std::string missingPath = writePly(work / "missing.ply", missing,
                                             straightOrder(missing[0].size()), {}, 0);

    const bool guards = throwsOn(badDegree) && throwsOn(ascii) && throwsOn(truncatedPath) &&
                        throwsOn(missingPath) && throwsOn((work / "nema.ply").string());

    report.check("krivi file baca", guards,
        fmt("12 koeficijenata %s, ascii %s, odrezan %s, bez scale_1 %s, nepostojeci %s",
            throwsOn(badDegree) ? "baca" : "NE BACA",
            throwsOn(ascii) ? "baca" : "NE BACA",
            throwsOn(truncatedPath) ? "baca" : "NE BACA",
            throwsOn(missingPath) ? "baca" : "NE BACA",
            throwsOn((work / "nema.ply").string()) ? "baca" : "NE BACA"));

    // -------------------------------------------------------------------------------
    // I da poruka kaze sto je
    // -------------------------------------------------------------------------------

    std::string message;
    try{ Spool::loadGaussianPly(missingPath); }
    catch(const std::runtime_error& error){ message = error.what(); }

    report.check("poruka imenuje krivca",
        message.find("scale_1") != std::string::npos && message.find("missing.ply") != std::string::npos,
        message.empty() ? "nije ni bacio" : message.c_str());

    std::filesystem::remove_all(work);
    return report.result();
}
