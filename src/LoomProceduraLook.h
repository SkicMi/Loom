#pragma once

// Colour of each Procedura library material, shared by the viewport preview (LoomPbr.h) and
// .glb export (LoomProceduraGlb.h), so what is exported looks like what the viewport showed.

#include <Engine/WeaverProcedura.h>

#include <glm/glm.hpp>

#include <map>
#include <string>

namespace Loom{

//Boja preview-a (sRGB) za materijal iz Procedura knjiznice (materialLibrary). Dok knjiznica nema
//teksture, materijal se u pogledu razlikuje samo bojom; 0 (bez materijala) ostaje plava preview boja
inline glm::vec3 proceduralMaterialColour(uint16_t material){
    static const std::map<std::string, glm::vec3> looks = {
        {"plaster", {0.82f, 0.79f, 0.72f}}, {"brick", {0.55f, 0.24f, 0.17f}}, {"stone", {0.52f, 0.50f, 0.46f}},
        {"concrete", {0.60f, 0.60f, 0.58f}}, {"wood_planks", {0.55f, 0.38f, 0.22f}}, {"wood_beam", {0.40f, 0.26f, 0.14f}},
        {"roof_tiles", {0.48f, 0.20f, 0.14f}}, {"roof_metal", {0.36f, 0.40f, 0.43f}}, {"glass", {0.55f, 0.72f, 0.80f}},
        {"metal", {0.62f, 0.63f, 0.65f}}, {"steel_chain", {0.48f, 0.49f, 0.51f}}, {"asphalt", {0.16f, 0.16f, 0.17f}},
        {"paving", {0.66f, 0.63f, 0.57f}}, {"rope_fiber", {0.66f, 0.55f, 0.36f}}, {"ground_dirt", {0.38f, 0.29f, 0.20f}},
        {"grass", {0.27f, 0.45f, 0.20f}}, {"fabric", {0.34f, 0.45f, 0.60f}}, {"ceramic", {0.93f, 0.93f, 0.91f}},
        {"lacquer", {0.90f, 0.90f, 0.88f}}, {"leather", {0.40f, 0.24f, 0.14f}}, {"linen", {0.74f, 0.71f, 0.64f}},
    };
    const auto found = looks.find(Engine::WeaverProcedura::materialName(material));
    if(found == looks.end()) return glm::vec3(0.18f, 0.58f, 0.82f);
    return glm::pow(found->second, glm::vec3(2.2f));   //tablica je u sRGB-u, faktor boje materijala je linearan
}

} // namespace Loom
