Sets the loop crossfade time in milliseconds for the oscillator's sample, so loops that don't join up perfectly still
sound smooth.

When playback gets this close to the loop end, the next pass starts playing alongside it, from the same distance before
the loop start. Over the crossfade time, the end of the loop fades out while the audio leading into the loop start fades
in (equal-power), and the next pass reaches the loop start just as the current one reaches the loop end, then carries on
alone. That way the loop keeps its length, so a tempo-synced loop stays in time. If there isn't enough audio before the
loop start (for example when the loop starts at the very start of the sample), the rest of the crossfade comes out of
the start of the loop, which shortens the loop by that much.

The crossfade time is measured in the sample's own time, so it gets shorter as the sample is played higher, and it is
limited to half the loop length.

It needs PITCH/SPEED linked and SPEED at 0. With them unlinked, any note that isn't at the sample's own pitch (other
keys, unison detune, vibrato) is time-stretched, and the loop then jumps without a crossfade.

Range: OFF, then 10 to 1000 ms in 10 ms steps.
