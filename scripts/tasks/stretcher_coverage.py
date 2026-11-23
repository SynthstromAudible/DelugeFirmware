"""The gate runs and the summary behind scripts/tasks/stretcher_coverage.sh, the time stretcher's coverage.

stretcher_coverage.py run <golden_vt_render> <out-dir>
    Runs golden-synth (narrowed by the caller's RENDER_PRESET_FILTER, if set) and every x86 render-song gate against the
    instrumented binary, through the gates' own checks. Each render that is compared with its record profiles
    into <out-dir>/profraw; a song gate's dry-twin and unswitched renders, which are only checked to differ, into
    <out-dir>/profraw-unrecorded. Exits 1 if a gate failed.

stretcher_coverage.py report <export.json>
    Prints, as Markdown, the line and branch coverage of every function in the stretcher's files and of each user
    function that calls into it (USERS), and every branch side no recorded render took.
"""

import concurrent.futures
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/tasks"))

# The stretcher's own files: every function in them is reported.
STRETCHER = (
    "dsp_legacy/timestretch/time_stretcher.cpp",
    "dsp_legacy/timestretch/time_stretcher.h",
)
# Each user's functions that call into TimeStretcher or branch on whether a voice has one. readSamplesNative and
# readSamplesResampled take a TimeStretcher* only to pass along, and never read it.
USERS = {
    "model/clip/audio_clip.cpp": (
        "AudioClip::processCurrentPos(",
        "AudioClip::resumePlayback(",
        "AudioClip::render(",
        "AudioClip::getLoopingType(",
        "AudioClip::getCullImmunity(",
    ),
    "model/sample/sample.cpp": (
        "Sample::fillPercCache(",
        "Sample::getAveragesForCrossfade(",
        "Sample::prepareToReadPercCache(",
    ),
    "model/sample/sample_low_level_reader.cpp": (
        "SampleLowLevelReader::readSamplesForTimeStretching(",
    ),
    "model/voice/voice_sample.cpp": (
        "VoiceSample::beenUnassigned(",
        "VoiceSample::noteOn(",
        "VoiceSample::endTimeStretching(",
        "VoiceSample::fudgeTimeStretchingToAvoidClick(",
        "VoiceSample::weShouldBeTimeStretchingNow(",
        "VoiceSample::render(",
        "VoiceSample::sampleZoneChanged(",
        "VoiceSample::getPlaySample(",
    ),
    "processing/engines/audio_engine.cpp": (
        "AudioEngine::solicitTimeStretcher(",
        "AudioEngine::timeStretcherUnassigned(",
    ),
}
# The basenames of the outputs render_song.py writes for renders it does not compare with a record.
UNRECORDED = ("dry.pcm", "unswitched.pcm")


def run(binary: Path, out: Path) -> int:
    import harness_registry as hr
    import scenario_gate
    import test_builds as tb

    spec = importlib.util.spec_from_file_location(
        "task_test", ROOT / "scripts/tasks/task-test.py"
    )
    task_test = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(task_test)

    for pool in ("profraw", "profraw-unrecorded"):
        for old in (out / pool).glob("*.profraw"):
            old.unlink()
        (out / pool).mkdir(parents=True, exist_ok=True)
    products = tb.products(ROOT)
    entries = [
        h
        for h in hr.load_registry(ROOT)
        if h.builds == ["golden-vt-render"]
        and (h.name == "golden-synth" or h.scenario == "render_song")
    ]
    gate_run = scenario_gate.Gate.run

    def profiled_run(self, scenario=None, env=None, timeout=None):
        env = dict(env or {})
        unrecorded = Path(env.get("RENDER_PCM_OUT", "")).name in UNRECORDED
        pool = out / ("profraw-unrecorded" if unrecorded else "profraw")
        # %c writes as the counters count, since the harness ends with a bare exit_group and no at-exit write runs;
        # %8m merges every process into a pool of files, where %p would let a process whose pid was reused overwrite
        # an earlier one's profile.
        env["LLVM_PROFILE_FILE"] = str(pool / "cov-%c%8m.profraw")
        return gate_run(self, scenario, env, timeout)

    scenario_gate.Gate.run = profiled_run

    def one(h):
        with (out / f"gates.{h.name}.log").open("w") as log:
            env = task_test._environment(ROOT, h, products)
            return h.name, scenario_gate.run_gate(ROOT, h, env, binary, log)

    # golden-synth runs its cases in parallel itself; the songs run one gate at a time each, a few at once.
    with concurrent.futures.ThreadPoolExecutor(6) as pool:
        results = sorted(pool.map(one, entries))
    failed = [name for name, ok in results if not ok]
    with (out / "gates.log").open("w") as log:
        for name, ok in results:
            print(f"{name}: {'PASS' if ok else 'FAIL'}", file=log)
            log.write((out / f"gates.{name}.log").read_text())
    print(
        f"coverage gates: {len(results) - len(failed)} passed, {len(failed)} failed {failed}"
    )
    return 1 if failed else 0


