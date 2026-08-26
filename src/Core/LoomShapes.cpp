#include "LoomShapes.h"
#include <cmath>
#include <stdexcept>

namespace LoomShapes{

namespace{

const glm::vec3 white{1.0f, 1.0f, 1.0f};

//Four corners of one flat face, in the order that winds counter clockwise seen from
//outside, with the face's own normal on every one of them. Every flat shape here is built
//out of these: a shared corner would have to average two normals and the edge would go soft
void addQuad(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices,
             const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
             const glm::vec3& normal){
    const uint16_t base = static_cast<uint16_t>(vertices.size());

    vertices.push_back({a, white, {0.0f,1.0f}, normal});
    vertices.push_back({b, white, {1.0f,1.0f}, normal});
    vertices.push_back({c, white, {1.0f,0.0f}, normal});
    vertices.push_back({d, white, {0.0f,0.0f}, normal});

    indices.insert(indices.end(), {
        base, uint16_t(base+1), uint16_t(base+2),
        uint16_t(base+2), uint16_t(base+3), base});
}

//The same for a triangle, which is what a pyramid's sides are
void addTriangle(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices,
                 const glm::vec3& a, const glm::vec3& b, const glm::vec3& apex,
                 const glm::vec3& normal){
    const uint16_t base = static_cast<uint16_t>(vertices.size());

    vertices.push_back({a, white, {0.0f,1.0f}, normal});
    vertices.push_back({b, white, {1.0f,1.0f}, normal});
    vertices.push_back({apex, white, {0.5f,0.0f}, normal});

    indices.insert(indices.end(), {base, uint16_t(base+1), uint16_t(base+2)});
}

//The outward normal of a triangle, from the winding itself. Used for the pyramid's slanted
//sides, where writing the normal out by hand is four chances to get a sign wrong
glm::vec3 faceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c){
    return glm::normalize(glm::cross(b - a, c - a));
}

void checkSphere(uint32_t segments, uint32_t rings){
    if(segments < 3 || rings < 2){
        throw std::runtime_error("LoomShapes: a sphere needs at least 3 segments and 2 rings");
    }
    //Mesh indexes with uint16_t, so the vertex count has a ceiling and it is better said
    //here than discovered as a wrapped index and a shape full of holes
    const uint64_t vertexCount = uint64_t(segments + 1) * uint64_t(rings + 1);
    if(vertexCount > 65535){
        throw std::runtime_error("LoomShapes: that sphere needs more than 65535 vertices, which does not fit a 16 bit index");
    }
}

}

std::vector<Vertex> planeVertices(){
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    //Flat in XZ, facing up. A floor, because that is what a plane is used for
    addQuad(vertices, indices,
        {-0.5f, 0.0f,-0.5f}, {-0.5f, 0.0f, 0.5f}, { 0.5f, 0.0f, 0.5f}, { 0.5f, 0.0f,-0.5f},
        {0.0f, 1.0f, 0.0f});

    return vertices;
}

std::vector<uint16_t> planeIndices(){
    return {0,1,2, 2,3,0};
}

std::vector<Vertex> cubeVertices(){
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    addQuad(vertices, indices, {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f});
    addQuad(vertices, indices, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f});
    addQuad(vertices, indices, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 1.0f, 0.0f, 0.0f});
    addQuad(vertices, indices, {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-1.0f, 0.0f, 0.0f});
    addQuad(vertices, indices, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.0f, 1.0f, 0.0f});
    addQuad(vertices, indices, {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, { 0.0f,-1.0f, 0.0f});

    return vertices;
}

std::vector<uint16_t> cubeIndices(){
    std::vector<uint16_t> indices;
    indices.reserve(36);

    //Six faces of four corners each, every one wound the same way. The vertices above are
    //laid down in exactly this order, four at a time
    for(uint16_t face = 0; face < 6; ++face){
        const uint16_t base = uint16_t(face * 4);
        indices.insert(indices.end(), {
            base, uint16_t(base+1), uint16_t(base+2),
            uint16_t(base+2), uint16_t(base+3), base});
    }

    return indices;
}

