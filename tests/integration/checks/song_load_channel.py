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

The registry entry requires each marker. This module checks the result line itself, because a failed result does not
stop the channel checks: every one of the three that fails is reported.
"""

import re

SERVED = re.compile(r"^SL-SUMMARY served=([0-9]*) ")
TICKS = re.compile(r"^SL-SUMMARY .*channelTicks=([0-9]*)")


def _captured(run, pattern: re.Pattern) -> str:
    return next((m.group(1) for line in run.lines if (m := pattern.match(line))), "")


def check(gate, run):
    failed = False
    if not run.has_line("SL-RESULT failures=0"):
        gate.fail("\n".join(run.starting("SL-RESULT")))
        failed = True
    served = _captured(run, SERVED)
    if not served or int(served) == 0:
        gate.fail(
            f"SL-CHANNEL — the load served {served or 'no'} staging read(s); the async arm never ran"
        )
        failed = True
    ticks = _captured(run, TICKS)
    if not ticks or int(ticks) == 0:
        gate.fail(
            "SL-TICKED — the staging read never ran through the channel across ticks"
        )
        failed = True
    if not failed:
        gate.say("PASS — the song loaded back, and the channel staged it across ticks")
