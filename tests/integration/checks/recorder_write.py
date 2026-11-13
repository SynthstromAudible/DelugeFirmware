"""The recorder write-relocation gate: the recorder_roundtrip scenario, then the offline drain.

What each check covers, and why each is separate:
  RW-ROUNDTRIP  that a real recording still reads back byte-correctly, mid-recording AND finalized multi-cluster. It
                is the half that catches a change that relocated the write and broke the recording on the way: a
                recycled stream handle collecting a previous recorder's answer shows up as exactly this, and nothing
                else in the run would notice.
  RW-CHANNEL    that the async arm was EXECUTED by the `ticked_` recordings: they must serve writes beyond the ones
                the oracle's cases served (RW-SUMMARY's count, taken before they run).
  RW-TICKED     that the recorder drain ran after boot, its writes on the stream channel. This is the gate's real
                subject: RW-CHANNEL alone says writes were served, not which drain issued them.
  RW-TICKED-CREATE  that CREATING the recording's file ran through the create machine.
  RW-TICKED-FINALIZE  that FINISHING a recording ran through the finalize machine. A served count says operations
                went through the channel, not which machine issued them.
  RW-LEAK       that an aborted recording gives its clusters back. It does NOT catch a premature unlink --
                single-flight sends that unlink down its synchronous arm anyway -- but it does catch a cleanup that
                fails to delete at all.
  RW-ABORT-OPEN  that an abort landing while the channel is opening the file -- for the create, or for finalize's
                reopen to alter it -- leaves no file behind and no stream-write slot taken: the open cannot be
                cancelled, so the abort has to wait it out and close what it opened.
  RW-TICKED-CASE  that each `ticked_` case -- a recording, the alteration branch, an abort -- ran and reads back
                byte-for-byte, its cluster writes through the channel. The alteration case exists because nothing
                else sets allowFileAlterationAfter, so without it finalize's whole batch machine would have no
                coverage.
  RW-TIER       how many synchronous card operations the recorder began on the interaction tier after boot, over
                every `ticked_` case, and how many of its channel requests were refused. Required 0: after boot
                every recorder operation goes through a channel.
  RW-BUSY       that a recording made while another stream-channel operation keeps taking the channel is refused,
                asks again, never writes synchronously, and still reads back exactly.
  RW-STALL      that a cluster write held past the recorder's bound fails the take while the write is still
                outstanding, keeps its buffer until the answer, closes the file through the channel, and never
                writes synchronously.
  RW-DIR-STALL  that a recording whose folder scan's directory open is held past the recorder's bound fails its
                create with a card error, makes no file, and never opens the folder synchronously.
  RW-OPEN-IOERR  that a create whose open fails with a card error other than NOT_FOUND -- the card unmounted under the
                open, and the channel refused the mount -- fails the recording without making its folder: only a
                missing folder is worth the folder retry. Its openStatus must be NO_FILESYSTEM (-11).
  RW-DRAIN      that the offline stem-export path still drains to completion. That loop calls the card routine in a
                bounded loop expecting each call to COMPLETE a write.
  RW-DRAIN-STEMS  that the drain's TRACK export actually wrote its stems: one WAV per track of the fixture (Cordae's
                eight kits), each with audio in its `data` chunk. A drain whose folder search gives up records nothing
                and still terminates, so RW-DRAIN alone passes without a stem ever being written; a stem that is only a
                header is a recording whose card writes never landed.

The stem-export folder search's CHANNEL arm is exercised by the offline render RW-DRAIN runs, which starts the export
after boot, and RW-DRAIN-STEMS checks its outcome. NOT covered: the search's inline arm, which runs only before boot
finishes. There is no stem case among the `ticked_` cases: routing a
harness recording down the stem path needs more engine state than `processStarted` + `currentStemExportType` +
`lastFolderNameForStemExport`, and a case that silently records to RESAMPLE while calling itself a stem case is worse
than none.

One leg plus the offline drain: the stream channel has no feature switch, so there is no build without it to compare
against.
"""

