"""Run a host driver as a golden_vt_render scenario, and get back what its own process would have given.

The drivers here (song_roundtrip, param_record_diff, ...) were once executables of their own. They now run inside
golden_vt_render, on the booted app, as stepped scenarios (tests/integration/scenarios/drivers/driver.h): the
scenario takes the driver's command line from DRV_ARGS, writes its stdout and stderr to the files DRV_OUT and DRV_ERR
name, and prints `DRV-EXIT code=<n>` with the value its main returned. `run()` turns that back into the exit status,
stdout and stderr the driver's process used to have, which is what the runners in this directory compare.

A run that ends with no DRV-EXIT line -- the harness aborted, froze or timed out -- comes back with a negative
returncode and the harness's own stderr.

Runs share nothing (each boots its own app on its own in-memory card, and the drivers read songs from the host), so
the sweeps run them `--jobs` at a time with `run_all()`.
"""

import concurrent.futures
import os
import re
import shlex
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EXIT = re.compile(r"^DRV-EXIT code=(-?\d+)$", re.MULTILINE)


@dataclass
class Result:
    returncode: int
    stdout: str
    stderr: str


def default_binary() -> Path:
    """golden_vt_render where `./dbt test` builds it: CARGO_TARGET_DIR, or build/cargo-harness."""
    target = os.environ.get("CARGO_TARGET_DIR") or str(REPO / "build" / "cargo-harness")
    return (
        Path(target)
        / os.environ.get("GOLDEN_VT_RENDER_TARGET", "")
        / "release"
        / "golden_vt_render"
    )


def default_corpus() -> str | None:
    """The song corpus: DELUGE_SONG_CORPUS, else DELUGE_BACKUP's SONGS, else ~/Deluge Backup/SONGS."""
    candidates = [os.environ.get("DELUGE_SONG_CORPUS")]
    if os.environ.get("DELUGE_BACKUP"):
        candidates.append(os.path.join(os.environ["DELUGE_BACKUP"], "SONGS"))
    candidates.append(os.path.join(os.path.expanduser("~"), "Deluge Backup", "SONGS"))
    for c in candidates:
        if c and os.path.isdir(c):
            return c
    return None


