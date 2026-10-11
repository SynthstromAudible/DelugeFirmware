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
    CG-RENDERALLOC           a kit clip playing for 200 blocks; CG-RENDERALLOC renderAllocs= counts heap allocations the
                             render made (host only) -- required 0
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
                             effect over it (CG-MODFXOOM injected=1 passthrough=1 recovered=1 noted=1 required; noted = the ring hand-over
                             reported itself to the structural-edit probe)
    CG-MODFXCLONE            an audio track whose chain runs chorus, cloned into a new track (AudioOutput::cloneFrom):
                             the clone holds its own ring before any block renders, and its chorus changes a block
                             (CG-MODFXCLONE ring=1 renders=1 required)
    CG-MODFXSWAP             a synth's mod FX type set chorus, flanger, phaser, chorus by the type menu's Edit while
                             the song plays: after each change a ring is held exactly when the type runs over one
                             (CG-MODFXSWAP rings=1 noted=1 required)
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
    if not result.has_line("CG-RENDERALLOC renderAllocs=0"):
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
