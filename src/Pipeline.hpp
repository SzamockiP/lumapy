#pragma once
#include <volk.h>
#include <algorithm>
#include <functional>
#include <vector>
#include <memory>
#include <expected>
#include <array>
#include <map>
#include <ranges>
#include <unordered_map>
#include <utility>
#include "ShaderCompiler.hpp"
#include "Context.hpp"
#include "RenderTarget.hpp"
#include "ScopeGuard.hpp"

// Renamed from Format: this describes a vertex attribute, and `Format` is needed
// for pixel formats in 0.5.
enum class VertexFormat {
    FLOAT2,
    FLOAT3,
    FLOAT4
};

enum class CullMode {
    NONE,
    BACK,
    FRONT,
    FRONT_AND_BACK
};

enum class FrontFace {
    CLOCKWISE,
    COUNTER_CLOCKWISE
};

class Pipeline {
public:
    // Maps binding index -> VkDescriptorType so DescriptorSet knows what type to write
    using BindingTypeMap = std::unordered_map<uint32_t, VkDescriptorType>;

    Pipeline(std::shared_ptr<Context> context, VkPipeline pipeline, VkPipelineLayout layout,
             std::vector<VkDescriptorSetLayout> descLayouts = {},
             std::map<uint32_t, BindingTypeMap> bindingTypes = {},
             VkShaderStageFlags pushConstantStages = 0,
             VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS)
        : context_(context), pipeline_(pipeline), layout_(layout),
          desc_layouts_(std::move(descLayouts)),
          binding_types_(std::move(bindingTypes)),
          push_constant_stages_(pushConstantStages),
          bind_point_(bindPoint) {}

    // Carried on the Pipeline so command recording doesn't hardcode
    // VK_PIPELINE_BIND_POINT_GRAPHICS. Compute pipelines (0.6) then need no
    // change at the call sites.
    VkPipelineBindPoint bind_point() const { return bind_point_; }

    // The builder already knows which stages the push constant range covers, so
    // push_constants() doesn't need the caller to repeat it — and can't be given
    // a mismatched one.
    VkShaderStageFlags push_constant_stages() const { return push_constant_stages_; }

    ~Pipeline() {
        destroy();
    }

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept
        : context_(std::move(other.context_)), pipeline_(other.pipeline_), layout_(other.layout_),
          desc_layouts_(std::move(other.desc_layouts_)),
          binding_types_(std::move(other.binding_types_)),
          push_constant_stages_(other.push_constant_stages_),
          bind_point_(other.bind_point_) {
        other.pipeline_ = VK_NULL_HANDLE;
        other.layout_ = VK_NULL_HANDLE;
    }

