import bazalt as bz

# Create window, logger, and renderer
logger = bz.Logger()
@logger.on_error
def error(msg):
    print(f"Error: {msg}")

window = bz.Window(1024, 720, "Bazalt Demo - Triangle")
ctx = bz.Context(logger)
renderer = bz.SwapchainRenderer(window, ctx)

# Load and compile shaders
vert_spv = ctx.compile_shader("triangle.vert", bz.ShaderStage.VERTEX)
frag_spv = ctx.compile_shader("triangle.frag", bz.ShaderStage.FRAGMENT)

# Build pipeline: Position (FLOAT3) + Color (FLOAT3)
pipeline = (ctx.pipeline_builder()
    .vertex_shader(vert_spv)
    .fragment_shader(frag_spv)
    .vertex_format([bz.Format.FLOAT3, bz.Format.FLOAT3])
    .build(renderer))

# Interleaved Position (x,y,z) and Color (r,g,b)
vertices = [
     0.0, -0.5, 0.0,   1.0, 0.0, 0.0, # Top / Red
    -0.5,  0.5, 0.0,   0.0, 1.0, 0.0, # Bottom-Left / Green
     0.5,  0.5, 0.0,   0.0, 0.0, 1.0, # Bottom-Right / Blue
]
vbuf = ctx.create_buffer(vertices, bz.BufferType.VERTEX, bz.DataType.FLOAT)

indices = [0, 1, 2]
ibuf = ctx.create_buffer(indices, bz.BufferType.INDEX, bz.DataType.UINT32)

# Record command buffer once
cmd = renderer.create_command_buffer()
cmd.begin()
cmd.begin_rendering(clear_color=[0.1, 0.2, 0.3, 1.0])
cmd.set_viewport()
cmd.set_scissor()
cmd.bind_pipeline(pipeline)
cmd.bind_vertex_buffer(vbuf)
cmd.bind_index_buffer(ibuf)
cmd.draw_indexed(3)
cmd.end_rendering()

# Main loop
while window.is_open():
    window.poll_events()
    if renderer.begin_frame():
        renderer.submit(cmd)