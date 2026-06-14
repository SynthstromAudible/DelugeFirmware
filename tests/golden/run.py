#!/usr/bin/env python3
"""Golden-master audio regression harness.

Renders every preset in corpus/ through the host-sim (deluge_host) — booted
from a generated FAT card image so the preset loads through the real
SYNTHS/ deserialization path — drives scripted DIN MIDI note events keyed to
the offline audio clock, and compares the SHA-256 of the rendered PCM against
the committed fixtures.json. Any difference from a code change is either a bug
or a deliberate sound change; both must be explicit (re-baseline with --update
and justify in the commit message).

Usage:
    python3 tests/golden/run.py [--binary PATH] [--filter SUBSTR] [--update]
                                [--keep DIR] [--jobs N]

Requirements: mtools (mformat/mmd/mcopy); a built host-sim (cmake -B build-sim
-S sim -G Ninja && cmake --build build-sim --target deluge_host).

Each corpus case is <name>.xml plus an optional <name>.notes MIDI script
(default.notes otherwise) and an optional capture-seconds override in
SECONDS below. Determinism: DELUGE_HOST_DETERMINISTIC drives all app clocks
and the dither seed off the rendered-frame count; ASLR is additionally
disabled via `setarch -R` when available (defence in depth — pointer values
must never influence rendering).
"""

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
CORPUS = HERE / "corpus"
FIXTURES = HERE / "fixtures.json"

DEFAULT_SECONDS = 2.0
# Per-case capture-length overrides (e.g. long release tails).
SECONDS = {
    "env_shape": 3.0,
}

CARD_BYTES = 64 * 1024 * 1024
RUN_TIMEOUT = 600  # seconds of wall clock per case; a render is ~2s of CPU


def stage_preset(preset_xml, tmp):
    """Copy the preset for the card image, injecting the harness MIDI binding.

    Every case must respond to the scripted DIN input on MIDI channel 1. The
    hand-written corpus presets carry inputMidiChannel="0" themselves; imported
    factory/community presets are committed pristine, so inject the binding as
    a child tag here (the deserializer reads root attributes and child tags
    uniformly, and this works for both the old tag-based and new attribute-based
    preset formats).
    """
    text = preset_xml.read_text()
    if "inputMidiChannel" not in text:
        open_tag_end = text.index(">", text.index("<sound")) + 1
        text = text[:open_tag_end] + "\n\t<inputMidiChannel>0</inputMidiChannel>" + text[open_tag_end:]
    staged = tmp / "PRESET.XML"
    staged.write_text(text)
    return staged


def build_card_image(path, staged_preset, sample_wavs=()):
    with open(path, "wb") as f:
        f.truncate(CARD_BYTES)
    subprocess.run(["mformat", "-i", str(path), "-F", "::"], check=True, capture_output=True)
    subprocess.run(["mmd", "-i", str(path), "::/SYNTHS"], check=True, capture_output=True)
    subprocess.run(
        ["mcopy", "-i", str(path), str(staged_preset), "::/SYNTHS/PRESET.XML"], check=True, capture_output=True
    )
    # A case may carry samples (e.g. a wavetable/sample osc): a sibling <name>.wav is staged to
    # SAMPLES/WT.WAV and an optional <name>.2.wav to SAMPLES/WT2.WAV (a second wavetable osc), which
    # its preset references by those paths.
    if sample_wavs:
        subprocess.run(["mmd", "-i", str(path), "::/SAMPLES"], check=True, capture_output=True)
        for sample_wav, dest in zip(sample_wavs, ("WT.WAV", "WT2.WAV")):
            subprocess.run(
                ["mcopy", "-i", str(path), str(sample_wav), f"::/SAMPLES/{dest}"], check=True, capture_output=True
            )


def read_wav_pcm(path):
    """Return (pcm_bytes, num_frames) from a 16-bit stereo RIFF WAV."""
    blob = path.read_bytes()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a WAV file")
    pos = 12
    while pos + 8 <= len(blob):
        chunk_id = blob[pos : pos + 4]
        chunk_len = struct.unpack_from("<I", blob, pos + 4)[0]
        if chunk_id == b"data":
            data = blob[pos + 8 : pos + 8 + chunk_len]
            return data, len(data) // 4
        pos += 8 + chunk_len + (chunk_len & 1)
    raise ValueError(f"{path} has no data chunk")


def pcm_stats(pcm):
    n = len(pcm) // 2
    if n == 0:
        return {"peak": 0, "rms_db": None}
    samples = struct.unpack(f"<{n}h", pcm)
    peak = max(max(samples), -min(samples))
    acc = 0
    for s in samples:
        acc += s * s
    rms = math.sqrt(acc / n)
    rms_db = round(20 * math.log10(rms / 32768), 2) if rms > 0 else None
    return {"peak": peak, "rms_db": rms_db}


def find_preset(name):
    for suffix in (".xml", ".XML"):
        p = CORPUS / f"{name}{suffix}"
        if p.exists():
            return p
    raise FileNotFoundError(name)


def run_case(name, binary, setarch_prefix, keep_dir):
    try:
        return run_case_inner(name, binary, setarch_prefix, keep_dir)
    except Exception as e:  # report per-case, don't kill the sweep
        return name, None, f"{type(e).__name__}: {e}"