    Pipeline& operator=(Pipeline&& other) noexcept {
        if (this != &other) {
            destroy();
            context_ = std::move(other.context_);
            pipeline_ = other.pipeline_;
            layout_ = other.layout_;
            desc_layouts_ = std::move(other.desc_layouts_);
            binding_types_ = std::move(other.binding_types_);
            push_constant_stages_ = other.push_constant_stages_;
            bind_point_ = other.bind_point_;

            other.pipeline_ = VK_NULL_HANDLE;
            other.layout_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkPipeline get() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

    VkDescriptorSetLayout descriptor_set_layout(uint32_t setIndex) const {
        if (setIndex < desc_layouts_.size()) {
            return desc_layouts_[setIndex];
        }
        return VK_NULL_HANDLE;
    }

    uint32_t descriptor_set_layout_count() const {
        return static_cast<uint32_t>(desc_layouts_.size());
    }

    const BindingTypeMap& binding_types(uint32_t setIndex) const {
        static const BindingTypeMap empty;
        auto it = binding_types_.find(setIndex);
        if (it != binding_types_.end()) {
            return it->second;
        }
        return empty;
    }

private:
    // One teardown for the destructor and move-assignment; it used to be written
    // out twice and the two copies had already started to drift.
    // Deferred: an in-flight frame may still be executing with this pipeline
    // bound.
    void destroy() {
        if (!context_) {
            return;
        }
        context_->defer_destroy(
            [device = context_->device(), pipeline = pipeline_, layout = layout_,
             desc_layouts = desc_layouts_] {
                if (pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                }
                if (layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, layout, nullptr);
                }
                for (auto dl : desc_layouts) {
                    if (dl != VK_NULL_HANDLE) {
                        vkDestroyDescriptorSetLayout(device, dl, nullptr);
                    }
                }
            });
    }

    std::shared_ptr<Context> context_;
    VkPipeline pipeline_;
    VkPipelineLayout layout_;
    std::vector<VkDescriptorSetLayout> desc_layouts_;
    std::map<uint32_t, BindingTypeMap> binding_types_;
    VkShaderStageFlags push_constant_stages_ = 0;
    VkPipelineBindPoint bind_point_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

class PipelineBuilder {
public:
    PipelineBuilder(Context& context) : context_(context) {}

    // Chained setters use C++23 deducing this: the object parameter's value
    // category is forwarded, so a chain on a temporary builder moves instead of
    // pinning an lvalue. The pybind layer binds these through lambdas — an
    // explicit object parameter turns &PipelineBuilder::vertex_shader into a
    // plain function-pointer type that .def() would misread.

    template <typename Self>
    Self&& vertex_shader(this Self&& self, std::shared_ptr<ShaderModule> shader) {
        self.vertex_shader_ = std::move(shader);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& fragment_shader(this Self&& self, std::shared_ptr<ShaderModule> shader) {
        self.fragment_shader_ = std::move(shader);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& vertex_format(this Self&& self, const std::vector<VertexFormat>& formats) {
        self.formats_ = formats;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& depth_test(this Self&& self, bool enable) {
        self.depth_test_ = enable;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& cull_mode(this Self&& self, CullMode mode, FrontFace frontFace) {
        self.cull_mode_ = mode;
        self.front_face_ = frontFace;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& blend(this Self&& self, bool enable) {
        self.blend_enable_ = enable;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& push_constant(this Self&& self, uint32_t size, ShaderStage stage) {
        VkPushConstantRange range{
            .stageFlags = static_cast<VkShaderStageFlags>(to_vk(stage)),
            .offset = 0,
            .size = size
        };
        self.push_constant_ranges_.push_back(range);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& uniform_buffer(this Self&& self, uint32_t binding, ShaderStage stage, uint32_t set) {
        return std::forward<Self>(self).add_descriptor_binding_(binding, stage, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, set);
    }

    template <typename Self>
    Self&& storage_buffer(this Self&& self, uint32_t binding, ShaderStage stage, uint32_t set) {
        return std::forward<Self>(self).add_descriptor_binding_(binding, stage, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, set);
    }

    template <typename Self>
    Self&& texture(this Self&& self, uint32_t binding, ShaderStage stage, uint32_t set) {
        return std::forward<Self>(self).add_descriptor_binding_(binding, stage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, set);
    }

    // Build the pipeline with explicit color/depth formats (decoupled from renderer)
    //
    // A short sequence of named steps. The ~300-line monolith this replaces mixed
    // descriptor-layout creation, vertex-input translation and fixed state into
    // one scroll, with the cleanup loop copy-pasted into every failure branch.
    std::expected<std::shared_ptr<Pipeline>, Error> build(std::vector<VkFormat> colorFormats, VkFormat depthFormat) {
        if (!vertex_shader_) {
            return std::unexpected(err_shader("A vertex shader must be provided"));
        }
        // A fragment shader is optional only when there is nothing to shade:
        // a depth-only pass (shadow maps) rasterizes straight into the depth
        // attachment and is valid Vulkan without one.
        if (!fragment_shader_ && !colorFormats.empty()) {
            return std::unexpected(err_shader(
                "A fragment shader must be provided when the target has colour "
                "attachments (only depth-only targets can omit it)"));
        }

        const std::vector<VkPipelineShaderStageCreateInfo> shaderStages = shader_stages_();

        // Descriptor set layouts — owned by the guard until the Pipeline exists.
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        std::map<uint32_t, Pipeline::BindingTypeMap> allBindingTypes;
        ScopeGuard cleanup_layouts([&] {
            for (auto dl : descriptorSetLayouts) {
                vkDestroyDescriptorSetLayout(context_.device(), dl, nullptr);
            }
        });
        if (auto r = create_set_layouts_(descriptorSetLayouts, allBindingTypes); !r) {
            return std::unexpected(r.error());
        }

        // Vertex input — the CreateInfo points into vertexInput, so it lives here.
        const VertexInput vertexInput = vertex_input_();
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (!vertexInput.attributes.empty()) {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &vertexInput.binding;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.attributes.size());
            vertexInputInfo.pVertexAttributeDescriptions = vertexInput.attributes.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        // Dynamic State
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };

        VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr
        };

        const VkPipelineRasterizationStateCreateInfo rasterizer = rasterization_state_();

        VkPipelineMultisampleStateCreateInfo multisampling{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };

        // One blend state per colour attachment (identical for now; per-target
        // blend control can arrive additively).
        const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
            colorFormats.size(), color_blend_attachment_());

        VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = static_cast<uint32_t>(blendAttachments.size()),
            .pAttachments = blendAttachments.data(),
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencil = depth_stencil_state_();

        auto layout = create_layout_(descriptorSetLayouts);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        VkPipelineLayout pipelineLayout = layout.value();
        ScopeGuard cleanup_pipeline_layout([&] {
            vkDestroyPipelineLayout(context_.device(), pipelineLayout, nullptr);
        });

        // Dynamic Rendering Info
        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
            .pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data(),
            .depthAttachmentFormat = depthFormat != VK_FORMAT_UNDEFINED ? depthFormat : VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
        };

        VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingCreateInfo,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pTessellationState = nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = pipelineLayout,
            .renderPass = VK_NULL_HANDLE, // Dynamic rendering
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };

        VkPipeline graphicsPipeline;
        // ErrorCode::Shader, not Initialization: a pipeline that fails to build is
        // almost always a shader/state mismatch the caller can fix and retry, and
        // hot reload (0.6) depends on catching exactly this as recoverable.
        if (auto e = check(vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline),
                           "create graphics pipeline", ErrorCode::Shader)) {
            return std::unexpected(*e);
        }

        const VkShaderStageFlags push_stages = std::ranges::fold_left(
            push_constant_ranges_ | std::views::transform(&VkPushConstantRange::stageFlags),
            VkShaderStageFlags{0}, std::bit_or{});

        // Everything now belongs to the Pipeline.
        cleanup_layouts.release();
        cleanup_pipeline_layout.release();

        return std::make_shared<Pipeline>(context_.shared_from_this(), graphicsPipeline, pipelineLayout,
                                          std::move(descriptorSetLayouts), std::move(allBindingTypes),
                                          push_stages, VK_PIPELINE_BIND_POINT_GRAPHICS);
    }

    // Convenience overload: a RenderTarget already knows its own formats, so the
    // caller shouldn't have to dig them out. This is what replaces build(renderer).
    std::expected<std::shared_ptr<Pipeline>, Error> build(const RenderTarget& target) {
        std::vector<VkFormat> colorFormats;
        colorFormats.reserve(target.color_count());
        for (std::uint32_t i = 0; i < target.color_count(); ++i) {
            colorFormats.push_back(target.color_format(i));
        }
        return build(std::move(colorFormats), target.depth_format());
    }

private:
    // ── build() steps ─────────────────────────────────────────────────────────

    // 1 stage (depth-only) or 2. The fragment stage is optional exactly when
    // the target has no colour attachments — build() enforces that pairing.
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_() const {
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader_->get(),
            .pName = "main",
            .pSpecializationInfo = nullptr
        });
        if (fragment_shader_) {
            stages.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment_shader_->get(),
                .pName = "main",
                .pSpecializationInfo = nullptr
            });
        }
        return stages;
    }

    // One VkDescriptorSetLayout per set index up to the highest one used; gap
    // set indices get an empty layout so shader set numbers stay meaningful.
    // Partially created layouts are the caller's guard's problem.
    std::expected<void, Error> create_set_layouts_(
        std::vector<VkDescriptorSetLayout>& layouts,
        std::map<uint32_t, Pipeline::BindingTypeMap>& bindingTypes)
    {
        if (descriptor_bindings_.empty()) {
            return {};
        }

        // Parenthesised to dodge the max() macro from <windows.h>.
        const uint32_t maxSetIndex = (std::ranges::max)(descriptor_bindings_ | std::views::keys);

        for (uint32_t s = 0; s <= maxSetIndex; s++) {
            auto it = descriptor_bindings_.find(s);
            const bool has_bindings = it != descriptor_bindings_.end() && !it->second.empty();

            VkDescriptorSetLayoutCreateInfo layoutInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = has_bindings ? static_cast<uint32_t>(it->second.size()) : 0,
                .pBindings = has_bindings ? it->second.data() : nullptr
            };

            VkDescriptorSetLayout layout;
            if (auto e = check(vkCreateDescriptorSetLayout(context_.device(), &layoutInfo, nullptr, &layout),
                               "create descriptor set layout for set " + std::to_string(s))) {
                return std::unexpected(*e);
            }
            layouts.push_back(layout);

            if (has_bindings) {
                Pipeline::BindingTypeMap btm;
                for (const auto& b : it->second) {
                    btm[b.binding] = b.descriptorType;
                }
                bindingTypes[s] = std::move(btm);
            }
        }
        return {};
    }

    struct VertexInput {
        VkVertexInputBindingDescription binding{};
        std::vector<VkVertexInputAttributeDescription> attributes;
    };

    // Translates the VertexFormat list into a binding + attribute descriptions
    // with packed offsets.
    VertexInput vertex_input_() const {
        VertexInput result;
        result.binding = {
            .binding = 0,
            .stride = 0,  // accumulated below
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        result.attributes.resize(formats_.size());
        uint32_t offset = 0;
        for (size_t i = 0; i < formats_.size(); i++) {
            result.attributes[i].binding = 0;
            result.attributes[i].location = static_cast<uint32_t>(i);
            result.attributes[i].offset = offset;

            switch (formats_[i]) {
                case VertexFormat::FLOAT2:
                    result.attributes[i].format = VK_FORMAT_R32G32_SFLOAT;
                    offset += 8;
                    break;
                case VertexFormat::FLOAT3:
                    result.attributes[i].format = VK_FORMAT_R32G32B32_SFLOAT;
                    offset += 12;
                    break;
                case VertexFormat::FLOAT4:
                    result.attributes[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    offset += 16;
                    break;
            }
        }
        result.binding.stride = offset;
        return result;
    }

    VkPipelineRasterizationStateCreateInfo rasterization_state_() const {
        VkCullModeFlags vkCullMode = VK_CULL_MODE_NONE;
        if (cull_mode_ == CullMode::BACK) vkCullMode = VK_CULL_MODE_BACK_BIT;
        else if (cull_mode_ == CullMode::FRONT) vkCullMode = VK_CULL_MODE_FRONT_BIT;
        else if (cull_mode_ == CullMode::FRONT_AND_BACK) vkCullMode = VK_CULL_MODE_FRONT_AND_BACK;

        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = vkCullMode,
            .frontFace = front_face_ == FrontFace::CLOCKWISE
                ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f
        };
    }

    VkPipelineColorBlendAttachmentState color_blend_attachment_() const {
        return {
            .blendEnable = blend_enable_ ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = blend_enable_ ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = blend_enable_ ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = blend_enable_ ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
    }

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_() const {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = depth_test_ ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = depth_test_ ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f
        };
    }

    std::expected<VkPipelineLayout, Error> create_layout_(const std::vector<VkDescriptorSetLayout>& layouts) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.empty() ? nullptr : layouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(push_constant_ranges_.size()),
            .pPushConstantRanges = push_constant_ranges_.data()
        };

        VkPipelineLayout pipelineLayout;
        if (auto e = check(vkCreatePipelineLayout(context_.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout),
                           "create pipeline layout")) {
            return std::unexpected(*e);
        }
        return pipelineLayout;
    }

    template <typename Self>
    Self&& add_descriptor_binding_(this Self&& self, uint32_t binding, ShaderStage stage, VkDescriptorType descriptorType, uint32_t setIndex) {
        VkShaderStageFlags stageFlag = static_cast<VkShaderStageFlags>(to_vk(stage));

        auto& bindings = self.descriptor_bindings_[setIndex];

        auto it = std::ranges::find(bindings, binding, &VkDescriptorSetLayoutBinding::binding);
        if (it != bindings.end()) {
            it->stageFlags |= stageFlag;
            return std::forward<Self>(self);
        }

        VkDescriptorSetLayoutBinding layoutBinding{
            .binding = binding,
            .descriptorType = descriptorType,
            .descriptorCount = 1,
            .stageFlags = stageFlag,
            .pImmutableSamplers = nullptr
        };
        bindings.push_back(layoutBinding);
        return std::forward<Self>(self);
    }

    Context& context_;
    std::shared_ptr<ShaderModule> vertex_shader_;
    std::shared_ptr<ShaderModule> fragment_shader_;
    std::vector<VertexFormat> formats_;
    bool depth_test_ = false;
    CullMode cull_mode_ = CullMode::BACK;
    FrontFace front_face_ = FrontFace::COUNTER_CLOCKWISE;
    bool blend_enable_ = false;
    std::vector<VkPushConstantRange> push_constant_ranges_;
    std::map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> descriptor_bindings_;
};
