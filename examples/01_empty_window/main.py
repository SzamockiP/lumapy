import bazalt as bz
import time

# Create window and renderer separately
window = bz.Window(1024, 720, "Bazalt Demo - Empty")
renderer = bz.Renderer()
renderer.connect(window)

@renderer.on_error
def error(msg):
    print(msg)

# Record command buffer once
cmd = renderer.create_command_buffer()
cmd.begin()
cmd.begin_rendering(clear_color=[0.1, 0.1, 0.1, 1.0])
cmd.end_rendering()

# Main loop
last_time = time.time()
frame_count = 0
fps_timer = 0.0

while window.is_open():
    window.poll_events()

    if renderer.begin_frame():
        current_time = time.time()
        dt = current_time - last_time
        last_time = current_time

        frame_count += 1
        fps_timer += dt

        if fps_timer >= 1.0:
            avg_fps = frame_count / fps_timer
            window.set_title(f"Bazalt Demo - Empty | {1000.0/avg_fps:.2f} ms/frame | {avg_fps:.1f} FPS")
            frame_count = 0
            fps_timer = 0.0

        renderer.submit(cmd)
