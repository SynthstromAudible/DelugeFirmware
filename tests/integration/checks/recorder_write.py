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

NOT covered: the stem-export folder search. Its machine is exercised only by the offline render RW-DRAIN runs, which
starts the export after boot, so the search takes its CHANNEL arm there; no check reads its outcome beyond the render
terminating, and its inline arm is not exercised at all. There is no stem case among the `ticked_` cases: routing a
harness recording down the stem path needs more engine state than `processStarted` + `currentStemExportType` +
`lastFolderNameForStemExport`, and a case that silently records to RESAMPLE while calling itself a stem case is worse
than none.

One leg plus the offline drain: the stream channel has no feature switch, so there is no build without it to compare
against.
"""

import re


def _largest(lines: list[str], pattern: str) -> int:
    """The largest number `pattern`'s group captures across `lines`, or 0 when none does."""
    found = [re.match(pattern, line) for line in lines]
    return max((int(m.group(1) or 0) for m in found if m), default=0)


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
        for line in [line for line in run.lines if re.search(r"^CASE|FAIL", line)][:20]:
            gate.say(line)
    if not run.has_line("RW-RESULT failures=0"):
        gate.fail("\n".join(run.starting("RW-RESULT")))

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
    tk_fin = _largest(tk, r".*channelFinalizeTicks=([0-9]*)")
    if tk_fin == 0:
        gate.fail(
            "RW-TICKED-FINALIZE — finalize never ran through the channel across ticks"
        )

    leak = run.starting("TK-LEAK ")
    if not leak:
        gate.fail("RW-LEAK — no TK-LEAK line; the abort case did not finish")
    elif not any(re.match(r"TK-LEAK .*ok=1", line) for line in leak):
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
    tier_match = (
        re.match(r"RW-TIER violations=([0-9]*)", tier_lines[0]) if tier_lines else None
    )
    tier = int(tier_match.group(1)) if tier_match and tier_match.group(1) else None
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
    tk_create = _largest(tk, r".*channelCreateTicks=([0-9]*)")
    if tk_create == 0:
        gate.fail(
            "RW-TICKED-CREATE — the create tick never ran through the channel across ticks"
        )

    tk_served = _largest(tk, r"TK-RESULT ok=[0-9]* served=([0-9]*)")

    summary = [
        m for m in (re.match(r"RW-SUMMARY (.*)", line) for line in run.lines) if m
    ]
    served_match = next(
        (re.match(r"served=([0-9]*)", m.group(1)) for m in summary), None
    )
    if served_match is None:
        gate.fail("no RW-SUMMARY line")
        served = 0
    else:
        served = int(served_match.group(1) or 0)
    ticks_match = next(
        (
            t
            for t in (re.match(r".*channelTicks=([0-9]*)", m.group(1)) for m in summary)
            if t
        ),
        None,
    )
    channel_ticks = int(ticks_match.group(1) or 0) if ticks_match else 0

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
        scenario="", env={"GOLDEN_STEM_OUT": str(gate.tmp / "drain-stems")}, timeout=900
    )
    if drain.code != 0:
        gate.fail(
            f"RW-DRAIN — the offline render exited {drain.code} (124 = it never drained)"
        )
        for line in drain.err.splitlines()[:20]:
            gate.say(line)

    if gate.failures:
        gate.say("recorder write: FAILED")
        return
    for line in roundtrip.lines + drain.lines:
        if re.match(r"TIER-SYNC(-AT)? ", line):
            gate.say(line)
    served, tk_served, channel_ticks, tk_fin, tk_create, tier = counts
    gate.say(
        f"recorder write: OK (served={served}->{tk_served} channel drain={channel_ticks} finalize={tk_fin} "
        f"create={tk_create} tier={tier})"
    )
