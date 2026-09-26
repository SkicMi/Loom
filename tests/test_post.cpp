// Post processing (Tracer/Post.h): svaki efekt protiv onoga sto se zna bez njega.
//
//   BLOOM cuva energiju: zbroj svjetla slike prije i poslije je isti (svjetlo se preraspodijeli,
//         ne stvara). Jedan vruci piksel se rasprsi monotono padajuce od sredista
//   ISKLJUCENO je identitet bit po bit - post koji "ne radi nista" ne smije pomaknuti ni bit
//   VINJETA: sredina ista, kut izgubi tocno zadani udio
//   ZRNO: srednja vrijednost ostaje (sum oko nule), isto sjeme = isto zrno, drugo sjeme = drugo
//   ZASICENJE 0: siva luminancije izvornika
//   BALANS BIJELE 6500 K je neutralan; 3200 K toplije (crveno > plavo), luminancija ista
//   ABERACIJA: sredina netaknuta, na rubu se crveni i plavi rub razmaknu
#include "TestHarness.h"

#include <Tracer/Post.h>

#include <cmath>
#include <numeric>

namespace{

std::vector<float> flat(uint32_t w, uint32_t h, float r, float g, float b){
    std::vector<float> v(size_t(w) * h * 4);
    for(size_t i = 0; i < size_t(w) * h; ++i){ v[i * 4] = r; v[i * 4 + 1] = g; v[i * 4 + 2] = b; v[i * 4 + 3] = 1.0f; }
    return v;
}

double total(const std::vector<float>& v, int channel){
    double sum = 0.0;
    for(size_t i = size_t(channel); i < v.size(); i += 4) sum += v[i];
    return sum;
}

}

