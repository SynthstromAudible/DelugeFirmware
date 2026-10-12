"""The container-guard gate: which structural edits to the containers the render walks are made while it could preempt.

With playback running, makes one structural edit to each kind of container the audio render walks, through the
model's own entry points, and reports how many structural edits were made while the render could have preempted them
(deluge::unguardedStructuralEditsTotal(), src/deluge/util/structural_edit_probe.h).

On the device the audio render preempts thread mode at any instruction. An insert, erase, reorder, relink or
reallocation of a container the render walks -- a clip's note rows, a row's notes, an automation node list, the
song's clip arrays, an output's clip instances, the song's output list, a kit's drum list -- can be interrupted
halfway by a render that walks it. Every such edit must run inside an AudioExcludedSection (libdeluge/system.h). The
containers report each structural edit to the probe, which counts those made outside a section.

What the lines cover (tests/integration/scenarios/container_guard_scenario.cpp writes them), each `CG-<LEG> unguarded=<n>
kinds=<kind=n,...>`:
    CG-NOTEADD / CG-NOTEDEL  a note added / deleted through NoteRow::attemptNoteAdd() / deleteNoteByPos()
    CG-ROWADD / CG-ROWDEL    a note row made (getOrCreateNoteRowForYNote()) / deleted (InstrumentClip::deleteNoteRow())
    CG-CLIPNEW / CG-CLIPDEL  a session clip made through the new-clip gesture / deleted (Song::removeSessionClip())
    CG-CLIPDUP               a session clip cloned (SessionView::cloneClip())
    CG-ARRANGE               a clip instance inserted into and erased from an output's clipInstances
    CG-AUTOMATE              an automation node inserted into and erased from a playing clip's volume AutoParam
    CG-PATCHCABLE            a patch cable added to and deleted from a playing synth (PatchCableSet::getPatchCableIndex()
                             creating it, then deletePatchCable()): the cable storage grows and shrinks, and
                             setupPatching() reorders the cables
    CG-DRUM                  a drum added to a playing kit (Kit::addDrum()) and removed (Kit::removeDrum())
    CG-REPLACE               a playing clip's synth replaced (Song::replaceInstrument())
    CG-UNDO                  a logged note delete, undone
    CG-ROWDRAG               the clip view's note-row drag (scrollVertical_dragSelectedNoteRow(): two rows swapped)
    CG-CLIPMOVE              the session view's clip drag (ClipArray::swapClips())
    CG-OUTPUTMOVE            the arranger's output drag (Song::moveOutput(): a three-pointer splice)
    CG-QUANTIZE              a row quantized (NoteRow::quantize(), which rotates its notes in place)
    CG-RENDERSET             a kit drum sounded from the interaction tier (an audition): its kit's rendering set gains it
    CG-RENDERALLOC           a kit clip playing for 200 blocks, then its drum's own delay, which starts, has its rate stepped
                             (new tapes grown and swapped in) and abandons its tapes; CG-RENDERALLOC renderAllocs=
                             counts heap allocations the render made (host only) -- required 0, with delayStarts and
                             delayStops at least 1
    CG-VOICEPOLY             more audition note-ons than a synth's voice limit, from the interaction tier
    CG-VOICEMONO             a MONO synth re-triggered while its voice plays
    CG-VOICEKILL             the oscillator-type menu's killAllVoices on a sounding synth
    CG-VOICEMULTIRANGE       a multi-range deleted from a sounding synth (Sound::deleteMultiRange)
    CG-VOICEFAIL             a ring-mod note-on whose voice fails to start (a sample source with no sample): the
                             voice goes back to the pool unpublished and the note-on still runs to its end
                             (CG-VOICEFAIL poolFreeDelta=0 voicesDelta=0 lastNoteSet=1 required)
    CG-HEAP                  the interaction tier allocating and freeing on the heaps while the render plays: every
                             heap step (crates/deluge_alloc/src/section.rs) must run inside a section
    CG-SLAB                  a cluster-slab slot claimed, touched and released from the interaction tier while the
                             render plays: each slot's claim or clear must run inside a section
    CG-MODFXOOM              the song master's mod FX type set to chorus with its ring's allocation made to fail:
                             the type change consumes the failure, no ring is held and the chorus passes its block
                             through unchanged; a change to flanger then holds a ring and the master's render runs the
                             effect over it (CG-MODFXOOM injected=1 passthrough=1 recovered=1 noted=1
                             required; noted = the ring hand-over reported itself to the structural-edit probe)
    CG-MODFXCLONE            an audio track whose chain runs chorus, cloned into a new track (AudioOutput::cloneFrom):
                             the clone holds its own ring before any block renders, and its chorus changes a block
                             (CG-MODFXCLONE ring=1 renders=1 required)
    CG-MODFXSWAP             a synth's mod FX type set chorus, flanger, phaser, chorus by the type menu's Edit while
                             the song plays: after each change a ring is held exactly when the type runs over one
                             (CG-MODFXSWAP rings=1 noted=1 required)
    CG-GRAINOOM              the song master's mod FX type set to the grain with its ring's allocation made to fail:
                             the type change consumes the failure, no ring is held and the grain passes its block
                             through unchanged; setting the grain again holds a ring and the master's render runs the
                             effect over it (CG-GRAINOOM injected=1 passthrough=1 recovered=1 noted=1 required; noted =
                             the grain's hand-over reported itself to the structural-edit probe)
    CG-DELAYKNOB             a synth's and the master's delay feedback turned by the gold knob's Edit: lifted off its
                             minimum while OFF it turns the delay on, digital; an ANALOG delay, or one set OFF with its
                             feedback already up, keeps its type (CG-DELAYKNOB lifted=1 analogKept=1 offKept=1 chain=1
                             required)
    CG-DELAYOOM              a synth's delay set on with the arena's region made to fail: the delay cannot start and
                             passes the audio through; the arena's next maintenance lends a region and it starts
                             (CG-DELAYOOM injected=1 passthrough=1 recovered=1 noted=1 required)
    CG-DELAYMARGIN           five synths' delays at their slowest rate starting on one block: their reserves, worked out
                             from their settings, are in the arena first, so every one starts on that block
                             (CG-DELAYMARGIN shortfall=0 started=1 required)
    CG-DELAYOFF              a ringing delay set OFF, on again, hibernated; a clone; a retired synth; then every delay
                             OFF (CG-DELAYOFF off=1 on=1 hibernate=1 clone=1 count=1 destroyed=1 released=1 noted=1
                             required)
    CG-GRAINSWAP             a synth's mod FX type set grain, chorus, grain, none by the type menu's Edit while the
                             song plays: after each change the grain holds its ring exactly when the type is the grain
                             (CG-GRAINSWAP blocks=1 noted=1 required)
    CG-CHOKE                 two choke-mode drums sounded in turn, each choking the other, over 400 blocks
    CG-BIGROW                a note row inserted below 128 others, moving them all; CG-MASK reports the edit's host
                             wall time (maskMicros=), an upper bound on the section it holds (a block is ~2900 us)

Every leg's count must be 0. A leg whose edit ends the process is reported with aborted=1 and fails the run, and so
does a missing leg line. The registry entry requires the
result line.
"""

