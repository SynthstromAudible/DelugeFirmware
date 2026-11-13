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

from scenario_gate import fields, has_fields, tag

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

# PM-EFFECTS' fields, in order; each reads 1 but room, a count of at least 1.
EFFECT_KEYS = [
    "order",
    "nested",
    "room",
    "room_ran",
    "unmasked",
    "drain",
    "render",
    "retire",
    "publish_oom",
]
# The fields every leg's line starts with.
LEG_KEYS = ("stray", "masked_alloc", "masked_actions", "hash")


def _check(gate, result) -> None:
    effects = result.first("PM-EFFECTS ")
    got = fields(effects)
    if not (
        list(got) == EFFECT_KEYS
        and all(got[key] == "1" for key in EFFECT_KEYS if key != "room")
        and got["room"].isdigit()
        and int(got["room"]) > 0
    ):
        gate.fail(
            f"{effects or 'no PM-EFFECTS line'}: a move's effects must run in order, once the"
            " outermost section ends and the render runs again, and never on the render; retired memory must outlive the"
            " render block it may be read in; a snapshot that cannot be built must leave the old one published, be"
            " counted, and be built at the next publish point"
        )
        return
    lines = {}
    for line in result.starting("PM-"):
        if (name := tag(line)) and all(key in fields(line) for key in LEG_KEYS):
            lines[name[3:]] = line
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
        line = lines[leg]
        f = fields(line)
        stray, alloc, actions = (
            int(f["stray"]),
            int(f["masked_alloc"]),
            int(f["masked_actions"]),
        )
        rest = " ".join(f"{k}={v}" for k, v in f.items() if k not in LEG_KEYS)
        reasons = []
        if stray:
            reasons.append(f"{stray} position writes the render could see half made")
        if alloc:
            reasons.append(f"{alloc} allocations with the render held off")
        if actions:
            reasons.append(f"{actions} undo Actions with the render held off")
        if leg in ("INJECTSTART", "INJECTAFTERMOVE") and not has_fields(
            line, injected=1, injected_ticks=0
        ):
            reasons.append(f"the render ticked inside the start ({rest})")
        if leg in ("LATESTART", "INJECTLAUNCH", "UNSOLO") and not has_fields(
            line, active=1, phase=0
        ):
            reasons.append(f"the launched clip is out of phase ({rest})")
        if leg == "CLONE" and not has_fields(line, cloned=1):
            reasons.append("no clone took the clip's place")
        if leg == "RESUME" and not has_fields(line, resumed=1):
            reasons.append("the clip did not take its place back")
        if leg == "ARRANGERRECORD" and not has_fields(line, began=1):
            reasons.append("the switch began no arrangement recording")
        if leg == "INJECTLAUNCH" and not has_fields(line, injected=1):
            reasons.append("the render block was never injected")
        if leg == "CLIPSHIFT" and not has_fields(line, cursor=1):
            reasons.append(
                "the shift positioned the cursor on the lane as published before it"
            )
        if leg == "SELFREPAIR" and not has_fields(line, render_writes=0, erased=1):
            reasons.append(
                f"the render wrote the lane, or the node was never erased ({rest})"
            )
        if leg == "CLIPSTOP" and not has_fields(
            line, render_writes=0, stopped=1, played=0, base=0
        ):
            reasons.append(
                f"the render wrote the lane, or the expression was not cleared ({rest})"
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
