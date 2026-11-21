"""The sound-lifetime probe.

Deletes a sounding synth and a sounding kit through each real deletion path, and requires that no path destroys a Sound
while it still has live voices (Sound::soundsDestroyedWithLiveVoices()), and that everything the deletions retired is
freed once the render has run past it.

A Sound destroyed while it still has live voices crashes itself, with no render involved: ~Sound's last step kills
them through its pool, and each one's Sound::unassignVoice (processing/sound/sound_voices.cpp) -> Sound::voiceUnassigned -> reassessRenderSkippingStatus calls getArpSettings(), pure
virtual in Sound by then. Reading the deletion paths guesses which of them can do that; this measures it. Every deletion
path must end a Sound's voices before any part of it is destroyed.

What the lines cover (tests/integration/scenarios/sound_lifetime_scenario.cpp writes them):
  SL-PROBE     per leg: how many voices were live at the deletion, whether the output is in the song (the render
               reaches only those voices), and how far the render's clock moved with the note held.
  SL-SYNTH     a synth in the song, deleted through Song::deleteOutputThatIsInMainList().
  SL-UNEDITED  an un-edited synth in no list, deleted through Song::deleteOrAddToHibernationListOutput().
  SL-KIT       a kit in the song with a sounding synth drum, deleted through deleteOutputThatIsInMainList().
  SL-DELETEDRUM a sounding drum deleted with InstrumentClipView::deleteDrum()'s steps: the note row's note stopped and
               the drum taken out of drumsWithRenderingActive, Kit::removeDrum(), the note row's reassignment, then
               AudioEngine::retire(). The only drum-delete site that can reach a sounding drum: the preset-replace site
               kills the voices first, and the folder-import, slicer and instrument-load sites only drop drums that
               never sounded.
  SL-OPPORTUNISTIC  in one step: retire a blank drum, render 3 blocks, retire another. The second retire must free the
               first on its own (pending=1): no reclaim-task tick runs inside a step.
  SL-BURST     in one step: retire 80 blank drums (more than the retire ring's 64 slots), then render and reclaim block
               by block. Required: violations=0 (no drum destroyed while still in the Sound registry, i.e. before the
               render let go of it) and pending=0.
  SL-RECLAIM   after the last leg and a few more blocks: how many retired objects still wait to be destroyed.

Every deletion retires (AudioEngine::retire): the render ends the voices and deregisters the Sounds at the top of its
next block, and the memory is freed by the first reclaim after that once two blocks have begun. So a leg's count is read
after its settle blocks, and a leg whose objects are still waiting at that point fails. Required: every leg
destroyedWithLiveVoices=0, SL-OPPORTUNISTIC pending=1, SL-BURST violations=0 pending=0 and SL-RECLAIM pending=0. The run
also fails when a leg's fixture could not be built or never sounded, a line is missing, or a deletion ends the process:
a firmware freeze (E321 from Kit::removeDrum on a drum still in drumsWithRenderingActive; FREEZE_WITH_ERROR aborts on
the host BSP) or a std::terminate, during the deletion or the deferred destruction in the settle. A leg that does still
prints its count first, marked aborted=1 with the step it died in, and the run's SL-RESULT.

Gate sound-lifetime:
    Every deletion path must end a Sound's voices before any part of it is destroyed. A Sound destroyed with live voices
    crashes itself, with no render involved: ~Sound's last step kills them through its pool,
    and each one's Sound::unassignVoice -> Sound::voiceUnassigned -> reassessRenderSkippingStatus calls
    getArpSettings(), pure virtual in Sound by then. The probe holds a note on a blank synth and on a kit's synth drum,
    renders a few blocks, then deletes it through each real path -- SL-SYNTH (a synth in the song,
    Song::deleteOutputThatIsInMainList), SL-UNEDITED (an un-edited synth in no list,
    Song::deleteOrAddToHibernationListOutput), SL-KIT (a kit in the song, deleteOutputThatIsInMainList) and
    SL-DELETEDRUM (InstrumentClipView::deleteDrum's steps: the note row's note stopped and the drum taken out of
    drumsWithRenderingActive, Kit::removeDrum, the note row's reassignment, then AudioEngine::retire; the only
    drum-delete site that can reach a sounding drum, since the preset-replace site kills the voices first and the
    folder-import, slicer and instrument-load sites only drop drums that never sounded) -- and requires
    Sound::soundsDestroyedWithLiveVoices() to be 0 for every path. Every deletion retires: the render ends the voices
    and deregisters the Sounds at the top of its next block and the memory is freed by the first reclaim after that once
    two blocks have begun, so each count is read after settle blocks, a leg whose objects are still waiting then fails,
    and SL-RECLAIM requires nothing retired to be left waiting at the end. SL-OPPORTUNISTIC requires a retire to free an
    older, already let-go object on its own (pending=1, with no reclaim-task tick in the step); SL-BURST retires 80
    blank drums (past the 64-slot ring, into the overflow, drained 16 per block) and requires violations=0 (no drum
    destroyed while still in the Sound registry) and pending=0. The run also fails when a leg's fixture could not be
    built or never sounded, a line is missing, or a deletion (or its deferred destruction) ends the process -- a
    firmware freeze (E321 from Kit::removeDrum on a drum still in drumsWithRenderingActive) or a std::terminate -- in
    which case the leg still prints its count with aborted=1 and the step it died in.
"""

from scenario_gate import has_fields


def _lines(run, prefix: str) -> str:
    return "\n".join(run.starting(prefix))


def check(gate, run):
    # Every deletion path must end a Sound's voices before any part of it is destroyed.
    for leg in ("SL-SYNTH", "SL-UNEDITED", "SL-KIT", "SL-DELETEDRUM"):
        if not run.has_line(f"{leg} destroyedWithLiveVoices=0"):
            gate.fail(
                f"{_lines(run, f'{leg} destroyedWithLiveVoices=')}: a Sound was destroyed with live voices"
            )
            return

    # A retire frees what the render has already let go of and run two blocks past, with no reclaim-task tick needed.
    if not run.has_line("SL-OPPORTUNISTIC pending=1"):
        gate.fail(
            f"{_lines(run, 'SL-OPPORTUNISTIC')}: a retire did not free an older, let-go object"
        )
        return

    # A burst past the ring's capacity: nothing is destroyed before the render lets go of it, and everything is freed.
    if not any(
        has_fields(line, violations=0, pending=0) for line in run.starting("SL-BURST ")
    ):
        gate.fail(
            f"{_lines(run, 'SL-BURST')}: a retired object was destroyed before the render let go of it, "
            "or never freed"
        )
        return

    # Everything retired has been freed once the render has run two blocks past it.
    if not run.has_line("SL-RECLAIM pending=0"):
        gate.fail(f"{_lines(run, 'SL-RECLAIM')}: a retired object was never freed")
        return

    gate.say(
        "PASS — every leg sounded and was deleted with no live voices, and everything retired was freed"
    )
