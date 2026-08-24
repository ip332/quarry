from ._version import __version__

__all__ = ["__version__", "load_qbs", "validate_brf"]


def load_qbs(*args, **kwargs):
    """Load a generic QBS image when the optional generic runtime is present."""
    from .runtime.generic import load_qbs as _load_qbs
    return _load_qbs(*args, **kwargs)


def validate_brf(*args, **kwargs):
    """Validate generic BRF when the optional generic runtime is present."""
    from .runtime.generic import validate_brf as _validate_brf
    return _validate_brf(*args, **kwargs)
