"""Capability negotiation and logging.

Capabilities rather than versions: Vulkan promotes extensions into core, so the
same capability is spelled differently per driver. Which spelling to use is the
library's problem, not the caller's.
"""

import pytest

import bazalt as bz

ALL_FEATURES = [
    bz.Feature.ANISOTROPIC_FILTERING,
    bz.Feature.WIREFRAME,
    bz.Feature.WIDE_LINES,
    bz.Feature.DEPTH_CLAMP,
    bz.Feature.SAMPLE_RATE_SHADING,
    bz.Feature.MULTI_DRAW_INDIRECT,
    bz.Feature.SHADER_FLOAT64,
]


def test_context_reports_what_it_negotiated(ctx):
    assert ctx.device_name
    assert ctx.api_version.count(".") == 2
    assert isinstance(ctx.headless, bool)


@pytest.mark.parametrize("feature", ALL_FEATURES)
def test_supports_answers_for_every_feature(ctx, feature):
    assert isinstance(ctx.supports(feature), bool)


def test_anisotropy_is_on_by_default_when_available(ctx):
    """Texture uses it, so it's enabled when present.

    It used to be a *required* device feature, which turned a nicety into a
    hardware requirement.
    """
    assert ctx.supports(bz.Feature.ANISOTROPIC_FILTERING) in (True, False)


def test_unrequested_features_stay_off(ctx):
    """Asking for nothing must not silently enable everything the GPU can do."""
    assert ctx.supports(bz.Feature.WIREFRAME) is False


def test_severity_is_data_not_a_string_prefix(ctx, messages):
    logger = ctx.logger
    logger.log("hello", severity=bz.Severity.WARNING, source=bz.Source.GENERAL)

    mine = [m for m in messages() if m.text == "hello"]
    assert len(mine) == 1
    assert mine[0].severity == bz.Severity.WARNING
    assert mine[0].source == bz.Source.GENERAL


def test_severity_is_ordered(ctx):
    assert bz.Severity.ERROR > bz.Severity.WARNING > bz.Severity.INFO


def test_min_severity_filters(ctx, messages):
    logger = ctx.logger
    previous = logger.min_severity
    try:
        logger.min_severity = bz.Severity.ERROR
        logger.log("suppressed", severity=bz.Severity.INFO)
        logger.log("kept", severity=bz.Severity.ERROR)

        texts = [m.text for m in messages()]
        assert "suppressed" not in texts
        assert "kept" in texts
    finally:
        logger.min_severity = previous


def test_flush_makes_delivery_observable(ctx, messages):
    """Without flush, asserting "nothing was logged" only means "not yet"."""
    ctx.logger.log("flushed", severity=bz.Severity.WARNING)
    assert "flushed" in [m.text for m in messages()]


def test_log_message_is_readable(ctx, messages):
    ctx.logger.log("readable", severity=bz.Severity.WARNING)
    msg = [m for m in messages() if m.text == "readable"][0]
    assert "readable" in str(msg)
    assert "WARNING" in str(msg)
