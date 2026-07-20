#pragma once
#include <volk.h>
#include <optional>
#include <unordered_map>

class Buffer;

// What a recorded command does to a buffer, named from the caller's point of
// view. This is the vocabulary of cmd.barrier() in manual mode; the automatic
// tracker speaks raw (stage, access) pairs directly for better precision.
enum class Access {
    SHADER_READ,
    SHADER_WRITE,
    VERTEX_READ,
    INDEX_READ,
    UNIFORM_READ
};

struct StageAccess {
    VkPipelineStageFlags stages;
    VkAccessFlags access;
};

// Core-1.0 pairs on purpose: the whole codebase rides vkCmdPipelineBarrier,
// not synchronization2, and mixing models would be a second way to say the
// same thing.
inline constexpr VkPipelineStageFlags kAllShaderStages =
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

inline constexpr StageAccess to_vk(Access access) {
    switch (access) {
        case Access::SHADER_READ:  return { kAllShaderStages, VK_ACCESS_SHADER_READ_BIT };
        case Access::SHADER_WRITE: return { kAllShaderStages, VK_ACCESS_SHADER_WRITE_BIT };
        case Access::VERTEX_READ:  return { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT };
        case Access::INDEX_READ:   return { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT };
        case Access::UNIFORM_READ: return { kAllShaderStages, VK_ACCESS_UNIFORM_READ_BIT };
    }
    // Not std::unreachable(): pybind enums accept arbitrary ints.
    return { kAllShaderStages, VK_ACCESS_SHADER_READ_BIT };
}

// Computes buffer barriers at RECORD time. Deferred recording fixes the usage
// sequence constructively — record once, replay every submit — so a barrier
// computed here is correct for every replay and nothing runs per frame.
//
// Keys on Buffer* (object identity), never VkBuffer: a DynamicBuffer has one
// handle per frame in flight, but it is one resource with one usage history.
//
// Scope (0.6): buffers only. The compute API has no storage images, and
// sampled images are covered by the RenderTarget final_layout contract, so a
// buffer tracker covers everything the Python API can express. Known limit:
// SSBO *writes* from graphics shaders are invisible here (no reflection) —
// cmd.barrier() in manual mode is the ceiling for that.
class ResourceTracker {
public:
    struct Barrier {
        VkPipelineStageFlags src_stages;
        VkPipelineStageFlags dst_stages;
        VkAccessFlags src_access;
        VkAccessFlags dst_access;
    };

    // Registers a use and returns the barrier that must precede it, if any.
    std::optional<Barrier> use(Buffer* buffer, VkPipelineStageFlags stages,
                               VkAccessFlags access, bool writes) {
        BufferState& st = states_[buffer];
        std::optional<Barrier> result;

        if (writes) {
            // WAW / WAR: everything that touched the buffer must drain first.
            if (st.written || st.read_stages != 0) {
                result = Barrier{
                    st.write_stages | st.read_stages,
                    stages,
                    st.write_access | st.read_access,
                    access
                };
            }
            st = {};
            st.written = true;
            st.write_stages = stages;
            st.write_access = access;
        } else {
            // RAW — only if the write isn't already visible to these stages.
            // (Two draws reading the same SSBO emit one barrier, not two.)
            if (st.written &&
                ((stages & ~st.visible_stages) != 0 || (access & ~st.visible_access) != 0)) {
                result = Barrier{ st.write_stages, stages, st.write_access, access };
                st.visible_stages |= stages;
                st.visible_access |= access;
            }
            st.read_stages |= stages;
            st.read_access |= access;
        }
        return result;
    }

    void reset() { states_.clear(); }

private:
    struct BufferState {
        bool written = false;
        VkPipelineStageFlags write_stages = 0;
        VkAccessFlags write_access = 0;
        // Stages/accesses already synchronized against the last write.
        VkPipelineStageFlags visible_stages = 0;
        VkAccessFlags visible_access = 0;
        // Reads since the last write (what a future write must wait for).
        VkPipelineStageFlags read_stages = 0;
        VkAccessFlags read_access = 0;
    };

    std::unordered_map<Buffer*, BufferState> states_;
};
