#include "vulkan_swapchain.hpp"

#include <SDL_vulkan.h>

#include <algorithm>

#include "core/log_assert.hpp"
#include "render/config.hpp"
#include "render/graphics_result.hpp"
#include "render/vulkan/vulkan_buffer.hpp"
#include "render/vulkan/vulkan_image.hpp"

using namespace ez;

VulkanSwapchain::VulkanSwapchain(const VulkanSwapchainCreateInfo& ci,
                                 VulkanSwapchainInfo&& info)
    : info(std::move(info))
    , logicalDevice(ci.logicalDevice)
    , physicalDevice(ci.physicalDevice)
    , surface(ci.surface)
    , window(ci.window)
{
}

ResultValue<SwapChainSupportDetails> VulkanSwapchain::QuerySwapchainSupport(
    vk::PhysicalDevice device, vk::SurfaceKHR surface)
{
    SwapChainSupportDetails details;
    CheckVkResult(device.getSurfaceCapabilitiesKHR(surface, &details.capabilities));

    auto formatsRV = device.getSurfaceFormatsKHR(surface);
    if (formatsRV.result != vk::Result::eSuccess)
    {
        EZLOG("device.getSurfaceFormatsKHR failed");
        return { GraphicsResult::Error, details };
    }

    details.formats = formatsRV.value;

    auto presentModesRV = device.getSurfacePresentModesKHR(surface);
    if (presentModesRV.result != vk::Result::eSuccess)
    {
        EZLOG("device.getSurfacePresentModesKHR failed");
        return { GraphicsResult::Error, details };
    }
    details.presentModes = presentModesRV.value;

    return { GraphicsResult::Ok, details };
}

ResultValue<std::unique_ptr<VulkanSwapchain>> VulkanSwapchain::CreateVulkanSwapchain(
    const VulkanSwapchainCreateInfo& ci)
{
    ResultValue<VulkanSwapchainInfo> vulkanSwapchainInfo = CreateSwapchain(ci);
    if (vulkanSwapchainInfo.result != GraphicsResult::Ok) { return vulkanSwapchainInfo.result; }

    ResultValue<vk::Image> imageRV =
        Image::CreateImage2D(ci.logicalDevice,
                             Config::DepthAttachmentFormat,
                             vk::ImageUsageFlagBits::eDepthStencilAttachment,
                             1,
                             vulkanSwapchainInfo.value.extent.width,
                             vulkanSwapchainInfo.value.extent.height);
    if (imageRV.result != GraphicsResult::Ok) { return imageRV.result; }
    vulkanSwapchainInfo.value.depthImage = imageRV.value;

    vk::MemoryRequirements memReqs{};
    vk::MemoryAllocateInfo memAllocInfo{};
    ci.logicalDevice.getImageMemoryRequirements(vulkanSwapchainInfo.value.depthImage, &memReqs);
    const uint32_t imageLocalMemoryTypeIndex = VulkanBuffer::FindMemoryType(
        ci.physicalDevice, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = imageLocalMemoryTypeIndex;
    CheckVkResult(ci.logicalDevice.allocateMemory(
        &memAllocInfo, nullptr, &vulkanSwapchainInfo.value.depthImageMemory));
    CheckVkResult(ci.logicalDevice.bindImageMemory(
        vulkanSwapchainInfo.value.depthImage, vulkanSwapchainInfo.value.depthImageMemory, 0));

    GraphicsResult imageViewsResult =
        CreateImageViews(ci.logicalDevice, vulkanSwapchainInfo.value);
    if (imageViewsResult != GraphicsResult::Ok) { return imageViewsResult; }

    return { GraphicsResult::Ok,
             std::make_unique<VulkanSwapchain>(ci, std::move(vulkanSwapchainInfo.value)) };
}

ResultValue<VulkanSwapchainInfo> VulkanSwapchain::CreateSwapchain(
    const VulkanSwapchainCreateInfo& ci)
{
    VulkanSwapchainInfo info = {};
    ResultValue<SwapChainSupportDetails> swapchainSupportResult =
        QuerySwapchainSupport(ci.physicalDevice, ci.surface);
    if (swapchainSupportResult.result != GraphicsResult::Ok)
    {
        return swapchainSupportResult.result;
    }
    SwapChainSupportDetails swapchainSupport = std::move(swapchainSupportResult.value);

    vk::SurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
    vk::PresentModeKHR presentMode = ChooseSwapPresentMode(swapchainSupport.presentModes);
    vk::Extent2D extent = ChooseSwapExtent(ci.window, swapchainSupport.capabilities);

    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapchainSupport.capabilities.maxImageCount)
    {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo = {};
    createInfo.surface = ci.surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = { ci.queueFamilyIndices.graphicsFamily,
                                      ci.queueFamilyIndices.presentFamily };

    if (ci.queueFamilyIndices.graphicsFamily != ci.queueFamilyIndices.presentFamily)
    {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (ci.logicalDevice.createSwapchainKHR(&createInfo, nullptr, &info.swapchain) !=
        vk::Result::eSuccess)
    {
        EZLOG("Failed to create swapchain!");
        return GraphicsResult::Error;
    }

    vkGetSwapchainImagesKHR(ci.logicalDevice.operator VkDevice(),
                            info.swapchain.operator VkSwapchainKHR(),
                            &imageCount,
                            nullptr);
    info.images.resize(imageCount);
    CheckVkResult(ci.logicalDevice.getSwapchainImagesKHR(
        info.swapchain, &imageCount, info.images.data()));

    info.imageFormat = surfaceFormat.format;
    info.extent = extent;

    return { GraphicsResult::Ok, info };
}

vk::SurfaceFormatKHR VulkanSwapchain::ChooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
    if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
    {
        return { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };
    }

    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == vk::Format::eB8G8R8A8Unorm &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

vk::PresentModeKHR VulkanSwapchain::ChooseSwapPresentMode(
    const std::vector<vk::PresentModeKHR>& availablePresentModes)
{
    vk::PresentModeKHR bestMode = vk::PresentModeKHR::eFifo;

    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox)
        {
            return availablePresentMode;
        }
        else if (availablePresentMode == vk::PresentModeKHR::eImmediate)
        {
            bestMode = availablePresentMode;
        }
    }

    return bestMode;
}

