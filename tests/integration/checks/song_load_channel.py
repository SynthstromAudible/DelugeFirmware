"""The song-load channel gate: a song saved and loaded back through the real LoadSongUI, its bytes through the channels.

A load that brings back the right song says nothing about how its bytes arrived. This gate pins the whole-file staging
read the song load depends on: it must be served by the channels, after boot, and a gate that only checks what came
back would stay green with that read broken in any way that still produced the song.

One leg: the byte channel has no feature switch, so there is no build without it to compare against.

What the checks cover (tests/integration/scenarios/song_load_channel_scenario.cpp writes the lines):
    SL-LOADED    the song came back off the card: a value stamped into it before saving and deliberately overwritten in
                 memory afterwards.
    SL-CHANNEL   the staging read was served by the channel -- the async arm ran.
    SL-TICKED    the staging ran through the channels after boot: `channelTicks` counts staging attempts after boot,
                 through the channels. The gate's actual claim.
    SL-CANCEL    BACK while a load stages drops it, leaves the current song playing, and parks the staging buffer the
                 byte channel still writes into (checked in the scenario, reported through SL-RESULT).
    SL-REFUSED   a cancel once the load is past STAGE is refused and the load completes (likewise).
    SL-*-LASTOPENED  with the startup-song mode LASTOPENED, the cancelled load writes no settings file and the
                 completed one does (likewise).

The registry entry requires each marker and the result line; this module adds the channel's two counts.

Gate song-load-channel:
    the song load must read its bytes through the storage channel. One leg: the byte channel has no feature switch.
    SL-LOADED requires a value stamped into the song before saving -- and deliberately overwritten in memory afterwards
    -- to come back, because nothing cheaper distinguishes a real load: the song's NAME is written by the save from what
    the user typed, so it matches whether or not the load happened, and its object IDENTITY is unchanged across a load
    that did happen. SL-CHANNEL requires a staging read to have been SERVED and SL-TICKED requires the staging to have
    run through the channels after boot. This gate exists because a load that brings back the right song says nothing
    about how its bytes arrived, so a gate that only checks what came back would stay green with the staging read
    broken. The read sizes its buffer with the byte channel's stat and fills it with the byte channel's read-into, and
    both are always on. SL-ORDER requires the sample the song carries to be bound by the time the load ends. The song is
    given that sample before the save: measured, the boot song is one instrument clip with no audio clips, so a
    sample-pass leg without it would pass by having nothing asked of it. cancel_in_stage (SL-CANCEL): with the song
    playing and the byte channel holding the staging read, BACK in the song browser must drop the load at once, leave
    the browser where it was with the current song still in place and playing and no loading animation, and park the
    staging buffer the channel still writes into rather than free it; past the hold nothing of the load comes back, the
    parked buffer is freed, and a cancel with no load in flight answers Stale; the load is started by the real ENTER
    with the startup-song mode LASTOPENED, and the cancelled load must not write the settings file that records the last
    opened song (SL-CANCEL-LASTOPENED). cancel_after_begin (SL-REFUSED): the load that brings the song back, again
    through ENTER with LASTOPENED, is asked to cancel once it is past STAGE, where the outgoing song may already be
    deleted, and must answer Refused and carry on to complete (SL-LOADED) and record the song as last opened
    (SL-LOADED-LASTOPENED). SL-NEXT requires a next song a MIDI NEXT_SONG left held behind a take or a slice to be
    dropped by the manual load, as main loads a NEXT_SONG at once and so never one after the song it was to follow is
    gone.
"""

from scenario_gate import count_field, field


def check(gate, run):
    summary = run.first("SL-SUMMARY ")
    failed = False
    served = count_field(summary, "served")
    if not served:
        gate.fail(
            f"SL-CHANNEL — the load served {field(summary, 'served') or 'no'} staging read(s); "
            "the async arm never ran"
        )
        failed = True
    if not count_field(summary, "channelTicks"):
        gate.fail(
            "SL-TICKED — the staging read never ran through the channel across ticks"
        )
        failed = True
    if not failed:
        gate.say("PASS — the song loaded back, and the channel staged it across ticks")
