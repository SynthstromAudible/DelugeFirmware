"""The threshold predicates: branches the voice takes on a value's side of a threshold, so a small change can flip
them. Each is evaluated on both builds; a flip is where their outcomes differ.

    slow_attack    an envelope's attack final value > 245632 (the voice holds its levels, or skips to decay)
    noise_gate     the noise volume is non-zero (the noise renders at all)
    modulator_gate an FM modulator's volume is non-zero
    cut_off        a unison part's source sounds (false above the 22.05 kHz cut-off, or when off)
    sample_cache   a source's sample may not be cached (a changing source modulates its pitch)
    clipping       the output reaches full scale (from the audio)

The voice-plane indices follow modulation/params/param.h's Local enum; a dump with another local-param count is refused.
"""

from collections.abc import Callable
from dataclasses import dataclass

import numpy as np

LOCAL_PARAMS = 45  # params::LOCAL_LAST
NOISE_VOLUME = 3
MODULATOR_VOLUME = (4, 5)
ENV_ATTACK = (33, 34, 35, 36)
SLOW_ATTACK = 245632
FULL_SCALE = (1 << 31) - 1


@dataclass(frozen=True)
class Predicate:
    name: str
    kind: str  # "plane" (a voice plane's finals) or "sources" (a voice's source states)
    evaluate: Callable[[np.ndarray], np.ndarray]  # rows in, bool[rows, columns] out
    columns: tuple  # what each output column is: a param index, or a (part, source) slot index


def _columns(finals: np.ndarray, indices) -> np.ndarray:
    return finals[:, list(indices)]


PREDICATES = [
    Predicate(
        "slow_attack",
        "plane",
        lambda f: _columns(f, ENV_ATTACK) > SLOW_ATTACK,
        ENV_ATTACK,
    ),
    Predicate(
        "noise_gate",
        "plane",
        lambda f: _columns(f, (NOISE_VOLUME,)) != 0,
        (NOISE_VOLUME,),
    ),
    Predicate(
        "modulator_gate",
        "plane",
        lambda f: _columns(f, MODULATOR_VOLUME) != 0,
        MODULATOR_VOLUME,
    ),
    Predicate("cut_off", "sources", lambda s: s["active"] != 0, ()),
    Predicate("sample_cache", "sources", lambda s: s["cache"] != 0, ()),
]


def check_layout(local_params: int) -> None:
    """Refuse a dump whose voice plane is not the layout the indices above assume."""
    if local_params != LOCAL_PARAMS:
        raise ValueError(
            f"the dump has {local_params} local params; the predicates know {LOCAL_PARAMS}"
        )


def clipping(pcm: np.ndarray) -> int:
    """How many output samples reach full scale."""
    return int((np.abs(pcm.astype(np.int64)) >= FULL_SCALE).sum())
