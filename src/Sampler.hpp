#pragma once
#include <volk.h>

#include <cstdint>

// How to read texels. Deliberately small: these three knobs cover every
// example and test; compare samplers and per-axis address modes can be added
// additively if something ever needs them.
enum class Filter
{
	LINEAR,
	NEAREST,
};

enum class AddressMode
{
	REPEAT,
	CLAMP,
	MIRROR,
};

struct SamplerDesc
{
	Filter filter = Filter::LINEAR;
	AddressMode address_mode = AddressMode::REPEAT;
	bool anisotropy = true;

	bool operator==(const SamplerDesc&) const = default;
};

// Cache key on the Context: the whole descriptor space fits in a handful of
// bits, so identical requests share one VkSampler. Texture used to create a
// fresh sampler per texture — pure waste of a pooled driver object.
constexpr std::uint32_t sampler_cache_key(const SamplerDesc& d)
{
	return static_cast<std::uint32_t>(d.filter)
	     | (static_cast<std::uint32_t>(d.address_mode) << 1)
	     | (d.anisotropy ? 1u << 3 : 0u);
}

// A non-owning view of a cached VkSampler. The Context owns the handle and
// destroys it at teardown; Sampler objects handed to Python just keep the
// cache entry (and thus the Context) reachable.
class Sampler
{
public:
	Sampler(VkSampler handle, SamplerDesc desc) : handle_(handle), desc_(desc) {}

	VkSampler get() const { return handle_; }
	const SamplerDesc& desc() const { return desc_; }

private:
	VkSampler handle_ = VK_NULL_HANDLE;
	SamplerDesc desc_{};
};
