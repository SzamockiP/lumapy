#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>
#include <expected>
#include <cstring>

#include "stb_image.h"
#include "Context.hpp"

class Texture {
public:
    Texture(std::shared_ptr<Context> context, VkImage image, VmaAllocation allocation,
            VkImageView imageView, VkSampler sampler, int width, int height)
        : context_(context), image_(image), allocation_(allocation),
          image_view_(imageView), sampler_(sampler), width_(width), height_(height) {}

    ~Texture() {
        if (context_) {
            if (sampler_ != VK_NULL_HANDLE) {
                vkDestroySampler(context_->device(), sampler_, nullptr);
            }
            if (image_view_ != VK_NULL_HANDLE) {
                vkDestroyImageView(context_->device(), image_view_, nullptr);
            }
            if (image_ != VK_NULL_HANDLE && allocation_ != VK_NULL_HANDLE) {
                vmaDestroyImage(context_->allocator(), image_, allocation_);
            }
        }
    }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView image_view() const { return image_view_; }
    VkSampler sampler() const { return sampler_; }
    int width() const { return width_; }
    int height() const { return height_; }

    static std::expected<std::shared_ptr<Texture>, Error> create(Context& context, const std::string& path) {
        // Load image from disk
        int width, height, channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            // stb knows whether this was a missing file or a corrupt one; saying
            // only "Failed to load" throws that away.
            const char* reason = stbi_failure_reason();
            return std::unexpected(err_resource("Failed to load texture: " + path +
                                                (reason ? std::string(" (") + reason + ")" : "")));
        }

        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        // Create staging buffer
        VkBufferCreateInfo stagingInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = imageSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;

        if (auto e = check(vmaCreateBuffer(context.allocator(), &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr),
                           "create staging buffer for texture", ErrorCode::Resource)) {
            stbi_image_free(pixels);
            return std::unexpected(*e);
        }

        void* mappedData;
        vmaMapMemory(context.allocator(), stagingAllocation, &mappedData);
        std::memcpy(mappedData, pixels, static_cast<size_t>(imageSize));
        vmaUnmapMemory(context.allocator(), stagingAllocation);

        stbi_image_free(pixels);

        // Create VkImage
        VkImageCreateInfo imageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        VmaAllocationCreateInfo imageAllocInfo{};
        imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage image;
        VmaAllocation allocation;
        if (auto e = check(vmaCreateImage(context.allocator(), &imageCreateInfo, &imageAllocInfo, &image, &allocation, nullptr),
                           "create texture image", ErrorCode::Resource)) {
            vmaDestroyBuffer(context.allocator(), stagingBuffer, stagingAllocation);
            return std::unexpected(*e);
        }

        // Record layout transitions + copy in a one-shot command buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = context.command_pool(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(context.device(), &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
        transition_image_layout(cmd, image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy staging buffer → image
        VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1}
        };

        vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
        transition_image_layout(cmd, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };

        vkQueueSubmit(context.graphics_queue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(context.graphics_queue());

        vkFreeCommandBuffers(context.device(), context.command_pool(), 1, &cmd);
        vmaDestroyBuffer(context.allocator(), stagingBuffer, stagingAllocation);

        // Create ImageView
        VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VkImageView imageView;
        if (auto e = check(vkCreateImageView(context.device(), &viewInfo, nullptr, &imageView),
                           "create texture image view", ErrorCode::Resource)) {
            vmaDestroyImage(context.allocator(), image, allocation);
            return std::unexpected(*e);
        }

        // Create Sampler
        VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = 16.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE
        };

        VkSampler sampler;
        if (auto e = check(vkCreateSampler(context.device(), &samplerInfo, nullptr, &sampler),
                           "create texture sampler", ErrorCode::Resource)) {
            vkDestroyImageView(context.device(), imageView, nullptr);
            vmaDestroyImage(context.allocator(), image, allocation);
            return std::unexpected(*e);
        }

        return std::make_shared<Texture>(context.shared_from_this(), image, allocation, imageView, sampler, width, height);
    }

private:
    static void transition_image_layout(VkCommandBuffer cmd, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = srcAccess,
            .dstAccessMask = dstAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
    }

    std::shared_ptr<Context> context_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
};
