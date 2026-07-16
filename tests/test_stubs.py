"""The .pyi is hand-written, so it drifts unless something checks it.

These tests are cheap and catch the common failure: an API renamed in C++ and
forgotten in the stub, which silently misleads every user's type checker.
"""

import pathlib
import re

import bazalt as bz

PYI = pathlib.Path(bz.__file__).parent / "_core.pyi"


def stub_text():
    return PYI.read_text(encoding="utf-8")


def test_stub_exists_and_package_is_typed():
    assert PYI.is_file()
    assert (PYI.parent / "py.typed").is_file()


def test_every_public_name_is_in_all():
    """`from bazalt import *` should give the same names as `bz.<name>`."""
    for name in bz.__all__:
        assert hasattr(bz, name), f"{name} is in __all__ but missing from the module"


def test_removed_names_are_gone_from_the_stub():
    """Names dropped in 0.4 must not linger in the stub."""
    text = stub_text()
    assert "class Format(" not in text, "Format was renamed to VertexFormat"
    assert "def on_error" not in text, "on_error was replaced by on_message"


def test_renamed_and_new_api_is_declared():
    text = stub_text()
    for expected in ("class VertexFormat(", "def on_message", "class LogMessage",
                     "class Severity(", "class Source(", "class Feature(",
                     "class RenderTarget(", "def read_pixels", "class BazaltError",
                     "def flush"):
        assert expected in text, f"{expected!r} missing from _core.pyi"


def test_stub_does_not_reference_an_undefined_buffer_type():
    """The old stub annotated arrays as `buffer`, which is not a Python type.

    Any type checker flags it, which trains users to ignore the stub.
    """
    assert not re.search(r":\s*buffer\b", stub_text())


def test_exception_hierarchy_matches_the_stub():
    for name in ("BazaltError", "InitializationError", "DeviceLostError",
                 "OutOfMemoryError", "ShaderError", "WindowError", "ResourceError"):
        assert f"class {name}(" in stub_text()
        assert hasattr(bz, name)


def test_version_is_declared():
    assert bz.__version__
