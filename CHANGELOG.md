# Changelog

All notable changes to **bazalt** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/) (pre-1.0: minor versions may break the API,
patch versions never do).

## [0.5.0] — 2026-07-19

"Images & Uploads": asynchronous texture streaming, the Texture →
Image + Sampler split, configurable render-target formats with MRT and
shadow-map (depth-only) targets, a runtime frame ring, and chainable
command recording. The largest release to date; API breaks are batched
here per the pre-1.0 policy.

### Added
- **Async image uploads.** `ctx.load_image(path)` returns immediately;
  the decode and GPU copy run on a background worker and every submit
  that samples the image waits for exactly that upload, GPU-side, via a
  context-wide timeline semaphore. `img.ready`, `img.wait()`,
  `ctx.wait_for_uploads()`, `ctx.uploads_done` and `ctx.upload_progress`
  (per-batch, 0.0–1.0) give explicit control — a loading screen needs no
  user-side threads and never serializes behind its own cargo.
- **`bz.Format`** pixel formats (RGBA8, RGBA8_SRGB, BGRA8, R8, RG8,
  R16F, RGBA16F, R32F, RGBA32F, D32F) with one table driving Vulkan
  formats, byte sizes and numpy dtypes.
- **`bz.Image`**: `ctx.create_image(w, h, format=)`,
  `ctx.create_image(numpy_array)` (shape+dtype pick the format; UNORM —
  arrays are data, files are pictures), `img.read()` → numpy with the
  format's dtype/shape, automatic mipmaps on `load_image`. No `usage=`
  parameter: every legal usage is enabled, driver-filtered.
- **`bz.Sampler`**, cached on the Context: `ctx.create_sampler(filter=,
  address_mode=, anisotropy=)` — identical descriptions return the
  identical object. `bz.Filter`, `bz.AddressMode`.
- **Configurable render targets**: `RenderTarget(ctx, w, h,
  color=None|Format|[Format, ...], depth=None|Format)`. MRT renders
  into every attachment in one pass; `color=None, depth=D32F` makes a
  shadow map, and a depth-only pipeline may omit the fragment shader.
  Attachments are ordinary Images (`target.color[i]`, `target.depth`) —
  render-to-texture and shadow sampling need no further API.
- **Chainable recording**: every `CommandBuffer` method returns the
  command buffer, so `cmd.begin_rendering(t).bind_pipeline(p).draw(3)`
  works; plus `with cmd.rendering(target, clear_color=...):` which
  records `end_rendering` on exit, exceptions included.
- **`Context(frames_in_flight=N)`** (1–4, default 2) replaces the
  compile-time constant.
- **`buffer.read(dtype)`** → 1-D numpy array (dtype mandatory — buffers
  carry no format). STATIC reads round-trip the GPU; DYNAMIC reads map
  the current frame's copy.
- Descriptor sets return to their pool when garbage-collected; pools
  are no longer one-way.
- Examples: `09_shadow_map` (two passes, one command buffer),
  `10_gbuffer_mrt` (RGBA16F + RGBA8 g-buffer with deferred composite).

### Changed (breaking)
- `Texture` → `Image` + `Sampler`; `load_texture` → `load_image`;
  `DescriptorSet.set_texture` → `set_image(binding, image,
  sampler=None)`.
- `begin_frame()` returns `Frame | None` instead of `bool`, and
  `renderer.submit(cmd)` moved to `frame.submit(cmd)`. A Frame
  submitted twice or held across ticks raises `ResourceError`.
- `RenderTarget(depth=True)` → `depth=bz.Format.D32F` (a bool raises
  with a migration hint).
- An offscreen target's depth attachment now ends every pass
  sampleable (`SHADER_READ_ONLY`); the swapchain's scratch depth is
  unchanged.

### Fixed
- Headless `ctx.submit()` never advanced the frame ring: DynamicBuffer
  slots and frame descriptor set copies beyond slot 0 were never
  exercised headlessly. The ring now advances after each headless
  submit (and at `begin_frame` in windowed mode), keeping `update()`
  and the submit that consumes it on the same slot.
- Dropping a resource whose only owner was a recorded command buffer
  freed GPU handles the previous in-flight frame could still be
  reading. All resource destruction now goes through a deletion queue
  keyed by the submission timeline.
- Per-texture `VkSampler` objects (pure waste) and their `maxLod = 0`
  (which would have clamped away every mip) — samplers are cached with
  `VK_LOD_CLAMP_NONE`.
- VMA was told API 1.3 even on the 1.2 fallback path.
- A script ending with its Context still in scope could die at
  interpreter shutdown ("could not acquire lock for stderr") — the
  logger's drain thread was calling into Python while the interpreter
  finalized. Drain threads are now joined via `atexit`, before teardown.

## [0.4.2] — 2026-07-18

A hotfix for fragment shaders that use `discard` on Vulkan 1.3.

### Fixed
- Fragment shaders using `discard` triggered a
  `vkCreateShaderModule` validation error (`SPIR-V Capability
  DemoteToHelperInvocation was declared…`) and a massive frame-rate
  collapse on Vulkan 1.3 devices (example 07 dropped from ~350 FPS to
  ~2 FPS). When shaders are compiled for SPIR-V 1.6, glslang translates
  `discard` into `OpDemoteToHelperInvocation`/`OpTerminateInvocation`,
  but the device was created without the matching
  `shaderDemoteToHelperInvocation`/`shaderTerminateInvocation` features.
  Both are mandatory in Vulkan 1.3 and are now enabled on the core-1.3
  path.
