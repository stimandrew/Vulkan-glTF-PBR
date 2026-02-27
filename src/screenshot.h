#pragma once

#include <vector>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstring>  // Добавлено для memcpy
#include "vulkan/vulkan.h"
#include "VulkanDevice.hpp"
#include "macros.h"

#include "stb_image_write.h"

class Screenshot {
private:
    struct ScreenshotContext {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        bool isComplete = false;
        std::vector<uint8_t> pixels;
    } context;

    vks::VulkanDevice* device;
    VkQueue queue;
    VkCommandPool commandPool;

    void createStagingBuffer(uint32_t width, uint32_t height);
    void createCommandBuffer();
    void copyImageToBuffer(VkImage srcImage, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void savePPM(const std::string& filename);
    void savePNG(const std::string& filename);

public:
    Screenshot(vks::VulkanDevice* vulkanDevice, VkQueue transferQueue, VkCommandPool cmdPool);
    ~Screenshot();

    bool capture(VkImage srcImage, uint32_t x, uint32_t y, uint32_t width, uint32_t height, VkImageLayout currentLayout);
    bool isComplete();
    std::vector<uint8_t> getPixels();
    void saveToFile(const std::string& filename);
    void cleanup();
};
