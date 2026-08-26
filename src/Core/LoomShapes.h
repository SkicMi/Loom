#pragma once
#include "LoomInitializer.h"
#include "../Vulkan/Mesh.h"
#include "../Vulkan/Material.h"
#include "../Vulkan/Texture.h"
#include "../Vulkan/Vertex.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

//The four shapes every scene starts with, and a way to draw one in a single line.
//
//Everything here is a convenience over what Loom already does - a Mesh, a Material and a
//draw call - and nothing here is a new capability. It exists because writing out
//twenty four vertices by hand to see whether a light points the right way is not learning
//anything, it is typing.
//
//All four are one unit across and centred on the origin, so a model matrix scales them into
//whatever is wanted and there is never a question of what "size 1" meant. Winding is
//counter clockwise seen from outside, which is what PipelineConfig culls by default.
namespace LoomShapes{

//The raw geometry, for anyone who wants their own Mesh or their own vertex layout. Plane
//lies in XZ facing up, so it is a floor; the other three are solid and centred
std::vector<Vertex> planeVertices();
std::vector<uint16_t> planeIndices();

std::vector<Vertex> cubeVertices();
std::vector<uint16_t> cubeIndices();

std::vector<Vertex> pyramidVertices();
std::vector<uint16_t> pyramidIndices();

//A UV sphere of diameter 1. segments goes around, rings goes pole to pole
std::vector<Vertex> sphereVertices(uint32_t segments = 32, uint32_t rings = 16);
std::vector<uint16_t> sphereIndices(uint32_t segments = 32, uint32_t rings = 16);


struct PrimitivesConfig{
    uint32_t sphereSegments = 32;
    uint32_t sphereRings = 16;

    //Used for the pipeline the texture overloads draw with. The material overloads bring
    //their own pipeline inside the Material, so these do not touch them
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    bool depthTest = true;

    //Uz depth prepass glavni prolaz vise ne treba pisati dubinu - ona je vec tocna - i smije
    //testirati eEqual, cime se svaki piksel sjenca tocno jednom bez obzira na preklapanje
    bool depthWrite = true;
    vk::CompareOp depthCompare = vk::CompareOp::eLess;
};


class Primitives{
    public:
    //Meshes are built the first time a shape is asked for, not here: a program that only
    //ever draws a cube does not pay for a sphere's five hundred vertices
    explicit Primitives(LoomInitializer& loom, const PrimitivesConfig& config = {});

    Primitives(const Primitives&) = delete;
    Primitives& operator=(const Primitives&) = delete;

    //Draw with a material that already exists. This is the plain path: the material carries
    //its own pipeline, so it decides what the shape looks like
    void plane  (const Material& material, const glm::mat4& model = glm::mat4(1.0f));
    void cube   (const Material& material, const glm::mat4& model = glm::mat4(1.0f));
    void pyramid(const Material& material, const glm::mat4& model = glm::mat4(1.0f));
    void sphere (const Material& material, const glm::mat4& model = glm::mat4(1.0f));

    //Draw with nothing but a texture. A material is built the first time each texture is
    //seen and kept, so calling this every frame does not allocate a descriptor set every
    //frame. The texture has to outlive the Primitives that cached it
    void plane  (const Texture& texture, const glm::mat4& model = glm::mat4(1.0f));
    void cube   (const Texture& texture, const glm::mat4& model = glm::mat4(1.0f));
    void pyramid(const Texture& texture, const glm::mat4& model = glm::mat4(1.0f));
    void sphere (const Texture& texture, const glm::mat4& model = glm::mat4(1.0f));

    //Draw with the renderer's own pipeline and no material at all. The vertices are white,
    //so what shows up is the lighting and nothing else
    void plane  (const glm::mat4& model = glm::mat4(1.0f));
    void cube   (const glm::mat4& model = glm::mat4(1.0f));
    void pyramid(const glm::mat4& model = glm::mat4(1.0f));
    void sphere (const glm::mat4& model = glm::mat4(1.0f));

    //The meshes themselves, for whatever the calls above do not cover - an instanced draw,
    //a different pipeline, a shadow pass
    const Mesh& planeMesh();
    const Mesh& cubeMesh();
    const Mesh& pyramidMesh();
    const Mesh& sphereMesh();

    //The pipeline the texture overloads use. Built on first use, from the textured shaders
    //Loom ships
    const VulkanGraphicsPipeline& getTexturedPipeline();

    //How many materials the texture overloads have cached so far
    size_t cachedMaterialCount() const {return texturedMaterials.size();}

    private:
    LoomInitializer& loom;
    PrimitivesConfig config;

    std::optional<Mesh> plane_;
    std::optional<Mesh> cube_;
    std::optional<Mesh> pyramid_;
    std::optional<Mesh> sphere_;

    std::optional<VulkanGraphicsPipeline> texturedPipeline;

    //Keyed by the texture's image view, which is the one handle that identifies it. A map
    //rather than a vector because a Material must not move once something points at it
    std::map<VkImageView, Material> texturedMaterials;

    const Material& materialFor(const Texture& texture);
};

}