- `Context.api_version()` reported the raw device version instead of the
  negotiated one: a 1.3-capable GPU behind a 1.2 loader takes the
  1.2 + `VK_KHR_dynamic_rendering` path, yet shaders were still compiled
  targeting SPIR-V for Vulkan 1.3, which such a device can reject. The
  shader target now follows the version the device was actually created
  against.

## [0.4.1] — 2026-07-17

A source-quality release: bug fixes, refactoring, and C++23 adoption.
No public Python API changes (one behavioural fix: `read_pixels()` on a
never-rendered target now raises `ResourceError` instead of returning
undefined VRAM contents).

### Fixed
- `read_pixels()` could return discarded content: the internal "has been
  rendered to" flag was never set, so the readback barrier always declared
  the image layout as `UNDEFINED`, which permits the driver to drop the
  rendered pixels. The flag now flips when rendering work is actually
  submitted, and `read_pixels()` on a never-rendered target raises
  `ResourceError` instead of returning uninitialised VRAM.
- `Buffer.update` on a STATIC buffer, oversized updates, and binding a
  DYNAMIC buffer to a static `DescriptorSet` now raise `bz.ResourceError`
  instead of a bare `RuntimeError` that `except bz.BazaltError` missed.
- Every `VkResult` on the one-shot upload/readback path (allocate, submit,
  wait, map) is now checked; a device loss during an upload surfaces as
  `DeviceLostError` at the failing call instead of garbage later.
- Swapchain creation failures propagate the real `VkResult` instead of
  collapsing to a bare "Failed to create swapchain"; mid-frame failures
  log the `VkResult` name.
- `DescriptorSet.set_buffer`/`set_texture` validate the binding against
  the pipeline layout: a typo'd binding index or a buffer/texture
  mismatch raises `ResourceError` at the call site instead of being
  silently written (a nonexistent binding used to be *assumed* to be a
  uniform buffer) and diagnosed, at best, by the validation layers at
  submit time.

### Changed
- CI now builds `release/**` branches and smoke-tests every built wheel
  (`import bazalt` + stub consistency); the GPU test suite remains local.
- Internal C++23 modernisation: `std::format` for diagnostics,
  `std::ranges` for searches and folds, `constexpr` lookup tables,
  `std::span<const std::byte>` on the buffer-update path, and deducing
  `this` for the pipeline builder's chained setters. Building from source
  now requires GCC 14+ or MSVC 19.36+ (prebuilt wheels are unaffected).

### Added
- Behaviour-pinning tests for previously untested bindings: non-indexed
  `draw()`, `push_constants`, `DescriptorSet`/`DescriptorPool` end-to-end,
  `STORAGE` buffers, `blend()`, and `load_texture` sampling.
- This changelog.

## [0.4.0] — 2026-07-16

The "Foundations" release: three interdependent pillars, API breaks batched.

### Added
- **Unified error handling**: `bz.BazaltError` exception hierarchy
  (`InitializationError`, `DeviceLostError`, `OutOfMemoryError`,
  `ShaderError` with `.path`/`.line`, `WindowError`, `ResourceError`);
  structured `Logger`/`LogMessage` with `severity` and `source` as data;
  default stderr logger; GLFW diagnostics routed through `WindowError`.
- **Feature negotiation**: `Context(features=[...], optional=[...])`,
  `ctx.supports(Feature.X)`, `ctx.device_name`, `ctx.api_version`,
  `ctx.headless`; Vulkan 1.2 baseline with 1.3 preferred.
- **Headless rendering**: `bz.RenderTarget(ctx, w, h)` + `ctx.submit(cmd)` +
  `read_pixels()`; `SwapchainRenderer` is now just one `RenderTargetBase`.
- First pytest suite (56 tests) with validation-layers-as-assert fixture.

### Changed (breaking)
- `Logger.on_error` → `Logger.on_message` with structured `LogMessage`.
- `begin_rendering(target, ...)` — target is now required.
- Zero-argument `set_viewport()`/`set_scissor()` removed
  (`begin_rendering` emits full-target versions automatically).
- `Format` → `VertexFormat`.
- `push_constants` no longer takes a `stage` argument.
- `build(renderer)` → `build(target)`.
- `create_command_buffer` moved from renderer to `Context`.
- Non-contiguous numpy arrays now raise `ResourceError` instead of
  silently uploading garbage.
- `RuntimeError` replaced by the `BazaltError` hierarchy at the API boundary.

## [0.3.0] — 2026-07-15

### Added
- `MemoryUsage` enum: `STATIC` (GPU-local) and `DYNAMIC` (host-visible,
  multi-buffered, updatable per frame) for explicit control over resource
  memory strategy.
- Keyword arguments on all Python bindings plus `_core.pyi` stubs — IDE
  autocompletion and type hints.

### Changed
- Core Vulkan environment (`Context`) separated from presentation
  (`SwapchainRenderer`): the GPU can be initialised and shaders compiled
  without creating any window.
- Swapchain creation reimplemented on raw Vulkan calls instead of
  vk-bootstrap, so a window can be attached after `Context` creation and
  swapchain recreation is robust.

### Fixed
- Vulkan teardown crashes during Python garbage collection: resource
  objects (`Buffer`, `Texture`, `Pipeline`, …) now keep the `Context`
  alive via `std::shared_ptr`.

## [0.2.0] — 2026-07-13

### Added
- Native OS window integration via `win32_hwnd` (embedding in PyQt/PySide).

### Changed
- Renderer decoupled from GLFW windowing.
- `Logger` extracted into a standalone module.

## [0.1.0] — 2026-07-08

### Added
- `DescriptorPool` and `DescriptorSet` API for custom descriptor
  management.
- More examples and expanded README documentation.

## [0.0.1] — 2026-07-06

Initial preview release: window with event handling, threaded logging
system, and basic shader prototyping.
