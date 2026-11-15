"""The recorder-take gate: an audio-input recording take leaves the app loop running.

The take is started the way a user starts it, from the menu's recorder item, so the gesture must return with the take
running under the recorder screen (RT-OPEN). A take that instead holds the caller until it settles holds the
whole app loop inside one executor poll: on this single-threaded harness neither the virtual clock nor the storage
channels the take writes through can then advance, so the run never ends. That is why this gate runs under a wall-clock
bound, and reports a run that hits it as the freeze rather than as a crash.

Gate recorder-take:
    an audio-input recording take must leave the rest of the app running, and must attach its sample only to the sound
    it was recorded for. Every take is started from the recorder menu item, and the gesture must return with the take
    running under the recorder screen (RT-OPEN). midi_during_take: a DIN script plays MIDI Start, two bars of clock and
    one note on channel 1 during the take; every byte must be read, the note must sound through MIDI follow, the clock
    must start playback, and the recorder screen's own step must handle the clipping the test input causes (RT-TAKE).
    BACK then ends the take, and the sample must be attached as the recorder always has: the source switched to SAMPLE,
    the range's holder holding the recording under SAMPLES/RECORD/ with audio in it (RT-ATTACH). refused_during_load:
    with a new-synth acquisition in flight, which swaps the clip's output when it lands, the recorder must refuse to
    start a take (RT-REFUSED). closed_mid_take: the UI stack is replaced under a running take, as a cleared song does;
    the take must be dropped, the source left as it was and the recording attached to nothing (RT-CLOSED), and a second
    take must then start and attach (RT-SECOND). nullified_mid_take: the UI stack is nullified under a running take with
    nothing opened in its place, as a song load leaves it; the take must be dropped all the same, its recording attached
    to nothing (RT-NULLED). output_replaced_mid_take: the clip's synth is replaced under a running take through the
    new-synth step itself, a test seam for any swap the refusal cannot cover; the old synth hibernates, and must not be
    switched to SAMPLE, nor the recording leased by anything (RT-REPLACED). next_song_mid_take: with the load browser's
    listing left behind as a loaded song leaves it and the song playing, a learned MIDI NEXT_SONG arrives on DIN
    mid-take and is held back with a popup; no song load may start while the take runs (a load takes down the recorder's
    UI and arms a swap that retires the song the take attaches to), BACK must end the take with its sample attached in
    the song it was recorded in, and the held-back NEXT_SONG must then load the next song (RT-NEXT). A take that holds
    the gesture until it settles freezes this single-threaded harness inside one executor poll, so the check runs under
    a 60 s wall-clock bound and reports that as RT-FROZEN.

Gate app-asan-recorder-take:
    the recorder-take gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap
    poisoning: any use-after-free, overflow or undefined behaviour the scenario reaches is fatal (a sound plays its
    record's state, so a record freed under it would read freed memory)
"""

# Wall-clock seconds. A passing run takes a few seconds.
TIMEOUT_S = 60


def run(gate):
    result = gate.run(timeout=TIMEOUT_S)
    if result.code == 124:
        gate.fail(
            f"RT-FROZEN the run did not end within {TIMEOUT_S} s of wall time: the recorder gesture held the app loop "
            "instead of returning with the take running"
        )
        gate.show(result)
        return
    if gate.require_standard(result):
        gate.show(result)
