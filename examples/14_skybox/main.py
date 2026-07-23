"""Skybox from a procedural cubemap — the 0.10 headline, end to end.

  * an EMPTY cubemap: ctx.create_image(size, size, cube=True) makes a 6-layer
    image with a CUBE view (for sampling) and a 2D_ARRAY view (for storage);
  * a compute shader fills all six faces with imageStore, writing through the
    2D_ARRAY storage view — no vertices, no fragment shader;
  * a fullscreen pass turns each pixel into a world-space ray and samples the
    cubemap as a samplerCube. The barrier taking the image from GENERAL
    (storage, all six layers) to SHADER_READ_ONLY (sampled) is recorded for you
    and hoisted before the render pass — no cmd.barrier() by hand.

The sky is regenerated each frame purely to keep the example one command buffer
(a static sky could be generated once, but pinning its layout across submits
would need a manual image barrier the API doesn't expose yet). The camera
slowly rotates so the cubemap is visibly all around you.
"""

import math
import struct
import time

import bazalt as bz

W, H = 900, 600
SKY = 512  # cubemap face resolution

logger = bz.Logger()


@logger.on_message
def on_message(msg):
    print(f"[{msg.severity}] {msg.text}")


window = bz.Window(W, H, "Bazalt Demo - Skybox (procedural cubemap)", logger=logger)
ctx = bz.Context(logger)
renderer = bz.SwapchainRenderer(window, ctx)

# Compute writes the six faces of an empty cubemap.
sky_comp = ctx.compile_shader("sky.comp", bz.ShaderStage.COMPUTE)
generate = ctx.compute_pipeline().shader(sky_comp).storage_image(0).build()
cubemap = ctx.create_image(SKY, SKY, bz.Format.RGBA8, cube=True)

# Skybox pass: fullscreen triangle, per-pixel ray, samplerCube.
vert = ctx.compile_shader("skybox.vert", bz.ShaderStage.VERTEX)
frag = ctx.compile_shader("skybox.frag", bz.ShaderStage.FRAGMENT)
skybox = (ctx.graphics_pipeline()
          .vertex_shader(vert)
          .fragment_shader(frag)
          .texture(0, bz.ShaderStage.FRAGMENT, set=0)
          .push_constant(64, bz.ShaderStage.FRAGMENT)
          .build(renderer))

# One image, bound two ways: storage (written) and sampled (read).
pool = ctx.create_descriptor_pool(max_sets=4, storage_images=4, samplers=4)
gen_set = pool.allocate_set(generate, set=0)
gen_set.set_storage_image(0, cubemap)
sky_set = pool.allocate_set(skybox, set=0)
sky_set.set_image(0, cubemap)


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / length, v[1] / length, v[2] / length)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


TAN_HALF_FOV = math.tan(math.radians(60.0) / 2.0)
ASPECT = W / H


def camera_push(t):
    yaw = t * 0.25
    forward = normalize((math.cos(yaw), 0.15 * math.sin(t * 0.5), math.sin(yaw)))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    up = cross(right, forward)
    return struct.pack(
        "16f",
        right[0], right[1], right[2], 0.0,
        up[0], up[1], up[2], 0.0,
        forward[0], forward[1], forward[2], 0.0,
        TAN_HALF_FOV, ASPECT, 0.0, 0.0)


def record(cmd, t):
    cmd.begin()
    (cmd.bind_pipeline(generate)
        .bind_descriptor_set(gen_set, generate, set=0)
        .dispatch((SKY + 7) // 8, (SKY + 7) // 8, 6))
    with cmd.rendering(renderer):
        (cmd.bind_pipeline(skybox)
            .bind_descriptor_set(sky_set, skybox, set=0)
            .push_constants(skybox, 0, camera_push(t))
            .draw(3))


cmd = ctx.create_command_buffer()
start = time.time()
while window.is_open():
    window.poll_events()
    frame = renderer.begin_frame()
    if frame is None:
        continue
    record(cmd, time.time() - start)
    frame.submit(cmd)
