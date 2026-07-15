"""Type stubs for bazalt._core (native Vulkan renderer)."""

from __future__ import annotations

from enum import IntEnum
from typing import Optional, Sequence, overload, Callable

# ── Enums ──────────────────────────────────────────────────────────────

class BufferType(IntEnum):
    """Type of GPU buffer."""
    VERTEX: int
    INDEX: int
    UNIFORM: int
    STORAGE: int

class DataType(IntEnum):
    """Element data type for buffer creation."""
    FLOAT: int
    UINT32: int
    UINT16: int
    INT32: int

class ShaderStage(IntEnum):
    """Shader pipeline stage."""
    VERTEX: int
    FRAGMENT: int

class Format(IntEnum):
    """Vertex attribute format."""
    FLOAT2: int
    FLOAT3: int
    FLOAT4: int

class CullMode(IntEnum):
    """Triangle culling mode."""
    NONE: int
    BACK: int
    FRONT: int
    FRONT_AND_BACK: int

class FrontFace(IntEnum):
    """Winding order for front-facing triangles."""
    CLOCKWISE: int
    COUNTER_CLOCKWISE: int

class MemoryUsage(IntEnum):
    """Memory usage strategy for buffer creation."""
    STATIC: int
    DYNAMIC: int

# ── Data Classes ───────────────────────────────────────────────────────

class MouseState:
    """Accumulated mouse movement state."""
    dx: float
    """Cumulative X movement since window creation."""
    dy: float
    """Cumulative Y movement since window creation."""

# ── GPU Resources ──────────────────────────────────────────────────────

class Buffer:
    """GPU buffer (vertex, index, uniform, or storage)."""

    @overload
    def update(self, data: bytes) -> None: ...
    @overload
    def update(self, array: buffer) -> None: ...
    @overload
    def update(self, list: list, data_type: Optional[DataType] = None) -> None: ...
    def update(self, *args, **kwargs) -> None:
        """Update buffer contents.

        Accepts raw bytes, a numpy array (buffer protocol), or a Python list.
        For lists, the data type is inferred from elements unless ``data_type``
        is specified explicitly.
        """
        ...

class ShaderModule:
    """Compiled SPIR-V shader module."""
    ...

class Texture:
    """GPU texture loaded from an image file."""

    @property
    def width(self) -> int:
        """Texture width in pixels."""
        ...

    @property
    def height(self) -> int:
        """Texture height in pixels."""
        ...

class Pipeline:
    """Compiled graphics pipeline (immutable after creation)."""
    ...

class DescriptorSet:
    """A descriptor set binding resources to a pipeline."""
    
    def set_texture(self, binding: int, texture: Texture) -> None:
        """Write a texture to this descriptor set."""
        ...
        
    def set_buffer(self, binding: int, buffer: Buffer) -> None:
        """Write a buffer to this descriptor set."""
        ...

class DescriptorPool:
    """A pool from which descriptor sets can be allocated."""
    
    def allocate_set(self, pipeline: Pipeline, set: int) -> DescriptorSet:
        """Allocate a static descriptor set (1 internal copy) for materials/textures."""
        ...
        
    def allocate_frame_set(self, pipeline: Pipeline, set: int) -> DescriptorSet:
        """Allocate a per-frame descriptor set for per-frame updated buffers."""
        ...

