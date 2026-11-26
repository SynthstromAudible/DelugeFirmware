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
            a real CALLER, driven end to end: the sample browser previews one file ONCE and is then driven only by its
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
  AB-PRESCAN-RANGE
            the background peak scan run to completion over the previewed sample, from an empty index, leaving its
            index complete.
  AB-PREVIEW-TIER
            for each of the preview stages: no sample was read from the card on the interaction tier.
  AB-SUMMARY / AB-DEFER
            that the loader was active and that every case deferred at least once, so no case was quietly answered on
            the synchronous path.

With AUDIO_LOAD_AB_LINES set to a file, the run's AB-CASE, AB-EJECT and AB-REF lines are written to it, whether the run
passed or not: the log shows only the summaries, and a change to how loads resolve is compared case by case against
these.

Gate audio-load-ab:
    the async Rust audio loader must load every file the SAME way the synchronous C++ card path does, loaded in the same
    process as the reference -- identical chosen path (regular vs alternate dir, including the case where the alternate
    dir IS the file's own directory and the two candidates stringify identically), identical
    byteDepth/numChannels/audioDataStartPosBytes/audioDataLengthBytes and the rest of the parsed geometry, identical
    bytes read back through the Sample's own stream, and the identical Error identity on a failure. Also pins that no
    load leaves a half-built zero-geometry object or a leaked reason behind, and that every case genuinely went through
    the collector (deferred at least once) rather than being answered synchronously. Finally, drives a real CALLER: the
    sample browser is asked to preview one file once and then driven only by its per-tick surface, and must eventually
    draw -- the one check here that fails if a caller stops re-asking for a deferred load, since every other check
    drives the loader through the harness's own retry loop. AB-PREVIEW-STALL holds a preview's load far past its load's
    600-walk stall bound and requires it to give up once: nothing pending, nothing drawn, and nothing pending again once
    the hold is lifted. AB-PREVIEW-TIER requires, for each of those three stages, that no preview sample was read from
    the card on the interaction tier.
"""

from pathlib import Path

# A floor on the case count, so a future edit that shrinks `kCases` cannot keep printing PASS over a comparison that no
# longer covers the properties this gate was built for. Bump it deliberately when cases are added; never lower it to
# make a run go green.
MIN_CASES = 11

SHOWN = (
    "AB-SUMMARY ",
    "AB-DEFER ",
    "AB-PREVIEW ",
    "AB-CANCEL ",
    "AB-PREVIEW-STALL ",
    "AB-PREVIEW-TIER ",
    "AB-PREVIEW-DRAW ",
    "AB-PREVIEW-DRAW-STALL ",
    "AB-PREVIEW-DRAW-RELEASE ",
    "AB-PRESCAN-RANGE ",
    "AB-EJECT-SUMMARY ",
)


def run(gate):
    result = gate.run()
    lines_file = gate.env.get("AUDIO_LOAD_AB_LINES")
    if lines_file:
        kept = result.starting("AB-CASE ", "AB-EJECT ", "AB-REF ")
        Path(lines_file).write_text("".join(line + "\n" for line in kept))
    if not gate.require_standard(result):
        return
    gate.show(result)

    if not result.has("AB-SUMMARY "):
        gate.fail("no AB-SUMMARY line; the scenario did not reach its summary")
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
    for line in result.starting(*SHOWN):
        gate.say(line)