import struct
from pathlib import Path

from scenario_gate import field, int_field, starting

# The stems the offline drain's TRACK export of the Cordae fixture writes: one per kit track.
_DRAIN_STEMS = 8


def _wav_data_bytes(path: Path) -> int:
    """The length of `path`'s RIFF `data` chunk, or 0 when it has none."""
    raw = path.read_bytes()
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        return 0
    pos = 12
    while pos + 8 <= len(raw):
        chunk_id = raw[pos : pos + 4]
        (size,) = struct.unpack_from("<I", raw, pos + 4)
        if chunk_id == b"data":
            return size
        pos += 8 + size + (size & 1)
    return 0


def _largest(lines: list[str], key: str) -> int:
    """The largest `key` on any of `lines`, or 0 when none carries it."""
    return max((int_field(line, key) or 0 for line in lines), default=0)


def _check_roundtrip(gate, run) -> tuple[int, int, int, int, int, int] | None:
    """The recorder_roundtrip scenario's checks. None when it did not run to completion."""
    # A failed run is still judged line by line: the give-up cases fail without stopping the tier report, and a
    # freeze aborts the process, so the lines that did print say which check went red.
    if run.code != 0:
        gate.fail(f"the run exited {run.code}")
        for line in run.err.splitlines()[:40]:
            gate.say(line)
    # An abort exits before this line is printed, which is what turns a freeze into a gate failure rather than a
    # silently truncated log.
    if not run.has("RW-RESULT "):
        gate.fail("no RW-RESULT line; the scenario did not run to completion")
        return None
    if not run.has("8/8 cases passed"):
        gate.fail("RW-ROUNDTRIP — a recording did not read back correctly")
        for line in [
            line for line in run.lines if line.startswith("CASE") or "FAIL" in line
        ][:20]:
            gate.say(line)
    # Judged without stopping: the lines after say which check went red.
    if (why := gate.shortfall(run.lines)) is not None:
        gate.fail(f"{why} {' '.join(run.starting('RW-RESULT'))}".strip())

    # The `ticked_` cases run AFTER the oracle has printed RW-SUMMARY, so their evidence is their own lines.
    for case in ("recording", "alterfile", "abort"):
        if not run.has(f"CASE ticked_{case} "):
            gate.fail(f"RW-TICKED-CASE — the ticked '{case}' case did not run")

    tk = run.starting("TK-RESULT ")
    tk_ok = run.starting("TK-RESULT ok=1 ")
    if len(tk) < 2 or len(tk_ok) != len(tk):
        gate.fail(
            f"RW-TICKED-CASE — {len(tk_ok)} of {len(tk)} ticked recordings read back correctly"
        )
        for line in tk:
            gate.say(line)

    # A recording finished without the finalize machine reads zero here while everything else stays green.
    tk_fin = _largest(tk, "channelFinalizeTicks")
    if tk_fin == 0:
        gate.fail(
            "RW-TICKED-FINALIZE — finalize never ran through the channel across ticks"
        )

    leak = run.starting("TK-LEAK ")
    if leak and not any(field(line, "ok") == "1" for line in leak):
        gate.fail(
            f"RW-LEAK — {chr(10).join(leak)}; an aborted recording did not give its clusters back"
        )

    for case in ("abortcreate", "abortalter"):
        found = run.starting(f"TK-ABORT-OPEN case={case} ")
        if not found:
            gate.fail(f"RW-ABORT-OPEN — no line for the {case} case; it did not finish")
        elif not any(line.endswith(" ok=1") for line in found):
            gate.fail(f"RW-ABORT-OPEN — {chr(10).join(found)}")
    for line in run.starting("TK-ABORT-OPEN "):
        gate.say(line)

    tier_lines = run.starting("RW-TIER ")
    tier = int_field(tier_lines[0], "violations") if tier_lines else None
    if tier is None:
        gate.fail("RW-TIER — no RW-TIER line; the ticked cases did not report")
        tier = 0
    else:
        for line in tier_lines:
            gate.say(line)
        if tier != 0:
            gate.fail(
                f"RW-TIER — the recorder began {tier} synchronous card operation(s) after boot"
            )

    for tag in ("RW-BUSY", "RW-STALL", "RW-DIR-STALL", "RW-OPEN-IOERR"):
        found = run.starting(f"{tag} ")
        if not found:
            gate.fail(f"{tag} — no {tag} line; the case did not finish")
        else:
            for line in found:
                gate.say(line)
            if not any(line.startswith(f"{tag} ok=1 ") for line in found):
                gate.fail(f"{tag} — see the line above")

    # Reading zero means the recorder created its file without the create machine, blocking on the card instead.
    tk_create = _largest(tk, "channelCreateTicks")
    if tk_create == 0:
        gate.fail(
            "RW-TICKED-CREATE — the create tick never ran through the channel across ticks"
        )

    tk_served = _largest(tk, "served")

    summary = run.first("RW-SUMMARY ")
    if summary is None:
        gate.fail("no RW-SUMMARY line")
    served = int_field(summary, "served") or 0
    channel_ticks = int_field(summary, "channelTicks") or 0

    if tk_served <= served:
        gate.fail(
            f"RW-CHANNEL — the ticked recording served nothing beyond the oracle's {served}"
        )
    if channel_ticks == 0:
        gate.fail(
            f"RW-TICKED — served={served} but no drain ran through the channel across ticks"
        )
    return served, tk_served, channel_ticks, tk_fin, tk_create, tier


