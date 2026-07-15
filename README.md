# Bazalt

**Bazalt** is a modern Python library for rapid prototyping and building graphical applications using the Vulkan API. It provides a clean, intuitive interface over a high-performance C++ core, allowing developers to create rendering applications quickly without the typical boilerplate.

## Installation

You can install `bazalt` easily via `pip`:

```bash
pip install bazalt
```

## Key Features

- **Modern Graphics API:** Built on top of Vulkan for optimal hardware utilization.
- **Easy to Use Interface:** Write clear and concise code with an intuitive API.
- **Automatic Shader Compilation:** Compile GLSL shaders (Vertex/Fragment) directly from your code.
- **Pipeline & Buffer Management:** Easy builder pattern for graphics pipelines and unified buffer creation.
- **Command Buffers:** Explicit, yet simple command recording for drawing operations.
- **Decoupled Architecture:** Clean separation of concerns between Windowing (GLFW), Vulkan Context (GPU initialization), and Surface Presenting (SwapchainRenderer).

## Quick Start: Drawing a Triangle

Here is a minimal example demonstrating how to initialize the window, Vulkan Context, and SwapchainRenderer, compile shaders, create a pipeline, and draw a colorful triangle.

```python
import bazalt as bz

# 1. Initialize Logger and register a callback
logger = bz.Logger()
@logger.on_error
def error(msg):
    print(f"Error: {msg}")

# 2. Create the window, Vulkan Context, and SwapchainRenderer
window = bz.Window(1024, 720, "Bazalt Demo - Triangle")
ctx = bz.Context(logger)
renderer = bz.SwapchainRenderer(window, ctx)

if __name__ == "__main__":
    # Load and compile shaders. The vertex shader processes our geometry,
    # and the fragment shader determines the final color of the pixels.
    # Shaders are compiled through the Context.
    vert_spv = ctx.compile_shader("triangle.vert", bz.ShaderStage.VERTEX)
    frag_spv = ctx.compile_shader("triangle.frag", bz.ShaderStage.FRAGMENT)

    # The pipeline is a baked state object that tells the GPU how to interpret our data.
    # It is built via the Context's pipeline_builder and compiled against the renderer
    # to inherit compatible color/depth format options.
    pipeline = (ctx.pipeline_builder()
        .vertex_shader(vert_spv)
        .fragment_shader(frag_spv)
        .vertex_format([bz.Format.FLOAT3, bz.Format.FLOAT3]) # Position + Color
        .build(renderer))

    # We interleave Position (x,y,z) and Color (r,g,b) in a single flat array.
    # Vulkan's Normalized Device Coordinates (NDC) range from -1 to 1,
    # where Y points downwards and X points to the right.
    vertices = [
         0.0, -0.5, 0.0,   1.0, 0.0, 0.0, # Top / Red
        -0.5,  0.5, 0.0,   0.0, 1.0, 0.0, # Bottom-Left / Green
         0.5,  0.5, 0.0,   0.0, 0.0, 1.0, # Bottom-Right / Blue
    ]
    
    # Create Vertex Buffer through the Context
    vbuf = ctx.create_buffer(vertices, bz.BufferType.VERTEX, bz.DataType.FLOAT)
    
    # Create Index Buffer through the Context
    ibuf = ctx.create_buffer([0, 1, 2], bz.BufferType.INDEX, bz.DataType.UINT32)

    # Command buffers store a sequence of commands for the GPU.
    # For a static triangle, we can record this buffer once during initialization 
    # and submit the same pre-recorded buffer every frame to save CPU time.
    cmd = renderer.create_command_buffer()
    
    cmd.begin()
    
    # begin_rendering starts a render pass, automatically handling framebuffers
    # and clearing the screen to the specified color before drawing.
    cmd.begin_rendering(clear_color=[0.1, 0.2, 0.3, 1.0])
    cmd.set_viewport()
    cmd.set_scissor()
    
    # Bind the baked pipeline and the geometry buffers
    cmd.bind_pipeline(pipeline)
    cmd.bind_vertex_buffer(vbuf)
    cmd.bind_index_buffer(ibuf)
    
    # Draw 3 indices (1 triangle)
    cmd.draw_indexed(3)
    cmd.end_rendering()

    # Main game/rendering loop
    while window.is_open():
        window.poll_events()
        
        # begin_frame returns True if the swapchain image is ready for rendering
        if renderer.begin_frame():
            renderer.submit(cmd)
```
