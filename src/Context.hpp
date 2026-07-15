#pragma once
#include <volk.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <expected>
#include <memory>
#include <string>

#include "Logger.hpp"

class Context : public std::enable_shared_from_this<Context>
{
public:
	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

	static std::expected<std::shared_ptr<Context>, std::string> create(std::shared_ptr<Logger> logger)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			return std::unexpected("Vulkan: Failed to initialize volk");
		}

		auto context = std::shared_ptr<Context>(new Context(logger));

		// Instance + Debug Messenger
		auto inst_builder = vkb::InstanceBuilder{}
			.set_app_name("Bazalt Engine")
			.set_app_version(1, 0, 0)
			.require_api_version(1, 3, 0);

		if (logger)
		{
			inst_builder.request_validation_layers()
				.set_debug_callback(debug_callback)
				.set_debug_callback_user_data_pointer(logger.get())
				.set_debug_messenger_severity(
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				.set_debug_messenger_type(
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
		}

		// Enable surface extensions for the current platform.
		// These are harmless when not used (headless/compute) and required
		// for SwapchainRenderer to create a surface later.
		inst_builder.enable_extension("VK_KHR_surface");
#ifdef _WIN32
		inst_builder.enable_extension("VK_KHR_win32_surface");
#endif

		auto inst_ret = inst_builder.build();

		if (!inst_ret)
		{
			return std::unexpected("Vulkan: " + inst_ret.error().message());
		}
		context->vkb_instance_ = inst_ret.value();
		volkLoadInstance(context->vkb_instance_.instance);

		// Physical device selection — without surface (headless-compatible).
		// Present support is verified by SwapchainRenderer at creation time.
		VkPhysicalDeviceFeatures device_features{
			.samplerAnisotropy = VK_TRUE
		};

		auto phys_ret = vkb::PhysicalDeviceSelector{ context->vkb_instance_ }
			.set_minimum_version(1, 3)
			.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
			.add_required_extension("VK_KHR_swapchain")
			.set_required_features(device_features)
			.require_present(false)
			.select();

		if (!phys_ret)
		{
			return std::unexpected("Vulkan: " + phys_ret.error().message());
		}
		context->vkb_physical_device_ = phys_ret.value();

		// Logical Device + Queues
		VkPhysicalDeviceVulkan13Features features13{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.pNext = nullptr,
			.shaderDemoteToHelperInvocation = VK_TRUE,
			.dynamicRendering = VK_TRUE
		};

		auto dev_ret = vkb::DeviceBuilder{ context->vkb_physical_device_ }
			.add_pNext(&features13)
			.build();

		if (!dev_ret)
		{
			return std::unexpected("Vulkan: " + dev_ret.error().message());
		}
		context->vkb_device_ = dev_ret.value();
		volkLoadDevice(context->vkb_device_.device);

		auto gq = context->vkb_device_.get_queue(vkb::QueueType::graphics);
		if (!gq)
		{
			return std::unexpected("Vulkan: Failed to get graphics queue");
		}
		context->graphics_queue_ = gq.value();
		context->graphics_queue_family_ =
			context->vkb_device_.get_queue_index(vkb::QueueType::graphics).value();

		// VMA Allocator
		VmaVulkanFunctions vulkanFunctions = {};
		vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = context->vkb_physical_device_.physical_device;
		allocatorInfo.device = context->vkb_device_.device;
		allocatorInfo.instance = context->vkb_instance_.instance;
		allocatorInfo.pVulkanFunctions = &vulkanFunctions;
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

		if (vmaCreateAllocator(&allocatorInfo, &context->allocator_) != VK_SUCCESS)
		{
			return std::unexpected("Vulkan: Failed to create VMA allocator");
		}

		// Command Pool
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = context->graphics_queue_family_
		};

		if (vkCreateCommandPool(context->vkb_device_.device, &poolInfo, nullptr, &context->command_pool_) != VK_SUCCESS)
		{
			return std::unexpected("Vulkan: Failed to create command pool");
		}

		if (logger)
		{
			logger->log("Vulkan: Initialized (" +
				std::string(context->vkb_physical_device_.properties.deviceName) + ")");
		}

		return context;
	}

	~Context()
	{
		if (vkb_device_.device)
		{
			vkDeviceWaitIdle(vkb_device_.device);
		}

		if (command_pool_)
		{
			vkDestroyCommandPool(vkb_device_.device, command_pool_, nullptr);
		}

		if (allocator_)
		{
			vmaDestroyAllocator(allocator_);
		}

		vkb::destroy_device(vkb_device_);
		vkb::destroy_instance(vkb_instance_);
	}

	Context(const Context&) = delete;
	Context& operator=(const Context&) = delete;

	VkInstance instance() const { return vkb_instance_.instance; }
	VkDevice device() const { return vkb_device_.device; }
	VkPhysicalDevice physical_device() const { return vkb_physical_device_.physical_device; }
	VkQueue graphics_queue() const { return graphics_queue_; }
	std::uint32_t graphics_queue_family() const { return graphics_queue_family_; }
	VmaAllocator allocator() const { return allocator_; }
	VkCommandPool command_pool() const { return command_pool_; }
	std::shared_ptr<Logger> logger() const { return logger_; }

	// Internal — for use by renderers and other subsystems
	const vkb::Instance& vkb_instance() const { return vkb_instance_; }
	const vkb::Device& vkb_device() const { return vkb_device_; }

	// Frame tracking — updated by the active renderer
	std::uint32_t current_frame() const { return current_frame_; }
	void set_current_frame(std::uint32_t frame) { current_frame_ = frame; }

private:
	Context(std::shared_ptr<Logger> logger) : logger_(logger) {}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data)
	{
		Logger* logger = static_cast<Logger*>(user_data);
		if (logger)
		{
			std::string prefix = "[Vulkan Validation] ";
			if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				prefix += "ERROR: ";
			}
			else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				prefix += "WARNING: ";
			}

			logger->log(prefix + callback_data->pMessage);
		}
		return VK_FALSE;
	}

	std::shared_ptr<Logger> logger_;

	vkb::Instance vkb_instance_;
	vkb::PhysicalDevice vkb_physical_device_;
	vkb::Device vkb_device_;

	VkQueue graphics_queue_ = VK_NULL_HANDLE;
	std::uint32_t graphics_queue_family_ = 0;

	VmaAllocator allocator_ = VK_NULL_HANDLE;
	VkCommandPool command_pool_ = VK_NULL_HANDLE;

	std::uint32_t current_frame_ = 0;
};
