"""Compute needs no images: dispatch -> SSBO -> numpy assert.

This is why compute tests are the first thing CI on a software rasterizer
runs — no golden images, just arithmetic.
"""

import struct

import numpy as np
import pytest

import bazalt as bz

from conftest import SHADER_DIR


def test_compute_shader_compiles(ctx):
    comp = ctx.compile_shader(str(SHADER_DIR / "double.comp"), bz.ShaderStage.COMPUTE)
    assert comp is not None


def test_compute_pipeline_builds_without_a_target(ctx):
    comp = ctx.compile_shader(str(SHADER_DIR / "double.comp"), bz.ShaderStage.COMPUTE)
    pipeline = ctx.compute_pipeline().shader(comp).storage_buffer(0).build()
    assert pipeline is not None


def test_compute_pipeline_without_shader_raises(ctx):
    with pytest.raises(bz.ShaderError):
        ctx.compute_pipeline().build()


def test_bad_compute_shader_raises_shader_error(ctx, tmp_path):
    bad = tmp_path / "bad.comp"
    bad.write_text("#version 450\nlayout(local_size_x = 1) in;\nvoid main() { nonsense }\n")

    with pytest.raises(bz.ShaderError) as info:
        ctx.compile_shader(str(bad), bz.ShaderStage.COMPUTE)
    assert info.value.path == str(bad)


# ── dispatch → readback ───────────────────────────────────────────────────


def test_dispatch_doubles_a_storage_buffer(ctx):
    comp = ctx.compile_shader(str(SHADER_DIR / "double.comp"), bz.ShaderStage.COMPUTE)
    pipeline = ctx.compute_pipeline().shader(comp).storage_buffer(0).build()

    data = np.arange(128, dtype=np.float32)
    sbuf = ctx.create_buffer(data, bz.BufferType.STORAGE, bz.MemoryUsage.STATIC)
    pool = ctx.create_descriptor_pool(max_sets=8, storage_buffers=8)
    dset = pool.allocate_set(pipeline, set=0)
    dset.set_buffer(0, sbuf)

    cmd = ctx.create_command_buffer()
    cmd.begin()
    cmd.bind_pipeline(pipeline)
    cmd.bind_descriptor_set(dset, pipeline, set=0)
    cmd.dispatch(2)  # 128 / local_size_x=64
    ctx.submit(cmd)

    # The headless submit waits idle, so no barrier is needed between the
    # dispatch and this readback.
    assert np.allclose(sbuf.read(np.float32), data * 2)


def test_push_constants_reach_a_compute_shader(ctx):
    comp = ctx.compile_shader(str(SHADER_DIR / "push_add.comp"), bz.ShaderStage.COMPUTE)
    pipeline = (ctx.compute_pipeline()
                .shader(comp)
                .storage_buffer(0)
                .push_constant(4)
                .build())

    data = np.arange(64, dtype=np.float32)
    sbuf = ctx.create_buffer(data, bz.BufferType.STORAGE, bz.MemoryUsage.STATIC)
    pool = ctx.create_descriptor_pool(max_sets=8, storage_buffers=8)
    dset = pool.allocate_set(pipeline, set=0)
    dset.set_buffer(0, sbuf)

    cmd = ctx.create_command_buffer()
    cmd.begin()
    cmd.bind_pipeline(pipeline)
    cmd.bind_descriptor_set(dset, pipeline, set=0)
    cmd.push_constants(pipeline, 0, struct.pack("<f", 5.0))
    cmd.dispatch(1)
    ctx.submit(cmd)

    assert np.allclose(sbuf.read(np.float32), data + 5.0)


def test_uniform_buffer_reaches_a_compute_shader(ctx):
    comp = ctx.compile_shader(str(SHADER_DIR / "ubo_scale.comp"), bz.ShaderStage.COMPUTE)
    pipeline = (ctx.compute_pipeline()
                .shader(comp)
                .storage_buffer(0)
                .uniform_buffer(1)
                .build())

    data = np.arange(64, dtype=np.float32)
    sbuf = ctx.create_buffer(data, bz.BufferType.STORAGE, bz.MemoryUsage.STATIC)
    ubuf = ctx.create_buffer(np.array([3.0], dtype=np.float32),
                             bz.BufferType.UNIFORM, bz.MemoryUsage.DYNAMIC)
    pool = ctx.create_descriptor_pool(max_sets=8, storage_buffers=8, uniform_buffers=8)
    dset = pool.allocate_frame_set(pipeline, set=0)
    dset.set_buffer(0, sbuf)
    dset.set_buffer(1, ubuf)

    cmd = ctx.create_command_buffer()
    cmd.begin()
    cmd.bind_pipeline(pipeline)
    cmd.bind_descriptor_set(dset, pipeline, set=0)
    cmd.dispatch(1)
    ctx.submit(cmd)

    assert np.allclose(sbuf.read(np.float32), data * 3.0)
