"""The clip-lifetime gate: nothing the render reads is destroyed while the render can still reach it.

Deletes a playing clip through each real clip deletion path, a drum through the two former direct drum destroys
(SAVEKIT naturally, WONTSOUND by forcing a state no natural note-on/off path reaches -- see its entry below), and the
song through each way it is replaced, and reports how many Clips, Songs and Sounds each path destroyed while the render
could still reach them.

## Why this gate exists

The render holds clip pointers across blocks: the current song's currentClip and syncScalingClip, every clip's
beingRecordedFromClip, and each output's activeClip. The gold knobs name a clip by id, which must not stay on a
retired clip.
Song::deleteClipObject() destroys a clip at once, on the interaction tier, and no deletion path clears all of those
first -- ~Clip's own fallback clears only currentClip. The clip deletion paths hand the clip to the render
(AudioEngine::retire(Clip*)), which drops those references at its next block before the clip is freed. SAVEKIT and
WONTSOUND hand their drum to the render the same way (AudioEngine::retire(SoundDrum*)). A replaced song is handed to
the render (AudioEngine::retire(Song*)), which lets go of every Sound of its outputs before ~Song runs, and a
stopped-clock load or clear waits (AudioEngine::awaitRetired()) for the old song to be freed before it builds the new
one. All of these legs are required clean (see "Required legs" below).

What the lines cover (tests/integration/scenarios/clip_lifetime_scenario.cpp writes them). Every leg plays its clip on the
internal clock, holding a note on the clip's blank synth, and the scenario renders 8 blocks before the deletion and 8
after it:
    CL-PROBE        per leg, before the deletion: whether the clip is playing, how many ticks the render actioned, the
                    synth's live voices, and which references point at the clip.
    CL-DANGLING     per leg, just after the deletion: which references still hold the clip's address, and whether the
                    clip is still in the song (compared by value; the clip may be freed).
    CL-SESSIONDELETE  the current, selected session clip, deleted through the session view's delete gesture
                    (SessionView::removeClip() -> Song::removeSessionClip()).
    CL-ARRANGERDELETE an arrangement-only clip that is the current clip, deleted from the arranger with the arranger's
                    delete gesture (ArrangerView::deleteClipInstance()'s steps: view to the song, a real
                    CLIP_INSTANCE_EDIT action, the instance's existence change, Song::deletingClipInstanceForClip()'s
                    logged branch), then destroyed by discarding the logs.
    CL-SWAPTYPE     the current, selected clip changed to an audio clip
                    (SessionView::replaceInstrumentClipWithAudioClip() -> Song::swapClips()).
    CL-UNDODELETE   the same arranger delete, undone (restored=), redone, and then destroyed by discarding the logs
                    (ConsequenceClipExistence::prepareForDestruction()). The only logged clip deletion the firmware
                    records: the session view's delete discards the logs and records nothing.
    CL-UNDO         UNDODELETE's undo: restored=1 when the clip is back in arrangementOnlyClips with an instance.
                    Printed before the redo, so it survives a redo that ends the process.
    CL-DISCARD      ARRANGERDELETE and UNDODELETE: held=1 when the undo log still held the delete at the discard.
    CL-ARRANGERNOUNDO the same arranger delete when the action logger has no action to give (no undo memory): the
                    gesture passes a null action, so Song::deletingClipInstanceForClip() deletes the clip at once. The
                    one path that leaves the song's currentClip on the deleted clip for the render to drop.
    CL-RECORDSOURCE the clip another clip records from (beingRecordedFromClip), deleted through the session view's
                    delete gesture.
    CL-SYNCSCALE    the sync-scaling clip, deleted through Song::removeSessionClip() (the session view refuses it).
    CL-SAVEKIT      a playing kit clip's kit holds a second drum no note row has, triggered just before the song is
                    saved (Song::writeToFile() into memory, as the song save serialises): Kit::writeDataToFile() hands
                    that drum to the render (AudioEngine::retire(SoundDrum*)) instead of destroying it on the spot.
                    CL-SAVE says what the drum was doing at the save and how many bytes it wrote; CL-DANGLING (inKit=,
                    renderingActive=) whether the kit still holds it afterwards.
    CL-EPOCH0       first, at boot: Create New Song right after deluge_app_init() returns, before the audio tier has
                    rendered a block. epoch= is the render's block count so far: 1, the block boot renders inline
                    from setupBlankSong() (a song exists only from there on, so epoch 0 with a song to clear is never
                    reachable). With no render running, the clear's bounded wait must drive the render itself
                    (deluge_audio_await_block()) and return (returned=1, pending=0) rather than hang. At epoch 0,
                    AudioEngine::retire() would destroy the song on the spot. Run by the golden runner's boot hook, not
                    the step machine.
    CL-SONGCLEAR    stopped clock, a synth sounding: the context menu's Create New Song (ClearSongContextMenu::runAccept()
                    -> deleteOldSongBeforeLoadingNew()). CL-CLEARED once it is over.
    CL-SONGLOAD     stopped clock, a synth sounding: a song the scenario wrote through Song::writeToFile() is loaded
                    through the load browser (LoadSongUI -> deleteOldSongBeforeLoadingNew()). CL-LOADED loaded=1 when
                    the song came off the card.
                    Both stopped-clock legs also say pendingAtBuild= (AudioEngine::retiredPending() when the new Song
                    was constructed: the old song must already be freed), pending= (what was still waiting once the
                    clear or load returned) and violations= (AudioEngine::retireViolations() over the leg).
    CL-AFTER        each stopped-clock song leg, once its clear or load is over: a UI timer fires (the play-enable
                    flash, which walks the current song's clips) and a MIDI note arrives on the DIN port. A regression
                    guard that the clear or load left currentSong set (song=1) and usable. It runs after the operation
                    returns, so it says nothing about the wait inside it: that no interaction task runs while
                    currentSong is null holds by construction (the wait runs no app-loop pass), and no leg can observe
                    that window.
    CL-SONGSWAP     the same load with the clock running and a synth sounding: the load arms a swap
                    (Session::armForSongSwap()), the render launches it (doSongSwap()), and
                    LoadSongUI::runFinishSegment() hands the old song to the render.
    CL-BURST        BURSTSONG's song before the clear: how many outputs, session clips and registered Sounds it holds.
    CL-LETGO        BURSTSONG's letGoMicros: the host wall time of the longest block-top let-go during the clear -- the
                    one that lets go of the whole song, all of it inside one render block. Reported, not required.
    CL-BURSTSONG    a song of 20 outputs -- 10 synths and 10 kits of 32 drums -- with two session clips each, cleared
                    with the clock stopped: the whole song is let go of and freed inside the clear's bounded wait
                    (pending=0, violations=0).
    CL-CLEARPLAYING Create New Song while following an external clock (runAccept() keeps that playback going), with an
                    audio clip holding what an unsaved recording leaves: a Sample with a TEMP path. The clear swaps in
                    the new song, hands the old one to the render, then frees every temp-recorded Sample
                    (deleteAnyTempRecordedSamplesFromMemory()). The old song must already be freed by then
                    (pendingAtFree=0); otherwise its ~SampleHolder later writes into the freed Sample (E219 on alpha).
                    CL-CLEARSAMPLE says whether the Sample is gone and what was still waiting when it went.
    CL-WINDOW       SESSIONDELETE and ARRANGERNOUNDO, just after the deletion and before any block: whether the song's
                    currentClip or the gold knobs' focus still names the retired clip. Required 0 and 0.
    CL-FREED        per clip leg, before its leg line: the scenario renders a few blocks and reclaims, so the retired
                    clip is destroyed (and ~Clip()'s check runs) inside its own leg; pending= is what is still waiting.
    CL-RECLAIM      printed twice: once after the other legs and before WONTSOUND, and once more after WONTSOUND has
                    run, each time AudioEngine::retiredPending() once the render has run and reclaimRetired() has been
                    called. The first is report-only; the final one (the last CL-RECLAIM line) is required pending=0.
    CL-WONTSOUND    last: a playing kit clip whose drum has an empty note row and is put back in the kit's
                    drumsWithRenderingActive by hand -- skipping rendering, yet still in that set, a state no natural
                    note-on/off path reaches (SoundDrum::setSkippingRendering() is the only writer of the flag, and it
                    erases from the set in the same call that sets it) -- then AudioEngine::songSwapAboutToHappen() --
                    what a load or clear started during playback runs first -> Song::deleteSoundsWhichWontSound() ->
                    InstrumentClip::deleteSoundsWhichWontSound(), which hands the drum to the render
                    (AudioEngine::retire(SoundDrum*)) instead of destroying it on the spot. This leg does not reproduce
                    a real precondition; it proves only that the retire hand-off is wired correctly. Its real safety
                    value is on release builds: the hasActiveVoices() check that would otherwise catch a drum with
                    genuinely active voices (FREEZE_WITH_ERROR("E176")) is ALPHA_OR_BETA_VERSION-only, and retire()'s
                    let-go kills a drum's voices unconditionally before it is freed, whichever build this runs on.
    CL-RESULT       fixtures that could not be built, or a clock that never ran.

Each leg line is `CL-<LEG> clipsReferenced=<n> songsReachable=<n> soundsLive=<n>`: how far
Clip::clipsDestroyedWhileReferenced(), Song::songsDestroyedWhileReachable() and Sound::soundsDestroyedWithLiveVoices()
moved over the leg. A leg reporting more than 0 is a red leg.

## Aborts

A leg whose deletion, or the render reading what it left dangling, ends the process -- a firmware freeze
(FREEZE_WITH_ERROR aborts on the host BSP), a std::terminate or a SIGSEGV -- still prints its line, marked aborted=1
with the step it died in, and a CL-RESULT. That is a red leg, not a failure of the run: this module records it and
starts a new run at the next leg (CLIP_LIFETIME_FROM), so one crashing path does not hide the others. The lines of
every run are checked together, and where a leg printed its line in more than one run, its last line counts.

## Required legs

SESSIONDELETE, ARRANGERDELETE, SWAPTYPE, UNDODELETE, ARRANGERNOUNDO, RECORDSOURCE and SYNCSCALE must each read
clipsReferenced=0, must not abort, and must show CL-FREED pending=0 (the clip really was freed inside the leg);
UNDODELETE must also read restored=1. SAVEKIT and WONTSOUND must each read soundsLive=0 and must not abort. The song legs
(EPOCH0, SONGCLEAR, SONGLOAD, SONGSWAP, BURSTSONG, CLEARPLAYING) must each read clipsReferenced=0 songsReachable=0
soundsLive=0 and violations=0, and must not abort; the stopped-clock ones (EPOCH0, SONGCLEAR, SONGLOAD, BURSTSONG) must
also read pendingAtBuild=0 and pending=0, EPOCH0 epoch=0 or 1 and returned=1, and SONGCLEAR, SONGLOAD and BURSTSONG
must show their CL-AFTER line; CLEARPLAYING must read pendingAtFree=0. The final CL-RECLAIM line (after WONTSOUND) must
read pending=0. Any of them failing that fails the gate.

The gate also fails when a leg line or a CL-RECLAIM line is missing, a fixture could not be built (CL-RESULT
failures>0), or the process ended some other way: an exit with no aborted=1 line for the leg that was running, or a
wedge.

This module runs the scenario itself: once, or once more after each leg that ends the process.
"""