import re

LEGS = [
    "NOTEADD",
    "NOTEDEL",
    "ROWADD",
    "ROWDEL",
    "CLIPNEW",
    "CLIPDEL",
    "CLIPDUP",
    "ARRANGE",
    "AUTOMATE",
    "PATCHCABLE",
    "DRUM",
    "REPLACE",
    "UNDO",
    "BIGROW",
    "ROWDRAG",
    "CLIPMOVE",
    "OUTPUTMOVE",
    "QUANTIZE",
    "RENDERSET",
    "RENDERALLOC",
    "CHOKE",
    "VOICEPOLY",
    "VOICEMONO",
    "VOICEKILL",
    "VOICEMULTIRANGE",
    "VOICEFAIL",
    "HEAP",
    "SLAB",
    "MODFXOOM",
    "MODFXCLONE",
    "MODFXSWAP",
    "GRAINOOM",
    "GRAINSWAP",
    "DELAYKNOB",
    "DELAYOOM",
    "DELAYMARGIN",
    "DELAYOFF",
]
# The legs whose edits must all be made while the render is excluded.


def _grep(result, pattern: str, otherwise: str) -> str:
    found = [line for line in result.lines if re.match(pattern, line)]
    return "\n".join(found) if found else otherwise


def _guarded(result, leg: str) -> bool:
    return result.has(f"CG-{leg} unguarded=0 ")


