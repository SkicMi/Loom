#pragma once

#include <vulkan/vulkan_raii.hpp>
#include "VulkanInstance.h"
#include "Window.h"
#include "VulkanDevice.h"
#include <limits>
#include <algorithm>
#include <optional>

struct SwapchainConfig {
    //Mailbox is preffered but not guaranteed on each device, so we will fallback to FiFo
    //eFifo is guaranteed to be on each GPU cause it is demanded in specs of each device
    vk::PresentModeKHR preferredPresentMode = vk::PresentModeKHR::eMailbox;
    
    vk::SurfaceFormatKHR preferredSurfaceFormat = {
        vk::Format::eB8G8R8A8Srgb,
        vk::ColorSpaceKHR::eSrgbNonlinear
    };
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    //Lets the window be read back. Added only if the surface supports it, and it is what
    //makes a window-level regression test possible at all
    bool allowReadback = true;
    uint32_t preferredImageCount = 0; //0 = use minImageCount + 1 heuristics
};


class VulkanSwapchain{
    public:
    VulkanSwapchain(const VulkanInstance& instance,
         const Window& window, 
         const VulkanDevice& device,
         const SwapchainConfig& config = {});

        

    //getters
    const vk::raii::SwapchainKHR& getSwapchain() const {return swapchain;}
    const std::vector<vk::raii::ImageView>& getImageViews() const {return imageViews;}
    const vk::SurfaceFormatKHR& getSurfaceFormat() const {return surfaceFormat;}
    const vk::Extent2D& getExtent() const {return extent;}

    //STO PROZOR SAD KAZE DA JEST, neovisno o tome koliki je swapchain.
    //
    //Dosad se na promjenu prozora cekalo da Vulkan javi eErrorOutOfDate ili eSuboptimal. To
    //dolazi samo tamo gdje velicinu diktira kompozitor. Gdje je diktira aplikacija ne dolazi
    //nikad, pa bi kadar mirno komitao staru velicinu i nitko ne bi ni primijetio
    vk::Extent2D getWindowExtent() const;

    //TKO OVDJE ODLUCUJE O VELICINI.
    //
    //Povrsina koja vrati currentExtent = 0xFFFFFFFF kaze doslovno "ti odluci" - tada je slika
    //koju predamo ta koja odredjuje velicinu. Povrsina koja vrati broj je vec odlucila, i tada
    //je jedini ispravan potez uzeti taj broj. Ovo se ne mijenja kroz zivot povrsine, pa se
    //cita jednom pri gradnji umjesto svaki kadar
    bool appDecidesExtent() const {return appExtent;}

    //VELICINA KOJU LOOM TRAZI.
    //
    //Vrijedi samo tamo gdje appDecidesExtent(); gdje odlucuje kompozitor, zahtjev se tiho ne
    //postuje - i mora se tiho ne postovati, jer bi swapchain koji se vjecno ne slaze sa svojim
    //ciljem svaki kadar isao u ponovno stvaranje i nijedan ne bi nacrtao
    void requestExtent(vk::Extent2D wanted){requested = wanted;}
    void clearRequestedExtent(){requested.reset();}

    //Kolika bi slika TREBALA biti sad: trazena velicina gdje se smije traziti, inace ona koju
    //je prozor u meduvremenu dobio
    vk::Extent2D desiredExtent() const;

    //Minimiziran prozor (0x0) se broji kao poklapanje: swapchain te velicine ne postoji, pa
    //bi se inace vrtjelo ponovno stvaranje u prazno dok god je prozor spusten
    bool matchesTarget() const;
    const std::vector<vk::Image>& getImages() const {return images;}
    bool canReadback() const {return readbackAvailable;}


    void recreateSwapchain();




    private:
    bool readbackAvailable = false;

    const VulkanInstance& instance;
    const Window& window;
    const VulkanDevice& device;
    SwapchainConfig config;
    vk::raii::SwapchainKHR swapchain = nullptr;

    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::PresentModeKHR presentMode;
    vk::Extent2D extent;
    std::optional<vk::Extent2D> requested;   //vidi requestExtent
    bool appExtent = false;                  //vidi appDecidesExtent
    uint32_t imageCount = 0;
    std::vector<vk::Image>images;
    std::vector<vk::raii::ImageView> imageViews;
    


    void queryCapabilities();
    vk::SurfaceFormatKHR chooseSurfaceFormat();
    vk::PresentModeKHR choosePresentMode();
    vk::Extent2D chooseExtent();
    vk::Extent2D clampToSurface(vk::Extent2D wanted) const;
    uint32_t chooseImageCount();
    void createSwapchain();
    void createImageViews();
    void build();
};
