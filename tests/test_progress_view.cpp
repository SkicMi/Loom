// Zivi prikaz onoga sto solver nalazi: zapis, citanje i projekcija.
//
// ZASTO SE OVO TESTIRA. Prozor se ne da snimiti na ovom sustavu, pa "vidjet ce se kad se otvori"
// nije provjera nego nada. Dvije stvari se ipak daju izmjeriti bez slike:
//
//   1. ZAPIS I CITANJE. Binarni raspored tise pukne od svega - promijeni netko redoslijed polja i
//      scena postane smece, a nijedan test ne padne.
//   2. PROJEKCIJA. Ako je mjerilo promaseno, sve tocke padnu u jedan piksel ili izvan kadra.
//      Oboje se broji, pa se oboje da obraniti.
//
// NEGATIVNA KONTROLA je ovdje bas ona greska koju smo vec jednom napravili: oblak s jednom tockom
// odbjeglom na 1e5. Ako se srediste racuna po min/max umjesto po postotcima, scena se skupi u
// tocku - i test to mora primijetiti.
#include "TestHarness.h"

#include "../src/LoomProgress.h"

#include <cstdio>
#include <filesystem>

namespace{

//Isti zapis kakav pise VideoSolve
void writeSnapshot(const std::string& path, const std::vector<glm::vec3>& cameras,
                   const std::vector<glm::vec3>& points){
    std::ofstream file(path, std::ios::binary);
    const uint32_t cameraCount = uint32_t(cameras.size());
    const uint32_t pointCount = uint32_t(points.size());
    file.write("LOOMPRG1", 8);
    file.write(reinterpret_cast<const char*>(&cameraCount), 4);
    file.write(reinterpret_cast<const char*>(&pointCount), 4);
    if(cameraCount) file.write(reinterpret_cast<const char*>(cameras.data()), cameraCount * 12);
    if(pointCount) file.write(reinterpret_cast<const char*>(points.data()), pointCount * 12);
}

}

int main(){
    TestReport report("S9 zivi prikaz napretka");

    //Scena kakva stvarno izlazi iz solvera: tocke razasute po kvadru, kamere na luku oko njih
    std::vector<glm::vec3> points;
    for(int i = 0; i < 2000; ++i){
        const float a = float(i) * 0.7919f;
        points.push_back(glm::vec3(std::sin(a) * 2.0f, std::cos(a * 1.7f) * 1.2f,
                                   std::sin(a * 0.3f) * 2.0f - 6.0f));
    }
    std::vector<glm::vec3> cameras;
    for(int i = 0; i < 20; ++i){
        const float a = float(i) * 0.08f;
        cameras.push_back(glm::vec3(std::sin(a) * 5.0f, 0.5f, std::cos(a) * 5.0f - 6.0f));
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom_napredak_test.bin";
    writeSnapshot(path.string(), cameras, points);

    //-- zapis i citanje se moraju poklopiti do zadnjeg bita ----------------------------------
    Loom::Snapshot read;
    const bool ok = Loom::readSnapshot(path.string(), read);

    bool same = ok && read.cameras.size() == cameras.size() && read.points.size() == points.size();
    if(same){
        for(size_t i = 0; i < points.size(); ++i) if(read.points[i] != points[i]) same = false;
        for(size_t i = 0; i < cameras.size(); ++i) if(read.cameras[i] != cameras[i]) same = false;
    }
    report.check("zapis i citanje daju isto", same,
        fmt("%zu kamera, %zu tocaka", read.cameras.size(), read.points.size()));

    //-- lose datoteke se odbijaju, ne tumace ------------------------------------------------
    {
        const std::filesystem::path bad = std::filesystem::temp_directory_path() / "loom_napredak_lose.bin";
        std::ofstream(bad, std::ios::binary) << "NIJETO12" << "smece";
        Loom::Snapshot ignored;
        report.check("kriv potpis se odbija", !Loom::readSnapshot(bad.string(), ignored), "NIJETO12");
        std::filesystem::remove(bad);

        Loom::Snapshot missing;
        report.check("nepostojeca datoteka se odbija",
            !Loom::readSnapshot("/nema/takve/datoteke.bin", missing), "bez pada");
    }

    //-- projekcija: tocke moraju pasti U KADAR i RAZASUTO ------------------------------------
    {
        Treadle::DrawList list;
        const uint32_t drawn = Loom::paintSnapshot(read, list, 1180.0f, 760.0f, 0.4f);

        report.check("vecina tocaka padne u kadar", drawn > points.size() / 2,
            fmt("%u od %zu", drawn, points.size()));

        //Raspon po ekranu: ako je mjerilo promaseno, sve je u jednoj tocki
        float lowX = 1e9f, highX = -1e9f, lowY = 1e9f, highY = -1e9f;
        for(const Treadle::Vertex& vertex : list.vertices){
            lowX = std::min(lowX, vertex.x); highX = std::max(highX, vertex.x);
            lowY = std::min(lowY, vertex.y); highY = std::max(highY, vertex.y);
        }
        const float spreadX = highX - lowX, spreadY = highY - lowY;
        report.check("scena zauzima kadar, nije tocka",
            spreadX > 200.0f && spreadY > 150.0f && spreadX < 2400.0f,
            fmt("%.0f x %.0f px na prozoru 1180 x 760", spreadX, spreadY));
    }

    //-- NEGATIVNA KONTROLA: jedna odbjegla tocka ne smije skupiti scenu ----------------------
    //
    //Ovo je greska koju smo vec jednom napravili na pravim podacima - min/max je ondje bio pet
    //redova velicine siri od scene
    {
        Loom::Snapshot strayed = read;
        strayed.points.push_back(glm::vec3(1.0e5f, -4.0e5f, 9.0e5f));

        Treadle::DrawList list;
        const uint32_t drawn = Loom::paintSnapshot(strayed, list, 1180.0f, 760.0f, 0.4f);

        float lowX = 1e9f, highX = -1e9f;
        for(const Treadle::Vertex& vertex : list.vertices){
            lowX = std::min(lowX, vertex.x); highX = std::max(highX, vertex.x);
        }
        report.check("odbjegla tocka ne skuplja scenu",
            drawn > points.size() / 2 && (highX - lowX) > 200.0f,
            fmt("%u nacrtano, raspon %.0f px", drawn, highX - lowX));
    }

    //-- premalo tocaka se ne crta, ne pada --------------------------------------------------
    {
        Loom::Snapshot tiny;
        tiny.points.assign(3, glm::vec3(0.0f, 0.0f, -5.0f));
        Treadle::DrawList list;
        report.check("premalo tocaka se preskace",
            Loom::paintSnapshot(tiny, list, 1180.0f, 760.0f, 0.0f) == 0 && list.vertices.empty(),
            "tri tocke");
    }

    std::filesystem::remove(path);
    return report.result();
}
