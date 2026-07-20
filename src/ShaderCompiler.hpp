#pragma once
#include <volk.h>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <expected>
#include <optional>
#include <filesystem>
#include <cctype>

#include "Context.hpp"
#include "Error.hpp"

enum class ShaderStage {
    VERTEX,
    FRAGMENT,
    COMPUTE
};

// A real switch, deliberately with no default case: adding a new stage makes
// every conversion site a compiler error instead of silently aliasing the new
// stage onto FRAGMENT, which is what the old `stage == VERTEX ? ... : ...`
// ternaries scattered across Pipeline.hpp and CommandBuffer.hpp would have done.
inline constexpr VkShaderStageFlagBits to_vk(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX:   return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::COMPUTE:  return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // Not std::unreachable(): pybind enums accept arbitrary ints, so a forged
    // ShaderStage from Python must degrade gracefully, not invoke UB.
    return VK_SHADER_STAGE_VERTEX_BIT;
}

inline constexpr shaderc_shader_kind to_shaderc_kind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX:   return shaderc_glsl_vertex_shader;
        case ShaderStage::FRAGMENT: return shaderc_glsl_fragment_shader;
        case ShaderStage::COMPUTE:  return shaderc_glsl_compute_shader;
    }
    return shaderc_glsl_vertex_shader;
}

class ShaderModule {
public:
    ShaderModule(std::shared_ptr<Context> context, VkShaderModule module, const std::string& path,
                 std::vector<std::string> includes, std::vector<uint32_t> spirv)
        : context_(context), module_(module), path_(path),
          includes_(std::move(includes)), spirv_(std::move(spirv)) {}

    ~ShaderModule() {
        destroy();
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    ShaderModule(ShaderModule&& other) noexcept
        : context_(std::move(other.context_)), module_(other.module_), path_(std::move(other.path_)),
          includes_(std::move(other.includes_)), spirv_(std::move(other.spirv_)) {
        other.module_ = VK_NULL_HANDLE;
    }

    ShaderModule& operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            destroy();
            context_ = std::move(other.context_);
            module_ = other.module_;
            path_ = std::move(other.path_);
            includes_ = std::move(other.includes_);
            spirv_ = std::move(other.spirv_);
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkShaderModule get() const { return module_; }
    const std::string& path() const { return path_; }
    // Files pulled in via #include, absolute and normalized. The 0.8 hot-reload
    // watcher watches path() plus these; empty for .spv and include-free sources.
    const std::vector<std::string>& includes() const { return includes_; }
    const std::vector<uint32_t>& spirv() const { return spirv_; }

private:
    void destroy() {
        if (module_ != VK_NULL_HANDLE && context_) {
            vkDestroyShaderModule(context_->device(), module_, nullptr);
        }
    }

    std::shared_ptr<Context> context_;
    VkShaderModule module_;
    std::string path_;
    std::vector<std::string> includes_;
    std::vector<uint32_t> spirv_;
};

class ShaderCompiler {
public:
    // One entry point for every shader form. The extension of `path` decides how
    // it is handled (GLSL by default); when `source` is given the file is never
    // opened and `path` is a virtual name — it still supplies the language, the
    // diagnostic tag, ShaderError.path, and the base directory for #include.
    static std::expected<std::shared_ptr<ShaderModule>, Error> compile(
            Context& context, const std::string& path, ShaderStage stage,
            std::optional<std::string> source = std::nullopt) {
        return compile_text(context, path, stage, std::move(source));
    }

private:
    static std::string lowercase_extension(const std::string& path) {
        std::string ext = std::filesystem::path(path).extension().string();
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    static std::expected<std::shared_ptr<ShaderModule>, Error> compile_text(
            Context& context, const std::string& path, ShaderStage stage,
            std::optional<std::string> source) {
        std::string text;
        if (source) {
            text = std::move(*source);
        } else {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                return std::unexpected(err_resource("Failed to open shader file: " + path));
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            text.resize(fileSize);
            file.seekg(0);
            file.read(text.data(), fileSize);
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        // Follow the negotiated device version rather than assuming 1.3: SPIR-V
        // targeted at 1.3 can be rejected by a 1.2 driver.
        shaderc_env_version env_version =
            VK_API_VERSION_MINOR(context.api_version()) >= 3
                ? shaderc_env_version_vulkan_1_3
                : shaderc_env_version_vulkan_1_2;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, env_version);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc_shader_kind kind = to_shaderc_kind(stage);

        shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(text, kind, path.c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::string log = module.GetErrorMessage();
            return std::unexpected(err_shader("Shader compilation failed: " + log,
                                              path, parse_error_line(log)));
        }

        std::vector<uint32_t> spirv(module.cbegin(), module.cend());
        return make_module(context, std::move(spirv), path, {});
    }

    static std::expected<std::shared_ptr<ShaderModule>, Error> make_module(
            Context& context, std::vector<uint32_t> spirv, const std::string& path,
            std::vector<std::string> includes) {
        VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = spirv.size() * sizeof(uint32_t),
            .pCode = spirv.data()
        };

        VkShaderModule vk_module;
        if (auto e = check(vkCreateShaderModule(context.device(), &createInfo, nullptr, &vk_module),
                           "create shader module", ErrorCode::Shader)) {
            return std::unexpected(*e);
        }

        return std::make_shared<ShaderModule>(context.shared_from_this(), vk_module, path,
                                              std::move(includes), std::move(spirv));
    }

    // shaderc formats diagnostics as "<name>:<line>: error: ...", so the first
    // ":<digits>:" is the line. Returns -1 when the message doesn't match, which
    // is honest — better than guessing a wrong line number.
    static int parse_error_line(const std::string& log) {
        for (std::size_t i = 0; i + 1 < log.size(); ++i) {
            if (log[i] != ':') {
                continue;
            }

            std::size_t j = i + 1;
            while (j < log.size() && std::isdigit(static_cast<unsigned char>(log[j]))) {
                ++j;
            }

            if (j > i + 1 && j < log.size() && log[j] == ':') {
                try {
                    return std::stoi(log.substr(i + 1, j - i - 1));
                } catch (const std::exception&) {
                    return -1;
                }
            }
        }
        return -1;
    }
};