class PipelineBuilder:
    """Fluent builder for constructing graphics pipelines.

    All configuration methods return ``self`` for chaining.
    """

    def vertex_shader(self, shader: ShaderModule) -> PipelineBuilder:
        """Set the vertex shader stage."""
        ...

    def fragment_shader(self, shader: ShaderModule) -> PipelineBuilder:
        """Set the fragment shader stage."""
        ...

    def vertex_format(self, formats: list[Format]) -> PipelineBuilder:
        """Define vertex input layout as a list of attribute formats."""
        ...

    def depth_test(self, enable: bool) -> PipelineBuilder:
        """Enable or disable depth testing and writing."""
        ...

    def cull_mode(self, mode: CullMode, front_face: FrontFace) -> PipelineBuilder:
        """Set triangle culling mode and front-face winding order."""
        ...

    def blend(self, enable: bool) -> PipelineBuilder:
        """Enable or disable alpha blending."""
        ...

    def push_constant(self, size: int, stage: ShaderStage) -> PipelineBuilder:
        """Declare a push constant range.

        Args:
            size: Size in bytes.
            stage: Shader stage that uses this constant.
        """
        ...

    def uniform_buffer(self, binding: int, stage: ShaderStage, set: int) -> PipelineBuilder:
        """Declare a uniform buffer descriptor binding."""
        ...

    def storage_buffer(self, binding: int, stage: ShaderStage, set: int) -> PipelineBuilder:
        """Declare a storage buffer descriptor binding."""
        ...

    def texture(self, binding: int, stage: ShaderStage, set: int) -> PipelineBuilder:
        """Declare a combined image sampler descriptor binding."""
        ...

    def build(self, renderer: SwapchainRenderer) -> Pipeline:
        """Compile the pipeline for a given renderer. Raises ``RuntimeError`` on failure.

        Args:
            renderer: The swapchain renderer providing color/depth format information.
        """
        ...

# ── Command Buffer ─────────────────────────────────────────────────────

class CommandBuffer:
    """Deferred GPU command recorder.

    Commands are recorded once via the ``begin…``/``end…`` methods,
    then replayed every frame when passed to ``SwapchainRenderer.submit()``.
    """

    def begin(self) -> None:
        """Clear previously recorded commands and start a new recording."""
        ...

    def begin_rendering(self, *, clear_color: list[float]) -> None:
        """Begin a dynamic rendering pass.

        Args:
            clear_color: RGBA clear color as ``[r, g, b, a]`` (0.0–1.0).
        """
        ...

    def end_rendering(self) -> None:
        """End the current rendering pass and transition to present layout."""
        ...

    def set_viewport(self) -> None:
        """Set the viewport to cover the full swapchain extent."""
        ...

    def set_scissor(self) -> None:
        """Set the scissor rectangle to cover the full swapchain extent."""
        ...

    def bind_pipeline(self, pipeline: Pipeline) -> None:
        """Bind a graphics pipeline for subsequent draw calls."""
        ...

    def bind_vertex_buffer(self, buffer: Buffer) -> None:
        """Bind a vertex buffer at binding 0."""
        ...

    def bind_index_buffer(self, buffer: Buffer) -> None:
        """Bind an index buffer (uint32 indices)."""
        ...

    def draw(self, vertex_count: int) -> None:
        """Record a non-indexed draw call."""
        ...

    def draw_indexed(self, index_count: int, first_index: int = 0, vertex_offset: int = 0) -> None:
        """Record an indexed draw call (1 instance)."""
        ...

    def draw_indexed_instanced(self, index_count: int, instance_count: int, first_index: int = 0, vertex_offset: int = 0) -> None:
        """Record an indexed, instanced draw call."""
        ...

    def push_constants(
        self,
        pipeline: Pipeline,
        stage: ShaderStage,
        offset: int,
        data: bytes,
    ) -> None:
        """Upload push constant data.

        Args:
            pipeline: Pipeline whose layout defines the push constant range.
            stage: Target shader stage.
            offset: Byte offset into the push constant range.
            data: Raw bytes to upload.
        """
        ...

    def bind_descriptor_set(
        self, descriptor_set: DescriptorSet, pipeline: Pipeline, set: int
    ) -> None:
        """Bind a descriptor set to the specified set index."""
        ...

# ── Window ─────────────────────────────────────────────────────────────