def _short(name: str) -> str:
    """@p name without its parameters; a lambda keeps the name of the function it is in."""
    name = name.replace("(anonymous namespace)::", "")
    outer = re.sub(r"\(.*", "", name)
    return f"{outer} (lambda)" if "::$_" in name else outer


def _selected(path: str, name: str) -> bool:
    if path in STRETCHER:
        return True
    return any(name.startswith(prefix) for prefix in USERS.get(path, ()))


def _shown(show: Path):
    """The executable lines' counts in llvm-cov show's text output under @p show, by source line, per file."""
    cache: dict[str, dict[int, str]] = {}

    def counts(path: str) -> dict[int, str]:
        if path not in cache:
            text = next(show.rglob(f"src/deluge/{path}.txt")).read_text()
            cache[path] = {}
            for line in text.split("\n"):
                m = re.match(r"^ *(\d+)\| *([0-9.]+[kMGE]?)\|", line)
                if m:
                    cache[path][int(m.group(1))] = m.group(2)
        return cache[path]

    return counts


def report(export: Path) -> int:
    shown = _shown(export.parent / "show")
    functions = json.loads(export.read_text())["data"][0]["functions"]
    mangled = [f["name"] for f in functions]
    demangled = subprocess.run(
        ["llvm-cxxfilt"],
        input="\n".join(mangled),
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split("\n")
    sources: dict[str, list[str]] = {}
    rows = []
    zeros = []
    seen = set()
    for function, name in zip(functions, demangled):
        path = function["filenames"][0].split("src/deluge/", 1)[-1]
        # A static function's name carries its translation unit ("unity_0_cxx.cxx:"); the unity build is not a source.
        name = re.sub(r"^[^ (]*\.cxx:", "", name)
        if not _selected(path, name) or (path, name) in seen:
            continue
        seen.add((path, name))
        # Code regions in the function's own file: [line, col, end line, end col, count, file, expanded file, kind].
        regions = [r for r in function["regions"] if r[5] == 0 and r[7] == 0]
        first, last = min(r[0] for r in regions), max(r[2] for r in regions)
        # Lines: the executable lines in its span, with the counts llvm-cov show gives them.
        counts = [c for line, c in shown(path).items() if first <= line <= last]
        lines_run = sum(1 for c in counts if c != "0")
        # Branches: [line, col, end line, end col, true count, false count, file, expanded file, kind].
        branches = [b for b in function["branches"] if b[6] == 0]
        sides = 2 * len(branches)
        sides_run = sum((b[4] > 0) + (b[5] > 0) for b in branches)
        short = _short(name)
        rows.append(
            (path, short, function["count"], lines_run, len(counts), sides_run, sides)
        )
        if path not in sources:
            sources[path] = (ROOT / "src/deluge" / path).read_text().split("\n")
        for b in branches:
            if b[4] == 0 or b[5] == 0:
                untaken = (
                    "both"
                    if b[4] == 0 and b[5] == 0
                    else ("true" if b[4] == 0 else "false")
                )
                text = sources[path][b[0] - 1].strip()
                zeros.append((path, b[0], b[1], short, untaken, b[4], b[5], text))
    print("## Functions\n")
    print("| file | function | calls | lines run | branch sides taken |")
    print("|---|---|---|---|---|")
    for path, short, calls, lines_run, lines, sides_run, sides in rows:
        print(
            f"| {path} | {short} | {calls} | {lines_run}/{lines} | {sides_run}/{sides} |"
        )
    print("\n## Branch sides no recorded render takes\n")
    print("| where | function | side not taken | true / false counts | source |")
    print("|---|---|---|---|---|")
    for path, line, col, short, untaken, t, f, text in sorted(zeros):
        text = text.replace("|", "\\|")
        print(
            f"| {Path(path).name}:{line}:{col} | {short} | {untaken} | {t} / {f} | `{text}` |"
        )
    return 0


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "run":
        return run(Path(sys.argv[2]), Path(sys.argv[3]))
    if len(sys.argv) == 3 and sys.argv[1] == "report":
        return report(Path(sys.argv[2]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