int main(){
    TestReport report("P1 post processing");
    const uint32_t w = 160, h = 90;

    //-- iskljuceno = identitet ------------------------------------------------------------------
    {
        std::vector<float> image = flat(w, h, 0.2f, 0.5f, 0.9f);
        image[1234] = 37.0f;
        const std::vector<float> before = image;
        Tracer::PostSettings off;
        Tracer::applyPost(image, w, h, off);
        Tracer::PostSettings disabled;
        disabled.enabled = false;
        disabled.bloom = 0.5f;
        disabled.grain = 0.3f;
        Tracer::applyPost(image, w, h, disabled);
        report.check("iskljuceno", image == before, "zadane postavke i enabled=false ne mijenjaju nijedan bit");
    }

    //-- bloom: energija, rasprsenje ----------------------------------------------------------------
    {
        std::vector<float> image = flat(w, h, 0.05f, 0.05f, 0.05f);
        const size_t hot = (size_t(45) * w + 80) * 4;
        image[hot] = image[hot + 1] = image[hot + 2] = 500.0f;
        const double before = total(image, 1);
        Tracer::PostSettings s;
        s.bloom = 0.2f;
        s.bloomRadius = 0.1f;
        Tracer::applyPost(image, w, h, s);
        const double after = total(image, 1);
        auto at = [&](int x, int y){ return image[(size_t(y) * w + size_t(x)) * 4 + 1]; };
        const bool monotonic = at(80, 45) > at(82, 45) && at(82, 45) > at(86, 45) && at(86, 45) > at(95, 45) && at(95, 45) > at(120, 45);
        report.check("bloom: energija", std::abs(after - before) / before < 0.01, fmt("zbroj %.2f -> %.2f", before, after));
        report.check("bloom: rasprsenje", monotonic && at(95, 45) > 0.05f,
                     fmt("od vrucega piksela: %.1f %.3f %.3f %.4f %.4f", at(80, 45), at(82, 45), at(86, 45), at(95, 45), at(120, 45)));
        //Prag: ono ispod praga ne svijetli
        std::vector<float> dim = flat(w, h, 0.5f, 0.5f, 0.5f);
        const std::vector<float> dimBefore = dim;
        s.bloomThreshold = 1.0f;
        Tracer::applyPost(dim, w, h, s);
        float worst = 0.0f;
        for(size_t i = 0; i < dim.size(); ++i) worst = std::max(worst, std::abs(dim[i] - dimBefore[i]));
        report.check("bloom: prag", worst < 1e-6f, fmt("najveca promjena %.1e", double(worst)));
    }

    //-- vinjeta ------------------------------------------------------------------------------------
    {
        std::vector<float> image = flat(w, h, 1.0f, 1.0f, 1.0f);
        Tracer::PostSettings s;
        s.vignette = 0.4f;
        Tracer::applyPost(image, w, h, s);
        const float centre = image[(size_t(45) * w + 80) * 4], corner = image[1];
        //Kutni piksel je pola piksela od kuta: udio 0.4 * (r^2 malo manji od 1)
        report.check("vinjeta", std::abs(centre - 1.0f) < 1e-3f && std::abs(corner - 0.6f) < 0.01f,
                     fmt("sredina %.4f, kut %.4f (0.6)", centre, corner));
    }

    //-- zrno ---------------------------------------------------------------------------------------
    {
        std::vector<float> a = flat(w, h, 0.18f, 0.18f, 0.18f), b = a, c = a;
        Tracer::PostSettings s;
        s.grain = 0.05f;
        Tracer::applyPost(a, w, h, s);
        Tracer::applyPost(b, w, h, s);
        s.grainSeed = 1;
        Tracer::applyPost(c, w, h, s);
        const double mean = total(a, 1) / double(w * h);
        double var = 0.0;
        for(size_t i = 1; i < a.size(); i += 4) var += (a[i] - mean) * (a[i] - mean);
        const double sigma = std::sqrt(var / double(w * h)) / 0.18;
        report.check("zrno", std::abs(mean - 0.18) < 0.002 && std::abs(sigma - 0.05) < 0.005 && a == b && a != c,
                     fmt("srednja %.4f (0.18), relativna sigma %.4f (0.05), isto sjeme isto, drugo drugo", mean, sigma));
    }

    //-- boja: zasicenje, balans bijele ---------------------------------------------------------------
    {
        std::vector<float> image = flat(4, 4, 0.8f, 0.3f, 0.1f);
        const float l = 0.2126f * 0.8f + 0.7152f * 0.3f + 0.0722f * 0.1f;
        Tracer::PostSettings s;
        s.saturation = 0.0f;
        Tracer::applyPost(image, 4, 4, s);
        report.check("zasicenje 0", std::abs(image[0] - l) < 1e-5f && std::abs(image[1] - l) < 1e-5f && std::abs(image[2] - l) < 1e-5f,
                     fmt("%.4f %.4f %.4f (%.4f)", image[0], image[1], image[2], l));

        std::vector<float> grey = flat(4, 4, 0.5f, 0.5f, 0.5f);
        Tracer::PostSettings warm;
        warm.temperature = 3200.0f;
        Tracer::applyPost(grey, 4, 4, warm);
        const float lum = 0.2126f * grey[0] + 0.7152f * grey[1] + 0.0722f * grey[2];
        report.check("balans bijele", grey[0] > grey[1] && grey[1] > grey[2] && std::abs(lum - 0.5f) < 1e-4f,
                     fmt("3200 K: %.3f %.3f %.3f, luminancija %.4f", grey[0], grey[1], grey[2], lum));
    }

    //-- kromatska aberacija ---------------------------------------------------------------------------
    {
        //Bijeli okvir na crnom: na desnom rubu crveni se pomakne van, plavi unutra
        std::vector<float> image = flat(w, h, 0.0f, 0.0f, 0.0f);
        for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 130; x < 140; ++x)
            for(int k = 0; k < 3; ++k) image[(size_t(y) * w + x) * 4 + size_t(k)] = 1.0f;
        std::vector<float> centreProbe = image;
        Tracer::PostSettings s;
        s.chromaticAberration = 3.0f;
        Tracer::applyPost(image, w, h, s);
        const size_t outer = (size_t(45) * w + 140) * 4, inner = (size_t(45) * w + 129) * 4, middle = (size_t(45) * w + 80) * 4;
        report.check("aberacija", image[outer] > image[outer + 2] && image[inner + 2] > image[inner] && image[middle] == centreProbe[middle],
                     fmt("vanjski rub R %.2f B %.2f, unutarnji R %.2f B %.2f", image[outer], image[outer + 2], image[inner], image[inner + 2]));
    }

    return report.result();
}
