"""Rendering with no window at all.

This is what the RenderTarget abstraction bought: before it, CommandBuffer's
recorded commands took a SwapchainRenderer&, so drawing anywhere other than a
window was impossible — and so was testing any of this.
"""

import numpy as np
import pytest

import bazalt as bz

CLEAR = [0.1, 0.2, 0.3, 1.0]
CLEAR_RGB = np.array([26, 51, 77])


def draw_triangle(ctx, target, shaders, buffers, clear=CLEAR):
    vert, frag = shaders
    vbuf, ibuf = buffers
    pipeline = (ctx.pipeline_builder()
                .vertex_shader(vert)
                .fragment_shader(frag)
                .vertex_format([bz.VertexFormat.FLOAT3, bz.VertexFormat.FLOAT3])
                .build(target))

    cmd = ctx.create_command_buffer()
    cmd.begin()
    cmd.begin_rendering(target, clear_color=clear)
    cmd.bind_pipeline(pipeline)
    cmd.bind_vertex_buffer(vbuf)
    cmd.bind_index_buffer(ibuf)
    cmd.draw_indexed(3)
    cmd.end_rendering(target)
    ctx.submit(cmd)
    return target.read_pixels()


def test_render_target_reports_its_size(ctx):
    target = bz.RenderTarget(ctx, 64, 48)
    assert (target.width, target.height) == (64, 48)


def test_read_pixels_shape_and_dtype(ctx):
    target = bz.RenderTarget(ctx, 32, 16)
    pixels = target.read_pixels()
    assert pixels.shape == (16, 32, 4)
    assert pixels.dtype == np.uint8


def test_clear_colour_reaches_the_image(ctx, triangle_shaders, triangle_buffers):
    target = bz.RenderTarget(ctx, 64, 64)
    pixels = draw_triangle(ctx, target, triangle_shaders, triangle_buffers)
    assert np.allclose(pixels[2, 2, :3], CLEAR_RGB, atol=2)


def test_triangle_is_actually_drawn(ctx, triangle_shaders, triangle_buffers):
    target = bz.RenderTarget(ctx, 64, 64)
    pixels = draw_triangle(ctx, target, triangle_shaders, triangle_buffers)
    centre = pixels[32, 32, :3]
    assert not np.allclose(centre, CLEAR_RGB, atol=2), "centre still shows the clear colour"
    assert centre.sum() > 100


def test_vertex_colours_are_interpolated(ctx, triangle_shaders, triangle_buffers):
    """Red at the top vertex, green bottom-left, blue bottom-right."""
    target = bz.RenderTarget(ctx, 64, 64)
    pixels = draw_triangle(ctx, target, triangle_shaders, triangle_buffers)

    near_top = pixels[20, 32, :3]
    bottom_left = pixels[46, 20, :3]
    bottom_right = pixels[46, 44, :3]

    assert near_top[0] == max(near_top), f"top should be reddest, got {near_top}"
    assert bottom_left[1] == max(bottom_left), f"bottom-left should be greenest, got {bottom_left}"
    assert bottom_right[2] == max(bottom_right), f"bottom-right should be bluest, got {bottom_right}"


def test_depth_target_renders(ctx, triangle_shaders, triangle_buffers):
    target = bz.RenderTarget(ctx, 64, 64, depth=True)
    pixels = draw_triangle(ctx, target, triangle_shaders, triangle_buffers)
    assert not np.allclose(pixels[32, 32, :3], CLEAR_RGB, atol=2)


def test_uint16_indices_draw_correctly(ctx, triangle_shaders, triangle_buffers):
    """bind_index_buffer used to hardcode VK_INDEX_TYPE_UINT32.

    A UINT16 index buffer was therefore read as UINT32 at half the count, which
    drew nothing or garbage, silently.
    """
    vbuf, _ = triangle_buffers
    ibuf16 = ctx.create_buffer([0, 1, 2], bz.BufferType.INDEX, bz.MemoryUsage.STATIC,
                               bz.DataType.UINT16)
    target = bz.RenderTarget(ctx, 64, 64)
    pixels = draw_triangle(ctx, target, triangle_shaders, (vbuf, ibuf16))
    assert not np.allclose(pixels[32, 32, :3], CLEAR_RGB, atol=2)


def test_two_targets_from_one_context(ctx, triangle_shaders, triangle_buffers):
    """Multiple render targets coexist — only SwapchainRenderers are restricted."""
    a = bz.RenderTarget(ctx, 32, 32)
    b = bz.RenderTarget(ctx, 48, 48)
    pa = draw_triangle(ctx, a, triangle_shaders, triangle_buffers)
    pb = draw_triangle(ctx, b, triangle_shaders, triangle_buffers)
    assert pa.shape == (32, 32, 4)
    assert pb.shape == (48, 48, 4)


def test_target_is_reusable_after_readback(ctx, triangle_shaders, triangle_buffers):
    """read_pixels must leave the image in final_layout so it stays samplable."""
    target = bz.RenderTarget(ctx, 32, 32)
    first = draw_triangle(ctx, target, triangle_shaders, triangle_buffers)
    second = target.read_pixels()
    assert np.array_equal(first, second)
