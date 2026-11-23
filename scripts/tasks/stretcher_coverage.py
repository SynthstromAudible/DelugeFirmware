"""The gate runs, the scope and the summary behind scripts/tasks/stretcher_coverage.sh, the time stretcher's coverage.

stretcher_coverage.py files
    Prints the repo-relative source files the report covers (SCOPE), one a line.
stretcher_coverage.py tree-defines | cargo-build
    Prints, one argument a line, the CMake defines and the cargo command build/host-app and golden_vt_render are built
    with (scripts/tasks/test_builds.py), so the coverage trees build as the test trees do.
stretcher_coverage.py run <golden_vt_render> <out-dir>
    Runs golden-synth (narrowed by the caller's RENDER_PRESET_FILTER, if set) and every render-song gate whose output is
    compared with tests/golden/render/songs.json against the instrumented binary, through the gates' own checks. Each
    gate profiles into its own pool, <out-dir>/profraw/<gate>/; a song gate's dry-twin and unswitched renders, which are
    only checked to differ, into <out-dir>/profraw-unrecorded/<gate>/. Gates the registry marks `alone` run on their
    own after the rest. Exits 1 if a gate failed.
stretcher_coverage.py report <export.json>
    Prints, as Markdown, the line and branch coverage of every function SCOPE selects, and every branch side no recorded
    render took.
"""

import concurrent.futures
import importlib.util
import inspect
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/tasks"))

# The files reported, by repo-relative path, and the functions of each that are: None for every function (the
# stretcher's own files, and the sample reader's, which the read head replaces whole), else the prefixes of the
# demangled names of its functions that call into TimeStretcher, branch on whether a voice has one, or keep the hop
# budget and the culling bypass a hop sets (renderAudio and renderAudioForStemExport reset
# numHopsEndedThisRoutineCall; setDireness passes bypassCulling to cullVoices).
SCOPE: dict[str, tuple[str, ...] | None] = {
    "src/deluge/dsp_legacy/timestretch/time_stretcher.cpp": None,
    "src/deluge/dsp_legacy/timestretch/time_stretcher.h": None,
    "src/deluge/model/clip/audio_clip.cpp": (
        "AudioClip::processCurrentPos(",
        "AudioClip::resumePlayback(",
        "AudioClip::render(",
        "AudioClip::getLoopingType(",
        "AudioClip::getCullImmunity(",
    ),
    "src/deluge/model/sample/sample.cpp": (
        "Sample::fillPercCache(",
        "Sample::getAveragesForCrossfade(",
        "Sample::prepareToReadPercCache(",
    ),
    "src/deluge/model/sample/sample_low_level_reader.cpp": None,
    "src/deluge/model/voice/voice_sample.cpp": (
        "VoiceSample::beenUnassigned(",
        "VoiceSample::noteOn(",
        "VoiceSample::endTimeStretching(",
        "VoiceSample::fudgeTimeStretchingToAvoidClick(",
        "VoiceSample::weShouldBeTimeStretchingNow(",
        "VoiceSample::render(",
        "VoiceSample::sampleZoneChanged(",
        "VoiceSample::getPlaySample(",
    ),
    "src/deluge/processing/engines/audio_engine.cpp": (
        "AudioEngine::solicitTimeStretcher(",
        "AudioEngine::timeStretcherUnassigned(",
        "AudioEngine::renderAudio(",
        "AudioEngine::renderAudioForStemExport(",
        "AudioEngine::setDireness(",
        "AudioEngine::cullVoices(",
    ),
}
# The record the render-song gates compare their output with: a gate that names it in its baseline pins what it plays.
SONG_RECORDS = "tests/golden/render/songs.json"
# The basenames of the outputs render_song.py writes for renders it does not compare with a record.
UNRECORDED = ("dry.pcm", "unswitched.pcm")
# The harness functions run() patches or calls that are no API of theirs, with the parameters it relies on.
GATE_RUN_PARAMETERS = ["self", "scenario", "env", "timeout"]
ENVIRONMENT_PARAMETERS = ["root", "h", "products"]


def load_task_test():
    """scripts/tasks/task-test.py as a module (its name is no identifier)."""
    spec = importlib.util.spec_from_file_location(
        "task_test", ROOT / "scripts/tasks/task-test.py"
    )
    task_test = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(task_test)
    return task_test


def check_seams(scenario_gate, task_test) -> None:
    """Raise RuntimeError unless scenario_gate.Gate.run and task-test.py's _environment are there with the parameters
    run() relies on: it replaces the first to profile each run, and calls the second for each gate's environment."""
    seams = (
        (
            "scenario_gate.Gate.run",
            getattr(getattr(scenario_gate, "Gate", None), "run", None),
            GATE_RUN_PARAMETERS,
        ),
        (
            "task-test.py's _environment",
            getattr(task_test, "_environment", None),
            ENVIRONMENT_PARAMETERS,
        ),
    )
    for name, function, wanted in seams:
        if function is None:
            raise RuntimeError(
                f"stretcher_coverage.py: {name} is gone; run() patches or calls it"
            )
        got = list(inspect.signature(function).parameters)
        if got != wanted:
            raise RuntimeError(
                f"stretcher_coverage.py: {name} takes ({', '.join(got)}), not ({', '.join(wanted)}); "
                "update run() to match"
            )


