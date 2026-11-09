"""The playhead-moves gate: the interaction tier moves the playhead only with the render excluded.

The render ticks the sequencer, so a write the interaction tier makes to playback position state while a clock runs --
a lane cursor, a clip's or note row's position, the next-event schedule, the playback mode or state -- can be seen half
made by a render block that preempts it. Each such move runs in one section that holds the render off
(src/deluge/playback/move_section.h), and what runs in it must not allocate or log undo: on the device a section masks
interrupts for its length.

First, PM-EFFECTS checks the section on its own: effects deferred in order, run when the outermost section ends with the
render running again, a full room making the caller act at once, nothing collected on the render. It also checks a lane
publish out of memory (publish_oom=1): the old snapshot stays published, the failure is counted, and the next publish
point publishes.

What the lines cover (tests/integration/scenarios/playhead_moves_scenario.cpp writes them), each
`PM-<LEG> stray=<n> masked_alloc=<n> masked_actions=<n> hash=<h>`:
    stray           position writes made off the render, a clock running, no section held (playback/playhead_checks.h)
    masked_alloc    heap allocations inside a move's section (memory/heaps.h maskedAllocations)
    masked_actions  Actions begun inside a move's section
    hash            the render's output over the blocks after the move: equal to a base build's, by hand

    PM-START / PM-RESTART / PM-CONTINUE  playback started, restarted (forceResetPlayPos), continued from mid-loop on
                                         the external clock
    PM-SPP                               a MIDI song position pointer on the external clock
    PM-TOARRANGER / PM-TOSESSION         a switch from session to arrangement and back while playing
    PM-UNDO                              a note delete undone while playing (the reversion re-reads automation)
    PM-CLIPLENGTH / PM-CLIPSHIFT         a playing clip's length doubled, its contents shifted by a quarter of its
                                         loop (cursor=1: the move put the volume cursor on the shifted lane)
    PM-ROWMUTE / PM-ROWLENGTH / PM-ROWDIRECTION  a playing row muted and unmuted, its length halved, reversed
    PM-LATESTART                         a stopped clip launched late at the playing clip's position (phase=0 required)
    PM-UNSOLO                            a soloed clip unsoloed: the others restart in phase with it
    PM-INJECTSTART / PM-INJECTAFTERMOVE  a render block run inside a start, before the clock is published / between
                                         the clips' moves and linear recording: injected=1 injected_ticks=0 required
    PM-INJECTLAUNCH                      a render block run inside a late launch, between its read of the playhead and
                                         its move: phase=0 required
    PM-ARRANGERRECORD                    a switch to the arranger while recording into it, an output armed: began=1
                                         required (arrangement recording made its clip)
    PM-CLONE / PM-RESUME                 recording session to arranger, a playing clip cloned for a recorded change
                                         (cloned=1) and the original resumed when recording stops (resumed=1)
    PM-SELFREPAIR                        a lane with a node past its length played from its published snapshot: the
                                         render writes no lane (render_writes=0) and the interaction tier erases the
                                         node at its request (erased=1)
    PM-CLIPSTOP                          a playing synth clip with expression stopped: the render writes no lane
                                         (render_writes=0), plays 0 (played=0) and the interaction tier makes 0 the
                                         base at its request (base=0)
"""

import re

LEGS = [
    "START",
    "RESTART",
    "CONTINUE",
    "SPP",
    "TOARRANGER",
    "TOSESSION",
    "UNDO",
    "CLIPLENGTH",
    "CLIPSHIFT",
    "ROWMUTE",
    "ROWLENGTH",
    "ROWDIRECTION",
    "LATESTART",
    "UNSOLO",
    "INJECTSTART",
    "INJECTAFTERMOVE",
    "INJECTLAUNCH",
    "ARRANGERRECORD",
    "CLONE",
    "RESUME",
    "SELFREPAIR",
    "CLIPSTOP",
]

_LINE = re.compile(
    r"PM-(\w+) stray=(\d+) masked_alloc=(\d+) masked_actions=(\d+) hash=([0-9a-f]+)(.*)"
)


def _check(gate, result) -> None:
    effects = [line for line in result.lines if line.startswith("PM-EFFECTS ")]
    if not effects or not re.match(
        r"PM-EFFECTS order=1 nested=1 room=[1-9][0-9]* room_ran=1 unmasked=1 drain=1 render=1 retire=1"
        r" publish_oom=1$",
        effects[0],
    ):
        gate.fail(
            f"{effects[0] if effects else 'no PM-EFFECTS line'}: a move's effects must run in order, once the"
            " outermost section ends and the render runs again, and never on the render; retired memory must outlive the"
            " render block it may be read in; a snapshot that cannot be built must leave the old one published, be"
            " counted, and be built at the next publish point"
        )
        return
    lines = {}
    for line in result.lines:
        found = _LINE.match(line)
        if found:
            lines[found.group(1)] = found
    missing = [leg for leg in LEGS if leg not in lines]
    if missing:
        gate.fail(
            "no line for"
            + "".join(" PM-" + leg for leg in missing)
            + ": a stage never ran"
        )
        return

    failed = []
    for leg in LEGS:
        found = lines[leg]
        stray, alloc, actions, rest = (
            int(found.group(2)),
            int(found.group(3)),
            int(found.group(4)),
            found.group(6),
        )
        reasons = []
        if stray:
            reasons.append(f"{stray} position writes the render could see half made")
        if alloc:
            reasons.append(f"{alloc} allocations with the render held off")
        if actions:
            reasons.append(f"{actions} undo Actions with the render held off")
        if (
            leg in ("INJECTSTART", "INJECTAFTERMOVE")
            and " injected=1 injected_ticks=0" not in rest
        ):
            reasons.append(f"the render ticked inside the start ({rest.strip()})")
        if leg in ("LATESTART", "INJECTLAUNCH", "UNSOLO") and not re.search(
            r" active=1 phase=0\b", rest
        ):
            reasons.append(f"the launched clip is out of phase ({rest.strip()})")
        if leg == "CLONE" and " cloned=1" not in rest:
            reasons.append("no clone took the clip's place")
        if leg == "RESUME" and " resumed=1" not in rest:
            reasons.append("the clip did not take its place back")
        if leg == "ARRANGERRECORD" and " began=1" not in rest:
            reasons.append("the switch began no arrangement recording")
        if leg == "INJECTLAUNCH" and " injected=1" not in rest:
            reasons.append("the render block was never injected")
        if leg == "CLIPSHIFT" and " cursor=1" not in rest:
            reasons.append(
                "the shift positioned the cursor on the lane as published before it"
            )
        if leg == "SELFREPAIR" and " render_writes=0 erased=1" not in rest:
            reasons.append(
                f"the render wrote the lane, or the node was never erased ({rest.strip()})"
            )
        if leg == "CLIPSTOP" and not re.search(
            r" render_writes=0 stopped=1 played=0 base=0\b", rest
        ):
            reasons.append(
                f"the render wrote the lane, or the expression was not cleared ({rest.strip()})"
            )
        if reasons:
            failed.append(f"PM-{leg}: " + "; ".join(reasons))
    if failed:
        for reason in failed:
            gate.fail(reason)
        return

    gate.say(
        "PASS — every leg moved the playhead with the render excluded, allocating and logging nothing inside"
    )


def check(gate, run):
    _check(gate, run)