def run(gate):
    gate.say("recorder write: running")
    roundtrip = gate.run()
    counts = _check_roundtrip(gate, roundtrip)
    if counts is None:
        return

    # RW-DRAIN: the offline stem-export path drains the recorder through the loop a change to the write path could
    # break. A non-terminating drain shows up as a timeout. This measures that the drain TERMINATES, so the stems it
    # renders go to this gate's own temporary directory and nowhere else.
    gate.say("recorder write: running the offline drain")
    drain = gate.run(
        scenario="stem_export",
        env={"GOLDEN_STEM_OUT": str(gate.tmp / "drain-stems")},
        timeout=900,
    )
    if drain.code != 0:
        gate.fail(
            f"RW-DRAIN — the offline render exited {drain.code} (124 = it never drained)"
        )
        for line in drain.err.splitlines()[:20]:
            gate.say(line)
    else:
        stems_dir = gate.tmp / "drain-stems"
        stems = sorted(stems_dir.rglob("*.WAV")) if stems_dir.is_dir() else []
        empty = [stem.name for stem in stems if _wav_data_bytes(stem) == 0]
        if not stems:
            gate.fail(
                "RW-DRAIN-STEMS — the TRACK export terminated without writing a stem"
            )
        elif len(stems) != _DRAIN_STEMS:
            gate.fail(
                f"RW-DRAIN-STEMS — the TRACK export wrote {len(stems)} stems, expected {_DRAIN_STEMS}"
            )
        elif empty:
            gate.fail(
                f"RW-DRAIN-STEMS — stems with no audio in their data chunk: {', '.join(empty)}"
            )
        else:
            gate.say(f"RW-DRAIN-STEMS stems={len(stems)}")

    if gate.failures:
        gate.say("recorder write: FAILED")
        return
    for line in starting(roundtrip.lines + drain.lines, "TIER-SYNC ", "TIER-SYNC-AT "):
        gate.say(line)
    served, tk_served, channel_ticks, tk_fin, tk_create, tier = counts
    gate.say(
        f"recorder write: OK (served={served}->{tk_served} channel drain={channel_ticks} finalize={tk_fin} "
        f"create={tk_create} tier={tier})"
    )
