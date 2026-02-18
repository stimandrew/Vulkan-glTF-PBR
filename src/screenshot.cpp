#include "screenshot.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sstream>

// Для сохранения в PNG можно использовать stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Screenshot::Screenshot(vks::VulkanDevice* vulkanDevice, VkQueue transferQueue, VkCommandPool cmdPool)
    : device(vulkanDevice), queue(transferQueue), commandPool(cmdPool) {
}

Screenshot::~Screenshot() {
    cleanup();
}

void Screenshot::createStagingBuffer(uint32_t width, uint32_t height) {
    VkDeviceSize bufferSize = width * height * 4; // RGBA

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &context.buffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->logicalDevice, context.buffer, &memReqs);

    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
                                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &context.memory));
    VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, context.buffer, context.memory, 0));

    context.width = width;
    context.height = height;
    context.pixels.resize(bufferSize);
}

void Screenshot::createCommandBuffer() {
    VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
    cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool = commandPool;
    cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount = 1;

    VK_CHECK_RESULT(vkAllocateCommandBuffers(device->logicalDevice, &cmdBufAllocateInfo, &context.commandBuffer));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK_RESULT(vkCreateFence(device->logicalDevice, &fenceInfo, nullptr, &context.fence));
}

void Screenshot::copyImageToBuffer(VkImage srcImage, uint32_t width, uint32_t height) {
    VkCommandBufferBeginInfo cmdBufBeginInfo{};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT(vkBeginCommandBuffer(context.commandBuffer, &cmdBufBeginInfo));

    // Переводим изображение в правильный layout для чтения
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageMemoryBarrier.image = srcImage;
    imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageMemoryBarrier.subresourceRange.levelCount = 1;
    imageMemoryBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(context.commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &imageMemoryBarrier);

    // Копируем изображение в буфер
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyImageToBuffer(context.commandBuffer, srcImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, context.buffer, 1, &region);

    // Возвращаем изображение в исходный layout
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(context.commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &imageMemoryBarrier);

    VK_CHECK_RESULT(vkEndCommandBuffer(context.commandBuffer));
}

bool Screenshot::capture(VkImage srcImage, uint32_t width, uint32_t height, VkImageLayout currentLayout) {
    if (context.isComplete) {
        cleanup();
    }

    createStagingBuffer(width, height);
    createCommandBuffer();
    copyImageToBuffer(srcImage, width, height);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context.commandBuffer;

    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, context.fence));

    context.isComplete = true;
    return true;
}

bool Screenshot::isComplete() {
    if (!context.isComplete) return false;

    VkResult result = vkGetFenceStatus(device->logicalDevice, context.fence);
    if (result == VK_SUCCESS) {
        // Копируем данные из буфера
        void* mappedData;
        VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, context.memory, 0,
                                    VK_WHOLE_SIZE, 0, &mappedData));
        memcpy(context.pixels.data(), mappedData, context.pixels.size());
        vkUnmapMemory(device->logicalDevice, context.memory);
        return true;
    }
    return false;
}

std::vector<uint8_t> Screenshot::getPixels() {
    return context.pixels;
}

void Screenshot::saveToFile(const std::string& filename) {
    if (!isComplete()) {
        std::cerr << "Screenshot not ready!" << std::endl;
        return;
    }

    // Сохраняем как PNG
    std::string pngFilename = filename + ".png";
    int result = stbi_write_png(pngFilename.c_str(), context.width, context.height, 4,
                                context.pixels.data(), context.width * 4);

    if (result) {
        std::cout << "Screenshot saved to " << pngFilename
                  << " (" << context.width << "x" << context.height << ")" << std::endl;
    } else {
        std::cerr << "Failed to save screenshot!" << std::endl;
    }
}

void Screenshot::cleanup() {
    if (context.fence != VK_NULL_HANDLE) {
        vkWaitForFences(device->logicalDevice, 1, &context.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device->logicalDevice, context.fence, nullptr);
        context.fence = VK_NULL_HANDLE;
    }

    if (context.commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device->logicalDevice, commandPool, 1, &context.commandBuffer);
        context.commandBuffer = VK_NULL_HANDLE;
    }

    if (context.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->logicalDevice, context.buffer, nullptr);
        context.buffer = VK_NULL_HANDLE;
    }

    if (context.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device->logicalDevice, context.memory, nullptr);
        context.memory = VK_NULL_HANDLE;
    }

    context.isComplete = false;
    context.pixels.clear();
}
