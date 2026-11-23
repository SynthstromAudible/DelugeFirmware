Sets the loop crossfade time in milliseconds for the oscillator's sample, so loops that don't join up perfectly still
sound smooth.

When playback gets this close to the loop end, the next pass through the loop starts playing from the loop start
alongside it. Over the crossfade time, the end of the loop fades out while its start fades in (equal-power), then the
next pass carries on alone. The crossfade time is measured in the sample's own time, so it gets shorter as the sample is
played higher, and it is limited to half the loop length.

It needs PITCH/SPEED linked and SPEED at 0. With them unlinked, any note that isn't at the sample's own pitch (other
keys, unison detune, vibrato) is time-stretched, and the loop then jumps without a crossfade.

Range: OFF, then 10 to 1000 ms in 10 ms steps.
