"""The audio-load A/B gate.

The Rust audio loader is always on, and the scenario holds it to an in-process reference: every sample is loaded first
through the synchronous C++ card path (`mayLoadAsync=false`) and then through the loader, and the two must agree on
every field.

What the checks cover (tests/integration/scenarios/audio_load_ab_scenario.cpp writes the lines):
  AB-CASE   one per sample: the Error, the file actually chosen (`file`/`alt`/`asked`), the whole parsed geometry, and a
            CRC of the bytes read back through the Sample's own stream. The scenario compares it with the synchronous
            reference in process, and prints AB-REF with a FAIL on any difference.
  AB-EJECT  the card-swap property: a header is parsed from one card and left uncollected, the card is ejected, a
            DIFFERENT file is put at the same path, and the load is asked for again. It must match a synchronous load
            of the file now at that path. A stale collect shows up here as the wrong geometry against the same file.
  AB-EJECT-SUMMARY
            that stage's deferral counts. A post-swap collect that deferred ZERO times is itself the bug (a flushed
            path reports UNKNOWN and is asked for again; a stale one is READY on the first ask).
  AB-PREVIEW
            a real CALLER, driven end to end: SampleBrowser previews one file ONCE and is then driven only by its
            per-tick surface. It must defer at least once and eventually draw.
  AB-CANCEL a preview asked for and then abandoned before its load runs: nothing may load, draw or stay pending.
  AB-PREVIEW-STALL
            a preview whose load is held far past its load's 600-walk stall bound: it must give up once, with nothing
            pending and nothing drawn, and stay given up once the hold is lifted.
  AB-PREVIEW-DRAW
            the preview stage's draw: once, ending on a complete render with nothing left to redraw.
  AB-PREVIEW-DRAW-STALL
            a preview whose clusters the streaming loader holds: redrawn, then given up once, nothing pending, and the
            next file still previewed.
  AB-PREVIEW-DRAW-RELEASE
            a redraw pending when the preview's sample is released (Select, a typed move): dropped, never drawn from
            the released sample.
  AB-PREVIEW-TIER
            for each of the preview stages: no sample was read from the card on the interaction tier.
  AB-SUMMARY / AB-DEFER
            that the loader was active and that every case deferred at least once, so no case was quietly answered on
            the synchronous path.

With AUDIO_LOAD_AB_LINES set to a file, the run's AB-CASE, AB-EJECT and AB-REF lines are written to it, whether the run
passed or not: the log shows only the summaries, and a change to how loads resolve is compared case by case against
these.
"""

import re
from pathlib import Path

# A floor on the case count, so a future edit that shrinks `kCases` cannot keep printing PASS over a comparison that no
# longer covers the properties this gate was built for. Bump it deliberately when cases are added; never lower it to
# make a run go green.
MIN_CASES = 10

SHOWN = re.compile(
    r"^(AB-SUMMARY|AB-DEFER|AB-PREVIEW|AB-CANCEL|AB-PREVIEW-STALL|AB-PREVIEW-TIER|AB-PREVIEW-DRAW"
    r"|AB-PREVIEW-DRAW-STALL|AB-PREVIEW-DRAW-RELEASE|AB-EJECT-SUMMARY) "
)


def run(gate):
    result = gate.run()
    lines_file = gate.env.get("AUDIO_LOAD_AB_LINES")
    if lines_file:
        kept = [
            line for line in result.lines if re.match(r"^AB-(CASE|EJECT|REF) ", line)
        ]
        Path(lines_file).write_text("".join(line + "\n" for line in kept))
    if not gate.require_standard(result):
        return
    gate.show(result)

    # The loader must be the one that ran. `asyncActive=0` means this binary resolved the weak C++ fallback, and every
    # load was answered by the very path it is compared against.
    if not result.has("AB-SUMMARY asyncActive=1 "):
        gate.fail(
            "the async loader was not active; every load took the synchronous path it is compared with"
        )
        return

    cases = len(result.starting("AB-CASE "))
    if cases < MIN_CASES:
        gate.fail(
            f"only {cases} case(s) ran, fewer than the {MIN_CASES} this gate requires; kCases was shrunk"
        )
        return

    gate.say(
        f"PASS — the async loader loaded all {cases} sample(s) as the synchronous path does, and agreed with it "
        "across a"
    )
    gate.say("       card swap, and a preview held past its bound gave up")
    for line in result.lines:
        if SHOWN.match(line):
            gate.say(line)