def run_case_inner(name, binary, setarch_prefix, keep_dir):
    preset = find_preset(name)
    notes = preset.with_suffix(".notes")
    if not notes.exists():
        notes = CORPUS / "default.notes"
    seconds = SECONDS.get(name, DEFAULT_SECONDS)

    with tempfile.TemporaryDirectory(prefix="golden_") as tmp:
        tmp = pathlib.Path(tmp)
        image = tmp / "card.img"
        wav = tmp / "out.wav"
        # Optional per-case samples (e.g. wavetable oscs): <name>.wav -> WT.WAV, <name>.2.wav -> WT2.WAV.
        sample = preset.with_suffix(".wav")
        sample2 = preset.with_suffix(".2.wav")
        samples = [s for s in (sample, sample2) if s.exists()]
        build_card_image(image, stage_preset(preset, tmp), samples)

        env = dict(os.environ)
        env.update(
            DELUGE_HOST_DETERMINISTIC="1",
            DELUGE_HOST_CARD_IMAGE=str(image),
            DELUGE_HOST_MIDI_SCRIPT=str(notes),
            DELUGE_HOST_CAPTURE_SECONDS=str(seconds),
            DELUGE_HOST_WAV=str(wav),
        )
        proc = subprocess.run(
            setarch_prefix + [str(binary)],
            env=env,
            capture_output=True,
            text=True,
            timeout=RUN_TIMEOUT,
        )
        if not wav.exists():
            return name, None, f"no WAV produced (exit {proc.returncode})\n{proc.stderr[-2000:]}"

        pcm, frames = read_wav_pcm(wav)
        expected_frames = int(seconds * 44100)
        if frames != expected_frames:
            return name, None, f"short capture: {frames}/{expected_frames} frames"

        if keep_dir:
            dest = keep_dir / f"{name}.wav"
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(wav, dest)

        result = {"sha256": hashlib.sha256(pcm).hexdigest(), "frames": frames, **pcm_stats(pcm)}
        # A silent render almost always means the preset failed to load (the
        # fallback default synth is not MIDI-learned) — fail loudly rather than
        # baselining silence.
        if result["peak"] < 16:
            return name, None, f"silent render (peak {result['peak']}) — preset load failure?"
        return name, result, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(HERE / "../../build-sim/deluge_host"))
    parser.add_argument(
        "--filter", default="", help="comma-separated substrings; run cases whose name contains any of them"
    )
    parser.add_argument("--update", action="store_true", help="re-baseline fixtures.json with current output")
    parser.add_argument("--keep", default=None, help="directory to copy rendered WAVs into (for listening)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                        help="parallel cases (default: all cores)")
    parser.add_argument(
        "--wrapper",
        default=None,
        help="command prefix to run the binary under, e.g. for the ARM-NEON oracle build: "
        "--wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9' "
        "--binary build-sim-arm/deluge_host (its output must match the x86 fixtures bit-exactly)",
    )
    args = parser.parse_args()

    binary = pathlib.Path(args.binary).resolve()
    if not binary.exists():
        sys.exit(f"host-sim binary not found: {binary}\nbuild it: cmake -B build-sim -S sim -G Ninja && "
                 "cmake --build build-sim --target deluge_host")
    for tool in ("mformat", "mmd", "mcopy"):
        if not shutil.which(tool):
            sys.exit(f"missing required tool: {tool} (install mtools)")

    if args.wrapper:
        import shlex

        setarch_prefix = shlex.split(args.wrapper)
    else:
        setarch_prefix = []
        if shutil.which("setarch"):
            setarch_prefix = ["setarch", os.uname().machine, "-R"]

    keep_dir = None
    if args.keep:
        keep_dir = pathlib.Path(args.keep)
        keep_dir.mkdir(parents=True, exist_ok=True)

    cases = sorted(
        str(p.relative_to(CORPUS).with_suffix(""))
        for ext in ("*.xml", "*.XML")
        for p in CORPUS.rglob(ext)
    )
    # --filter takes a comma-separated OR-list of substrings: a case runs if any
    # substring is in its name. This keeps targeted runs cheap — notably the ARM/QEMU
    # oracle, which is far too slow to sweep in full, so only run the cases that
    # exercise the code under test (e.g. --filter sine,fm for the sine kernel).
    if args.filter:
        needles = [s for s in args.filter.split(",") if s]
        cases = [c for c in cases if any(n in c for n in needles)]
    if not cases:
        sys.exit(f"no cases match filter {args.filter!r}")

    fixtures = json.loads(FIXTURES.read_text()) if FIXTURES.exists() else {}

    results, failures = {}, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_case, c, binary, setarch_prefix, keep_dir): c for c in cases}
        for future in concurrent.futures.as_completed(futures):
            name, result, error = future.result()
            if error:
                failures.append((name, error))
                print(f"  ERROR {name}: {error}")
                continue
            results[name] = result
            if args.update:
                print(f"  baselined {name}: {result['sha256'][:16]} peak={result['peak']} rms={result['rms_db']}dB")
            elif name not in fixtures:
                failures.append((name, "no fixture (run with --update to baseline)"))
                print(f"  NEW {name}: no fixture")
            elif fixtures[name]["sha256"] != result["sha256"]:
                failures.append((name, "hash mismatch"))
                print(
                    f"  FAIL {name}: hash mismatch\n"
                    f"        expected {fixtures[name]['sha256'][:16]} peak={fixtures[name]['peak']} rms={fixtures[name]['rms_db']}dB\n"
                    f"        got      {result['sha256'][:16]} peak={result['peak']} rms={result['rms_db']}dB"
                )
            else:
                print(f"  ok {name}")

    if args.update:
        merged = {**fixtures, **results}
        FIXTURES.write_text(json.dumps(dict(sorted(merged.items())), indent=1) + "\n")
        print(f"wrote {FIXTURES} ({len(results)} of {len(merged)} cases re-baselined)")

    print(f"\n{len(cases) - len(failures)}/{len(cases)} cases passed")
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
