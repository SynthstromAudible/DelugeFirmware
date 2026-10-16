"""The scenes' specs (sim/scenes_spec) on golden_vt_render, one file per process.

The scenario lists its entry table (SCENES_SPEC=--list), which must name exactly the spec files under sim/scenes_spec,
selftest/ aside: the table is generated from the files, so a mismatch means a stale build. Each file then runs in its
own process on its card (tests/integration/checks/scenes_spec_cards.txt; the fixture card otherwise) and passes only
on exit 0 and its own SCENES-SPEC-RESULT failures=0 line, so a crash or a file that never reaches its result line
fails by name, as does one still running after RUN_TIMEOUT_S. The runner's self-specs pin that a passing file and the
card checks pass and that a failing, a crashing and an empty one (no examples) do not. Every file runs in a fresh process, so no file's leftover
state (a stack song in currentSong, a guard it left open) can reach another.
"""

from pathlib import Path

RESULT_OK = "SCENES-SPEC-RESULT failures=0"
FIXTURE_CARD = "tests/fixtures/card"
# A spec runs synchronously inside the app's init, where golden_vt_render's own deadlines cannot fire, so a hung file
# is ended here (exit 124) and reported by name. Every file takes well under a second.
RUN_TIMEOUT_S = 120


def _cards(root: Path) -> dict[str, str]:
    cards = {}
    for line in (
        (root / "tests/integration/checks/scenes_spec_cards.txt")
        .read_text()
        .splitlines()
    ):
        line = line.strip()
        if line and not line.startswith("#"):
            spec_id, card = line.split()
            cards[spec_id] = card
    return cards


def _spec_files(root: Path) -> set[str]:
    base = root / "sim/scenes_spec"
    return {
        p.relative_to(base).with_suffix("").as_posix()
        for p in base.rglob("*_spec.cpp")
        if not p.relative_to(base).as_posix().startswith("selftest/")
    }


def _listed(gate) -> list[str] | None:
    run = gate.run(env={"SCENES_SPEC": "--list"}, timeout=RUN_TIMEOUT_S)
    if run.code != 0:
        gate.fail(f"--list exited {run.code}")
        gate.show(run)
        return None
    return [line.split()[1] for line in run.lines if line.startswith("SCENES-SPEC-ID ")]


def _passes(run) -> bool:
    return run.code == 0 and run.has_line(RESULT_OK)


def _run_spec(gate, spec_id: str, cards: dict[str, str]):
    return gate.run(
        env={
            "SCENES_SPEC": spec_id,
            "SCENES_SPEC_CARD": cards.get(spec_id, FIXTURE_CARD),
        },
        timeout=RUN_TIMEOUT_S,
    )


def _report(gate, spec_id: str, run) -> None:
    gate.fail(
        f"{spec_id}: exit {run.code}, {'its result line' if run.has_line(RESULT_OK) else 'no passing result line'}"
    )
    for line in run.lines[-30:]:
        gate.say(f"    {line}")
    for line in run.err.splitlines()[-10:]:
        gate.say(f"    {line}")


def run(gate) -> None:
    cards = _cards(gate.root)
    listed = _listed(gate)
    if listed is None:
        return
    files = _spec_files(gate.root)
    in_table = {spec_id for spec_id in listed if not spec_id.startswith("selftest/")}
    for spec_id in sorted(files - in_table):
        gate.fail(
            f"{spec_id}: a spec file the entry table does not list (stale build?)"
        )
    for spec_id in sorted(in_table - files):
        gate.fail(f"{spec_id}: listed by the entry table but no such file")
    for spec_id in sorted(set(cards) - files):
        gate.fail(
            f"{spec_id}: scenes_spec_cards.txt names a card for a file that does not exist"
        )

    passed = 0
    for spec_id in sorted(in_table & files):
        run = _run_spec(gate, spec_id, cards)
        if _passes(run):
            passed += 1
        else:
            _report(gate, spec_id, run)
    gate.say(f"{passed}/{len(in_table & files)} scenes spec files passed")

    for spec_id, should_pass in (
        ("selftest/pass_spec", True),
        # SdRoot/NoSdRoot confirm the installed card, so a card spec cannot silently list the wrong one.
        ("selftest/card_access_spec", True),
        ("selftest/fail_spec", False),
        ("selftest/empty_spec", False),
        ("selftest/crash_spec", False),
    ):
        run = _run_spec(gate, spec_id, cards)
        if _passes(run) != should_pass:
            gate.fail(
                f"{spec_id}: the runner reported it {'passing' if _passes(run) else 'failing'}"
            )
            gate.show(run)
