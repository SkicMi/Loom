// shapes: the four primitives are the size they claim, wound the way the culling expects,
//         and drawing one is a single call that caches what it builds
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include <glm/gtc/matrix_transform.hpp>

//Deliberately not "using namespace LoomShapes": TestScene.h has had its own
//cubeVertices since v1, and the two would be ambiguous rather than one shadowing
//the other. Qualifying says which cube is meant every time

struct Wound{
    size_t triangles = 0;
    size_t inverted = 0;   //the winding says one way, the vertex normals say the other
    float worstNormalLength = 0.0f;
    float furthest = 0.0f; //how far the outermost vertex sits from the origin
};

//A triangle's winding decides which side is the front, and the vertex normals decide which
//side is lit. If those two disagree the shape is inside out: it renders, it is culled
//wrongly, and it is lit from within. Nothing but this check catches it before a picture does
static Wound inspect(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices){
    Wound out;

    for(const Vertex& vertex : vertices){
        out.worstNormalLength = std::max(out.worstNormalLength,
            std::abs(glm::length(vertex.normal) - 1.0f));
        out.furthest = std::max(out.furthest, glm::length(vertex.position));
    }

    for(size_t i = 0; i + 2 < indices.size(); i += 3){
        const glm::vec3& a = vertices[indices[i+0]].position;
        const glm::vec3& b = vertices[indices[i+1]].position;
        const glm::vec3& c = vertices[indices[i+2]].position;

        const glm::vec3 geometric = glm::cross(b - a, c - a);
        const glm::vec3 declared = vertices[indices[i+0]].normal +
                                   vertices[indices[i+1]].normal +
                                   vertices[indices[i+2]].normal;

        ++out.triangles;
        if(glm::dot(geometric, declared) <= 0.0f) ++out.inverted;
    }

    return out;
}

static size_t countNonBlackPixels(const std::vector<uint8_t>& pixels){
    return countNonBlack(pixels);
}

