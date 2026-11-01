"""Render the audible window's comparison set: every golden-synth case and the song set, each with its parameter dump.

    out/presets/<case>.pcm, out/presets/<case>.npz   from golden-synth (RENDER_PRESET_KEEP, RENDER_PARAM_DUMP_DIR)
    out/songs/<song>.pcm, out/songs/<song>.npz       from render_song, 30 s each
    out/manifest.json                                the commit, the cases and songs rendered, any that failed

golden-synth compares each case with its record and fails a changed one, which a candidate build is expected to do; the
renders are kept either way, so a case counts as rendered when its output and dump exist.
"""

import json
import os
import subprocess
from pathlib import Path

import songs as song_set
from dump import read_dump, save_npz

SONG_BLOCKS = 10336  # 30 s of 128-frame blocks at 44.1 kHz
HARNESS = Path("build") / "cargo-harness" / "release" / "golden_vt_render"


def _pack(raw: Path, packed: Path) -> bool:
    try:
        dump = read_dump(raw)
    except ValueError:
        return False
    packed.parent.mkdir(parents=True, exist_ok=True)
    save_npz(dump, packed)
    raw.unlink()
    return True


def render_presets(
    root: Path, out: Path, needles: list[str]
) -> tuple[list[str], list[str]]:
    """(rendered, failed) case names, rendered through golden-synth into @p out / presets."""
    presets, raw = out / "presets", out / "presets-raw"
    env = dict(
        os.environ, RENDER_PRESET_KEEP=str(presets), RENDER_PARAM_DUMP_DIR=str(raw)
    )
    if needles:
        env["RENDER_PRESET_FILTER"] = ",".join(needles)
    subprocess.run(["./dbt", "test", "golden-synth"], cwd=root, env=env, check=False)
    # A case is one whose output golden-synth kept; its other runs (the MIDI-learn check's) dump without keeping
    rendered, failed = [], []
    for pcm in sorted(presets.rglob("*.pcm")):
        name = pcm.relative_to(presets).with_suffix("").as_posix()
        if _pack(raw / f"{name}.params", presets / f"{name}.npz"):
            rendered.append(name)
        else:
            failed.append(name)
    return rendered, failed


def render_song(root: Path, corpus: Path, out: Path, name: str) -> bool:
    """Render the corpus song @p name (its path under SONGS/, no suffix) into @p out / songs."""
    target = out / "songs" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    raw = target.with_suffix(".params")
    env = dict(
        os.environ,
        GOLDEN_SCENARIO="render_song",
        RENDER_TREE=str(corpus.parent),
        RENDER_SONG=f"SONGS/{name}.json",
        RENDER_BLOCKS=str(SONG_BLOCKS),
        RENDER_PCM_OUT=str(target.with_suffix(".pcm")),
        RENDER_PARAM_DUMP=str(raw),
    )
    result = subprocess.run(
        [str(root / HARNESS)], cwd=root, env=env, capture_output=True, check=False
    )
    return (
        result.returncode == 0
        and target.with_suffix(".pcm").exists()
        and _pack(raw, target.with_suffix(".npz"))
    )


def render_all(root: Path, out: Path, needles: list[str], corpus: Path) -> dict:
    """Render the presets matching @p needles (all when empty) and the song set from @p corpus into @p out."""
    out.mkdir(parents=True, exist_ok=True)
    cases, failed_cases = render_presets(root, out, needles)
    names = song_set.pick(corpus)
    rendered_songs = [n for n in names if render_song(root, corpus, out, n)]
    manifest = {
        "commit": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip(),
        "cases": cases,
        "songs": rendered_songs,
        "failed": failed_cases + [n for n in names if n not in rendered_songs],
        "song_blocks": SONG_BLOCKS,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    return manifest
