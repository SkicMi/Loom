#include "TreadlePaint/UiPainter.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace{

//Nosi li format sRGB krivulju. Vulkan tada sam pretvara ono sto shader vrati, pa shader mora
//pretvoriti unatrag - vidi ui.slang
bool isSrgb(vk::Format format){
    switch(format){
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eA8B8G8R8SrgbPack32:
        case vk::Format::eR8G8B8Srgb:
        case vk::Format::eB8G8R8Srgb:
        case vk::Format::eR8Srgb:
        case vk::Format::eR8G8Srgb:
            return true;
        default:
            return false;
    }
}

}

PipelineConfig UiPainter::makeConfig(vk::Format colorFormat){
    PipelineConfig config;

    config.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/ui.vert.spv";
    config.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/ui.frag.spv";

    //Vlastiti raspored vrha, ne Loomov Vertex: suicelje ima dvije koordinate i boju s
    //prozirnoscu, a normala i texCoord bi bili 20 bajtova po vrhu koje nitko ne cita
    vk::VertexInputBindingDescription binding;
    binding.binding = 0;
    binding.stride = sizeof(Treadle::Vertex);
    binding.inputRate = vk::VertexInputRate::eVertex;
    config.vertexBindings = {binding};

    std::vector<vk::VertexInputAttributeDescription> attributes(2);
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = vk::Format::eR32G32Sfloat;
    attributes[0].offset = offsetof(Treadle::Vertex, x);

    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
    attributes[1].offset = offsetof(Treadle::Vertex, r);
    config.vertexAttributes = attributes;

    //Bez seta 0 i bez descriptora: shader ne cita ni kameru ni teksturu. Cjevovod koji bi ih
    //deklarirao trazio bi vezanje seta koji nitko ne puni
    config.useFrameData = false;
    config.descriptorBindings.clear();

    config.pushConstantSize = sizeof(Push);
    config.pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    config.cullMode = vk::CullModeFlagBits::eNone;
    config.blendMode = BlendMode::Alpha;
    config.colorFormat = colorFormat;

    //Suicelje je iznad svega i ne sudjeluje u dubini. Da pise dubinu, ono sto se crta poslije
    //njega u istom prolazu nestalo bi iza nevidljivog pravokutnika
    config.depthTestEnable = false;
    config.depthWriteEnable = false;

    //Slika stope se na suicelje ne primjenjuje: tekst od pet piksela nacrtan u grubljem
    //rasteru postane mrlja
    config.allowShadingRateAttachment = false;

    return config;
}

UiPainter::UiPainter(const VulkanDevice& device,
                     const VulkanCommand& command,
                     vk::Format colorFormat,
                     vk::Format depthFormat,
                     uint32_t maxVertices)
: device(device),
  maxVertices(maxVertices),
  srgbTarget(isSrgb(colorFormat)),
  pipeline(device, makeConfig(colorFormat), colorFormat, depthFormat){

    //Sest indeksa na svaka cetiri vrha, jer je sve pravokutnik. Zaokruzeno navise da broj
    //vrhova koji nije visekratnik cetiri ne prekoraci polje indeksa
    const uint32_t maxIndices = maxVertices / 4 * 6 + 6;

    const size_t inFlight = command.getCommandBuffers().size();
    vertexBuffers.reserve(inFlight);
    indexBuffers.reserve(inFlight);

    for(size_t frame = 0; frame < inFlight; ++frame){
        vertexBuffers.emplace_back(device, sizeof(Treadle::Vertex) * maxVertices,
                                   vk::BufferUsageFlagBits::eVertexBuffer, MemoryUsage::CPU_TO_GPU);
        indexBuffers.emplace_back(device, sizeof(uint32_t) * maxIndices,
                                  vk::BufferUsageFlagBits::eIndexBuffer, MemoryUsage::CPU_TO_GPU);
        vertexBuffers.back().setDebugName("ui vertices");
        indexBuffers.back().setDebugName("ui indices");
    }
}

void UiPainter::draw(VulkanRenderer& renderer, const Treadle::DrawList& list,
                     uint32_t width, uint32_t height){
    if(list.empty() || width == 0 || height == 0) return;

    uint32_t vertexCount = uint32_t(list.vertices.size());
    uint32_t indexCount = uint32_t(list.indices.size());

    if(vertexCount > maxVertices){
        //Odsijeca se na cijeli pravokutnik, jer bi pola pravokutnika bio trokut preko pola
        //ekrana. Javlja se jednom: suicelje koje ne stane javljalo bi to sezdeset puta u
        //sekundi i zatrpalo bi sve ostalo
        if(!warned){
            printf("UiPainter: suicelje trazi %u vrhova, stane %u - visak se odsijeca "
                   "(povecaj maxVertices)\n", vertexCount, maxVertices);
            warned = true;
        }
        vertexCount = maxVertices / 4 * 4;
        indexCount = vertexCount / 4 * 6;
        if(indexCount > uint32_t(list.indices.size())) indexCount = uint32_t(list.indices.size());
    }

    VulkanBuffer& vertices = vertexBuffers[nextBuffer];
    VulkanBuffer& indices = indexBuffers[nextBuffer];
    nextBuffer = (nextBuffer + 1) % uint32_t(vertexBuffers.size());

    vertices.upload(list.vertices.data(), sizeof(Treadle::Vertex) * vertexCount);
    indices.upload(list.indices.data(), sizeof(uint32_t) * indexCount);

    const vk::raii::CommandBuffer& commandBuffer = renderer.borrowCommands();

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.getPipeline());

    Push push;
    push.screenWidth = float(width);
    push.screenHeight = float(height);
    push.encodeSrgb = srgbTarget ? 1.0f : 0.0f;

    commandBuffer.pushConstants<Push>(*pipeline.getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);

    commandBuffer.bindVertexBuffers(0, {*vertices.getBuffer()}, {0});
    commandBuffer.bindIndexBuffer(*indices.getBuffer(), 0, vk::IndexType::eUint32);
    commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}