def run(
    binary,
    scenario: str,
    args: list[str],
    timeout: float | None = None,
    wrapper: str | list[str] = "",
    direct: bool = False,
) -> Result:
    """Run `scenario` with `args` as its command line, under `wrapper` (an emulator, say) when one is given.

    With `direct`, the driver prints straight to the harness's own streams (DRV_DIRECT), so what it printed before a
    crash survives it; stdout and stderr then also carry the harness's own lines."""
    prefix = shlex.split(wrapper) if isinstance(wrapper, str) else list(wrapper)
    with tempfile.TemporaryDirectory(prefix=f"{scenario}.") as tmp:
        out_path = os.path.join(tmp, "out")
        err_path = os.path.join(tmp, "err")
        env = dict(os.environ)
        env.update(
            {"GOLDEN_SCENARIO": scenario, "DRV_ARGS": "\x1f".join(str(a) for a in args)}
        )
        if direct:
            env["DRV_DIRECT"] = "1"
        else:
            env.update({"DRV_OUT": out_path, "DRV_ERR": err_path})
        try:
            proc = subprocess.run(
                [*prefix, str(binary)],
                env=env,
                capture_output=True,
                text=True,
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return Result(-1, "", "TIMEOUT")
        found = EXIT.findall(proc.stdout)
        if direct:
            code = int(found[-1]) if found else -2
            return Result(code, proc.stdout, proc.stderr)
        if not found:
            return Result(
                -2, "", f"no DRV-EXIT (harness exit {proc.returncode})\n{proc.stderr}"
            )

        def read(path: str) -> str:
            try:
                with open(path, encoding="utf-8", errors="replace") as fh:
                    return fh.read()
            except FileNotFoundError:
                return ""

        return Result(int(found[-1]), read(out_path), read(err_path))


def default_jobs() -> int:
    """How many runs a sweep makes at once: GOLDEN_SWEEP_JOBS (./dbt test sets it to share the CPUs among the tests it
    runs at once), else one per CPU."""
    try:
        return max(1, int(os.environ["GOLDEN_SWEEP_JOBS"]))
    except (KeyError, ValueError):
        return os.cpu_count() or 1


def map_ordered(fn, items: list, jobs: int = 1):
    """`fn(item)` for each item, `jobs` at a time; yields the results in the order of `items`. The first call goes
    alone, so anything a run sets up once for all the others (the fixture tree the card is filled from) is in place
    before they start."""
    if not items:
        return
    yield fn(items[0])
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = [pool.submit(fn, item) for item in items[1:]]
        for future in futures:
            yield future.result()


def run_all(
    binary,
    scenario: str,
    arg_lists: list[list[str]],
    timeout: float | None = None,
    wrapper: str | list[str] = "",
    jobs: int = 1,
    direct: bool = False,
):
    """`run()` for each argument list, `jobs` at a time (map_ordered); yields the results in order."""
    return map_ordered(
        lambda args: run(binary, scenario, args, timeout, wrapper, direct),
        arg_lists,
        jobs,
    )


def find_songs(corpus: str, name_filter: str = "") -> list[str]:
    """Every song (.XML or .json) under `corpus` whose file name holds `name_filter`, sorted."""
    songs = []
    for root, _dirs, files in os.walk(corpus):
        for n in files:
            if n.lower().endswith((".xml", ".json")) and name_filter in n:
                songs.append(os.path.join(root, n))
    return sorted(songs)


def baseline_diff(
    scenario: str,
    baseline_dir: str,
    argv=None,
    description: str = "",
    require_exit_0: bool = False,
) -> int:
    """For each `<name>.txt` baseline in `baseline_dir`, run the driver on the corpus song `<name>.XML` and
    byte-compare its stdout (minus '#' comment lines, which carry the song's absolute path) against the baseline.
    With `require_exit_0`, a run that does not exit 0 is a regression too. Returns 0, 1 on a regression, or 77
    (skip) with no corpus or no baselines."""
    import argparse

    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("--binary", default=str(default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=default_corpus() or "")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=default_jobs())
    args = ap.parse_args(argv)

    def strip_comments(text):
        return "\n".join(
            line for line in text.splitlines() if not line.startswith("#")
        ).strip()

    if not os.path.isdir(args.corpus):
        print("SKIP: no song corpus (DELUGE_SONG_CORPUS)")
        return 77
    if not os.path.isdir(baseline_dir):
        print("SKIP: no baselines directory")
        return 77
    baselines = sorted(f[:-4] for f in os.listdir(baseline_dir) if f.endswith(".txt"))
    if not baselines:
        print("SKIP: no baselines")
        return 77

    ok = regress = absent = 0
    present = []
    for name in baselines:
        song = os.path.join(args.corpus, name + ".XML")
        if os.path.isfile(song):
            present.append((name, song))
        else:
            print(f"ABSENT  {name} (fixture not in corpus)")
            absent += 1
    results = run_all(
        args.binary,
        scenario,
        [[song] for _, song in present],
        args.timeout,
        args.wrapper,
        args.jobs,
    )
    for (name, _), got in zip(present, results):
        with open(os.path.join(baseline_dir, name + ".txt")) as fh:
            expected = fh.read().strip()
        actual = strip_comments(got.stdout)
        if actual == expected and (got.returncode == 0 or not require_exit_0):
            print(f"OK      {name}")
            ok += 1
        else:
            print(f"REGRESS {name} (rc={got.returncode})")
            print(f"  expected: {expected!r}")
            print(f"  actual:   {actual!r}")
            if got.stderr.strip():
                print(f"  stderr:   {got.stderr.strip()!r}")
            regress += 1
    print("=" * 60)
    print(
        f"OK={ok} REGRESS={regress} ABSENT={absent}  (corpus={args.corpus}, {len(baselines)} baselines)"
    )
    return 1 if regress else 0


def self_checking_sweep(scenario: str, argv=None, description: str = "") -> int:
    """Run a self-checking driver over every song in the corpus: exit 0 passes, 1 is a song it cannot load (not this
    driver's concern, so skipped), anything else fails. Returns 0, 1 on a failure, or 77 (skip) with no songs."""
    import argparse

    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("--binary", default=str(default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=default_corpus() or "")
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=default_jobs())
    args = ap.parse_args(argv)

    if not os.path.isdir(args.corpus):
        print("SKIP: no song corpus (DELUGE_SONG_CORPUS)")
        return 77
    songs = find_songs(args.corpus, args.filter)
    if not songs:
        print("SKIP: no songs found in corpus")
        return 77

    ok = fail = 0
    results = run_all(
        args.binary,
        scenario,
        [[song] for song in songs],
        args.timeout,
        args.wrapper,
        args.jobs,
    )
    for song, r in zip(songs, results):
        rel = os.path.relpath(song, args.corpus)
        if r.returncode == 0:
            print(f"OK      {rel}")
            ok += 1
        elif r.returncode == 1:
            print(f"skip(load) {rel}")
        else:
            print(r.stderr, end="" if r.stderr.endswith("\n") else "\n")
            print(f"FAIL(rc={r.returncode}) {rel}")
            fail += 1
    print("=" * 60)
    print(f"OK={ok} FAIL={fail}  (corpus={args.corpus})")
    return 1 if fail else 0
