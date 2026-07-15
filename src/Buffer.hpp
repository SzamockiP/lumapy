#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>
#include <span>
#include <stdexcept>
#include <memory>
#include <expected>
#include <cstring>
#include <array>
#include "Context.hpp"

enum class BufferType {
    VERTEX,
    INDEX,
    UNIFORM,
    STORAGE
};

enum class DataType {
    FLOAT,
    UINT32,
    UINT16,
    INT32
};

enum class MemoryUsage {
    STATIC,
    DYNAMIC
};

class Buffer {
public:
    virtual ~Buffer() = default;

    virtual VkBuffer get() const = 0;
    virtual size_t size() const = 0;
    virtual bool is_dynamic() const { return false; }
    virtual VkBuffer get_for_frame(uint32_t /*frame*/) const { return get(); }
    
    virtual void update(const void* data, size_t size) {
        throw std::runtime_error("Update not supported for this buffer type");
    }

    static std::expected<std::shared_ptr<Buffer>, std::string> create(Context& context, const void* data, size_t data_size, BufferType type, MemoryUsage usage);
};

class StaticBuffer : public Buffer {
public:
    StaticBuffer(std::shared_ptr<Context> context, VkBuffer buffer, VmaAllocation allocation, size_t size)
        : context_(context), buffer_(buffer), allocation_(allocation), size_(size) {}

    ~StaticBuffer() override {
        if (buffer_ != VK_NULL_HANDLE && context_) {
            vmaDestroyBuffer(context_->allocator(), buffer_, allocation_);
        }
    }

    StaticBuffer(const StaticBuffer&) = delete;
    StaticBuffer& operator=(const StaticBuffer&) = delete;

    VkBuffer get() const override { return buffer_; }
    size_t size() const override { return size_; }

    static std::expected<std::shared_ptr<StaticBuffer>, std::string> create(Context& context, const void* data, size_t data_size, BufferType type) {
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        switch (type) {
            case BufferType::VERTEX: usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
            case BufferType::INDEX: usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
            case BufferType::STORAGE: usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
            default: break;
        }

        VkBufferCreateInfo stagingInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = data_size,
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
        VmaAllocationInfo stagingAllocInfoOut;

        if (vmaCreateBuffer(context.allocator(), &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfoOut) != VK_SUCCESS) {
            return std::unexpected("Failed to create staging buffer");
        }

        if (data != nullptr && data_size > 0) {
            void* mappedData;
            vmaMapMemory(context.allocator(), stagingAllocation, &mappedData);
            std::memcpy(mappedData, data, data_size);
            vmaUnmapMemory(context.allocator(), stagingAllocation);
        }

        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = data_size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkBuffer buffer;
        VmaAllocation allocation;
        if (vmaCreateBuffer(context.allocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(context.allocator(), stagingBuffer, stagingAllocation);
            return std::unexpected("Failed to create device local buffer");
        }

        VkCommandBufferAllocateInfo allocCmdInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = context.command_pool(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(context.device(), &allocCmdInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy copyRegion{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = data_size
        };
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &copyRegion);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };

        vkQueueSubmit(context.graphics_queue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(context.graphics_queue());

        vkFreeCommandBuffers(context.device(), context.command_pool(), 1, &commandBuffer);
        vmaDestroyBuffer(context.allocator(), stagingBuffer, stagingAllocation);

        return std::make_shared<StaticBuffer>(context.shared_from_this(), buffer, allocation, data_size);
    }

private:
    std::shared_ptr<Context> context_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    size_t size_ = 0;
};

class DynamicBuffer : public Buffer {
public:
    DynamicBuffer(std::shared_ptr<Context> context, 
                  std::array<VkBuffer, Context::MAX_FRAMES_IN_FLIGHT> buffers, 
                  std::array<VmaAllocation, Context::MAX_FRAMES_IN_FLIGHT> allocations, 
                  size_t size,
                  BufferType type)
        : context_(context), buffers_(buffers), allocations_(allocations), size_(size), type_(type) {}

    ~DynamicBuffer() override {
        if (context_) {
            for (size_t i = 0; i < Context::MAX_FRAMES_IN_FLIGHT; ++i) {
                if (buffers_[i] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(context_->allocator(), buffers_[i], allocations_[i]);
                }
            }
        }
    }

    DynamicBuffer(const DynamicBuffer&) = delete;
    DynamicBuffer& operator=(const DynamicBuffer&) = delete;

    VkBuffer get() const override { 
        return buffers_[context_->current_frame()]; 
    }
    
    size_t size() const override { return size_; }
    bool is_dynamic() const override { return true; }
    VkBuffer get_for_frame(uint32_t frame) const override { return buffers_[frame]; }
    BufferType buffer_type() const { return type_; }

    void update(const void* data, size_t size) override {
        if (size > size_) {
            throw std::runtime_error("Update size exceeds buffer size");
        }
        uint32_t frame = context_->current_frame();
        void* mappedData;
        if (vmaMapMemory(context_->allocator(), allocations_[frame], &mappedData) == VK_SUCCESS) {
            std::memcpy(mappedData, data, size);
            vmaUnmapMemory(context_->allocator(), allocations_[frame]);
        } else {
            throw std::runtime_error("Failed to map memory for buffer update");
        }
    }

    static std::expected<std::shared_ptr<DynamicBuffer>, std::string> create(Context& context, const void* data, size_t data_size, BufferType type) {
        VkBufferUsageFlags usage = (type == BufferType::STORAGE) 
            ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
            : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = data_size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        std::array<VkBuffer, Context::MAX_FRAMES_IN_FLIGHT> buffers{};
        std::array<VmaAllocation, Context::MAX_FRAMES_IN_FLIGHT> allocations{};

        for (size_t i = 0; i < Context::MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vmaCreateBuffer(context.allocator(), &bufferInfo, &allocInfo, &buffers[i], &allocations[i], nullptr) != VK_SUCCESS) {
                for (size_t j = 0; j < i; ++j) {
                    vmaDestroyBuffer(context.allocator(), buffers[j], allocations[j]);
                }
                return std::unexpected(std::string("Failed to create ") + (type == BufferType::STORAGE ? "storage" : "uniform") + " buffer");
            }

            if (data != nullptr && data_size > 0) {
                void* mappedData;
                vmaMapMemory(context.allocator(), allocations[i], &mappedData);
                std::memcpy(mappedData, data, data_size);
                vmaUnmapMemory(context.allocator(), allocations[i]);
            }
        }
        return std::make_shared<DynamicBuffer>(context.shared_from_this(), buffers, allocations, data_size, type);
    }

private:
    std::shared_ptr<Context> context_;
    std::array<VkBuffer, Context::MAX_FRAMES_IN_FLIGHT> buffers_ = {VK_NULL_HANDLE};
    std::array<VmaAllocation, Context::MAX_FRAMES_IN_FLIGHT> allocations_ = {VK_NULL_HANDLE};
    size_t size_ = 0;
    BufferType type_ = BufferType::UNIFORM;
};

// Keep backward-compatible alias
using UniformBuffer = DynamicBuffer;

inline std::expected<std::shared_ptr<Buffer>, std::string> Buffer::create(Context& context, const void* data, size_t data_size, BufferType type, MemoryUsage usage) {
    if (usage == MemoryUsage::DYNAMIC) {
        return DynamicBuffer::create(context, data, data_size, type);
    } else {
        return StaticBuffer::create(context, data, data_size, type);
    }
}