std::vector<Vertex> pyramidVertices(){
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    const glm::vec3 apex{0.0f, 0.5f, 0.0f};
    const glm::vec3 frontLeft {-0.5f,-0.5f, 0.5f};
    const glm::vec3 frontRight{ 0.5f,-0.5f, 0.5f};
    const glm::vec3 backRight { 0.5f,-0.5f,-0.5f};
    const glm::vec3 backLeft  {-0.5f,-0.5f,-0.5f};

    //Base, facing down
    addQuad(vertices, indices, backLeft, backRight, frontRight, frontLeft, {0.0f,-1.0f,0.0f});

    //Four slanted sides. The normal comes from the winding rather than from a guess, because
    //a slanted face's normal is not axis aligned and four hand written ones are four bugs
    addTriangle(vertices, indices, frontLeft,  frontRight, apex, faceNormal(frontLeft,  frontRight, apex));
    addTriangle(vertices, indices, frontRight, backRight,  apex, faceNormal(frontRight, backRight,  apex));
    addTriangle(vertices, indices, backRight,  backLeft,   apex, faceNormal(backRight,  backLeft,   apex));
    addTriangle(vertices, indices, backLeft,   frontLeft,  apex, faceNormal(backLeft,   frontLeft,  apex));

    return vertices;
}

std::vector<uint16_t> pyramidIndices(){
    //Six for the base, three for each of the four sides
    return {0,1,2, 2,3,0,  4,5,6,  7,8,9,  10,11,12,  13,14,15};
}

std::vector<Vertex> sphereVertices(uint32_t segments, uint32_t rings){
    checkSphere(segments, rings);

    std::vector<Vertex> vertices;
    vertices.reserve(size_t(segments + 1) * (rings + 1));

    const float pi = 3.14159265358979323846f;

    //A UV sphere: rings walk from the north pole down, segments walk around. The seam gets a
    //duplicated column of vertices (segments + 1 of them) so the texture can run 0 to 1
    //without the last quad having to wrap back to u = 0
    for(uint32_t ring = 0; ring <= rings; ++ring){
        const float v = float(ring) / float(rings);
        const float theta = v * pi;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for(uint32_t segment = 0; segment <= segments; ++segment){
            const float u = float(segment) / float(segments);
            const float phi = u * 2.0f * pi;

            const glm::vec3 normal{sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};

            //Diameter one, like every other shape here, so radius is a half
            vertices.push_back({normal * 0.5f, white, {u, v}, normal});
        }
    }

    return vertices;
}

std::vector<uint16_t> sphereIndices(uint32_t segments, uint32_t rings){
    checkSphere(segments, rings);

    std::vector<uint16_t> indices;
    indices.reserve(size_t(segments) * rings * 6);

    const uint32_t stride = segments + 1;

    for(uint32_t ring = 0; ring < rings; ++ring){
        //The top row of a UV sphere is the north pole: every vertex in it sits on the same
        //point, so the quad there is really a triangle and its other half has zero area. The
        //bottom row is the south pole and loses the opposite half. Emitting them anyway would
        //cost 64 degenerate triangles on a 32 by 16 sphere - vertex work and rasteriser setup
        //spent on nothing, and a pile of zero length normals for anything that inspects them
        const bool atNorthPole = (ring == 0);
        const bool atSouthPole = (ring == rings - 1);

        for(uint32_t segment = 0; segment < segments; ++segment){
            const uint32_t topLeft = ring * stride + segment;
            const uint32_t topRight = topLeft + 1;
            const uint32_t bottomLeft = topLeft + stride;
            const uint32_t bottomRight = bottomLeft + 1;

            //Wound so the normal the right hand rule gives points out of the sphere, which is
            //the direction the vertex normals already point
            if(!atNorthPole){
                indices.push_back(uint16_t(topLeft));
                indices.push_back(uint16_t(topRight));
                indices.push_back(uint16_t(bottomLeft));
            }

            if(!atSouthPole){
                indices.push_back(uint16_t(topRight));
                indices.push_back(uint16_t(bottomRight));
                indices.push_back(uint16_t(bottomLeft));
            }
        }
    }

    return indices;
}


