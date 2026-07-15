"""Bazalt — Python library for rapid GPU shader prototyping using Vulkan."""

from bazalt._core import *  # noqa: F401, F403
from bazalt._core import (  # noqa: F401 — explicit re-exports for IDE visibility
    Context,
    SwapchainRenderer,
    Window,
    Logger,
    Buffer,
    ShaderModule,
    Texture,
    Pipeline,
    PipelineBuilder,
    DescriptorPool,
    DescriptorSet,
    CommandBuffer,
    MouseState,
    BufferType,
    DataType,
    ShaderStage,
    Format,
    CullMode,
    FrontFace,
    MemoryUsage,
)

__version__ = "0.3.0"

__all__ = [
    # Core
    "Context",
    "SwapchainRenderer",
    "Window",
    "Logger",
    # Resources
    "Buffer",
    "ShaderModule",
    "Texture",
    "Pipeline",
    "PipelineBuilder",
    "DescriptorPool",
    "DescriptorSet",
    "CommandBuffer",
    # Data types
    "MouseState",
    "BufferType",
    "DataType",
    "ShaderStage",
    "Format",
    "CullMode",
    "FrontFace",
    "MemoryUsage",
    # Version
    "__version__",
]
