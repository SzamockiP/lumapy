import bazalt as bz
import time

engine = bz.Engine()

@engine.onError
def error(msg):
    print(msg)

last_time = time.time()
frame_count = 0
fps_timer = 0.0

@engine.onFrame
def on_update():
    global last_time, frame_count, fps_timer

    current_time = time.time()
    dt = current_time - last_time
    last_time = current_time
    
    frame_count += 1
    fps_timer += dt
    
    if fps_timer >= 1.0:
        avg_fps = frame_count / fps_timer
        engine.setTitle(f"Bazalt Demo - Triangle | {1000.0/avg_fps:.2f} ms/frame | {avg_fps:.1f} FPS")
        frame_count = 0
        fps_timer = 0.0

    engine.submit(cmd)

if __name__ == "__main__":
    engine.init(1024, 720, "Bazalt Demo - Triangle")

    vert_spv = engine.compileShader("triangle.vert", bz.ShaderStage.VERTEX)
    frag_spv = engine.compileShader("triangle.frag", bz.ShaderStage.FRAGMENT)

    pipeline = (engine.createPipeline()
        .vertexShader(vert_spv)
        .fragmentShader(frag_spv)
        .vertexFormat([bz.Format.FLOAT3, bz.Format.FLOAT3])
        .build())

    # Format: pos x, y, z, color r, g, b
    vertices = [
         0.0, -0.5, 0.0,   1.0, 0.0, 0.0,
        -0.5,  0.5, 0.0,   0.0, 1.0, 0.0,
         0.5,  0.5, 0.0,   0.0, 0.0, 1.0,
    ]
    vbuf = engine.createBuffer(vertices, bz.BufferType.VERTEX, bz.DataType.FLOAT)

    indices = [0, 1, 2]
    ibuf = engine.createBuffer(indices, bz.BufferType.INDEX, bz.DataType.UINT32)

    cmd = engine.createCommandBuffer()

    # Nagrywamy liste komend tylko RAZ!
    cmd.begin()
    cmd.beginRendering(clear_color=[0.1, 0.2, 0.3, 1.0])
    cmd.setViewport()
    cmd.setScissor()
    cmd.bindPipeline(pipeline)
    cmd.bindVertexBuffer(vbuf)
    cmd.bindIndexBuffer(ibuf)
    cmd.drawIndexed(3)
    cmd.endRendering()

    engine.run()
