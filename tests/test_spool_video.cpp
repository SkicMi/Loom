// 0a: Spool cita video.  0b: i pise ga natrag kao niz slika.
//
// Prvi korak prema tome da svjetlo koje smo dokazali u koraku 1 dobije pravu snimku umjesto
// Loomove vlastite dubine. Dekoder zivi u procesu - nema temp fileova i nema medukoraka - a
// zaglavlje koje ga objavljuje ne spominje nijedan tip biblioteke koja to radi.
//
// Istina se pravi ovdje: Spool sam napise frameove kao PNG, ffmpeg ih spoji BEZ GUBITKA, i
// onda ih Spool procita natrag. Ako se piksel vrati bit za bit, put je zatvoren s obje strane
// i nijedna od te dvije polovice ne moze tiho lagati.
//
// Isti krug zatvara i izvoz: PNG -> video -> PNG. To je put kojim ce snimka doci do modela za
// procjenu dubine, jer oni rade nad slikama a ne nad kontejnerima.
#include "TestHarness.h"

#include <Spool/ImageFile.h>
#include <Spool/Sequence.h>
#include <Spool/VideoFile.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace{

const uint32_t frameWidth = 96;
const uint32_t frameHeight = 64;
const uint32_t frameTotal = 12;

//Svaki frame jedna puna boja, i to boje koje se ne daju zamijeniti jedna za drugu. Puna boja
//zato sto tada nikakvo poduzorkovanje krominancije ne moze biti izgovor: ako se boja vrati
//kriva, kriv je put a ne kodek
Spool::Image solidFrame(uint32_t index){
    const uint8_t r = uint8_t(20 + index * 19);
    const uint8_t g = uint8_t(index % 2 ? 200 : 40);
    const uint8_t b = uint8_t(240 - index * 17);

    std::vector<uint8_t> pixels(size_t(frameWidth) * frameHeight * 4);
    for(size_t i = 0; i + 3 < pixels.size(); i += 4){
        pixels[i+0] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = 255;
    }
    return Spool::imageFromPixels(pixels.data(), frameWidth, frameHeight);
}

bool haveFfmpeg(){
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
}

int run(const std::string& command){
    return std::system((command + " > /dev/null 2>&1").c_str());
}

//Najveca razlika po kanalu izmedu dvije slike
int worstChannelDelta(const Spool::Image& a, const Spool::Image& b){
    if(a.width != b.width || a.height != b.height) return 255;
    int worst = 0;
    for(size_t i = 0; i < a.pixels.size(); ++i){
        worst = std::max(worst, std::abs(int(a.pixels[i]) - int(b.pixels[i])));
    }
    return worst;
}

}

