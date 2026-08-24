// vma: the allocator suballocates, keeps host memory mapped, and puts every resource in the
// kind of memory it asked for. Before VMA each of these buffers was its own vkAllocateMemory
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Vertex.h"

int main(){
    TestReport report("vma memory");

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "vma"; config.engineName = "Loom tests";

    LoomInitializer loom(config);

    const VulkanAllocator& allocator = loom.device.getAllocator();

    report.check("allocator", allocator.isValid(), loom.device.getDeviceName());

    //Which of the two optional extensions this card offered. Neither is required, so this is
    //reported rather than asserted - the numbers below just get less precise without them
    const AllocatorConfig& allocatorConfig = allocator.getConfig();
    report.check("opcije", true,
        fmt("memory_budget %s, memory_priority %s",
            allocatorConfig.useMemoryBudget ? "da" : "ne",
            allocatorConfig.useMemoryPriority ? "da" : "ne"));

    // ---------------------------------------------------------------------------
    // Suballokacija: mnogo bufera, malo VkDeviceMemory objekata
    // ---------------------------------------------------------------------------

    const MemoryStats before = allocator.getStats();

    //Small enough that no driver would give each one its own block, many enough that the old
    //one-vkAllocateMemory-per-buffer path would have burned 500 VkDeviceMemory objects on
    //128 KB of actual data. The spec only guarantees maxMemoryAllocationCount >= 4096, and
    //this card reports far more than that, but the low number is the one that has to be
    //survivable - so 4096 is what the counts below are judged against, not the driver's own
    const size_t bufferCount = 500;
    const vk::DeviceSize smallSize = 256;

    std::vector<VulkanBuffer> many;
    many.reserve(bufferCount);
    for(size_t i = 0; i < bufferCount; ++i){
        many.emplace_back(loom.device, smallSize,
            vk::BufferUsageFlagBits::eUniformBuffer, MemoryUsage::CPU_TO_GPU);
    }

    const MemoryStats after = allocator.getStats();

    const uint32_t newAllocations = after.allocationCount - before.allocationCount;
    const uint32_t newBlocks = after.blockCount - before.blockCount;

    //The spec's floor for maxMemoryAllocationCount. Everything here is measured against this
    //rather than what this particular driver happens to allow
    const uint32_t guaranteedLimit = 4096;

    report.check("suballokacija", newAllocations == bufferCount && newBlocks <= 2,
        fmt("%u bufera u %u novih VkDeviceMemory blokova (bez VMA bi ih bilo %zu)",
            newAllocations, newBlocks, bufferCount));

    //The whole scene - per frame UBOs, light SSBOs, 500 buffers - has to stay far away from
    //the limit. Without VMA it would already be past 500 of the guaranteed 4096
    report.check("blokovi ukupno", after.blockCount < guaranteedLimit / 8,
        fmt("%u blokova, %u alokacija, %llu KiB iskoristeno od %llu KiB rezerviranog (limit ovog drajvera %u, spec jamci %u)",
            after.blockCount, after.allocationCount,
            (unsigned long long)(after.allocationBytes / 1024),
            (unsigned long long)(after.blockBytes / 1024),
            after.deviceAllocationLimit, guaranteedLimit));

    many.clear();

    const MemoryStats freed = allocator.getStats();
    report.check("oslobadanje", freed.allocationCount == before.allocationCount,
        fmt("natrag na %u alokacija", freed.allocationCount));

    // ---------------------------------------------------------------------------
    // Memorijski tipovi: svaki MemoryUsage dobiva ono sto je trazio
    // ---------------------------------------------------------------------------

    VulkanBuffer deviceOnly(loom.device, 4096,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        MemoryUsage::GPU_ONLY);

    VulkanBuffer hostWrite(loom.device, 4096,
        vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);

    VulkanBuffer hostRead(loom.device, 4096,
        vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    report.check("GPU_ONLY", deviceOnly.isDeviceLocal(),
        deviceOnly.isDeviceLocalHostVisible() ? "device local + host visible (resizable BAR)" : "device local");

    report.check("CPU_TO_GPU", hostWrite.isHostVisible(),
        fmt("host visible, coherent %s, device local %s",
            hostWrite.isHostCoherent() ? "da" : "ne",
            hostWrite.isDeviceLocal() ? "da (resizable BAR)" : "ne"));

    report.check("GPU_TO_CPU", hostRead.isHostVisible(),
        fmt("host visible, coherent %s", hostRead.isHostCoherent() ? "da" : "ne"));

    // ---------------------------------------------------------------------------
    // Trajno mapiranje: nema map/unmap para po frameu
    // ---------------------------------------------------------------------------

    report.check("trajno mapiran", hostWrite.getMappedData() != nullptr && hostRead.getMappedData() != nullptr,
        "host vidljivi buferi ostaju mapirani");

    report.check("GPU_ONLY nije mapiran", deviceOnly.getMappedData() == nullptr,
        "device local memorija nema pokazivac za CPU");

    // ---------------------------------------------------------------------------
    // Round trip: ono sto CPU zapise je ono sto CPU procita natrag
    //   Ovo je provjera flusha i invalidatea: memorijski tip vise nije nas izbor, pa vise
    //   nije zajamceno coherent kao kad je stara sifra trazila eHostCoherent
    // ---------------------------------------------------------------------------

    std::vector<uint32_t> written(1024);
    for(size_t i = 0; i < written.size(); ++i){
        written[i] = static_cast<uint32_t>(i * 2654435761u);
    }

    VulkanBuffer roundTrip(loom.device, written.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::GPU_TO_CPU);
    roundTrip.upload(written.data(), written.size() * sizeof(uint32_t));

    std::vector<uint32_t> read(written.size(), 0);
    roundTrip.download(read.data(), read.size() * sizeof(uint32_t));

    size_t mismatches = 0;
    for(size_t i = 0; i < written.size(); ++i){
        if(written[i] != read[i]) ++mismatches;
    }
    report.check("round trip", mismatches == 0,
        fmt("%zu od %zu rijeci se razlikuje", mismatches, written.size()));

    // ---------------------------------------------------------------------------
    // Mesh ide u device local memoriju, ne u host vidljivu
    // ---------------------------------------------------------------------------

    const std::vector<Vertex> vertices = {
        {{-0.5f,-0.5f,0.0f},{1,0,0},{0,0},{0,0,1}},
        {{ 0.5f,-0.5f,0.0f},{0,1,0},{1,0},{0,0,1}},
        {{ 0.0f, 0.5f,0.0f},{0,0,1},{0.5f,1},{0,0,1}},
    };
    const std::vector<uint16_t> indices = {0,1,2};

    Mesh mesh(loom.device, loom.command, vertices, indices);

    report.check("mesh device local",
        mesh.getVertexBuffer().isDeviceLocal() && mesh.getIndexBuffer().isDeviceLocal(),
        fmt("vertex %s, index %s",
            mesh.getVertexBuffer().isDeviceLocal() ? "device local" : "host",
            mesh.getIndexBuffer().isDeviceLocal() ? "device local" : "host"));

    // ---------------------------------------------------------------------------
    // Granice koje sad daju recenicu umjesto VUID-a
    // ---------------------------------------------------------------------------

    bool zeroThrew = false;
    try{
        VulkanBuffer empty(loom.device, 0, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
    }
    catch(const std::exception&){
        zeroThrew = true;
    }
    report.check("velicina 0", zeroThrew, "baca iznimku");

    bool uploadThrew = false;
    try{
        deviceOnly.upload(written.data(), 16);
    }
    catch(const std::exception&){
        uploadThrew = true;
    }
    report.check("upload u GPU_ONLY", uploadThrew, "baca iznimku");

    printf("   %s\n", allocator.summary().c_str());

    report.checkNoValidationMessages();
    return report.result();
}
