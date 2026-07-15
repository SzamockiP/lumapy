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

#include "Context.hpp"

enum class ShaderStage {
    VERTEX,
    FRAGMENT
};

class ShaderModule {
public:
    ShaderModule(std::shared_ptr<Context> context, VkShaderModule module, const std::string& path)
        : context_(context), module_(module), path_(path) {}

    ~ShaderModule() {
        if (module_ != VK_NULL_HANDLE && context_) {
            vkDestroyShaderModule(context_->device(), module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    ShaderModule(ShaderModule&& other) noexcept
        : context_(std::move(other.context_)), module_(other.module_), path_(std::move(other.path_)) {
        other.module_ = VK_NULL_HANDLE;
    }

    ShaderModule& operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            if (module_ != VK_NULL_HANDLE && context_) {
                vkDestroyShaderModule(context_->device(), module_, nullptr);
            }
            context_ = std::move(other.context_);
            module_ = other.module_;
            path_ = std::move(other.path_);
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkShaderModule get() const { return module_; }
    const std::string& path() const { return path_; }

private:
    std::shared_ptr<Context> context_;
    VkShaderModule module_;
    std::string path_;
};

class ShaderCompiler {
public:
    static std::expected<std::shared_ptr<ShaderModule>, std::string> compile(Context& context, const std::string& source_path, ShaderStage stage) {
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

        VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = spirv.size() * sizeof(uint32_t),
            .pCode = spirv.data()
        };

        VkShaderModule vk_module;
        if (vkCreateShaderModule(context.device(), &createInfo, nullptr, &vk_module) != VK_SUCCESS) {
            return std::unexpected("Failed to create shader module");
        }

        return std::make_shared<ShaderModule>(context.shared_from_this(), vk_module, source_path);
    }
};