vk::Extent2D VulkanSwapchain::ChooseSwapExtent(SDL_Window* window,
                                               const vk::SurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width, height;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    vk::Extent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

    actualExtent.width =
        std::max(capabilities.minImageExtent.width,
                 std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height =
        std::max(capabilities.minImageExtent.height,
                 std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

GraphicsResult VulkanSwapchain::CreateImageViews(vk::Device logicalDevice,
                                                 VulkanSwapchainInfo& info)
{
    info.imageViews.resize(info.images.size());

    for (size_t i = 0; i < info.images.size(); i++)
    {
        ResultValue<vk::ImageView> imageViewRV =
            Image::CreateImageView2D(logicalDevice,
                                     info.images[i],
                                     info.imageFormat,
                                     vk::ImageAspectFlagBits::eColor,
                                     1);
        if (imageViewRV.result != GraphicsResult::Ok) { return imageViewRV.result; }
        info.imageViews[i] = imageViewRV.value;
    }

    ResultValue<vk::ImageView> depthImageViewRV =
        Image::CreateImageView2D(logicalDevice,
                                 info.depthImage,
                                 Config::DepthAttachmentFormat,
                                 vk::ImageAspectFlagBits::eDepth,
                                 1);
    if (depthImageViewRV.result != GraphicsResult::Ok) { return depthImageViewRV.result; }
    info.depthImageView = depthImageViewRV.value;

    return GraphicsResult::Ok;
}

GraphicsResult VulkanSwapchain::CreateFramebuffersForRenderPass(vk::RenderPass vkRenderPass)
{
    info.framebuffers.resize(info.imageViews.size());

    for (size_t i = 0; i < info.imageViews.size(); i++)
    {
        vk::ImageView attachments[] = { info.imageViews[i], info.depthImageView };

        vk::FramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.renderPass = vkRenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = info.extent.width;
        framebufferInfo.height = info.extent.height;
        framebufferInfo.layers = 1;

        if (logicalDevice.createFramebuffer(&framebufferInfo, nullptr, &info.framebuffers[i]) !=
            vk::Result::eSuccess)
        {
            EZLOG("Failed to create framebuffer!");
            return GraphicsResult::Error;
        }
    }
    return GraphicsResult::Ok;
}

VulkanSwapchain::~VulkanSwapchain()
{
    for (size_t i = 0; i < info.framebuffers.size(); i++)
    {
        logicalDevice.destroyFramebuffer(info.framebuffers[i], nullptr);
    }

    for (size_t i = 0; i < info.imageViews.size(); i++)
    {
        logicalDevice.destroyImageView(info.imageViews[i], nullptr);
    }
    logicalDevice.destroyImageView(info.depthImageView, nullptr);

    logicalDevice.destroySwapchainKHR(info.swapchain);
    info = {};
}