Primitives::Primitives(LoomInitializer& loom, const PrimitivesConfig& config) : loom(loom), config(config){
    if(config.sphereSegments < 3 || config.sphereRings < 2){
        throw std::runtime_error("LoomShapes::Primitives: a sphere needs at least 3 segments and 2 rings");
    }
}

const Mesh& Primitives::planeMesh(){
    if(!plane_){
        plane_.emplace(loom.device, loom.command, planeVertices(), planeIndices());
    }
    return *plane_;
}

const Mesh& Primitives::cubeMesh(){
    if(!cube_){
        cube_.emplace(loom.device, loom.command, cubeVertices(), cubeIndices());
    }
    return *cube_;
}

const Mesh& Primitives::pyramidMesh(){
    if(!pyramid_){
        pyramid_.emplace(loom.device, loom.command, pyramidVertices(), pyramidIndices());
    }
    return *pyramid_;
}

const Mesh& Primitives::sphereMesh(){
    if(!sphere_){
        sphere_.emplace(loom.device, loom.command,
            sphereVertices(config.sphereSegments, config.sphereRings),
            sphereIndices(config.sphereSegments, config.sphereRings));
    }
    return *sphere_;
}

const VulkanGraphicsPipeline& Primitives::getTexturedPipeline(){
    if(!texturedPipeline){
        PipelineConfig pipelineConfig;
        pipelineConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
        pipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
        pipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";
        pipelineConfig.cullMode = config.cullMode;

        //Asking for a depth test the window has no depth buffer for would throw inside the
        //pipeline. A shape drawn without depth is still a shape, so this quietly does the
        //possible thing rather than refusing to build
        const bool hasDepth = loom.depthImage.has_value();
        pipelineConfig.depthTestEnable = config.depthTest && hasDepth;
        pipelineConfig.depthWriteEnable = pipelineConfig.depthTestEnable && config.depthWrite;
        pipelineConfig.depthCompare = config.depthCompare;

        texturedPipeline.emplace(loom.createPipeline(pipelineConfig));
    }
    return *texturedPipeline;
}

const Material& Primitives::materialFor(const Texture& texture){
    const SampledImage sampled = texture.getSampled();
    const VkImageView key = static_cast<VkImageView>(sampled.view);

    auto found = texturedMaterials.find(key);
    if(found != texturedMaterials.end()){
        return found->second;
    }

    //Built once per texture and kept. A descriptor set per frame in flight is not something
    //to allocate again every time a cube is drawn
    auto inserted = texturedMaterials.emplace(std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(loom.device, loom.command, loom.getDescriptorPool(),
                              getTexturedPipeline(), sampled, MaterialData{}));

    return inserted.first->second;
}

void Primitives::plane(const Material& material, const glm::mat4& model){
    loom.renderer.draw(planeMesh(), model, material);
}

void Primitives::cube(const Material& material, const glm::mat4& model){
    loom.renderer.draw(cubeMesh(), model, material);
}

void Primitives::pyramid(const Material& material, const glm::mat4& model){
    loom.renderer.draw(pyramidMesh(), model, material);
}

void Primitives::sphere(const Material& material, const glm::mat4& model){
    loom.renderer.draw(sphereMesh(), model, material);
}

void Primitives::plane(const Texture& texture, const glm::mat4& model){
    plane(materialFor(texture), model);
}

void Primitives::cube(const Texture& texture, const glm::mat4& model){
    cube(materialFor(texture), model);
}

void Primitives::pyramid(const Texture& texture, const glm::mat4& model){
    pyramid(materialFor(texture), model);
}

void Primitives::sphere(const Texture& texture, const glm::mat4& model){
    sphere(materialFor(texture), model);
}

void Primitives::plane(const glm::mat4& model){
    loom.renderer.draw(planeMesh(), model);
}

void Primitives::cube(const glm::mat4& model){
    loom.renderer.draw(cubeMesh(), model);
}

void Primitives::pyramid(const glm::mat4& model){
    loom.renderer.draw(pyramidMesh(), model);
}

void Primitives::sphere(const glm::mat4& model){
    loom.renderer.draw(sphereMesh(), model);
}

}