def _check(gate, result) -> None:
    for leg in LEGS:
        if not any(
            re.match(rf"CG-{leg} unguarded=[0-9]+ ", line) for line in result.lines
        ):
            gate.fail(f"no CG-{leg} line: a stage never ran")
            return

    # The render must not allocate: a kit's drums start and stop rendering without touching the heap.
    alloc = re.search(
        r"CG-RENDERALLOC renderAllocs=(\d+) delayStarts=(\d+) delayStops=(\d+)",
        "\n".join(result.lines),
    )
    if alloc and (int(alloc.group(2)) < 1 or int(alloc.group(3)) < 1):
        gate.fail(
            f"CG-RENDERALLOC — the drum's delay must start and abandon its tapes ({alloc.group(0)})"
        )
    if not (alloc and alloc.group(1) == "0"):
        found = _grep(
            result,
            "CG-RENDERALLOC renderAllocs=",
            "no CG-RENDERALLOC renderAllocs= line",
        )
        gate.fail(f"{found}: the render allocated")
        return

    # A note-on whose voice cannot start returns it to the pool; it never reaches the Sound's voices.
    if not result.has_line("CG-VOICEFAIL poolFreeDelta=0 voicesDelta=0 lastNoteSet=1"):
        found = _grep(result, "CG-VOICEFAIL poolFreeDelta", "no CG-VOICEFAIL line")
        gate.fail(
            f"{found}: a failed note-on leaked or published its voice, or did not run to its end"
        )
        return

    # A mod FX ring that could not be allocated passes the signal through; the next type change takes one.
    if not result.has_line("CG-MODFXOOM injected=1 passthrough=1 recovered=1 noted=1"):
        found = _grep(result, "CG-MODFXOOM injected", "no CG-MODFXOOM line")
        gate.fail(
            f"{found}: a mod FX ring that could not be allocated did not pass the signal through, or the next type"
            " change did not bring the effect back"
        )
        return

    # A cloned chain holds its own ring as soon as it is cloned, and runs its effect over it.
    if not result.has_line("CG-MODFXCLONE ring=1 renders=1"):
        found = _grep(result, "CG-MODFXCLONE ring", "no CG-MODFXCLONE line")
        gate.fail(f"{found}: a cloned chain held no ring, or did not run its effect")
        return

    # Each type change holds a ring exactly when the type runs over one.
    if not result.has_line("CG-MODFXSWAP rings=1 noted=1"):
        found = _grep(result, "CG-MODFXSWAP rings", "no CG-MODFXSWAP line")
        gate.fail(
            f"{found}: a mod FX type change left a ring that does not match its type"
        )
        return

    # A grain ring that could not be allocated passes the signal through; setting the grain again takes one.
    if not result.has_line("CG-GRAINOOM injected=1 passthrough=1 recovered=1 noted=1"):
        found = _grep(result, "CG-GRAINOOM injected", "no CG-GRAINOOM line")
        gate.fail(
            f"{found}: a grain ring that could not be allocated did not pass the signal through, or setting the grain"
            " again did not bring the effect back"
        )
        return

    # Each type change holds the grain's ring exactly when the type is the grain.
    if not result.has_line("CG-DELAYKNOB lifted=1 analogKept=1 offKept=1 chain=1"):
        found = _grep(result, "CG-DELAYKNOB lifted", "no CG-DELAYKNOB line")
        gate.fail(
            f"CG-DELAYKNOB — a knob lifting an OFF delay's feedback must turn it on, digital, and leave a delay "
            f"already on, or one set OFF with its feedback up, as it is ({found})"
        )
    for line, why in (
        (
            "CG-DELAYOOM injected=1 passthrough=1 recovered=1 noted=1",
            (
                "a delay whose arena region could not be lent must pass the audio through, then start once the "
                "arena's maintenance lends one"
            ),
        ),
        (
            "CG-DELAYMARGIN shortfall=0 started=1",
            (
                "five delays at their slowest rate starting on one block must find their reserves in the arena and "
                "all start on that block"
            ),
        ),
        (
            "CG-DELAYOFF off=1 on=1 hibernate=1 clone=1 count=1 destroyed=1 released=1 noted=1",
            (
                "a delay set OFF, hibernated or destroyed must give its tapes back and leave the count, a clone must "
                "be counted, and with no delay on the arena must hold no region"
            ),
        ),
    ):
        if not result.has_line(line):
            leg = line.split(" ")[0]
            found = _grep(result, leg + " ", f"no {leg} line")
            gate.fail(f"{leg} — {why} ({found})")
    if not result.has_line("CG-GRAINSWAP blocks=1 noted=1"):
        found = _grep(result, "CG-GRAINSWAP blocks", "no CG-GRAINSWAP line")
        gate.fail(
            f"{found}: a mod FX type change left a grain ring that does not match its type"
        )
        return

    failed = [leg for leg in LEGS if not _guarded(result, leg)]
    if failed:
        gate.fail(
            "legs made structural edits while the render could preempt them:"
            + "".join(" " + leg for leg in failed)
        )
        return

    gate.say(
        "PASS — every leg's structural edits were made while the render was excluded"
    )


def check(gate, run):
    _check(gate, run)
