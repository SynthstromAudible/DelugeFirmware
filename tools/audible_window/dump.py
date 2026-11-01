"""The audible window's parameter dumps: the binary stream the host writes (RENDER_PARAM_DUMP) and its packed form.

The stream (little-endian) starts with a header -- b"PDMP", u16 version, u16 local params, u16 global params -- and
holds records: u8 kind (0 a pass's final values, 1 a voice's source states), u8 scope (0 voice, 1 sound), u16 sound,
u32 time (the audio sample timer), u32 voice serial (the sound's note-on count when the voice started; 0 for the sound
scope), u16 count, then count i32 final values (kind 0) or count source states (kind 1).

`records` reads the stream with the standard library alone (the test harness's Python has no numpy); `read_dump`,
`save_npz` and `load_npz` build numpy arrays from it for the analyser.
"""

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import NamedTuple

MAGIC = b"PDMP"
VERSION = 1
HEADER = struct.Struct("<4sHHH")
RECORD = struct.Struct("<BBHIIH")
SOURCE_STATE = struct.Struct("<BBBBI")
SOURCE_FIELDS = [
    ("part", "u1"),
    ("source", "u1"),
    ("active", "u1"),
    ("cache", "u1"),
    ("phase_increment", "<u4"),
]


class Record(NamedTuple):
    kind: int  # 0 a pass's final values, 1 a voice's source states
    scope: int  # 0 voice, 1 sound
    sound: int
    time: int
    voice: int
    payload: bytes  # count i32 values, or count SOURCE_STATE entries
    count: int


def records(path) -> tuple[int, int, list[Record]]:
    """(local params, global params, records) from the dump at @p path. Raises ValueError for anything that is not a
    whole version-1 dump."""
    try:
        data = Path(path).read_bytes()
    except OSError as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if len(data) < HEADER.size:
        raise ValueError("too short for a header")
    magic, version, local_params, global_params = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"not a version {VERSION} dump: {magic!r} v{version}")
    out = []
    at = HEADER.size
    while at < len(data):
        if at + RECORD.size > len(data):
            raise ValueError(f"a record cut short at byte {at}")
        kind, scope, sound, time, voice, count = RECORD.unpack_from(data, at)
        at += RECORD.size
        if kind not in (0, 1):
            raise ValueError(f"unknown record kind {kind} at byte {at - RECORD.size}")
        size = (4 if kind == 0 else SOURCE_STATE.size) * count
        if at + size > len(data):
            raise ValueError(f"a record's values cut short at byte {at}")
        out.append(Record(kind, scope, sound, time, voice, data[at : at + size], count))
        at += size
    return local_params, global_params, out


@dataclass
class Dump:
    local_params: int
    global_params: int
    # (sound, scope, voice) -> (times u32[n], finals i32[n, count])
    planes: dict = field(default_factory=dict)
    # (sound, voice) -> (times u32[n], states SOURCE[n, count])
    sources: dict = field(default_factory=dict)


def read_dump(path) -> Dump:
    """The dump at @p path as numpy arrays. Raises ValueError for anything that is not a whole version-1 dump."""
    import numpy as np

    local_params, global_params, recs = records(path)
    source = np.dtype(SOURCE_FIELDS)
    planes: dict = {}
    sources: dict = {}
    for r in recs:
        if r.kind == 0:
            entry = planes.setdefault((r.sound, r.scope, r.voice), ([], []))
            entry[1].append(np.frombuffer(r.payload, "<i4", r.count))
        else:
            entry = sources.setdefault((r.sound, r.voice), ([], []))
            entry[1].append(np.frombuffer(r.payload, source, r.count))
        entry[0].append(r.time)
    dump = Dump(local_params, global_params)
    dump.planes = {k: (np.array(t, "<u4"), np.stack(v)) for k, (t, v) in planes.items()}
    dump.sources = {
        k: (np.array(t, "<u4"), np.stack(v)) for k, (t, v) in sources.items()
    }
    return dump


def save_npz(dump: Dump, path) -> None:
    """@p dump packed into a compressed .npz at @p path."""
    import numpy as np

    arrays = {"header": np.array([dump.local_params, dump.global_params], "<i4")}
    for (sound, scope, voice), (times, finals) in dump.planes.items():
        arrays[f"p_{sound}_{scope}_{voice}_t"] = times
        arrays[f"p_{sound}_{scope}_{voice}_v"] = finals
    for (sound, voice), (times, states) in dump.sources.items():
        arrays[f"s_{sound}_{voice}_t"] = times
        arrays[f"s_{sound}_{voice}_v"] = states
    np.savez_compressed(path, **arrays)


def load_npz(path) -> Dump:
    """The dump save_npz packed at @p path."""
    import numpy as np

    with np.load(path) as npz:
        local_params, global_params = (int(x) for x in npz["header"])
        dump = Dump(local_params, global_params)
        for name in npz.files:
            if not name.endswith("_t"):
                continue
            kind, *key = name[:-2].split("_")
            pair = (npz[name], npz[name[:-2] + "_v"])
            (dump.planes if kind == "p" else dump.sources)[
                tuple(int(k) for k in key)
            ] = pair
    return dump
