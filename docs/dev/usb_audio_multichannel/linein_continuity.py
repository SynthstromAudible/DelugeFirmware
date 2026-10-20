# The line-in tone on to-host ch1 should be a perfect 1000 Hz sine whatever the load. Report each discontinuity: is it
# silence padding, or a jump in phase (frames dropped or repeated)?
import sys
import wave

import numpy as np

d = sys.argv[1]
rate = 44100
f = 1000
with wave.open(f"{d}/th.wav") as w:
    n = w.getnframes()
    frames = w.readframes(n)
raw = np.frombuffer(frames, dtype=np.uint8).reshape(-1, 4, 3).astype(np.int32)
x = (
    ((raw[:, 0, 0] << 8) | (raw[:, 0, 1] << 16) | (raw[:, 0, 2] << 24)).astype(np.int32)
) / 2.0**31
k = 2 * np.cos(2 * np.pi * f / rate)
res = np.abs(x[2:] - k * x[1:-1] + x[:-2])
bad = np.where(res > 1e-3)[0] + 2
bad = bad[bad > rate]
groups = np.split(bad, np.where(np.diff(bad) > 8)[0] + 1) if len(bad) else []
print(f"{d}: {n / rate:.1f}s, {len(groups)} discontinuities")


# phase of the tone, to measure how many frames each jump skipped
def phase(i):
    s = x[i : i + 44]
    t = np.arange(44)
    c = np.sum(s * np.cos(2 * np.pi * f * t / rate))
    si = np.sum(s * np.sin(2 * np.pi * f * t / rate))
    return np.arctan2(-c, si)


for g in groups[:40]:
    i0, i1 = g[0], g[-1]
    zeros = int(np.sum(np.abs(x[i0 - 2 : i1 + 2]) < 1e-6))
    pb = phase(i0 - 60)
    pa = phase(i1 + 4)
    expected = 2 * np.pi * f * ((i1 + 4) - (i0 - 60)) / rate
    jump = ((pa - pb - expected + np.pi) % (2 * np.pi)) - np.pi
    print(
        f"  t={i0 / rate:8.3f}s span {i1 - i0 + 1:4d} frames, zeros {zeros:4d}, phase jump {jump * rate / (2 * np.pi * f):+6.1f} frames"
    )