int main(){
    TestReport report("0a+0b spool video");

    if(!haveFfmpeg()){
        report.check("ffmpeg", false,
            "nije na putanji - testni materijal se ne moze napraviti, pa se nema sto citati");
        return report.result();
    }

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_spool_video";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    // -------------------------------------------------------------------------------
    // Materijal: Spool ga napise, ffmpeg ga spoji bez gubitka
    // -------------------------------------------------------------------------------

    std::vector<Spool::Image> source;

    Spool::SequenceConfig sequenceConfig;
    sequenceConfig.directory = (work / "src").string();
    Spool::SequenceWriter writer(sequenceConfig);

    for(uint32_t i = 0; i < frameTotal; ++i){
        source.push_back(solidFrame(i));
        writer.write(source.back());
    }

    report.check("izvor je napisan", writer.frameCount() == frameTotal,
        fmt("%u frameova %ux%u", writer.frameCount(), frameWidth, frameHeight));

    //ffv1 u gbrp: bez gubitka i BEZ pretvorbe u YUV, pa je piksel koji izade tocno onaj koji
    //je usao. Svaka razlika poslije ovoga je nasa
    const std::string lossless = (work / "lossless.mkv").string();
    const int encoded = run("ffmpeg -y -framerate 24 -i " + (work / "src" / "frame_%04d.png").string() +
                            " -c:v ffv1 -pix_fmt gbrp " + lossless);

    if(encoded != 0){
        report.check("ffmpeg je spojio snimku", false, "kodiranje nije uspjelo");
        return report.result();
    }

    // -------------------------------------------------------------------------------
    // Sto kontejner kaze
    // -------------------------------------------------------------------------------

    Spool::VideoReader reader(lossless);
    const Spool::VideoInfo& info = reader.info();

    report.check("dimenzije", info.width == frameWidth && info.height == frameHeight,
        fmt("%ux%u", info.width, info.height));

    report.check("broj slika u sekundi", info.frameRateNumerator == 24 && info.frameRateDenominator == 1,
        fmt("%u/%u = %.3f", info.frameRateNumerator, info.frameRateDenominator, info.frameRate()));

    report.check("broj frameova", info.frameCount == int64_t(frameTotal),
        fmt("%lld %s", (long long)info.frameCount, info.frameCountIsExact ? "(tocno iz kontejnera)" : "(procijenjeno iz trajanja)"));

    report.check("kodek i format piksela", !info.codec.empty() && !info.pixelFormat.empty(),
        fmt("%s, %s", info.codec.c_str(), info.pixelFormat.c_str()));

    report.check("nema okretanja", info.rotation == 0,
        fmt("rotacija %d stupnjeva", info.rotation));

    // -------------------------------------------------------------------------------
    // Citanje naprijed: piksel za piksel
    // -------------------------------------------------------------------------------

    std::vector<Spool::Image> decoded;
    while(!reader.atEnd()){
        Spool::Image frame = reader.readNext();
        if(!frame.isValid()) break;
        decoded.push_back(std::move(frame));
    }

    report.check("procitano je koliko je i napisano", decoded.size() == frameTotal,
        fmt("%zu od %u", decoded.size(), frameTotal));

    int worst = 0;
    size_t mismatched = 0;
    for(size_t i = 0; i < decoded.size() && i < source.size(); ++i){
        const int delta = worstChannelDelta(source[i], decoded[i]);
        worst = std::max(worst, delta);
        if(delta != 0) ++mismatched;
    }

    //Bez gubitka znaci bez gubitka. Tolerancija bi ovdje sakrila tocno onu vrstu greske koju
    //ovaj test postoji da uhvati - zamijenjene kanale, pomak za red, krivi raspon
    report.check("frameovi su bit za bit isti", mismatched == 0 && worst == 0,
        fmt("%zu od %zu frameova odstupa, najveca razlika po kanalu %d",
            mismatched, decoded.size(), worst));

    // -------------------------------------------------------------------------------
    // Nasumican pristup
    // -------------------------------------------------------------------------------

    //Redoslijed je namjerno ispreskakan: unatrag, naprijed, dva puta isti. Trazenje sleti na
    //kljucni frame ISPRED trazenog, pa je jedini nacin da se dokaze da je stigao na pravi
    //taj da se usporedi s onim sto je citanje naprijed vec dalo
    const std::vector<int64_t> order = {7, 0, 11, 3, 3, 10, 1};
    size_t wrongFrame = 0;

    for(int64_t index : order){
        const Spool::Image frame = reader.readFrame(index);
        if(!frame.isValid() || worstChannelDelta(frame, source[size_t(index)]) != 0) ++wrongFrame;
    }

    report.check("frame po broju je taj frame", wrongFrame == 0,
        fmt("%zu od %zu trazenih frameova nije bio onaj trazeni", wrongFrame, order.size()));

    //I da nije slucajno svaki frame isti - tada bi gornja provjera prosla i nad dekoderom
    //koji uvijek vraca isto
    report.check("frameovi se medusobno razlikuju",
        worstChannelDelta(source[0], source[frameTotal - 1]) > 40,
        fmt("prvi i zadnji se razlikuju za %d", worstChannelDelta(source[0], source[frameTotal - 1])));

    // -------------------------------------------------------------------------------
    // Metapodaci
    // -------------------------------------------------------------------------------

    //Kljuc koji smo sami upisali. Ako se vrati, vraca se i sve ostalo sto kontejner nosi -
    //Appleovi com.apple.quicktime.* i Sonyjevi vlasnicki kljucevi na istom su mjestu
    const std::string tagged = (work / "tagged.mp4").string();
    const int taggedOk = run("ffmpeg -y -framerate 24 -i " + (work / "src" / "frame_%04d.png").string() +
                             " -c:v libx264 -crf 0 -pix_fmt yuv444p"
                             " -metadata comment=LoomTestKljuc -metadata artist=Spool " + tagged);

    if(taggedOk == 0){
        Spool::VideoReader withTags(tagged);

        report.check("metapodaci se citaju",
            withTags.info().find("comment") == "LoomTestKljuc" &&
            withTags.info().find("COMMENT") == "LoomTestKljuc",
            fmt("comment='%s', artist='%s', ukupno %zu kljuceva; trazenje ne razlikuje velika i mala slova",
                withTags.info().find("comment").c_str(),
                withTags.info().find("artist").c_str(),
                withTags.info().metadata.size()));

        report.check("kljuc kojeg nema vraca prazno",
            withTags.info().find("focal_length").empty(),
            "nepostojeci kljuc je prazan string, ne izmisljena vrijednost");
    }
    else{
        report.check("metapodaci", false, "H.264 kodiranje nije uspjelo, pa se nema odakle citati");
    }

    // -------------------------------------------------------------------------------
    // 0b: snimka natrag na disk
    // -------------------------------------------------------------------------------

    //Ovo je put kojim snimka dolazi do modela za procjenu dubine - oni rade nad slikama, ne
    //nad kontejnerima. Krug se zatvara: PNG -> video -> PNG, sve bez gubitka, pa se smije
    //traziti da izade tocno ono sto je uslo
    {
        Spool::TranscodeConfig transcode;
        transcode.sequence.directory = (work / "out").string();
        transcode.sequence.prefix = "shot_";

        std::vector<uint32_t> progress;
        transcode.onFrame = [&](uint32_t written, int64_t expected){
            progress.push_back(written);
            (void)expected;
            return true;
        };

        const Spool::TranscodeResult out = Spool::videoToSequence(lossless, transcode);

        report.check("izvezeno je koliko je i bilo",
            out.framesWritten == frameTotal && !out.cancelled,
            fmt("%u frameova, prvi '%s', zadnji '%s'", out.framesWritten,
                std::filesystem::path(out.firstPath).filename().string().c_str(),
                std::filesystem::path(out.lastPath).filename().string().c_str()));

        report.check("izvjestaj o snimci dolazi s njim",
            out.info.width == frameWidth && out.info.height == frameHeight,
            fmt("%ux%u pri %.3f slika u sekundi - pozivatelj je ne mora otvarati drugi put",
                out.info.width, out.info.height, out.info.frameRate()));

        report.check("napredak se javlja svaki frame",
            progress.size() == frameTotal && progress.front() == 1 && progress.back() == frameTotal,
            fmt("%zu javljanja, od %u do %u", progress.size(),
                progress.empty() ? 0 : progress.front(), progress.empty() ? 0 : progress.back()));

        //I da je na disku doslovno ono sto je u snimku uslo
        size_t different = 0;
        int worstOut = 0;
        for(uint32_t i = 0; i < frameTotal; ++i){
            const Spool::Image fromDisk = Spool::loadImage(
                (work / "out" / ("shot_" + fmt("%04u", i))).string() + ".png");
            const int delta = worstChannelDelta(source[i], fromDisk);
            worstOut = std::max(worstOut, delta);
            if(delta != 0) ++different;
        }

        report.check("krug PNG -> video -> PNG je zatvoren",
            different == 0 && worstOut == 0,
            fmt("%zu od %u frameova odstupa od izvornika, najveca razlika po kanalu %d",
                different, frameTotal, worstOut));
    }

    // -------------------------------------------------------------------------------
    // Raspon i prekid
    // -------------------------------------------------------------------------------

    {
        Spool::TranscodeConfig slice;
        slice.sequence.directory = (work / "slice").string();
        slice.firstFrame = 4;
        slice.frameCount = 3;

        const Spool::TranscodeResult out = Spool::videoToSequence(lossless, slice);

        //Brojanje datoteka je neovisno o broju framea u snimci: peti frame snimke je i dalje
        //frame_0000 na disku, jer se sekvenca najcesce gleda kao cjelina za sebe
        size_t wrong = 0;
        for(uint32_t i = 0; i < 3; ++i){
            const Spool::Image fromDisk = Spool::loadImage(
                (work / "slice" / ("frame_" + fmt("%04u", i))).string() + ".png");
            if(worstChannelDelta(source[4 + i], fromDisk) != 0) ++wrong;
        }

        report.check("izvozi se trazeni raspon",
            out.framesWritten == 3 && wrong == 0,
            fmt("%u frameova od cetvrtog nadalje, %zu ih nije taj frame", out.framesWritten, wrong));
    }

    {
        Spool::TranscodeConfig stopped;
        stopped.sequence.directory = (work / "stopped").string();
        stopped.onFrame = [](uint32_t written, int64_t){ return written < 5; };

        const Spool::TranscodeResult out = Spool::videoToSequence(lossless, stopped);

        //Dugotrajan posao koji se ne da zaustaviti je posao koji se pokrene jednom pa nikad
        //vise. Prekid nije neuspjeh - zapisano ostaje zapisano - ali se mora razlikovati od
        //dovrsenog, inace se ne zna je li sekvenca cijela
        report.check("prekid staje i kaze da je stao",
            out.framesWritten == 5 && out.cancelled,
            fmt("zapisano %u od %u, prekinuto: %s", out.framesWritten, frameTotal,
                out.cancelled ? "da" : "ne"));
    }

    // -------------------------------------------------------------------------------
    // Okretanje, protiv ffmpegovog vlastitog
    // -------------------------------------------------------------------------------

    //Ovdje se ne moze koristiti nas materijal od punih boja: puna boja okrenuta izgleda
    //isto, pa bi test prosao i da se ne okrene nista. Treba slika koja ima gore i dolje
    const std::string structured = (work / "structured.mkv").string();
    const int madeStructured = run("ffmpeg -y -f lavfi -i testsrc=size=96x64:rate=24:duration=0.3"
                                   " -c:v ffv1 -pix_fmt gbrp " + structured);

    //I ne moze se provjeravati protiv nase vlastite predodzbe o tome kamo koji piksel ide -
    //to bi bila provjera mene protiv mene. Istina je ono sto ffmpeg sam ispise kad snimku
    //okrene: ista datoteka, isti dekoder, jedina razlika je tko primjenjuje matricu prikaza
    for(int wanted : {90, 180, 270}){
        if(madeStructured != 0) break;

        const std::string name = std::to_string(wanted);
        const std::string turned = (work / ("turned" + name + ".mkv")).string();
        const std::string expected = (work / ("expected" + name + ".png")).string();

        if(run("ffmpeg -y -display_rotation " + name + " -i " + structured + " -c copy " + turned) != 0 ||
           run("ffmpeg -y -i " + turned + " -frames:v 1 " + expected) != 0){
            report.check(("okretanje za " + name).c_str(), false, "materijal nije napravljen");
            continue;
        }

        Spool::VideoReader rotated(turned);
        const Spool::VideoInfo& turnedInfo = rotated.info();

        if(turnedInfo.rotation == 0){
            report.check(("okretanje za " + name).c_str(), false,
                "ovaj ffmpeg nije upisao matricu prikaza, pa put nije provjeren");
            continue;
        }

        const Spool::Image ours = rotated.readNext();
        const Spool::Image theirs = Spool::loadImage(expected);

        report.check(("okretanje za " + name + " je ffmpegovo okretanje").c_str(),
            ours.width == theirs.width && ours.height == theirs.height &&
            worstChannelDelta(ours, theirs) == 0,
            fmt("prijavljeno %d stupnjeva; nase %ux%u, ffmpegovo %ux%u, najveca razlika po kanalu %d",
                turnedInfo.rotation, ours.width, ours.height, theirs.width, theirs.height,
                worstChannelDelta(ours, theirs)));
    }

    //Kontrola: da okretanje nije primijenjeno, gornje bi provjere prosle nad slikom koja se
    //nije ni promijenila. Neokrenuta ista snimka mora se RAZLIKOVATI od okrenute
    if(madeStructured == 0){
        Spool::VideoReader upright(structured);
        const Spool::Image plain = upright.readNext();

        const std::string turned90 = (work / "turned90.mkv").string();
        if(std::filesystem::exists(turned90)){
            Spool::VideoReader sideways(turned90);
            const Spool::Image turnedFrame = sideways.readNext();

            report.check("okrenuto se stvarno razlikuje od neokrenutog",
                plain.width != turnedFrame.width || plain.height != turnedFrame.height,
                fmt("uspravno %ux%u, okrenuto %ux%u",
                    plain.width, plain.height, turnedFrame.width, turnedFrame.height));
        }
    }

    std::filesystem::remove_all(work);
    return report.result();
}
