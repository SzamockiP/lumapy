#pragma once
#include <volk.h>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <expected>
#include <print>

enum class ShaderStage {
    VERTEX,
    FRAGMENT
};

class ShaderModule {
public:
    ShaderModule(VkDevice device, VkShaderModule module, const std::string& path)
        : device_(device), module_(module), path_(path) {}

    ~ShaderModule() {
        if (module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    ShaderModule(ShaderModule&& other) noexcept
        : device_(other.device_), module_(other.module_), path_(std::move(other.path_)) {
        other.module_ = VK_NULL_HANDLE;
    }

    ShaderModule& operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            if (module_ != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, module_, nullptr);
            }
            device_ = other.device_;
            module_ = other.module_;
            path_ = std::move(other.path_);
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkShaderModule get() const { return module_; }
    const std::string& path() const { return path_; }

private:
    VkDevice device_;
    VkShaderModule module_;
    std::string path_;
};

class ShaderCompiler {
public:
    static std::expected<std::shared_ptr<ShaderModule>, std::string> compile(VkDevice device, const std::string& source_path, ShaderStage stage) {
        std::ifstream file(source_path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return std::unexpected("Failed to open shader file: " + source_path);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        std::string source(buffer.begin(), buffer.end());

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc_shader_kind kind = stage == ShaderStage::VERTEX ? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader;
        
        shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, kind, source_path.c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
            return std::unexpected("Shader compilation failed: " + module.GetErrorMessage());
        }

        std::vector<uint32_t> spirv(module.cbegin(), module.cend());

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule vk_module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &vk_module) != VK_SUCCESS) {
            return std::unexpected("Failed to create shader module");
        }

        return std::make_shared<ShaderModule>(device, vk_module, source_path);
    }
};
