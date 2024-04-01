File browser for selecting a sample to load in to the currently selected oscillator.

If the oscillator is in <string-for name="STRING_FOR_WAVETABLE">WAVETABLE</string-for> mode, the sample is first tried
as a wave table. See the [wavetable] documentation for an explanation of when this is possible. If wavetable loading
fails, or the oscillator was not in wavetable mode already, the oscillator is placed in to
<string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for> mode.
