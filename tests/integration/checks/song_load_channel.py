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
"""

from scenario_gate import count_field


def check(gate, run):
    summary = run.first("SL-SUMMARY ")
    failed = False
    served = count_field(summary, "served")
    if not served:
        gate.fail(
            f"SL-CHANNEL — the load served {'no' if served is None else served} staging read(s); the async arm never ran"
        )
        failed = True
    if not count_field(summary, "channelTicks"):
        gate.fail(
            "SL-TICKED — the staging read never ran through the channel across ticks"
        )
        failed = True
    if not failed:
        gate.say("PASS — the song loaded back, and the channel staged it across ticks")