def run(binary: Path, out: Path) -> int:
    import harness_registry as hr
    import scenario_gate
    import test_builds as tb

    task_test = load_task_test()
    check_seams(scenario_gate, task_test)

    for pool in ("profraw", "profraw-unrecorded"):
        for old in (out / pool).rglob("*.profraw"):
            old.unlink()
    products = tb.products(ROOT)
    entries = [
        h
        for h in hr.load_registry(ROOT)
        if h.builds == ["golden-vt-render"]
        and h.status == "gate"
        and (
            h.name == "golden-synth"
            or (h.scenario == "render_song" and SONG_RECORDS in h.baseline)
        )
    ]
    gate_run = scenario_gate.Gate.run

    def profiled_run(self, scenario=None, env=None, timeout=None):
        env = dict(env or {})
        unrecorded = Path(env.get("RENDER_PCM_OUT", "")).name in UNRECORDED
        pool = out / ("profraw-unrecorded" if unrecorded else "profraw") / self.name
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

    # golden-synth runs its cases in parallel itself; the songs run one gate at a time each, a few at once. A gate the
    # registry marks `alone` measures real time or loads the machine, so it runs by itself afterwards, as ./dbt test
    # runs it.
    with concurrent.futures.ThreadPoolExecutor(6) as pool:
        results = list(pool.map(one, [h for h in entries if not h.alone]))
    results += [one(h) for h in entries if h.alone]
    results.sort()
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
    if path not in SCOPE:
        return False
    prefixes = SCOPE[path]
    return prefixes is None or any(name.startswith(prefix) for prefix in prefixes)


def _repo_path(path: str) -> str:
    """@p path, an absolute source path from the coverage mapping, relative to the repository."""
    return str(Path(path).resolve().relative_to(ROOT))


def _shown(show: Path):
    """The executable lines' counts in llvm-cov show's text output under @p show, by source line, per file."""
    cache: dict[str, dict[int, str]] = {}

    def counts(path: str) -> dict[int, str]:
        if path not in cache:
            shown_file = next(show.rglob(f"{path}.txt"), None)
            if shown_file is None:
                raise FileNotFoundError(
                    f"llvm-cov show wrote no {path}.txt under {show}: is {path} in the binary's coverage mapping?"
                )
            text = shown_file.read_text()
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
    picked = []
    seen = set()
    for function, name in zip(functions, demangled):
        try:
            path = _repo_path(function["filenames"][0])
        except ValueError:
            continue
        # A static function's name carries its translation unit ("unity_0_cxx.cxx:"); the unity build is not a source.
        name = re.sub(r"^[^ (]*\.cxx:", "", name)
        if not _selected(path, name) or (path, name) in seen:
            continue
        seen.add((path, name))
        # Code regions in the function's own file: [line, col, end line, end col, count, file, expanded file, kind].
        regions = [r for r in function["regions"] if r[5] == 0 and r[7] == 0]
        span = (min(r[0] for r in regions), max(r[2] for r in regions))
        picked.append((path, name, function, span))
    rows = []
    zeros = []
    total_sides = 0
    for path, name, function, (first, last) in picked:
        # A lambda's lines are its own, not also its enclosing function's.
        nested = [
            s
            for p, n, _, s in picked
            if p == path
            and n != name
            and first <= s[0]
            and s[1] <= last
            and s != (first, last)
        ]
        counts = [
            c
            for line, c in shown(path).items()
            if first <= line <= last and not any(a <= line <= b for a, b in nested)
        ]
        lines_run = sum(1 for c in counts if c != "0")
        # Branches: [line, col, end line, end col, true count, false count, file, expanded file, kind].
        branches = [b for b in function["branches"] if b[6] == 0]
        sides = 2 * len(branches)
        total_sides += sides
        sides_run = sum((b[4] > 0) + (b[5] > 0) for b in branches)
        short = _short(name)
        rows.append(
            (path, short, function["count"], lines_run, len(counts), sides_run, sides)
        )
        if path not in sources:
            sources[path] = (ROOT / path).read_text().split("\n")
        for b in branches:
            if b[4] == 0 or b[5] == 0:
                untaken = (
                    "both"
                    if b[4] == 0 and b[5] == 0
                    else ("true" if b[4] == 0 else "false")
                )
                text = sources[path][b[0] - 1].strip()
                zeros.append((path, b[0], b[1], short, untaken, b[4], b[5], text))
    untaken_sides = sum(2 if z[4] == "both" else 1 for z in zeros)
    print("## Functions\n")
    print("| file | function | calls | lines run | branch sides taken |")
    print("|---|---|---|---|---|")
    for path, short, calls, lines_run, lines, sides_run, sides in rows:
        print(
            f"| {path} | {short} | {calls} | {lines_run}/{lines} | {sides_run}/{sides} |"
        )
    print(
        f"\nBranch sides taken: {total_sides - untaken_sides} of {total_sides}; untaken: {untaken_sides}."
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
    if sys.argv[1:] == ["files"]:
        print("\n".join(SCOPE))
        return 0
    if sys.argv[1:] in (["tree-defines"], ["cargo-build"]):
        import test_builds as tb

        print(
            "\n".join(
                tb.TEST_TREE_DEFINES
                if sys.argv[1] == "tree-defines"
                else tb.GOLDEN_CARGO_BUILD
            )
        )
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "run":
        return run(Path(sys.argv[2]), Path(sys.argv[3]))
    if len(sys.argv) == 3 and sys.argv[1] == "report":
        return report(Path(sys.argv[2]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
