#pragma once
#include <volk.h>
#include <ranges>
#include <algorithm>
#include <span>
#include <expected>
#include <memory>

#include "Logger.hpp"
#include "window.hpp"

class Renderer
{
private:
	Renderer(Logger& logger, Window& window, VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger) :
		logger_(logger), window_(window), instance_(instance), debug_messenger_(debug_messenger)
	{
		logger_.log("Vulkan: Initialized");
	}

public:
	static std::expected<std::unique_ptr<Renderer>, std::string> create(Logger& logger, Window& window)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			return std::unexpected("Vulkan: Failed to initialize volk");
		}

		auto instance_res = create_instance();
		if (!instance_res)
		{
			return std::unexpected(instance_res.error());
		}
		VkInstance instance = *instance_res;

		volkLoadInstance(instance);

		auto messenger_res = setup_debug_messenger(instance, logger);
		if (!messenger_res)
		{
			vkDestroyInstance(instance, nullptr);
			return std::unexpected(messenger_res.error());
		}
		VkDebugUtilsMessengerEXT debug_messenger = *messenger_res;

		return std::unique_ptr<Renderer>(new Renderer(logger, window, instance, debug_messenger));
	}

	~Renderer()
	{
		if (debug_messenger_)
		{
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr)
			{
				func(instance_, debug_messenger_, nullptr);
			}
		}

		if (instance_)
		{
			vkDestroyInstance(instance_, nullptr);
		}
	}

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	static std::expected<VkInstance, std::string> create_instance()
	{
		if (!check_validation_layer_support())
		{
			return std::unexpected("Vulkan: Required validation layers are not available");
		}

		VkApplicationInfo application_info{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "LumaPy Engine",
			.applicationVersion = VK_MAKE_VERSION(1,0,0),
			.pEngineName = "No Engine",
			.engineVersion = VK_MAKE_VERSION(1,0,0),
			.apiVersion = VK_API_VERSION_1_3
		};

		auto extensions = get_required_extensions();

		VkInstanceCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &application_info,
			.enabledLayerCount = static_cast<std::uint32_t>(validation_layers_.size()),
			.ppEnabledLayerNames = validation_layers_.data(),
			.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
			.ppEnabledExtensionNames = extensions.data(),
		};

		VkInstance instance = VK_NULL_HANDLE;
		if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS)
		{
			return std::unexpected("Vulkan: Failed to create an instance");
		}
		return instance;
	}

	static std::vector<const char*> get_required_extensions()
	{
		std::uint32_t glfw_extension_count{ 0 };
		const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

		std::vector<const char*> extensions(
			std::span(glfw_extensions, glfw_extension_count).begin(),
			std::span(glfw_extensions, glfw_extension_count).end()
		);

		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		return extensions;
	}

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

	static bool check_validation_layer_support()
	{
		std::uint32_t layer_count;
		vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
		std::vector<VkLayerProperties> available_layers(layer_count);
		vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

		auto available_names = available_layers
			| std::views::transform([](const VkLayerProperties& prop)
				{
					return std::string_view(prop.layerName);
				}
			);

		return std::ranges::all_of(validation_layers_, [&available_names](std::string_view required_layer)
			{
				return std::ranges::contains(available_names, std::string_view(required_layer));
			}
		);
	}

	static std::expected<VkDebugUtilsMessengerEXT, std::string> setup_debug_messenger(VkInstance instance, Logger& logger)
	{
		VkDebugUtilsMessengerCreateInfoEXT create_info
		{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = debug_callback,
			.pUserData = &logger
		};
		
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func != nullptr)
		{
			VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
			if (func(instance, &create_info, nullptr, &debug_messenger) != VK_SUCCESS)
			{
				return std::unexpected("Vulkan: Failed to connect debug messenger");
			}
			return debug_messenger;
		}
		return VK_NULL_HANDLE;
	}

private:
	Logger& logger_;
	Window& window_;
	VkInstance instance_ = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

	static inline const std::vector<const char*> validation_layers_ = {
		"VK_LAYER_KHRONOS_validation"
	};
};