class Window:
    """Manages the OS window and GLFW input callbacks."""

    def __init__(self, width: int, height: int, title: str) -> None: ...

    def is_open(self) -> bool:
        """Return ``True`` if the window is open."""
        ...

    def should_close(self) -> bool:
        """Return ``True`` if the window should close."""
        ...

    def poll_events(self) -> None:
        """Poll and process OS input/window events."""
        ...

    def is_key_pressed(self, key: int) -> bool:
        """Check if a keyboard key is currently held down.

        Use module-level ``KEY_*`` constants for key codes.
        """
        ...

    def is_mouse_button_pressed(self, button: int) -> bool:
        """Check if a mouse button is currently held down.

        Use ``MOUSE_BUTTON_LEFT``, ``MOUSE_BUTTON_RIGHT``,
        ``MOUSE_BUTTON_MIDDLE``.
        """
        ...

    def set_cursor_mode(self, mode: int) -> None:
        """Set cursor visibility/capture mode.

        Use ``CURSOR_NORMAL``, ``CURSOR_DISABLED``, or ``CURSOR_HIDDEN``.
        """
        ...

    def get_mouse_state(self) -> MouseState:
        """Return the current accumulated mouse state."""
        ...

    def set_title(self, title: str) -> None:
        """Update the window title."""
        ...

    @property
    def width(self) -> int:
        """Current window width in pixels (updates on resize)."""
        ...

    @property
    def height(self) -> int:
        """Current window height in pixels (updates on resize)."""
        ...

# ── Logger ─────────────────────────────────────────────────────────────

class Logger:
    """Asynchronous validation/error logger."""

    def __init__(self) -> None: ...

    def on_error(self, callback: Callable[[str], None]) -> Callable[[str], None]:
        """Register an error/log callback. Can be used as a decorator."""
        ...

    def log(self, msg: str) -> None:
        """Send a message to the logger."""
        ...

# ── Context ────────────────────────────────────────────────────────────

class Context:
    """Manages the Vulkan instance, device, queues, allocator, and command pool.

    This is the shared GPU context that can be used by one or more renderers.
    Resources (buffers, textures, shaders, pipelines) are created through this class.
    """

    def __init__(self, logger: Optional[Logger] = None) -> None: ...

    @overload
    def create_buffer(
        self, list: list, type: BufferType, usage: MemoryUsage, data_type: Optional[DataType] = None
    ) -> Buffer: ...
    @overload
    def create_buffer(self, array: buffer, type: BufferType, usage: MemoryUsage) -> Buffer: ...
    @overload
    def create_buffer(self, size_in_bytes: int, type: BufferType, usage: MemoryUsage) -> Buffer: ...
    def create_buffer(self, *args, **kwargs) -> Buffer:
        """Create a GPU buffer.

        Three overloads:

        1. ``create_buffer(list, type, usage, data_type=None)`` — from a Python list
        2. ``create_buffer(array, type, usage)`` — from a numpy array (buffer protocol)
        3. ``create_buffer(size_in_bytes, type, usage)`` — empty buffer of given size
        """
        ...

    def pipeline_builder(self) -> PipelineBuilder:
        """Create a new pipeline builder."""
        ...

    def compile_shader(self, path: str, stage: ShaderStage) -> ShaderModule:
        """Compile a GLSL shader file to SPIR-V at runtime.

        Args:
            path: Path to the ``.vert`` or ``.frag`` source file.
            stage: Shader stage (``VERTEX`` or ``FRAGMENT``).
        """
        ...

    def load_texture(self, path: str) -> Texture:
        """Load an image file as a GPU texture (supports PNG, JPG, BMP, etc.)."""
        ...
        
    def create_descriptor_pool(
        self, max_sets: int, samplers: int = 0, uniform_buffers: int = 0, storage_buffers: int = 0
    ) -> DescriptorPool:
        """Create a descriptor pool for allocating descriptor sets."""
        ...

# ── SwapchainRenderer ──────────────────────────────────────────────────

class SwapchainRenderer:
    """Manages a Vulkan swapchain, synchronization, and frame presentation.

    Requires a ``Context`` for GPU access and a window (or HWND) as the render target.
    """

    @overload
    def __init__(self, window: Window, context: Context) -> None: ...
    @overload
    def __init__(self, win32_hwnd: int, context: Context) -> None: ...
    def __init__(self, *args, **kwargs) -> None: ...

    def begin_frame(self) -> bool:
        """Acquire the next swapchain image.

        Returns ``True`` if successful, or ``False`` if the frame should be skipped (e.g. window minimized).
        """
        ...

    def submit(self, cmd: CommandBuffer) -> None:
        """Submit a recorded command buffer for the current frame to the presentation queue."""
        ...

    def create_command_buffer(self) -> CommandBuffer:
        """Allocate a new command buffer."""
        ...