int main(){
    TestReport report("shapes");

    const vk::Extent2D size{256,256};

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "shapes"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    // -------------------------------------------------------------------------------
    // Geometrija, prije nego je itko nacrtao
    // -------------------------------------------------------------------------------

    const Wound plane = inspect(LoomShapes::planeVertices(), LoomShapes::planeIndices());
    const Wound cube = inspect(LoomShapes::cubeVertices(), LoomShapes::cubeIndices());
    const Wound pyramid = inspect(LoomShapes::pyramidVertices(), LoomShapes::pyramidIndices());
    const Wound sphere = inspect(LoomShapes::sphereVertices(), LoomShapes::sphereIndices());

    report.check("plane namot", plane.inverted == 0 && plane.triangles == 2,
        fmt("%zu trokuta, %zu naopako", plane.triangles, plane.inverted));

    report.check("cube namot", cube.inverted == 0 && cube.triangles == 12,
        fmt("%zu trokuta, %zu naopako", cube.triangles, cube.inverted));

    report.check("pyramid namot", pyramid.inverted == 0 && pyramid.triangles == 6,
        fmt("%zu trokuta, %zu naopako", pyramid.triangles, pyramid.inverted));

    //Two triangles per quad, minus the one degenerate half each pole row would contribute
    const size_t sphereTriangles = size_t(32) * 16 * 2 - size_t(32) * 2;
    report.check("sphere namot", sphere.inverted == 0 && sphere.triangles == sphereTriangles,
        fmt("%zu trokuta, ocekivano %zu, %zu naopako", sphere.triangles, sphereTriangles, sphere.inverted));

    const float worstNormal = std::max(std::max(plane.worstNormalLength, cube.worstNormalLength),
                                       std::max(pyramid.worstNormalLength, sphere.worstNormalLength));
    report.check("normale su jedinicne", worstNormal < 1e-5f,
        fmt("najveci odmak od duljine 1: %.3e", double(worstNormal)));

    //Every shape is one unit across, so nothing may stick out past a half - except a cube's
    //corner, which is a half in all three axes at once
    const float cubeCorner = std::sqrt(0.75f);
    report.check("velicina je jedan",
        std::abs(plane.furthest - std::sqrt(0.5f)) < 1e-5f &&
        std::abs(cube.furthest - cubeCorner) < 1e-5f &&
        std::abs(sphere.furthest - 0.5f) < 1e-5f &&
        pyramid.furthest <= cubeCorner + 1e-5f,
        fmt("plane %.4f, cube %.4f, pyramid %.4f, sphere %.4f",
            double(plane.furthest), double(cube.furthest), double(pyramid.furthest), double(sphere.furthest)));

    //A sphere is a sphere: every vertex exactly the same distance out, not merely close
    float worstRadius = 0.0f;
    for(const Vertex& vertex : LoomShapes::sphereVertices()){
        worstRadius = std::max(worstRadius, std::abs(glm::length(vertex.position) - 0.5f));
    }
    report.check("sfera je okrugla", worstRadius < 1e-6f,
        fmt("najveci odmak od polumjera 0.5: %.3e", double(worstRadius)));

    //A plane is flat and faces up, which is what makes it a floor rather than a wall
    bool planeIsFlat = true;
    for(const Vertex& vertex : LoomShapes::planeVertices()){
        if(vertex.position.y != 0.0f) planeIsFlat = false;
        if(vertex.normal != glm::vec3(0.0f,1.0f,0.0f)) planeIsFlat = false;
    }
    report.check("plane lezi u XZ", planeIsFlat, "svi vrhovi na y = 0, normala +Y");

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool tinySphereThrew = false;
    try{ LoomShapes::sphereVertices(2, 2); }
    catch(const std::exception&){ tinySphereThrew = true; }
    report.check("sfera od dva", tinySphereThrew, "baca iznimku");

    bool hugeSphereThrew = false;
    try{ LoomShapes::sphereVertices(400, 400); }
    catch(const std::exception&){ hugeSphereThrew = true; }
    report.check("sfera preko 16 bita", hugeSphereThrew, "baca iznimku umjesto omotanih indeksa");

    // -------------------------------------------------------------------------------
    // Crtanje: jedan poziv po obliku
    // -------------------------------------------------------------------------------

    LoomShapes::Primitives shapes(loom);

    CameraConfig camConfig;
    camConfig.position = {0.0f, 1.2f, 2.2f};
    camConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {0.15f,0.15f,0.15f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f,-1.0f,-0.4f};
    Light light(lightConfig);
    loom.renderer.addLight(light);

    const std::vector<uint8_t> checker = makeCheckerboard(64, 8);
    Texture texture(loom.device, loom.command, checker.data(), vk::Extent2D{64,64});

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    RenderTarget outPlane(loom.device, size, readConfig);
    RenderTarget outCube(loom.device, size, readConfig);
    RenderTarget outPyramid(loom.device, size, readConfig);
    RenderTarget outSphere(loom.device, size, readConfig);

    const glm::mat4 identity = glm::mat4(1.0f);
    const glm::mat4 flatOut = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f,1.0f,2.0f));

    int drawn = 0;
    while(drawn < 3 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        //This is the whole point of the class: one line per shape, and the texture is all it
        //is given. No Mesh built by hand, no Material built by hand, no draw call written out
        loom.renderer.beginPass(outPlane);   shapes.plane(texture, flatOut);   loom.renderer.endPass();
        loom.renderer.beginPass(outCube);    shapes.cube(texture, identity);   loom.renderer.endPass();
        loom.renderer.beginPass(outPyramid); shapes.pyramid(texture, identity);loom.renderer.endPass();
        loom.renderer.beginPass(outSphere);  shapes.sphere(texture, identity); loom.renderer.endPass();

        loom.renderer.beginPass();
        shapes.cube(texture, identity);
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const size_t planePixels = countNonBlackPixels(outPlane.readPixels(loom.command).pixels);
    const size_t cubePixels = countNonBlackPixels(outCube.readPixels(loom.command).pixels);
    const size_t pyramidPixels = countNonBlackPixels(outPyramid.readPixels(loom.command).pixels);
    const size_t spherePixels = countNonBlackPixels(outSphere.readPixels(loom.command).pixels);

    report.check("sve cetiri se vide",
        planePixels > 1000 && cubePixels > 1000 && pyramidPixels > 1000 && spherePixels > 1000,
        fmt("plane %zu, cube %zu, pyramid %zu, sphere %zu",
            planePixels, cubePixels, pyramidPixels, spherePixels));

    //Four different shapes have to cover four different numbers of pixels. If they did not,
    //the check above would pass on four copies of the same mesh
    report.check("cetiri razlicita oblika",
        cubePixels != pyramidPixels && pyramidPixels != spherePixels && cubePixels != spherePixels,
        fmt("cube %zu, pyramid %zu, sphere %zu piksela", cubePixels, pyramidPixels, spherePixels));

    //A pyramid is a cube with the top four corners pulled into one, so from the same camera
    //it cannot cover more
    report.check("piramida je manja od kocke", pyramidPixels < cubePixels,
        fmt("piramida %zu, kocka %zu", pyramidPixels, cubePixels));

    //Four shapes, one texture, one material. Building a descriptor set per draw would be a
    //leak that only shows up when the pool runs dry
    report.check("materijal se cuva", shapes.cachedMaterialCount() == 1,
        fmt("%zu materijala za jednu teksturu kroz cetiri oblika i tri framea",
            shapes.cachedMaterialCount()));

    report.checkNoValidationMessages();
    return report.result();
}
