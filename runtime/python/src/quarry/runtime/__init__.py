"""Runtime APIs for generated and generic Quarry codecs.

The generic runtime is loaded lazily so generated-only runtime packages remain
usable when they intentionally omit generic runtime modules.
"""

_GENERIC_NAMES = {
    "BrfError", "BrfLimits", "FieldValue", "GenericRuntimeError", "QbsError",
    "QbsField", "QbsRecord", "QbsSchema", "QbsType", "ResourceLimitError",
    "TypeAccessError", "ValidatedRecord", "BrfTraversalEvent",
    "BrfTraversalEventKind", "BrfTraversalLimits", "load_qbs", "validate_brf",
}

__all__ = sorted(_GENERIC_NAMES)


def __getattr__(name):
    if name in _GENERIC_NAMES:
        from . import generic
        return getattr(generic, name)
    raise AttributeError(name)