import re

LEGS = [
    "EPOCH0",
    "SESSIONDELETE",
    "ARRANGERDELETE",
    "SWAPTYPE",
    "UNDODELETE",
    "ARRANGERNOUNDO",
    "RECORDSOURCE",
    "SYNCSCALE",
    "SAVEKIT",
    "SONGCLEAR",
    "SONGLOAD",
    "SONGSWAP",
    "BURSTSONG",
    "CLEARPLAYING",
    "WONTSOUND",
]
# The clip legs.
REQUIRED_LEGS = [
    "SESSIONDELETE",
    "ARRANGERDELETE",
    "SWAPTYPE",
    "UNDODELETE",
    "ARRANGERNOUNDO",
    "RECORDSOURCE",
    "SYNCSCALE",
]
# The two direct-drum-destroy legs: soundsLive=0 and no abort -- not clipsReferenced (they destroy a drum, not a clip)
# and not CL-FREED (that stage only runs for the clip legs).
REQUIRED_SOUND_LEGS = ["SAVEKIT", "WONTSOUND"]
# The song legs: nothing of the old song reachable when it is destroyed, and no retire violation.
REQUIRED_SONG_LEGS = [
    "EPOCH0",
    "SONGCLEAR",
    "SONGLOAD",
    "SONGSWAP",
    "BURSTSONG",
    "CLEARPLAYING",
]
# The stopped-clock song legs, which must also have freed the old song before building the new one (pendingAtBuild=0)
# and left nothing waiting once the clear or load returned (pending=0).
STOPPED_SONG_LEGS = ["EPOCH0", "SONGCLEAR", "SONGLOAD", "BURSTSONG"]
# The stopped-clock song legs the scenario drives a UI timer and a MIDI note-in after (CL-AFTER): currentSong must be
# set and usable once the clear or load has returned.
AFTER_LEGS = ["SONGCLEAR", "SONGLOAD", "BURSTSONG"]

