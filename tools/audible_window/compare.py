"""Compare two render sets (audible_window.py render) case by case: the audio, the parameters, the threshold flips and
the voices' lifetimes.

Parameters and predicates are compared at the times both sides have, so a voice that ends earlier in one build is a
lifetime change, not a run of differences. The audio measures are compare_renders.py's, with a null test (the RMS of the
aligned difference) and octave-band energies added.
"""

import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import predicates
from dump import load_npz

sys.path.insert(
    0, str(Path(__file__).resolve().parents[2] / "tests" / "integration" / "golden")
)
import compare_renders

FULL_SCALE_16 = 32768.0
BANDS_HZ = [
    31.25 * 2**k for k in range(10)
]  # each band's lower edge, the last running to Nyquist
RATE = 44100


@dataclass
class CaseResult:
    name: str
    audio: dict | None = None
    null_db: float = -math.inf
    bands: list = field(
        default_factory=list
    )  # per band: the new render's energy change in dB
    clipping: tuple = (0, 0)  # full-scale samples, reference and new
    params: list = field(default_factory=list)
    flips: list = field(default_factory=list)
    lifetimes: list = field(default_factory=list)
    identical: bool = False
    missing: str | None = None


def _aligned(ref: np.ndarray, new: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    shift = compare_renders.onset(new) - compare_renders.onset(ref)
    if shift > 0:
        new = new[shift:]
    elif shift < 0:
        ref = ref[-shift:]
    n = min(len(ref), len(new))
    return ref[:n], new[:n]


def _null_db(ref: np.ndarray, new: np.ndarray) -> float:
    ref, new = _aligned(ref, new)
    rms = math.sqrt(float(((new - ref) ** 2).mean())) if len(ref) else 0.0
    return -math.inf if rms == 0 else 20 * math.log10(rms / FULL_SCALE_16)


def _bands(ref: np.ndarray, new: np.ndarray) -> list:
    ref, new = _aligned(ref, new)
    if len(ref) < 2:
        return []
    freqs = np.fft.rfftfreq(len(ref), 1 / RATE)
    spectra = [np.abs(np.fft.rfft(x.sum(axis=1))) ** 2 for x in (ref, new)]
    out = []
    for low, high in zip(BANDS_HZ, BANDS_HZ[1:] + [RATE / 2]):
        mask = (freqs >= low) & (freqs < high)
        r, n = (float(s[mask].sum()) for s in spectra)
        out.append(0.0 if r == n else 10 * math.log10((n + 1e-12) / (r + 1e-12)))
    return out


def _occurrences(times: np.ndarray) -> np.ndarray:
    """Each record's key within its key's records: its time and how many records at that time came before it (a note's
    first patching and its first block share a time)."""
    times = times.astype(np.int64)
    starts = np.searchsorted(times, times, side="left")
    return times * 4096 + (np.arange(len(times)) - starts)


def _common(
    t_ref: np.ndarray, t_new: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """The records both sides have, matched by time and occurrence: (times, reference rows, new rows)."""
    keys, i_ref, i_new = np.intersect1d(
        _occurrences(t_ref),
        _occurrences(t_new),
        assume_unique=True,
        return_indices=True,
    )
    return keys // 4096, i_ref, i_new


def _params(key, ref_pair, new_pair) -> list:
    times, i_ref, i_new = _common(ref_pair[0], new_pair[0])
    ref, new = ref_pair[1][i_ref].astype(np.int64), new_pair[1][i_new].astype(np.int64)
    delta = np.abs(new - ref)
    out = []
    for p in np.nonzero(delta.max(axis=0) if len(delta) else [])[0]:
        rows = np.nonzero(delta[:, p])[0]
        out.append(
            {
                "sound": key[0],
                "scope": key[1],
                "voice": key[2],
                "param": int(p),
                "max_lsb": int(delta[:, p].max()),
                "first_time": int(times[rows[0]]),
                "differing": len(rows),
            }
        )
    return out


def _flips(kind: str, key, ref_pair, new_pair) -> list:
    times, i_ref, i_new = _common(ref_pair[0], new_pair[0])
    out = []
    for pred in (p for p in predicates.PREDICATES if p.kind == kind):
        a = pred.evaluate(ref_pair[1][i_ref])
        b = pred.evaluate(new_pair[1][i_new])
        differ = a != b
        for column in np.nonzero(differ.any(axis=0) if len(differ) else [])[0]:
            rows = np.nonzero(differ[:, column])[0]
            out.append(
                {
                    "predicate": pred.name,
                    "sound": key[0],
                    "voice": key[-1],
                    "column": pred.columns[column] if pred.columns else int(column),
                    "first_time": int(times[rows[0]]),
                    "blocks": len(rows),
                    "ref": bool(a[rows[0], column]),
                    "new": bool(b[rows[0], column]),
                }
            )
    return out


def _lifetimes(ref, new) -> list:
    out = []
    for key in sorted(set(ref.planes) | set(new.planes)):
        if key[1] != 0:
            continue
        r, n = ref.planes.get(key), new.planes.get(key)
        span = lambda pair: (
            (int(pair[0][0]), int(pair[0][-1])) if pair is not None else (None, None)
        )
        (rf, rl), (nf, nl) = span(r), span(n)
        if (rf, rl) != (nf, nl):
            out.append(
                {
                    "sound": key[0],
                    "voice": key[2],
                    "ref_first": rf,
                    "ref_last": rl,
                    "new_first": nf,
                    "new_last": nl,
                }
            )
    return out


def compare_case(ref_dir: Path, new_dir: Path, rel: str) -> CaseResult:
    result = CaseResult(rel)
    paths = {
        side: (d / f"{rel}.pcm", d / f"{rel}.npz")
        for side, d in (("ref", ref_dir), ("new", new_dir))
    }
    for side, (pcm, npz) in paths.items():
        if not (pcm.exists() and npz.exists()):
            result.missing = side
            return result
    ref_raw = np.fromfile(paths["ref"][0], "<i4")
    new_raw = np.fromfile(paths["new"][0], "<i4")
    ref_dump, new_dump = load_npz(paths["ref"][1]), load_npz(paths["new"][1])
    predicates.check_layout(ref_dump.local_params)
    predicates.check_layout(new_dump.local_params)

    ref_audio, new_audio = (
        compare_renders.load(paths["ref"][0]),
        compare_renders.load(paths["new"][0]),
    )
    result.audio = compare_renders.compare(ref_audio, new_audio)
    result.null_db = _null_db(ref_audio, new_audio)
    result.bands = _bands(ref_audio, new_audio)
    result.clipping = (predicates.clipping(ref_raw), predicates.clipping(new_raw))
    for key in sorted(set(ref_dump.planes) & set(new_dump.planes)):
        result.params += _params(key, ref_dump.planes[key], new_dump.planes[key])
        if key[1] == 0:
            result.flips += _flips(
                "plane", key, ref_dump.planes[key], new_dump.planes[key]
            )
    for key in sorted(set(ref_dump.sources) & set(new_dump.sources)):
        result.flips += _flips(
            "sources", key, ref_dump.sources[key], new_dump.sources[key]
        )
    if (result.clipping[0] == 0) != (result.clipping[1] == 0):
        result.flips.append(
            {
                "predicate": "clipping",
                "ref": result.clipping[0],
                "new": result.clipping[1],
            }
        )
    result.lifetimes = _lifetimes(ref_dump, new_dump)
    result.identical = (
        ref_raw.tobytes() == new_raw.tobytes()
        and not result.params
        and not result.flips
        and not result.lifetimes
        and set(ref_dump.planes) == set(new_dump.planes)
        and set(ref_dump.sources) == set(new_dump.sources)
    )
    return result


def compare_sets(ref: Path, new: Path) -> list:
    """Every case and song the reference rendered, compared with the new set."""
    manifest = json.loads((Path(ref) / "manifest.json").read_text())
    names = [("presets", c) for c in manifest["cases"]] + [
        ("songs", s) for s in manifest["songs"]
    ]
    return [
        compare_case(Path(ref) / kind, Path(new) / kind, name) for kind, name in names
    ]
