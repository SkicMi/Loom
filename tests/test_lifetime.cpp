// lifetime: two Looms with windows can be alive at the same time, and one of them going
//           away leaves the other one drawing exactly what it drew before.
//
// This is a regression test for a real bug: Window's destructor used to call
// glfwTerminate(), which is process wide. The second Window destroyed took the first one's
// window down with it, and the first destructor then ran glfwDestroyWindow on a dangling
// pointer. Nothing in the suite noticed, because no test had ever held two.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Window.h"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

//LoomInitializer is deliberately neither copyable nor movable, so holding two of them at
//once means holding two pointers
struct Instance{
    std::unique_ptr<LoomInitializer> loom;
    std::unique_ptr<LoomShapes::Primitives> shapes;
    std::unique_ptr<RenderTarget> target;

    //Torn down by hand, and in this order. The target and the shapes hold buffers and images
    //allocated out of the Loom's device, so the Loom has to be the last thing to go.
    //
    //Member order alone cannot arrange that: destruction runs in reverse declaration order
    //but assignment runs in declaration order, so the two want opposite layouts. Assigning
    //an empty Instance over this one destroyed the Loom first and VMA asserted on the
    //allocations still outstanding - which is exactly what that assert is for
    void reset(){
        target.reset();
        shapes.reset();
        loom.reset();
    }

    Instance() = default;
    ~Instance(){ reset(); }

    //Moving is fine - the pointers just change hands. Move ASSIGNMENT is deliberately gone:
    //it would run in declaration order and destroy the Loom first, which is the very thing
    //reset() exists to avoid. Anything that wants to drop an Instance calls reset()
    Instance(Instance&&) = default;
    Instance& operator=(Instance&&) = delete;
};

static Instance make(bool headless, vk::Extent2D size, const char* name){
    LoomConfig config;
    config.width = size.width;
    config.height = size.height;
    config.appName = name;
    config.engineName = "Loom tests";
    config.headless = headless;
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    Instance instance;
    instance.loom = std::make_unique<LoomInitializer>(config);
    instance.shapes = std::make_unique<LoomShapes::Primitives>(*instance.loom);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    instance.target = std::make_unique<RenderTarget>(instance.loom->device, size, readConfig);

    return instance;
}

static std::vector<uint8_t> render(Instance& instance){
    LoomInitializer& loom = *instance.loom;

    CameraConfig camConfig;
    camConfig.position = {1.8f, 1.4f, 2.4f};
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
    loom.renderer.clearLights();
    loom.renderer.addLight(light);

    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.3f,1.0f,0.1f));

    int drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(*instance.target);
        instance.shapes->cube(model);
        loom.renderer.endPass();

        //A windowed run has to draw into the image it acquired, or there is nothing to present
        if(loom.hasWindow()){
            loom.renderer.beginPass();
            instance.shapes->cube(model);
            loom.renderer.endPass();
        }

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    return instance.target->readPixels(loom.command).pixels;
}

int main(){
    TestReport report("lifetime");

    const vk::Extent2D size{256,256};

    report.check("na pocetku nista", GlfwContext::liveCount() == 0,
        fmt("%u drzaca GLFW-a prije ijednog prozora", GlfwContext::liveCount()));

    //A headless Loom must never touch GLFW at all - that is what makes it runnable with no
    //display and no windowing library underneath
    {
        Instance headless = make(true, size, "lifetime headless");
        report.check("headless ne dira GLFW", GlfwContext::liveCount() == 0,
            fmt("%u drzaca dok headless Loom radi", GlfwContext::liveCount()));

        const std::vector<uint8_t> pixels = render(headless);
        report.check("headless crta", countNonBlack(pixels) > 500,
            fmt("%zu ne-crnih piksela", countNonBlack(pixels)));
    }

    // -------------------------------------------------------------------------------
    // Dva prozora u isto vrijeme
    //   Mjereno na razini Windowa, ne dva cijela Looma: bug je bio u Windowu, a dvije
    //   istovremene VkInstance uz validation layer rusi NVIDIJIN driver kad jedna ode -
    //   isti test prolazi na lavapipeu. To Loom ne moze popraviti i nije njegovo da tvrdi
    // -------------------------------------------------------------------------------

    {
        Window first(128, 128, "lifetime first");
        Window second(128, 128, "lifetime second");

        report.check("dva prozora", GlfwContext::liveCount() == 2,
            fmt("%u drzaca dok su oba ziva", GlfwContext::liveCount()));

        //Here is the bug this test exists for. The old destructor called glfwTerminate,
        //which is process wide: this line used to destroy the second window as well, and
        //everything below was undefined behaviour that happened to look like it worked
        {
            Window doomed(128, 128, "lifetime doomed");
            report.check("tri prozora", GlfwContext::liveCount() == 3,
                fmt("%u drzaca", GlfwContext::liveCount()));
        }

        report.check("jedan otisao", GlfwContext::liveCount() == 2,
            fmt("%u drzaca nakon sto je treci otisao", GlfwContext::liveCount()));

        //GLFW answers GLFW_NOT_INITIALIZED to everything once it has been terminated, so
        //this is the question asked directly rather than inferred from a crash
        glfwGetError(nullptr); //clear whatever came before
        first.pollEvents();
        const int afterPoll = glfwGetError(nullptr);

        report.check("GLFW je ziv", afterPoll == GLFW_NO_ERROR,
            fmt("glfwPollEvents nakon rusenja jednog prozora javlja %d (0 = bez greske)", afterPoll));

        //And the survivors are still real windows, not freed handles
        const bool bothAlive = first.getWindow() != nullptr && second.getWindow() != nullptr &&
                               !first.shouldClose() && !second.shouldClose();
        report.check("prezivjeli su citavi", bothAlive, "oba prozora i dalje odgovaraju");
    }

    report.check("nakon prozora nista", GlfwContext::liveCount() == 0,
        fmt("%u drzaca nakon sto su svi prozori otisli", GlfwContext::liveCount()));

    // -------------------------------------------------------------------------------
    // GLFW se gasi do kraja i pali ispocetka
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> firstRun;
    {
        Instance windowed = make(false, size, "lifetime windowed");
        report.check("Loom drzi GLFW", GlfwContext::liveCount() == 1,
            fmt("%u drzaca dok Loom s prozorom radi", GlfwContext::liveCount()));
        firstRun = render(windowed);
    }

    report.check("Loom pusta GLFW", GlfwContext::liveCount() == 0,
        fmt("%u drzaca nakon sto je Loom otisao", GlfwContext::liveCount()));

    {
        //A second Loom after GLFW was fully shut down: glfwInit has to work again, and the
        //picture has to be the same one
        Instance again = make(false, size, "lifetime again");
        const std::vector<uint8_t> secondRun = render(again);

        const ByteDiff difference = diffBytes(firstRun, secondRun);
        report.check("ponovno pokretanje", difference.different == 0,
            fmt("%zu razlicitih od %zu bajtova nakon punog gasenja i novog pokretanja GLFW-a",
                difference.different, firstRun.size()));
    }

    report.check("cisto za sobom", GlfwContext::liveCount() == 0,
        fmt("%u drzaca na kraju testa", GlfwContext::liveCount()));

    report.checkNoValidationMessages();
    return report.result();
}