ABORTED = re.compile(r"^CL-([A-Z0-9]+) .* aborted=1( |$)")
RECLAIM = re.compile(r"^CL-RECLAIM pending=[0-9]+$")
LEG_LINE = re.compile(r"^CL-[A-Z0-9]+ (restored=[01] )?clipsReferenced=")


def _next_leg(leg: str) -> str:
    """The leg after `leg`, or "" after the last or for a name that is not a leg."""
    if leg not in LEGS:
        return ""
    i = LEGS.index(leg)
    return LEGS[i + 1] if i + 1 < len(LEGS) else ""


def _has(lines: list[str], pattern: str) -> bool:
    return any(re.search(pattern, line) for line in lines)


def _last(lines: list[str], pattern: str) -> str:
    found = [line for line in lines if re.search(pattern, line)]
    return found[-1] if found else ""


def _runs(gate) -> list[str] | None:
    """Every run's CL- lines, in order; None when a run ended the process without a leg saying it did."""
    lines: list[str] = []
    start = ""
    while True:
        result = gate.run(env={"CLIP_LIFETIME_FROM": start})
        lines += result.starting("CL-")
        if result.code == 0:
            return lines
        aborted = [m.group(1) for line in result.lines if (m := ABORTED.match(line))]
        if not aborted:
            gate.fail(f"the run exited {result.code} with no leg reporting aborted=1")
            for line in lines or result.lines[:60]:
                gate.say(line)
            for line in result.err.splitlines()[-20:]:
                gate.say(line)
            return None
        gate.say(
            f"clip lifetime: CL-{aborted[-1]} ended the process (exit {result.code}); recorded as a red leg"
        )
        start = _next_leg(aborted[-1])
        if not start:
            return lines