# ── Keyboard Constants ─────────────────────────────────────────────────

KEY_SPACE: int
KEY_APOSTROPHE: int
KEY_COMMA: int
KEY_MINUS: int
KEY_PERIOD: int
KEY_SLASH: int
KEY_0: int
KEY_1: int
KEY_2: int
KEY_3: int
KEY_4: int
KEY_5: int
KEY_6: int
KEY_7: int
KEY_8: int
KEY_9: int
KEY_SEMICOLON: int
KEY_EQUAL: int
KEY_A: int
KEY_B: int
KEY_C: int
KEY_D: int
KEY_E: int
KEY_F: int
KEY_G: int
KEY_H: int
KEY_I: int
KEY_J: int
KEY_K: int
KEY_L: int
KEY_M: int
KEY_N: int
KEY_O: int
KEY_P: int
KEY_Q: int
KEY_R: int
KEY_S: int
KEY_T: int
KEY_U: int
KEY_V: int
KEY_W: int
KEY_X: int
KEY_Y: int
KEY_Z: int
KEY_LEFT_BRACKET: int
KEY_BACKSLASH: int
KEY_RIGHT_BRACKET: int
KEY_GRAVE_ACCENT: int
KEY_WORLD_1: int
KEY_WORLD_2: int
KEY_ESCAPE: int
KEY_ENTER: int
KEY_TAB: int
KEY_BACKSPACE: int
KEY_INSERT: int
KEY_DELETE: int
KEY_RIGHT: int
KEY_LEFT: int
KEY_DOWN: int
KEY_UP: int
KEY_PAGE_UP: int
KEY_PAGE_DOWN: int
KEY_HOME: int
KEY_END: int
KEY_CAPS_LOCK: int
KEY_SCROLL_LOCK: int
KEY_NUM_LOCK: int
KEY_PRINT_SCREEN: int
KEY_PAUSE: int
KEY_F1: int
KEY_F2: int
KEY_F3: int
KEY_F4: int
KEY_F5: int
KEY_F6: int
KEY_F7: int
KEY_F8: int
KEY_F9: int
KEY_F10: int
KEY_F11: int
KEY_F12: int
KEY_F13: int
KEY_F14: int
KEY_F15: int
KEY_F16: int
KEY_F17: int
KEY_F18: int
KEY_F19: int
KEY_F20: int
KEY_F21: int
KEY_F22: int
KEY_F23: int
KEY_F24: int
KEY_F25: int
KEY_KP_0: int
KEY_KP_1: int
KEY_KP_2: int
KEY_KP_3: int
KEY_KP_4: int
KEY_KP_5: int
KEY_KP_6: int
KEY_KP_7: int
KEY_KP_8: int
KEY_KP_9: int
KEY_KP_DECIMAL: int
KEY_KP_DIVIDE: int
KEY_KP_MULTIPLY: int
KEY_KP_SUBTRACT: int
KEY_KP_ADD: int
KEY_KP_ENTER: int
KEY_KP_EQUAL: int
KEY_LEFT_SHIFT: int
KEY_LEFT_CONTROL: int
KEY_LEFT_ALT: int
KEY_LEFT_SUPER: int
KEY_RIGHT_SHIFT: int
KEY_RIGHT_CONTROL: int
KEY_RIGHT_ALT: int
KEY_RIGHT_SUPER: int
KEY_MENU: int
KEY_LAST: int

# ── Mouse Constants ────────────────────────────────────────────────────

MOUSE_BUTTON_LEFT: int
MOUSE_BUTTON_RIGHT: int
MOUSE_BUTTON_MIDDLE: int

# ── Cursor Mode Constants ──────────────────────────────────────────────

CURSOR_NORMAL: int
CURSOR_DISABLED: int
CURSOR_HIDDEN: int
