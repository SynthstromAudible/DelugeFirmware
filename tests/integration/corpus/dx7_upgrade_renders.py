#!/usr/bin/env python3
"""A DX7 preset the upgrader converts must play through the DX7 engine as its original played through the slot.

Each preset of tests/golden/render/dx7_presets.json (a DX7 on an oscillator) is upgraded by tools/deluge-upgrade into a
copy of the golden preset corpus, in place of its original, and rendered against the original's record
(tests/golden/render/presets.json) through the golden-synth gate (RENDER_PRESET_CORPUS). A preset whose other
oscillator is silent must render exactly as recorded; one whose other oscillator could sound loses it (the DX7 is a
sound-level engine) and must render differently. Any other outcome fails.

DX7_UPGRADE_EVIDENCE=<dir> also renders the originals and writes each audible case's two renders there, as
`<case>.before.wav` and `<case>.after.wav`, for the record of the change.

    python3 tests/integration/corpus/dx7_upgrade_renders.py

Prints DX7-UPGRADE SAME=<n> CHANGED=<n> FAIL=<n>, and DX7-UPGRADE-FAIL <case>: <why> per failure; exits 1 on any
failure.

Gate dx7-upgrade-renders:
    each DX7 preset of the golden corpus (tests/golden/render/dx7_presets.json), upgraded, plays through the DX7 engine
    exactly as its original played through the slot when its other oscillator is silent, and differently when that
    oscillator could sound
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import driver
import upgrade_run

ROOT = driver.REPO
GOLDEN = driver.GOLDEN / "render" / "presets"
MANIFEST = driver.GOLDEN / "render" / "dx7_presets.json"
CHANGED = re.compile(r"RP-PCM (\S.*?): the rendered output changed")
FAILED_CASE = re.compile(r"RP-(?:CASE|SILENT|LOADED|MIDI|RENDER) (\S.*?):")
SAMPLE_RATE = 44100


def render(corpus: Path | None, names: list[str], keep: Path | None) -> tuple[int, str]:
    """Run golden-synth over `names` of `corpus` (the committed corpus when none); (returncode, output)."""
    env = dict(os.environ, RENDER_PRESET_FILTER=",".join(names))
    if corpus is not None:
        env["RENDER_PRESET_CORPUS"] = str(corpus)
    if keep is not None:
        env["RENDER_PRESET_KEEP"] = str(keep)
    r = subprocess.run(
        [str(ROOT / "dbt"), "test", "golden-synth"],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    return r.returncode, r.stdout + r.stderr


def write_wav(pcm: Path, out: Path) -> None:
    """A kept render (interleaved little-endian i32 stereo) as a 32-bit WAV."""
    out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(out), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(4)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.read_bytes())


def main() -> int:
    presets = json.loads(MANIFEST.read_text())["presets"]
    names = [str(Path(p["path"]).with_suffix("")) for p in presets]
    audible = {
        str(Path(p["path"]).with_suffix("")) for p in presets if p["other_audible"]
    }
    tool = upgrade_run.build_tool()
    fail = 0
    work = Path(tempfile.mkdtemp(prefix="dx7-upgrade-"))
    try:
        corpus = work / "presets"
        shutil.copytree(GOLDEN, corpus)
        report = work / "report.json"
        for preset in presets:
            original = corpus / preset["path"]
            ok, err = upgrade_run.upgrade(tool, original, original.parent, None, report)
            if not ok:
                fail += 1
                print(
                    f"DX7-UPGRADE-FAIL {preset['path']}: the upgrader failed: {err.strip()[-300:]}"
                )
                continue
            original.unlink()
        evidence = os.environ.get("DX7_UPGRADE_EVIDENCE")
        after_keep = work / "after" if evidence else None
        _, output = render(corpus, names, after_keep)
        changed = set(CHANGED.findall(output))
        broken = set(FAILED_CASE.findall(output))
        for name in names:
            if name in broken:
                fail += 1
                print(f"DX7-UPGRADE-FAIL {name}: its upgraded preset did not render")
            elif name in audible and name not in changed:
                fail += 1
                print(
                    f"DX7-UPGRADE-FAIL {name}: its other oscillator could sound, yet it renders as recorded"
                )
            elif name not in audible and name in changed:
                fail += 1
                print(
                    f"DX7-UPGRADE-FAIL {name}: its other oscillator is silent, yet it renders differently"
                )
        ran = re.search(r"RP-CASES run=(\d+)", output)
        if ran is None or int(ran.group(1)) != len(names):
            fail += 1
            print(
                f"DX7-UPGRADE-FAIL golden-synth ran {ran.group(1) if ran else 'no'} case(s), not {len(names)}"
            )
        if evidence:
            before_keep = work / "before"
            render(None, sorted(audible), before_keep)
            for name in sorted(audible):
                for side, keep in (("before", before_keep), ("after", after_keep)):
                    pcm = keep / f"{name}.pcm"
                    if pcm.is_file():
                        write_wav(pcm, Path(evidence) / f"{name}.{side}.wav")
            print(f"DX7-UPGRADE-EVIDENCE {evidence}")
        same = len(names) - len(audible)
        print(f"DX7-UPGRADE SAME={same} CHANGED={len(audible & changed)} FAIL={fail}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