def _required_failures(lines: list[str]) -> str:
    """The required legs that failed, each followed by a space, in the order they are checked."""
    failed = ""
    for leg in REQUIRED_LEGS:
        line = _last(lines, rf"^CL-{leg} (restored=[01] )?clipsReferenced=")
        if (
            not re.search(r"(^| )clipsReferenced=0( |$)", line)
            or "aborted=1" in line
            or (leg == "UNDODELETE" and " restored=1 " not in line)
        ):
            failed += f"{leg} "
        elif f"CL-FREED leg={leg} pending=0" not in lines:
            # The clip was never freed inside the leg, so ~Clip()'s check never ran on it: a 0 would prove nothing.
            failed += f"{leg}(not-freed) "
    # Just after the deletion, before any block: the interaction tier must no longer see the retired clip as the
    # current clip or the view's active one (AudioEngine::retire(Clip*) clears them itself).
    for leg in ("SESSIONDELETE", "ARRANGERNOUNDO"):
        if f"CL-WINDOW leg={leg} currentClip=0 view=0" not in lines:
            failed += f"{leg}(window) "
    # The drum must be handed to the render (soundsLive=0) rather than destroyed on the spot, and the leg must not end
    # the process.
    for leg in REQUIRED_SOUND_LEGS:
        line = _last(lines, rf"^CL-{leg} clipsReferenced=")
        if (
            not line
            or "aborted=1" in line
            or not re.search(r"(^| )soundsLive=0( |$)", line)
        ):
            failed += f"{leg} "
    # The old song must be unreachable when ~Song runs.
    for leg in REQUIRED_SONG_LEGS:
        line = _last(lines, rf"^CL-{leg} clipsReferenced=")
        if (
            not line
            or "aborted=1" in line
            or not re.search(
                r" clipsReferenced=0 songsReachable=0 soundsLive=0( |$)", line
            )
            or not re.search(r"(^| )violations=0( |$)", line)
        ):
            failed += f"{leg} "
    # The stopped-clock ones must free it before they build the new one.
    for leg in STOPPED_SONG_LEGS:
        line = _last(lines, rf"^CL-{leg} clipsReferenced=")
        if not re.search(r"(^| )pendingAtBuild=0( |$)", line) or not re.search(
            r"(^| )pending=0( |$)", line
        ):
            failed += f"{leg}(pending) "
    # epoch=1 is the one block boot renders itself (setupBlankSong() -> setUIForLoadedSong() runs the audio routine
    # inline, before the render task exists); no scheduled render has run yet.
    if not _has(lines, r"^CL-EPOCH0 .* epoch=[01] returned=1 "):
        failed += "EPOCH0(epoch) "
    # Create New Song with a clock running frees the temp-recorded Samples right after it hands the old song over: the
    # old song, whose audio clip holds one of them, must already be freed by then.
    if (
        not _has(lines, r"^CL-CLEARPLAYING .* pendingAtFree=0 ")
        or "CL-CLEARSAMPLE leg=CLEARPLAYING sampleFreed=1 pendingAtFree=0" not in lines
    ):
        failed += "CLEARPLAYING(order) "
    for leg in AFTER_LEGS:
        if f"CL-AFTER leg={leg} uiTimer=1 midiNoteIn=1 song=1" not in lines:
            failed += f"{leg}(after) "
    return failed


