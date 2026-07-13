"""Bazalt — Python library for rapid GPU shader prototyping using Vulkan."""

from bazalt._core import *  # noqa: F401, F403
from bazalt._core import (  # noqa: F401 — explicit re-exports for IDE visibility
    Renderer,
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
)

__version__ = "0.2.0"

__all__ = [
    # Core
    "Renderer",
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
    # Version
    "__version__",
]