def _check(gate, lines: list[str]) -> None:
    for leg in LEGS:
        if not _has(lines, rf"^CL-{leg} (restored=[01] )?clipsReferenced="):
            gate.fail(f"no CL-{leg} line: a leg never ran")
            for line in lines:
                gate.say(line)
            return
    reclaims = [line for line in lines if RECLAIM.match(line)]
    if not reclaims:
        gate.fail("no CL-RECLAIM line")
        for line in lines:
            gate.say(line)
        return

    for line in lines:
        gate.say(line)

    results = [line for line in lines if line.startswith("CL-RESULT ")]
    if not results:
        gate.fail("no CL-RESULT line: the scenario never reached its summary")
        return
    if any(line != "CL-RESULT failures=0" for line in results):
        found = "".join(f"{line} " for line in lines if line.startswith("CL-RESULT"))
        gate.fail(f"a fixture could not be built or its clock never ran: {found}")
        return

    failed = _required_failures(lines)
    if failed:
        gate.fail(
            "a required leg left a reference to a freed clip or song, a live-voiced Sound destroyed on the spot, built"
            f" a song before the old one was freed, aborted, or did not restore: {failed}"
        )
        return

    # Everything the render let go of, including the two direct-drum-destroy legs' retired drums, must actually have
    # been reclaimed by the end of the run.
    if not reclaims[-1].endswith(" pending=0"):
        gate.fail(f"the final CL-RECLAIM did not read pending=0: {reclaims[-1]}")
        return

    red_legs = sorted(
        {
            re.sub(r"^CL-([A-Z0-9]+) .*", r"\1", line)
            for line in lines
            if LEG_LINE.match(line)
            and re.search(
                r"aborted=1|(clipsReferenced|songsReachable|soundsLive)=[1-9]", line
            )
        }
    )
    red = "".join(f"{leg} " for leg in red_legs)
    if _has(lines, r"^CL-RECLAIM pending=[1-9]"):
        red += "RECLAIM "
    gate.say(f"PASS — every required leg clean; red legs: {red or 'none'}")


def run(gate):
    lines = _runs(gate)
    if lines is not None:
        _check(gate, lines)
