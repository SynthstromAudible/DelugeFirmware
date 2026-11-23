#! /usr/bin/env python3
"""Generate the card tree `render_song` plays in CI: a small song whose playback streams samples from the card.

Two synths, each playing its own six-second stereo sample through a long note in a looping session clip. Each
sample is about 32 clusters, so playback has to stream it cluster by cluster rather than finding it resident. Both
samples are generated here, deterministically, so nothing binary is committed and every run plays the same bytes.

    make_render_song.py <out-dir> [--song NAME]

writes <out-dir>/SONGS/<NAME>.XML (RS_STREAM by default) and <out-dir>/SAMPLES/RS/{A,B}.WAV.

The other songs give each home of the effects a golden render of its chain at settings away from its defaults: a kit
(RS_KIT_FX), an audio track (RS_AUDIO_FX) and the song's master (RS_MASTER_FX), each beside RS_STREAM's two synths so
sample voices still stream. Each has a dry twin (RS_KIT_DRY, RS_AUDIO_DRY, RS_MASTER_DRY) with every one of those
settings at its default: the gate fails if a song and its twin render the same, which would mean the settings never
reached the render. RS_KIT_FILTERS, RS_AUDIO_FILTERS and RS_MASTER_FILTERS do the same for the filter families and
routes: only the filters are on, each with its own `_DRY` twin. RS_KIT_SVF_MORPH, RS_AUDIO_DRIVE,
RS_KIT_FILTER_FADE and RS_AUDIO_FILTER_FADE cover the filter paths those leave out: the SVF morphs, the oversampled
drive filter, the saturating ladder HPF, the granular mod FX, and each filter fading in when its automation switches
it on. RS_AUDIO_DUCKED, RS_KIT_COMP, RS_KIT_DELAY, RS_AUDIO_DELAY and RS_KIT_PINGPONG cover the dynamics and delay
paths: sidechain hits ducking an audio track, the reverb and a drum; a drum's compressor and a kit's blended one; a
delay's rate stepped through new buffers until it is abandoned; and ping-pong, which its gate renders in stereo.
RS_AUDIO_DELAY_TOGGLE switches an audio track's analog delay off and on again while it rings. RS_AUDIO_DELAY_SWITCH
holds an audio track's analog delay ringing for its gate to switch between analog and digital mid-render; it has no dry
twin, because its gate's twin is the same song rendered unswitched.
RS_PINGPONG_SWEEP crosses ping-pong in stereo while the rate steps through new buffers, digital on a kit and analog
on an audio track: the resampled and analog crossings RS_KIT_PINGPONG leaves out.
RS_AUDIO_DELAY_EXTREMES steps an audio track's delay rate from its minimum to its maximum and back: its tape heads at
the fastest and the slowest the rate's range reaches.
RS_AUDIO_CENTS plays the audio clip 30 cents up beside its dry twin, so an audio clip's fine tune is pinned.
RS_AUDIO_STRETCH plays ten audio clips alone, pitch and speed independent, each at its own ratio to the song's tempo, so
the time stretcher stretches them in step with the song's ticks: the sample goldens' tone (SAMPLES/RS/TONE.WAV) and
percussive source (SAMPLES/RS/PERC.WAV) sped up and slowed within an octave, past one and past two, reversed and
transposed, and sample A within a hair of its native tempo, where it is stretched only when it drifts out of sync. Its
dry twin plays the same clips with pitch and speed tied: nine resample, and the transposed one still stretches, as a
tied clip with a transpose does.
RS_AUDIO_STRETCH_NATIVE plays the tone and the percussive source at their native tempo, where they are not stretched but
a loop end that a render block starts within reach of has a time stretcher fudged in to crossfade into its pre-margin;
its twin plays the same frames from files that hold only them, with no pre-margin to crossfade into.
RS_AUDIO_STRETCH_ARRANGED plays stretched clips from arrangement instances that end without looping; the two tones,
with waveform past the end they play to, fade out before their instances end. Its twin plays them resampled.
RS_AUDIO_STRETCH_EDGES plays clips at the edges of the stretcher's paths and its caller's: a pre-margin too short for
a loop's crossfade, tied clips with a fine tune, a transpose and neither, a reversed tied clip, a 48 kHz file, a mono
file, an end marker past the file's end, two native-tempo loops with pre-margins too short to fudge a crossfade into
whole, a clip whose pitch adjust is raised, and a source over 2^22 frames long, whose percussiveness cache is kept in
clusters: played forwards from 0 into a loop's cache zone further on, and looped across a cache cluster's end forwards
and reversed (STRETCH_EDGE_TRACKS).
Its twin ties pitch and speed on every clip.
RS_LOW_CLIP covers a host chain's saturation below amount 3, which RS_KIT_FX and RS_AUDIO_FX (at 4) leave out: a
kit at 1 and an audio track at 2.
RS_BITCRUSH covers the bitcrush paths the FX songs leave out: a kit's bit depth alone at preset 6, an audio track's
sample-rate reduction switched off and on again, so it restarts, and the master's bit depth and sample-rate reduction
at preset 7. RS_METRONOME is RS_STREAM under its own name, for its gate to switch the metronome on while it plays.
RS_REVERB_DUCK_OVERRIDE and RS_REVERB_DUCK_OVERRIDE_TRACK put the reverb's sidechain on auto, so the reverb ducks as
the owner with the most reverb does: a synth through its sidechain cable, and an audio track through its sidechain
volume.
RS_EQ_BANDS covers the EQ's single bands: a kit's bass alone, and an audio track's treble alone,
switched flat and back.
RS_AUDIO_COMP_ONSET switches an audio track's compressor on mid-render, so it seeds from a sounding frame rather than
from the silence every other compressor golden starts in.
RS_AUDIO_COMP_RESET switches an audio track's compressor off mid-sound, where it resets, and on again, so it renders
after a reset with its level and detector carried over.
RS_AUDIO_COMP_BLEND blends an audio track's compressor half wet on a loud track (SAMPLES/RS/LOUD.WAV, written for it
alone, its EQ boosting), so the dry half carries the loud input into the level the compressor measures, loud enough
that a q31 sum of a block's squares would pass 2^31, which the compressor's 64-bit level sum holds.
RS_DUCK_SHAPES covers the sidechain envelope's paths RS_AUDIO_DUCKED leaves out on the host owners: the instant
attack, the straight and clamped release shapes, and a stronger hit taken mid-attack and mid-release.
RS_MODFX_MONO, RS_MODFX_STEREO_A and RS_MODFX_STEREO_B put every comb mod FX type on a host owner: the warble and the
dimension, which no synth golden plays, in mono, and all six in stereo, whose gates render with headphones plugged in.
RS_MODFX_SOFT_OFF takes a mod FX's selected knob to its minimum and back while the audio plays, which switches the
effect off and on again: a flanger, whose delay line is freed while off and starts again zeroed, and a phaser, whose
allpass memory carries across. RS_MODFX_SWITCH and RS_MODFX_CLEAR hold an audio track's warble and flanger playing
for their gates to switch the type (RENDER_MODFX_SWITCH) and clear the memory (RENDER_MODFX_CLEAR) mid-render; like
RS_AUDIO_DELAY_SWITCH they have no dry twin, because each gate's twin is the same song rendered unswitched.
RS_GRAIN_MONO and RS_GRAIN_STEREO put the granular mod FX on the host owners, in mono on a kit and the master and in
stereo on all three, the kit's pitch randomness at its maximum so its grains start in every pitch class. RS_GRAIN_SWITCH
holds an audio track's chorus playing for its gate to switch it to the grain, clear the grain and switch back
mid-render; it has no dry twin. RS_GRAIN_RESTART runs the grain on a kit whose hits stop long enough for its grain to
decay and release its ring, on an audio track whose depth goes to its minimum and back (which releases the ring and
takes a new one), and on a synth whose one note stops long enough for the sound to skip rendering; the clip's loop at
8 s, and the depth's return, start each again on a new ring, whose grains read slots not yet written since it was
taken; those read 0. RS_GRAIN_OWNERS runs the grain on a synth holding its note, a kit and the master, for its gate to
switch the synth's type away from the grain and back and to clear each owner's grain mid-render; it has no dry twin.
RS_GRAIN_MASTER holds the master's chorus playing for its gate to switch the master to the grain and back mid-render;
it has no dry twin.
RS_REVERB_FREEVERB, RS_REVERB_MUTABLE and RS_REVERB_DIGITAL put each reverb model at knobs away from the boot song's,
Freeverb at half width and Digital panned in stereo. RS_REVERB_SWITCH holds Mutable playing for its gate to switch the
model and turn its knobs mid-render.
RS_MASTER_PAN is RS_MASTER_FX under its own name, for a gate that renders it in stereo, where the master's pan applies.
RS_MASTER_ARRANGER plays RS_MASTER_FX's master in the arranger, its low-pass cutoff and volume ramping over two bars.
RS_MASTER_GRAIN_OFF holds the master's grain in its soft off (its selected knob at its minimum), its other effects as
RS_MASTER_FX's.
"""

import hashlib
import math
import re
import struct
import sys
from pathlib import Path

SONG = "SONGS/RS_STREAM.XML"
RATE = 44100
SAMPLE_SECONDS = 6
# Four bars at the song's 120 BPM (a bar is 384 ticks), about 8 s: each sample plays through once, and the clip
# loops, retriggering both, just inside the default 3,000-block render.
CLIP_TICKS = 1536

# The synth sound each clip plays: the synth goldens' sample-oscillator preset (sample/linear), with the oscillator
# pointed at a long one-shot sample instead of a short loop. A song keeps a synth's parameters in each clip that plays
# it (`soundParams`, below), not in the sound, which is how the firmware itself writes a song.
SOUND = """\
		<sound presetName="{name}" presetFolder="SYNTHS" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
			<osc1 type="sample" loopMode="0" reversed="0" timeStretchEnable="0" timeStretchAmount="0" transpose="0" cents="0" fileName="{file}">
				<zone startSamplePos="0" endSamplePos="{frames}" />
			</osc1>
			<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<lfo1 type="triangle" syncLevel="0" />
			<lfo2 type="triangle" />
			<unison num="1" detune="8" />
			<delay pingPong="1" analog="0" syncLevel="7" />
		</sound>
"""

# One note held for the whole clip, in the format this firmware version writes: position, length, velocity, lift,
# probability 0x14 (always play), no iterance, no fill.
CLIP = """\
		<instrumentClip instrumentPresetName="{name}" instrumentPresetFolder="SYNTHS" isPlaying="1" length="{ticks}" section="0">
			<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
				<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
				<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
				<patchCables>
					<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
				</patchCables>
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
			<noteRows>
				<noteRow y="{note}" noteDataWithSplitProb="0x00000000{ticks:08X}404014000000" />
			</noteRows>
		</instrumentClip>
"""


def chord_values(partials: list[float]) -> list[float]:
    """Six seconds of a slowly beating chord, one value per frame, before any level or noise."""
    values = []
    for i in range(RATE * SAMPLE_SECONDS):
        t = i / RATE
        swell = 0.6 + 0.4 * math.sin(2 * math.pi * 0.5 * t)
        values.append(
            sum(math.sin(2 * math.pi * f * t) / (k + 1) for k, f in enumerate(partials))
            * swell
        )
    return values


def sample_frames(seed: int, partials: list[float], level: float = 0.3) -> bytes:
    """Six seconds of a slowly beating chord at @p level with a little noise, 16-bit stereo, the same bytes on every
    run."""
    state = seed
    out = bytearray()
    for value in chord_values(partials):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        noise = (state / 0x7FFFFFFF - 0.5) * 0.02
        left = int(max(-1.0, min(1.0, level * value + noise)) * 32767)
        right = int(max(-1.0, min(1.0, level * value - noise)) * 32767)
        out += struct.pack("<hh", left, right)
    return bytes(out)


def loud_sample_frames(seed: int, partials: list[float]) -> bytes:
    """sample_frames' chord normalised so its peak, noise included, is 0.99 of full scale."""
    peak = max(abs(value) for value in chord_values(partials))
    return sample_frames(seed, partials, 0.98 / peak)


def wav(pcm: bytes) -> bytes:
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(pcm),
        b"WAVE",
        b"fmt ",
        16,
        1,
        2,
        RATE,
        RATE * 4,
        4,
        16,
        b"data",
        len(pcm),
    )
    return header + pcm


# A host owner's effect settings: its own attributes and tags (`kit`, `audioTrack`, the song itself) and its params
# (a kit clip's `kitParams`, an audio clip's `params`, the song's `songParams`). Every value is away from its default;
# DRY holds the defaults the firmware writes for a new owner. The mod FX's knob selects its depth: a host chain's mod FX
# is off while its selected param is at its minimum. Pan changes nothing here: the harness renders as the device does
# through its speaker, in mono, where pan does not apply.
FX = {
    "attributes": 'modFXCurrentParam="depth" modFXType="chorus" lpfMode="24dB" hpfMode="HPLadder" filterRoute="H2L" clippingAmount="4"',
    "delay": '<delay pingPong="0" analog="1" syncLevel="7" syncType="0" />',
    "compressor": '<audioCompressor attack="83886080" release="83886080" thresh="1073741824" ratio="1073741824" '
    'compHPF="0" compBlend="2147483647" />',
    "params": 'reverbAmount="0x10000000" volume="0x30000000" pan="0x20000000" modFXDepth="0x20000000" '
    'modFXRate="0x10000000" sampleRateReduction="0xC0000000" bitCrush="0xA0000000" compressorThreshold="0x40000000"',
    "param_tags": '<delay rate="0x00000000" feedback="0xC0000000" /><lpf frequency="0x40000000" resonance="0x20000000" />'
    '<hpf frequency="0x80000000" resonance="0x80000000" />'
    '<equalizer bass="0x30000000" treble="0x30000000" bassFrequency="0x10000000" trebleFrequency="0xF0000000" />',
}
DRY = {
    "attributes": 'modFXCurrentParam="feedback" modFXType="none" lpfMode="24dB" hpfMode="HPLadder" filterRoute="H2L"',
    "delay": '<delay pingPong="1" analog="0" syncLevel="7" syncType="0" />',
    "compressor": '<audioCompressor attack="83886080" release="83886080" thresh="0" ratio="1073741824" compHPF="0" '
    'compBlend="2147483647" />',
    "params": 'reverbAmount="0x80000000" volume="0x00000000" pan="0x00000000" modFXDepth="0x00000000" '
    'modFXRate="0xE0000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" compressorThreshold="0x00000000"',
    "param_tags": '<delay rate="0x00000000" feedback="0x80000000" /><lpf frequency="0x7FFFFFFF" resonance="0x80000000" />'
    '<hpf frequency="0x80000000" resonance="0x80000000" />'
    '<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />',
}


# The filter families and routes, one set per host: only the filters are on, every other effect at DRY's value. The
# kit takes a drive LPF and a ladder HPF in L2H; the audio track an SVF notch and an SVF band in PARA; the master a
# 12 dB ladder LPF and an SVF band HPF in H2L. `lpfMorph` (the kit's) is above its minimum so the morph gate opens too.
def _filters(attributes: str, lpf: str, hpf: str, extra_params: str = "") -> dict:
    return {
        **DRY,
        "attributes": 'modFXCurrentParam="depth" modFXType="none" ' + attributes,
        "params": DRY["params"] + extra_params,
        "param_tags": DRY["param_tags"]
        .replace('<lpf frequency="0x7FFFFFFF" resonance="0x80000000" />', lpf)
        .replace('<hpf frequency="0x80000000" resonance="0x80000000" />', hpf),
    }


KIT_FILTERS = _filters(
    'lpfMode="24dBDrive" hpfMode="HPLadder" filterRoute="L2H"',
    '<lpf frequency="0x30000000" resonance="0x40000000" />',
    '<hpf frequency="0xC0000000" resonance="0x30000000" />',
    ' lpfMorph="0x20000000"',
)
AUDIO_FILTERS = _filters(
    'lpfMode="SVF_Notch" hpfMode="SVF_Band" filterRoute="PARA"',
    '<lpf frequency="0x20000000" resonance="0x50000000" />',
    '<hpf frequency="0xB0000000" resonance="0x20000000" />',
)
MASTER_FILTERS = _filters(
    'lpfMode="12dB" hpfMode="SVF_Band" filterRoute="H2L"',
    '<lpf frequency="0x38000000" resonance="0x30000000" />',
    '<hpf frequency="0xA0000000" resonance="0x10000000" />',
)


def _stepped(start: int, value: int, tick: int) -> str:
    """A param automated to hold @p start from the clip's start and step to @p value at @p tick, as a song saves it:
    the current value, then each node's value and position."""
    return f"0x{start:08X}{start:08X}00000000{value:08X}{tick:08X}"


# The rest of the filter paths in the host chains. The kit's SVFs with both morphs up (the band's past its midpoint,
# where it turns from band to high), in L2H.
KIT_SVF_MORPH = _filters(
    'lpfMode="SVF_Band" hpfMode="SVF_Notch" filterRoute="L2H"',
    '<lpf frequency="0x20000000" resonance="0x30000000" />',
    '<hpf frequency="0xC0000000" resonance="0x20000000" />',
    ' lpfMorph="0x60000000" hpfMorph="0x20000000"',
)
# The audio track's drive LPF high and resonant enough to oversample, and its ladder HPF resonant enough to saturate
# but not to anti-alias, with its morph up; and the granular mod FX, whose wet path runs through a one-pole low-pass.
AUDIO_DRIVE = _filters(
    'lpfMode="24dBDrive" hpfMode="HPLadder" filterRoute="L2H"',
    '<lpf frequency="0x60000000" resonance="0x60000000" />',
    '<hpf frequency="0xC0000000" resonance="0x00000000" />',
) | {
    "attributes": 'modFXCurrentParam="depth" modFXType="grainFX" lpfMode="24dBDrive" hpfMode="HPLadder" '
    'filterRoute="L2H"',
    "params": DRY["params"].replace(
        'modFXDepth="0x00000000"', 'modFXDepth="0x20000000"'
    )
    + ' hpfMorph="0x20000000"',
}
# Each filter starts off (the low-pass fully open, the high-pass fully closed) and is switched on by its clip's
# automation, the low-pass at the third beat and the high-pass at the fourth, so each fades in from dry. Neither ran
# before it fades in, so each starts from the cleared state of a new chain; the clip loops at 8 s, and the steps are
# late enough that neither switches on a second time before the render ends. The kit's are a 24 dB ladder and an SVF
# band in PARA, so between the two steps the parallel route runs with one filter on; the audio track's are an SVF
# notch and a ladder HPF in H2L.
KIT_FILTER_FADE = _filters(
    'lpfMode="24dB" hpfMode="SVF_Band" filterRoute="PARA"',
    f'<lpf frequency="{_stepped(0x7FFFFFFF, 0x30000000, 192)}" resonance="0x30000000" />',
    f'<hpf frequency="{_stepped(0x80000000, 0xB0000000, 288)}" resonance="0x20000000" />',
)
AUDIO_FILTER_FADE = _filters(
    'lpfMode="SVF_Notch" hpfMode="HPLadder" filterRoute="H2L"',
    f'<lpf frequency="{_stepped(0x7FFFFFFF, 0x20000000, 192)}" resonance="0x30000000" />',
    f'<hpf frequency="{_stepped(0x80000000, 0xB0000000, 288)}" resonance="0x20000000" />',
)

# A kit of two sample drums; each note row keeps its drum's params, as a song saves a kit row.
KIT = """\
		<kit presetName="RSK" presetFolder="KITS" defaultVelocity="100" {attributes}>
			{delay}
			{compressor}
			<soundSources>
{drums}			</soundSources>
		</kit>
"""
DRUM = """\
				<sound name="{name}" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
					<osc1 type="sample" loopMode="0" reversed="0" timeStretchEnable="0" timeStretchAmount="0" transpose="0" cents="0" fileName="{file}">
						<zone startSamplePos="0" endSamplePos="{frames}" />
					</osc1>
					<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<unison num="1" detune="8" />
				</sound>
"""
KIT_CLIP = """\
		<instrumentClip instrumentPresetName="RSK" instrumentPresetFolder="KITS" isPlaying="1" length="{ticks}" section="0">
			<kitParams {params}>{param_tags}</kitParams>
			<noteRows>
{rows}			</noteRows>
		</instrumentClip>
"""
KIT_ROW = """\
				<noteRow drumIndex="{index}" noteDataWithSplitProb="0x{position:08X}{length:08X}404014000000">
{params}				</noteRow>
"""
AUDIO_TRACK = """\
		<audioTrack name="RSAUD" {attributes}>
			{delay}
			{compressor}
		</audioTrack>
"""
AUDIO_CLIP = """\
		<audioClip trackName="RSAUD" filePath="SAMPLES/RS/A.WAV" startSamplePos="0" endSamplePos="{frames}" isPlaying="1" length="{ticks}" section="0"{clip_attributes}>
			<params {params}>{param_tags}</params>
		</audioClip>
"""


def song_xml(
    voices: list[tuple[str, str, int]],
    *,
    song_settings: str = ">",
    instruments: str = "",
    clips: str = "",
) -> str:
    frames = RATE * SAMPLE_SECONDS
    sounds = "".join(
        SOUND.format(name=name, file=file, frames=frames) for name, file, _ in voices
    )
    synth_clips = "".join(
        CLIP.format(name=name, ticks=CLIP_TICKS, note=note) for name, _, note in voices
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        # The tempo, as the firmware writes it for a new song. Without it the song's clock runs so fast that every
        # note retriggers within a fraction of a second, and only the start of each sample ever plays.
        '<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" '
        f'timerTickFraction="-1342177280" inputTickMagnitude="2"{song_settings}\n'
        f"\t<instruments>\n{sounds}{instruments}\t</instruments>\n"
        f"\t<sessionClips>\n{synth_clips}{clips}\t</sessionClips>\n"
        "</song>\n"
    )


def row_params() -> str:
    """A kit row's own params: the synth clips' `soundParams`, as a song saves a sound drum's."""
    start = CLIP.index("<soundParams")
    end = CLIP.index("</soundParams>") + len("</soundParams>")
    return "\t" * 5 + CLIP[start:end].replace("\n\t\t\t", "\n\t\t\t\t\t") + "\n"


def kit_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    frames = RATE * SAMPLE_SECONDS
    drums = "".join(
        DRUM.format(name=name, file=file, frames=frames)
        for name, file in (("RSD1", "SAMPLES/RS/A.WAV"), ("RSD2", "SAMPLES/RS/B.WAV"))
    )
    rows = "".join(
        KIT_ROW.format(
            index=i,
            position=i * CLIP_TICKS // 2,
            length=CLIP_TICKS // 2,
            params=row_params(),
        )
        for i in range(2)
    )
    return song_xml(
        voices,
        instruments=KIT.format(drums=drums, **fx),
        clips=KIT_CLIP.format(ticks=CLIP_TICKS, rows=rows, **fx),
    )


def audio_clip(fx: dict) -> str:
    """The audio clip, playing the track's sample over the clip, with @p fx's params and any clip attributes."""
    return AUDIO_CLIP.format(
        frames=RATE * SAMPLE_SECONDS,
        ticks=CLIP_TICKS,
        clip_attributes=fx.get("clip_attributes", ""),
        **{key: value for key, value in fx.items() if key != "clip_attributes"},
    )


def audio_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    return song_xml(
        voices,
        instruments=AUDIO_TRACK.format(**fx),
        clips=audio_clip(fx),
    )


# An audio clip tuned up 30 cents, its effects dry: the clip's fine tune reaches the render (its twin plays it untuned).
AUDIO_CENTS = {**DRY, "clip_attributes": ' cents="30"'}


def master_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    settings = (
        f" {fx['attributes']}>\n\t{fx['delay']}\n\t{fx['compressor']}\n"
        f"\t<songParams {fx['params']}>{fx['param_tags']}</songParams>"
    )
    return song_xml(voices, song_settings=settings)


# The master's soft off of its granular mod FX: GRAIN with its selected knob (depth) at its minimum, every other
# effect as FX has it, so the grain's ring is taken up afresh each block while the rest of the chain renders.
MASTER_GRAIN_OFF = {
    **FX,
    "attributes": FX["attributes"].replace('modFXType="chorus"', 'modFXType="grainFX"'),
    "params": FX["params"].replace(
        'modFXDepth="0x20000000"', 'modFXDepth="0x80000000"'
    ),
}


def _ramped(start: int, value: int, tick: int) -> str:
    """A param automated to ramp from @p start at the arrangement's start to @p value at @p tick, as a song saves it:
    the current value, then each node's value and position, the high bit of each position marking it interpolated."""
    return f"0x{start:08X}{start:08X}80000000{value:08X}{tick | 0x80000000:08X}"


# The song's master in the arranger, where its automation plays: FX's settings with its low-pass cutoff and its
# volume ramping over the first two bars, so the master's params interpolate while it renders.
ARRANGER_TICKS = CLIP_TICKS * 4
MASTER_ARRANGER = {
    **FX,
    "params": FX["params"].replace(
        'volume="0x30000000"', f'volume="{_ramped(0x30000000, 0x50000000, 768)}"'
    ),
    "param_tags": FX["param_tags"].replace(
        '<lpf frequency="0x40000000"',
        f'<lpf frequency="{_ramped(0x40000000, 0x10000000, 768)}"',
    ),
}


def arranger_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    """master_song in the arranger: each synth plays its clip from an instance at the start of the arrangement, and
    the song opens in the arranger view, so play starts the arrangement."""
    song = master_song(voices, fx).replace(
        'inputTickMagnitude="2"', 'inputTickMagnitude="2" inArrangementView="1"'
    )
    for index, (name, _, _) in enumerate(voices):
        song = song.replace(
            f'<sound presetName="{name}" ',
            f'<sound presetName="{name}" clipInstances="0x00000000{ARRANGER_TICKS:08X}{index:08X}" ',
        )
    return song


# The dynamics and delay paths the songs above leave out, each beside RS_STREAM's two synths. A drum here is a dict:
# its name and sample, extra `<sound>` attributes and tags, its notes as (tick, length) pairs, and extra attributes
# and patch cables for its row's params.
SIDECHAIN_CABLE = '<patchCable source="compressor" destination="volumePostReverbSend" amount="0x1D800000" />'
# A sidechain's attack and release at their defaults (user values 7 and 28), unsynced.
SIDECHAIN_DEFAULT = (
    '<sidechain attack="327244" release="992" syncLevel="0" syncType="0" />'
)
COMPRESSOR_DEFAULT = (
    '<audioCompressor attack="83886080" release="83886080" thresh="0" ratio="1073741824" compHPF="0" '
    'compBlend="2147483647" />'
)


def _notes(notes: list[tuple[int, int]]) -> str:
    """A note row's notes, in the format this firmware version writes (see CLIP)."""
    return "0x" + "".join(
        f"{tick:08X}{length:08X}404014000000" for tick, length in notes
    )


def _steps(start: int, steps: list[tuple[int, int]]) -> str:
    """A param automated to hold @p start from the clip's start and step to each (value, tick) of @p steps."""
    return f"0x{start:08X}{start:08X}00000000" + "".join(
        f"{value:08X}{tick:08X}" for value, tick in steps
    )


def _drum(
    name: str,
    file: str,
    notes: list[tuple[int, int]],
    attrs: str = "",
    tags: str = "",
    row_params: str = "",
    cables: str = "",
) -> dict:
    """A kit drum playing @p file: its extra `<sound>` attributes @p attrs and tags @p tags, its notes as (tick,
    length) pairs, and extra attributes @p row_params and patch cables @p cables for its row's params."""
    return {
        "name": name,
        "file": file,
        "notes": notes,
        "attrs": attrs,
        "tags": tags,
        "row_params": row_params,
        "cables": cables,
    }


DRUM_WITH = """\
				<sound name="{name}" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none"{attrs}>
					<osc1 type="sample" loopMode="0" reversed="0" timeStretchEnable="0" timeStretchAmount="0" transpose="0" cents="0" fileName="{file}">
						<zone startSamplePos="0" endSamplePos="{frames}" />
					</osc1>
					<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<unison num="1" detune="8" />
{tags}				</sound>
"""
KIT_ROW_NOTES = """\
				<noteRow drumIndex="{index}" noteDataWithSplitProb="{notes}">
{params}				</noteRow>
"""


def _row_params(drum: dict) -> str:
    """A drum row's params: row_params(), with the drum's extra attributes and cables."""
    params = row_params()
    if drum["row_params"]:
        params = params.replace("<soundParams ", f"<soundParams {drum['row_params']} ")
    if drum["cables"]:
        params = params.replace(
            "</patchCables>", f"\t{drum['cables']}\n\t\t\t\t\t\t</patchCables>"
        )
    return params


def kit_parts(fx: dict, drums: list[dict], extra_tags: str = "") -> tuple[str, str]:
    """A kit of @p drums with the host settings @p fx and its own extra tags @p extra_tags: its instrument and its
    clip."""
    frames = RATE * SAMPLE_SECONDS
    sounds = "".join(
        DRUM_WITH.format(
            name=d["name"],
            file=d["file"],
            frames=frames,
            attrs=f" {d['attrs']}" if d["attrs"] else "",
            tags="".join(f"\t\t\t\t\t{tag}\n" for tag in d["tags"].split("\n") if tag),
        )
        for d in drums
    )
    rows = "".join(
        KIT_ROW_NOTES.format(index=i, notes=_notes(d["notes"]), params=_row_params(d))
        for i, d in enumerate(drums)
    )
    kit = KIT.format(drums=sounds, **fx)
    if extra_tags:
        kit = kit.replace(
            "\t\t\t<soundSources>", f"\t\t\t{extra_tags}\n\t\t\t<soundSources>"
        )
    return kit, KIT_CLIP.format(ticks=CLIP_TICKS, rows=rows, **fx)


def audio_parts(fx: dict, extra_tags: str = "") -> tuple[str, str]:
    """An audio track with the host settings @p fx playing sample A: its instrument and its clip."""
    track = AUDIO_TRACK.format(**fx)
    if extra_tags:
        track = track.replace(
            "\t\t</audioTrack>", f"\t\t\t{extra_tags}\n\t\t</audioTrack>"
        )
    return track, audio_clip(fx)


# An owner's own stutter settings, as its element saves them when it does not use the song's: free (unquantized) and
# forward on the audio track; quantized and reversed on the kit; ping-pong on the master. The gates begin and end each
# stutter mid-render (RENDER_STUTTER), as the gold knob's press and release do.
STUTTER_FREE = '<stutter quantized="0" reverse="0" pingPong="0" useSongStutter="0" />'
STUTTER_QUANT_REV = (
    '<stutter quantized="1" reverse="1" pingPong="0" useSongStutter="0" />'
)
STUTTER_PINGPONG = '<stutter quantized="0" reverse="0" pingPong="1" />'


def parts_song(
    voices: list[tuple[str, str, int]],
    parts: list[tuple[str, str]],
    song_settings: str = ">",
) -> str:
    return song_xml(
        voices,
        song_settings=song_settings,
        instruments="".join(p[0] for p in parts),
        clips="".join(p[1] for p in parts),
    )


def _with_attributes(base: str, over: str) -> str:
    """The attributes @p base with each of @p over in place of its own, or after them when @p base has none."""
    pairs = dict(re.findall(r'(\w+)="([^"]*)"', base))
    pairs.update(re.findall(r'(\w+)="([^"]*)"', over))
    return " ".join(f'{key}="{value}"' for key, value in pairs.items())


def _host(
    attributes: str = "",
    delay: str = DRY["delay"],
    params: str = "",
    param_tags: str = "",
    **rest,
) -> dict:
    """Host settings at DRY's values but for those given."""
    fx = {**DRY, **rest, "delay": delay}
    if attributes:
        fx["attributes"] = attributes
    if params:
        fx["params"] = _with_attributes(DRY["params"], params)
    if param_tags:
        fx["param_tags"] = param_tags
    return fx


# Sidechain hits ducking an audio track and the song's reverb. Drum RSD1 sends a full-strength hit at the start of
# each of the first two bars and RSD2 a quarter-strength one 6 ticks later, inside the duck, so the host's and the
# reverb's envelopes ignore it. The audio track's sidechain is synced, so its rates follow the tempo; the reverb's is
# set by hand (its volume above zero) rather than following the sound with the most reverb. RSD3 is ducked by the same
# hits through its own sidechain → volume cable, with an attack slow enough to still be under way when it starts:
# each of its notes wakes it from skipping rendering, so it takes up the last hit where that hit's envelope has got to
# by then -- in its attack in the first bar, its release in the second, and over in the fourth.
SC_DRUMS = [
    _drum(
        "RSD1",
        "SAMPLES/RS/A.WAV",
        [(0, 24), (384, 24)],
        attrs='sideChainSend="2147483647"',
    ),
    _drum(
        "RSD2",
        "SAMPLES/RS/B.WAV",
        [(6, 24), (390, 24)],
        attrs='sideChainSend="536870912"',
    ),
    _drum(
        "RSD3",
        "SAMPLES/RS/B.WAV",
        [(24, 24), (584, 24), (1200, 24)],
        tags='<sidechain attack="256" release="64" syncLevel="0" syncType="0" />',
        cables=SIDECHAIN_CABLE,
    ),
]
SC_DRUMS_DRY = [{**d, "attrs": "", "tags": "", "cables": ""} for d in SC_DRUMS]
DUCKED_AUDIO = _host(
    params='sidechainCompressorVolume="0x40000000" reverbAmount="0x40000000"'
)
DUCKED_AUDIO_DRY = _host(params='reverbAmount="0x40000000"')
# The reverb's sidechain at full volume, unsynced.
DUCKED_REVERB = (
    '>\n\t<reverb roomSize="1288490240" dampening="1546188288" width="2147483647" pan="0">\n'
    '\t\t<compressor attack="327244" release="992" volume="1073741800" shape="-601295438" syncLevel="0" />\n'
    "\t</reverb>"
)
DUCKED_REVERB_DRY = DUCKED_REVERB.replace('volume="1073741800"', 'volume="0"')
# file 7 at the song's input tick magnitude 2: the sixteenth-note level, whose attack rate is shifted right
AUDIO_SIDECHAIN_SYNCED = (
    '<sidechain attack="327244" release="992" syncLevel="7" syncType="0" />'
)


def ducked_song(voices: list[tuple[str, str, int]], dry: bool) -> str:
    return parts_song(
        voices,
        [
            kit_parts(DRY, SC_DRUMS_DRY if dry else SC_DRUMS),
            audio_parts(
                DUCKED_AUDIO_DRY if dry else DUCKED_AUDIO, AUDIO_SIDECHAIN_SYNCED
            ),
        ],
        DUCKED_REVERB_DRY if dry else DUCKED_REVERB,
    )


# The reverb's sidechain on auto (its volume below zero, user value -1): the reverb ducks as the owner with more reverb
# than the song does. In RS_REVERB_DUCK_OVERRIDE that owner is a synth holding one note, its reverb amount above the
# audio track's and its sidechain patched to its post-reverb volume; in RS_REVERB_DUCK_OVERRIDE_TRACK it is the audio
# track, through its sidechain volume, with no synth beside it. An owner's reverb amount counts only while it is not
# automated, so each song holds its owner throughout. RSD1's full-strength hits fall in every bar. Each twin sets the
# reverb's sidechain by hand to nothing (volume 0), so it differs only by the override's duck.
OVERRIDE_REVERB = DUCKED_REVERB.replace('volume="1073741800"', 'volume="-21474836"')
OVERRIDE_DRUMS = [
    {**SC_DRUMS[0], "notes": [(0, 24), (384, 24), (768, 24), (1152, 24)]},
    *SC_DRUMS[1:],
]
OVERRIDE_AUDIO = _host(
    params='sidechainCompressorVolume="0x40000000" reverbAmount="0x40000000"'
)
OVERRIDE_AUDIO_LEADS = _host(
    params='sidechainCompressorVolume="0x40000000" reverbAmount="0x70000000"'
)


def override_synth_parts() -> tuple[str, str]:
    """A synth holding one note with more reverb than OVERRIDE_AUDIO, its sidechain patched to its post-reverb volume:
    its instrument and its clip."""
    sound = SOUND.format(
        name="RSR", file="SAMPLES/RS/B.WAV", frames=RATE * SAMPLE_SECONDS
    )
    clip = (
        CLIP.replace('reverbAmount="0x80000000"', 'reverbAmount="0x60000000"')
        .replace("</patchCables>", f"\t{SIDECHAIN_CABLE}\n\t\t\t\t</patchCables>")
        .format(name="RSR", ticks=CLIP_TICKS, note=67)
    )
    return sound, clip


def reverb_override_song(
    voices: list[tuple[str, str, int]], track_leads: bool, dry: bool
) -> str:
    parts = [kit_parts(DRY, OVERRIDE_DRUMS)]
    if track_leads:
        parts.append(audio_parts(OVERRIDE_AUDIO_LEADS))
    else:
        parts += [audio_parts(OVERRIDE_AUDIO), override_synth_parts()]
    return parts_song(voices, parts, DUCKED_REVERB_DRY if dry else OVERRIDE_REVERB)


# The song's reverb at settings of its own: the model as a file saves it (0 Freeverb, 1 Mutable, 2 Digital), room,
# damping, width, high-pass, low-pass and pan as q31 fractions, and its sidechain set by hand to nothing (volume 0),
# so no sound's sidechain moves it. Each song's _DRY twin keeps the model and the sidechain and takes the boot song's
# knobs (Song::Song: room 30/50, damping 36/50, width 1, high-pass 0, low-pass 1, pan 0): the gate fails if the two
# render alike, which would mean the knobs never reached the render. The audio track's volume is turned down so every
# render peaks well below full scale: a clipped sample would hide a change in the reverb's output.
REVERB_SEND = _host(params='reverbAmount="0x40000000" volume="0xC0000000"')
REVERB_DEFAULT_KNOBS = 'roomSize="1288490189" dampening="1546188227" width="2147483647" hpf="0" lpf="2147483647" pan="0"'
REVERB_TURNED_KNOBS = 'roomSize="1932735283" dampening="429496730" width="1073741824" hpf="1288490189" lpf="858993459"'


def _reverb(model: int, knobs: str, pan: int = 0) -> str:
    """The song element closing the <song> tag and holding the reverb: @p model, @p knobs, @p pan."""
    pan_attribute = "" if 'pan="' in knobs else f' pan="{pan}"'
    return (
        f'>\n\t<reverb model="{model}" {knobs}{pan_attribute}>\n'
        '\t\t<compressor attack="327244" release="992" volume="0" shape="-601295438" syncLevel="7" />\n'
        "\t</reverb>"
    )


# The sidechain envelope's paths RS_AUDIO_DUCKED leaves out, each on a host owner. Drum RSD2 sends a quarter-strength
# hit at the start of each of the first two bars and RSD1 a full-strength one inside its duck, 6 ticks later in the
# first bar and 24 in the second, so every owner takes the stronger hit mid-duck. The audio track's attack is at its
# minimum, so each hit skips the attack and the second lands mid-release. The kit's own sidechain has its shape param
# at its maximum, where the release's curve is clamped, and its default attack, so the second hit lands mid-release.
# The reverb's has its shape param at its minimum, a straight release, and an attack slow enough that the second hit
# lands mid-attack; the audio track sends to it, so its duck is heard.
DUCK_SHAPES_DRUMS = [
    _drum(
        "RSD1",
        "SAMPLES/RS/A.WAV",
        [(6, 24), (408, 24)],
        attrs='sideChainSend="2147483647"',
    ),
    _drum(
        "RSD2",
        "SAMPLES/RS/B.WAV",
        [(0, 24), (384, 24)],
        attrs='sideChainSend="536870912"',
    ),
]
DUCK_SHAPES_DRUMS_DRY = [{**d, "attrs": ""} for d in DUCK_SHAPES_DRUMS]
DUCK_SHAPES_KIT = _host(
    params='sidechainCompressorVolume="0x40000000" sidechainCompressorShape="0x7FFFFFFF"'
)
# The attack param's minimum: attackRateTable[0] << 2.
SIDECHAIN_INSTANT = (
    '<sidechain attack="1048576" release="992" syncLevel="0" syncType="0" />'
)
DUCK_SHAPES_AUDIO = _host(
    params='sidechainCompressorVolume="0x40000000" reverbAmount="0x40000000"'
)
DUCK_SHAPES_REVERB = (
    '>\n\t<reverb roomSize="1288490240" dampening="1546188288" width="2147483647" pan="0">\n'
    '\t\t<compressor attack="256" release="992" volume="1073741800" shape="-2147483648" syncLevel="0" />\n'
    "\t</reverb>"
)
DUCK_SHAPES_REVERB_DRY = DUCK_SHAPES_REVERB.replace('volume="1073741800"', 'volume="0"')


def duck_shapes_song(voices: list[tuple[str, str, int]], dry: bool) -> str:
    return parts_song(
        voices,
        [
            kit_parts(DRY, DUCK_SHAPES_DRUMS_DRY)
            if dry
            else kit_parts(DUCK_SHAPES_KIT, DUCK_SHAPES_DRUMS, SIDECHAIN_DEFAULT),
            audio_parts(DUCKED_AUDIO_DRY)
            if dry
            else audio_parts(DUCK_SHAPES_AUDIO, SIDECHAIN_INSTANT),
        ],
        DUCK_SHAPES_REVERB_DRY if dry else DUCK_SHAPES_REVERB,
    )


# A kit drum's own compressor, and the kit's compressor blended half wet with its sidechain high-pass up.
COMP_DRUMS = [
    _drum(
        "RSD1",
        "SAMPLES/RS/A.WAV",
        [(0, CLIP_TICKS // 2)],
        tags=COMPRESSOR_DEFAULT,
        row_params='compressorThreshold="0x40000000"',
    ),
    _drum(
        "RSD2",
        "SAMPLES/RS/B.WAV",
        [(CLIP_TICKS // 2, CLIP_TICKS // 2)],
        tags=COMPRESSOR_DEFAULT,
    ),
]
COMP_DRUMS_DRY = [{**d, "row_params": ""} for d in COMP_DRUMS]
KIT_COMP = _host(
    compressor='<audioCompressor attack="83886080" release="83886080" thresh="0" ratio="1073741824" '
    'compHPF="1073741824" compBlend="1073741824" />',
    params='compressorThreshold="0x40000000"',
)

# A digital delay whose rate the clip's automation steps up past double its buffer's native rate at the second bar,
# down a little at the third (where it resamples until the rate has settled), and below half the native rate at the
# fourth, each growing a new buffer that is swapped in once written. Unsynced, so the rates are the param's own.
DELAY_RATE_STEPS = _steps(
    0x00000000, [(0x30000000, 384), (0x28000000, 768), (0xF0000000, 1056)]
)
DIGITAL_DELAY = '<delay pingPong="0" analog="0" syncLevel="0" syncType="0" />'
# The kit's in ping-pong (heard as plain feedback in mono, where ping-pong does not cross channels), with feedback low
# enough to repeat three times. Its drum plays through the first three bars and a beat of the fourth, across the
# rate's steps, and then stops: the rest of the clip is silence long enough for the delay's repeats to run out and its
# buffers to be abandoned.
KIT_DELAY = _host(
    delay='<delay pingPong="1" analog="0" syncLevel="0" syncType="0" />',
    param_tags=DRY["param_tags"].replace(
        '<delay rate="0x00000000" feedback="0x80000000" />',
        f'<delay rate="{DELAY_RATE_STEPS}" feedback="0x90000000" />',
    ),
)
DELAY_DRUMS = [_drum("RSD1", "SAMPLES/RS/A.WAV", [(0, 1080)])]
# The audio track's digital delay, with the same rate steps and feedback. An audio clip renders until it loops, so
# this delay never runs out.
AUDIO_DELAY = _host(
    delay=DIGITAL_DELAY,
    param_tags=KIT_DELAY["param_tags"],
)
# The kit's delay in ping-pong, for a gate that renders in stereo, where ping-pong crosses channels.
KIT_PINGPONG = _host(
    delay='<delay pingPong="1" analog="0" syncLevel="7" syncType="0" />',
    param_tags=DRY["param_tags"].replace(
        '<delay rate="0x00000000" feedback="0x80000000" />',
        '<delay rate="0x00000000" feedback="0x00000000" />',
    ),
)
PINGPONG_DRUMS = [
    _drum("RSD1", "SAMPLES/RS/A.WAV", [(0, 48), (768, 48)]),
    _drum("RSD2", "SAMPLES/RS/B.WAV", [(384, 48), (1152, 48)]),
]
# An audio track's analog delay (its tanh at a host chain's drive) switched off and on again while it rings: the
# clip's automation drops the feedback to its minimum at the second bar, which abandons the delay's buffers at once,
# and raises it again a beat later, while the audio still plays, so the delay starts again on new buffers. The
# processor keeps the impulse response's history from the last block before the delay went off, so the new delay's
# first samples carry the old response's tail. Its twin holds the feedback the whole way through.
ANALOG_DELAY = '<delay pingPong="0" analog="1" syncLevel="0" syncType="0" />'
AUDIO_DELAY_TOGGLE = _host(
    delay=ANALOG_DELAY,
    param_tags=DRY["param_tags"].replace(
        '<delay rate="0x00000000" feedback="0x80000000" />',
        '<delay rate="0x00000000" feedback="'
        + _steps(0x00000000, [(0x80000000, 384), (0x00000000, 480)])
        + '" />',
    ),
)
AUDIO_DELAY_TOGGLE_DRY = _host(
    delay=ANALOG_DELAY,
    param_tags=DRY["param_tags"].replace(
        '<delay rate="0x00000000" feedback="0x80000000" />',
        '<delay rate="0x00000000" feedback="0x00000000" />',
    ),
)
# An audio track's analog delay with its feedback held, ringing the whole way through: the song its gate switches
# between analog and digital mid-render (RENDER_DELAY_SWITCH), the same delay each time.
AUDIO_DELAY_SWITCH = AUDIO_DELAY_TOGGLE_DRY
# Ping-pong crossing channels while the rate steps, for a gate that renders in stereo: the kit's digital ping-pong
# delay of RS_KIT_DELAY, its rate stepped past double, a little down and below half, its drum stopping so the delay is
# abandoned, beside an audio track's analog ping-pong delay with the same rate steps and feedback. Each crosses
# channels at its native rate, resampling below and above it, and on a new delay's first blocks, while its primary
# is silent and its first buffer grows; the audio track's in the analog shaper, the kit's in the digital one.
AUDIO_PINGPONG_SWEEP = _host(
    delay='<delay pingPong="1" analog="1" syncLevel="0" syncType="0" />',
    param_tags=KIT_DELAY["param_tags"],
)
# The rate's range end to end, on an audio track's digital delay: the rate held at its minimum (65536, 1/256 of the
# neutral rate 16777216), which sizes the delay's buffer at its largest, two seconds, native at 3116530, stepped at the
# third beat of the second bar to its maximum (the param saturating at 2147483647), where that buffer resamples 689
# times faster than its native rate: past the 8.24 spin's 256, so it wraps to about 177 steps per sample, until a new
# buffer of 128 samples, grown at the new rate, is swapped in; and stepped back to its minimum at the third bar, where
# that small buffer crawls at the slowest a tape resamples (2^-15 of its native rate) while a new two-second buffer
# grows. The feedback holds high, so the delay rings throughout.
AUDIO_DELAY_EXTREMES = _host(
    delay=DIGITAL_DELAY,
    param_tags=DRY["param_tags"].replace(
        '<delay rate="0x00000000" feedback="0x80000000" />',
        f'<delay rate="{_steps(0x80000000, [(0x7FFFFFFF, 480), (0x80000000, 768)])}" feedback="0x90000000" />',
    ),
)
# A host chain's saturation below amount 3, where it does not shift back up: the kit at 1 and the audio track at 2,
# every other setting at DRY's value. RS_KIT_FX and RS_AUDIO_FX render it from 3 up.
KIT_LOW_CLIP = _host(
    attributes=_with_attributes(DRY["attributes"], 'clippingAmount="1"')
)
AUDIO_LOW_CLIP = _host(
    attributes=_with_attributes(DRY["attributes"], 'clippingAmount="2"')
)

# The bitcrush paths the FX songs leave out (they run preset 1 with the sample-rate reduction on): the kit's bit depth
# alone at preset 6, its mask and a volume shift of 2; the audio track's sample-rate reduction, at a rate whose grab
# position carries a fraction from frame to frame, stepped off at the second bar and on again at the third, so it
# restarts from a zeroed position; the master with both at preset 7, a volume shift of 3 (a master's
# automation plays only in the arranger, so its settings hold).
KIT_BITCRUSH = _host(params='bitCrush="0x40000000"')
AUDIO_SRR_TOGGLE = _host(
    params=f'sampleRateReduction="{_steps(0xC7654321, [(0x80000000, 384), (0xC7654321, 768)])}"',
)
MASTER_BITCRUSH = _host(params='bitCrush="0x7FFFFFFF" sampleRateReduction="0xE0000000"')

# The EQ paths the FX songs leave out (they run both bands): the kit's bass alone, and the audio track's treble alone,
# stepped flat at the second bar and back at the third, so it switches off and on while the bass stays flat.
KIT_EQ_BASS = _host(
    param_tags=DRY["param_tags"].replace(
        '<equalizer bass="0x00000000"', '<equalizer bass="0x50000000"'
    )
)
AUDIO_EQ_TREBLE = _host(
    param_tags=DRY["param_tags"].replace(
        'treble="0x00000000" bassFrequency',
        f'treble="{_steps(0xB0000000, [(0x00000000, 384), (0xB0000000, 768)])}" bassFrequency',
    )
)

# An audio track's compressor switched on mid-render: its threshold, held at its minimum (the compressor off and reset
# every block), steps up by the clip's automation at the second bar while the audio plays, so the compressor's first
# block seeds its saturators from a sounding frame instead of silence. Its other settings are RS_AUDIO_FX's.
AUDIO_COMP_ONSET = _host(
    compressor=FX["compressor"],
    params=f'compressorThreshold="{_stepped(0x00000000, 0x40000000, 384)}"',
)

# An audio track's compressor reset mid-sound: its threshold, above the minimum from the clip's start, drops to the
# minimum at the second bar (the compressor off and reset every block, its detector's level and filters carrying) and
# steps back up at the third while the audio plays, so the compressor renders again after a reset with no seed
# pending from silence. Its compressor's other settings are RS_AUDIO_FX's. Its twin holds the threshold at the minimum.
AUDIO_COMP_RESET = _host(
    compressor=FX["compressor"],
    params=f'compressorThreshold="{_steps(0x40000000, [(0x00000000, 384), (0x40000000, 768)])}"',
)

# An audio track's compressor blended half wet on a loud track: the clip plays LOUD.WAV (sample A's chord normalised
# near full scale) at the clip's full volume with the EQ's bass and treble at their maximum boost, so the dry half of
# the blend carries the loud input back into the block the compressor measures, loud enough that a q31 sum of a
# block's squares would pass 2^31, which the 64-bit level sum holds (the clip's volume alone is not that loud). Its
# threshold is above the minimum and every other host setting at DRY's. Its twin holds the threshold at the minimum,
# where the compressor is off.
LOUD_SAMPLE = "SAMPLES/RS/LOUD.WAV"
COMP_BLEND = (
    '<audioCompressor attack="83886080" release="83886080" thresh="1073741824" ratio="1073741824" compHPF="0" '
    'compBlend="1073741824" />'
)
LOUD_EQ = DRY["param_tags"].replace(
    '<equalizer bass="0x00000000" treble="0x00000000"',
    '<equalizer bass="0x7FFFFFFF" treble="0x7FFFFFFF"',
)
AUDIO_COMP_BLEND = _host(
    compressor=COMP_BLEND,
    params='compressorThreshold="0x40000000" volume="0x7FFFFFFF"',
    param_tags=LOUD_EQ,
)
AUDIO_COMP_BLEND_DRY = _host(
    compressor=COMP_BLEND,
    params='compressorThreshold="0x00000000" volume="0x7FFFFFFF"',
    param_tags=LOUD_EQ,
)


# A mod FX on each host owner, every other setting at DRY's value: the type, its knob selecting the depth, and its
# rate, depth, feedback and offset away from their defaults.
def _mod_fx(mod_fx_type: str, params: str = "") -> dict:
    return _host(
        attributes=_with_attributes(
            DRY["attributes"],
            f'modFXCurrentParam="depth" modFXType="{mod_fx_type}"',
        ),
        params='modFXDepth="0x20000000" modFXRate="0x10000000" modFXFeedback="0x40000000" '
        'modFXOffset="0x20000000"' + (f" {params}" if params else ""),
    )


def master_settings(fx: dict) -> str:
    """The song's own settings for its master chain @p fx, as master_song writes them."""
    return (
        f" {fx['attributes']}>\n\t{fx['delay']}\n\t{fx['compressor']}\n"
        f"\t<songParams {fx['params']}>{fx['param_tags']}</songParams>"
    )


def mod_fx_song(
    voices: list[tuple[str, str, int]], kit: dict, audio: dict, master: dict
) -> str:
    """A kit, an audio track and the song's master with the host settings @p kit, @p audio and @p master."""
    return parts_song(
        voices,
        [kit_parts(kit, PINGPONG_DRUMS), audio_parts(audio)],
        song_settings=master_settings(master),
    )


# The mod FX's knob, its depth, held above its minimum, taken to it at the second bar, which switches the effect off,
# and back at the third while the audio plays, which switches it on again.
MOD_FX_SOFT_OFF_DEPTH = (
    f'modFXDepth="{_steps(0x20000000, [(0x80000000, 384), (0x20000000, 768)])}"'
)


# Each owner's volume below its default in the songs whose mod FX would otherwise take the output to full scale, so
# the peaks stay well below it: where clipping would hide a change to the loudest samples, a change shows. Their dry
# twins take the same volume, so only the mod FX differ.
MOD_FX_STEREO_A_VOLUME = 'volume="0xE0000000"'
MOD_FX_SOFT_OFF_VOLUME = 'volume="0xC0000000"'
MOD_FX_STEREO_B_VOLUME = 'volume="0xE0000000"'
MOD_FX_CLEAR_VOLUME = 'volume="0xC0000000"'
GRAIN_MONO_VOLUME = 'volume="0xE0000000"'
GRAIN_STEREO_VOLUME = 'volume="0xE0000000"'
GRAIN_SWITCH_VOLUME = 'volume="0xE0000000"'
GRAIN_RESTART_VOLUME = 'volume="0xE0000000"'
GRAIN_OWNERS_VOLUME = 'volume="0xE0000000"'
GRAIN_MASTER_VOLUME = 'volume="0xE0000000"'


# The granular mod FX on a host owner: _mod_fx's knobs, which for the grain are its rate, mix (the depth), density (the
# offset) and pitch randomness (the feedback), with the pitch randomness at its maximum, so grains start in every pitch
# class.
def _grain(params: str = "") -> dict:
    return _mod_fx(
        "grainFX", 'modFXFeedback="0x7FFFFFFF"' + (f" {params}" if params else "")
    )


# A synth whose sound runs the granular mod FX, playing one short note of sample B at the clip's start, so the sound
# falls silent long before the clip loops at 8 s and plays it again.
GRAIN_SOUND = SOUND.replace('modFXType="none"', 'modFXType="grainFX"')
GRAIN_CLIP = CLIP.replace(
    'noteDataWithSplitProb="0x00000000{ticks:08X}404014000000"',
    'noteDataWithSplitProb="0x0000000000000030404014000000"',
).replace(
    'modFXOffset="0x00000000" modFXFeedback="0x00000000"',
    'modFXOffset="0x20000000" modFXFeedback="0x7FFFFFFF"',
)


def grain_synth_parts() -> tuple[str, str]:
    """A synth with the granular mod FX playing one short note: its instrument and its clip."""
    return (
        GRAIN_SOUND.format(
            name="RSG", file="SAMPLES/RS/B.WAV", frames=RATE * SAMPLE_SECONDS
        ),
        GRAIN_CLIP.format(name="RSG", ticks=CLIP_TICKS, note=67),
    )


# grain_synth_parts' synth holding its note for the whole clip, so its sound renders throughout.
GRAIN_HELD_CLIP = CLIP.replace(
    'modFXOffset="0x00000000" modFXFeedback="0x00000000"',
    'modFXOffset="0x20000000" modFXFeedback="0x7FFFFFFF"',
)


def grain_synth_held_parts() -> tuple[str, str]:
    """A synth with the granular mod FX holding one note: its instrument and its clip."""
    return (
        GRAIN_SOUND.format(
            name="RSG", file="SAMPLES/RS/B.WAV", frames=RATE * SAMPLE_SECONDS
        ),
        GRAIN_HELD_CLIP.format(name="RSG", ticks=CLIP_TICKS, note=67),
    )


def grain_synth_dry_parts() -> tuple[str, str]:
    """grain_synth_parts with the mod FX off."""
    sound, clip = grain_synth_parts()
    return sound.replace('modFXType="grainFX"', 'modFXType="none"'), clip


# One short hit of each drum at the clip's start, so the kit falls silent and its grain decays long before the clip
# loops at 8 s and plays them again.
GRAIN_RESTART_DRUMS = [
    _drum("RSD1", "SAMPLES/RS/A.WAV", [(0, 48)]),
    _drum("RSD2", "SAMPLES/RS/B.WAV", [(0, 48)]),
]


# A delay on each owner the delay gates' kits and audio tracks leave out: a synth's sound, a kit drum's own sound and
# the song's master. The synth holds one note for RS_KIT_DELAY's drum's 1080 ticks with its digital delay's rate
# stepped as that kit's is, so its delay starts, grows and swaps in new buffers while it plays, and abandons them
# before the clip loops; the kit drum's analog delay plays the same way, its kit's own delay off. The master's digital
# delay starts on the mix; a master's automation plays only in the arranger, so its rate holds. The dry twin holds
# every feedback at its minimum.
OWNERS_DELAY = '<delay pingPong="0" analog="0" syncLevel="0" syncType="0" />'
OWNERS_DRUM_DELAY = '<delay pingPong="0" analog="1" syncLevel="0" syncType="0" />'
OWNERS_DRUMS = [
    _drum("RSD1", "SAMPLES/RS/A.WAV", [(0, 1080)], tags=OWNERS_DRUM_DELAY),
]


def delay_owners_parts(dry: bool) -> list[tuple[str, str]]:
    """A synth with a delay holding one note, and a kit whose drum has its own delay: their instruments and clips;
    with their feedback at its minimum when @p dry."""
    feedback = "0x80000000" if dry else "0x90000000"
    synth = SOUND.replace(
        '<delay pingPong="1" analog="0" syncLevel="7" />', OWNERS_DELAY
    ).format(name="RSY", file="SAMPLES/RS/B.WAV", frames=RATE * SAMPLE_SECONDS)
    clip = (
        CLIP.replace(
            'noteDataWithSplitProb="0x00000000{ticks:08X}404014000000"',
            'noteDataWithSplitProb="0x0000000000000438404014000000"',
        )
        .replace(
            'delayRate="0x00000000" delayFeedback="0x80000000"',
            f'delayRate="{DELAY_RATE_STEPS}" delayFeedback="{feedback}"',
        )
        .format(name="RSY", ticks=CLIP_TICKS, note=67)
    )
    kit, kit_clip = kit_parts(DRY, OWNERS_DRUMS)
    kit_clip = kit_clip.replace(
        'delayRate="0x00000000" delayFeedback="0x80000000"',
        f'delayRate="{DELAY_RATE_STEPS}" delayFeedback="{feedback}"',
    )
    return [(synth, clip), (kit, kit_clip)]


def delay_owners_master(dry: bool) -> dict:
    """The master's digital delay; its feedback at its minimum when @p dry."""
    if dry:
        return _host(delay=OWNERS_DELAY)
    return _host(delay=OWNERS_DELAY, param_tags=KIT_DELAY["param_tags"])


# Five kit drums, each with its own digital delay synced to a half note and its feedback held, all ringing when the
# gate drops the tempo mid-render (RENDER_TEMPO): every delay's tape then needs a new, longer size, and every one
# grows its new tape from the same block. The delays' arena must have room for all of them at once.
TEMPO_DELAY = '<delay pingPong="0" analog="0" syncLevel="3" syncType="0" />'
TEMPO_DRUMS = [
    _drum(
        f"RST{n}",
        "SAMPLES/RS/A.WAV" if n % 2 else "SAMPLES/RS/B.WAV",
        [(0, 1500)],
        tags=TEMPO_DELAY,
    )
    for n in range(5)
]


def tempo_delays_parts() -> list[tuple[str, str]]:
    """A kit of TEMPO_DRUMS, each drum's delay feedback held up: its instrument and its clip."""
    kit, clip = kit_parts(DRY, TEMPO_DRUMS)
    return [
        (kit, clip.replace('delayFeedback="0x80000000"', 'delayFeedback="0xA0000000"'))
    ]


def loud_audio_parts(fx: dict) -> tuple[str, str]:
    """audio_parts, its clip playing LOUD_SAMPLE."""
    track, clip = audio_parts(fx)
    return track, clip.replace("SAMPLES/RS/A.WAV", LOUD_SAMPLE)


# Audio clips stretched to the song's tempo: pitch and speed independent, each clip's length in ticks set against its
# sample's length, so a tempo away from a clip's native one makes the time stretcher hop through it in step with the
# song's ticks. Their sources are the sample goldens' tone and percussive sources (presets/make_sample.py), read from
# their committed files; each file's sha256 is checked here, so a changed source fails generation rather than reaching
# a card cached under this generator's unchanged key.
STRETCH_SOURCES = {
    "SAMPLES/RS/TONE.WAV": (
        "_tone.wav",
        "e39bf9988e513fe346ca81eeb68e1a8adee200cf1ab4f480cead30e073e895b4",
    ),
    "SAMPLES/RS/PERC.WAV": (
        "_perc.wav",
        "7750c096031b6f669c2df4924a99513680bb175304b60bd3f98415bf8b16969b",
    ),
    "SAMPLES/RS/TONE_MONO.WAV": (
        "_tone_mono.wav",
        "6dccb81a751e1a3a208599a183408d68da498c0ee5a265222e4d54a6e2ab7210",
    ),
}
# A source's frames from a start marker on, in a file of their own: the native song's twin plays them, so its loops
# have no pre-margin. Each is its card path: (source card path, first frame kept).
TRIMMED_SOURCES = {
    "SAMPLES/RS/TONE_TRIM.WAV": ("SAMPLES/RS/TONE.WAV", 596),
    "SAMPLES/RS/PERC_TRIM.WAV": ("SAMPLES/RS/PERC.WAV", 1018),
}
# A track: its name, sample, start and end markers, clip length in ticks and extra clip attributes. At the song's
# 120 BPM (229.6875 samples a tick) each clip's ratio, its sample's length over its clip's, is its own:
#   RSTONE  ~1.115  the tone from 512, so its loops crossfade into the 512 frames before the start marker
#   RSPERC  ~1.115  the whole percussive source
#   RSSLOW  ~0.867  the tone slowed, within an octave (the fine hop tables)
#   RSFAST  ~2.229  the percussive source sped up past an octave (the coarse tables)
#   RSCRAWL ~0.476  the percussive source slowed past an octave (the coarse tables)
#   RSREV   ~0.836  the tone reversed between 512 and 3,584, so its reversed loops have a pre-margin after the end marker
#   RSPITCH ~0.927  the tone two semitones up from 512: a transposed stretch reads a cache, and its loops round into the
#                   pre-margin at the transposed rate
#   RSCLAMP ~4.459  the percussive source sped up past two octaves, where the coarse tables clamp at their top
#   RSHALT  ~0.238  the percussive source slowed past two octaves, where the coarse tables clamp at their bottom
#   RSDRIFT ~1.003  sample A, six seconds long, within the ratio window where playback is left unstretched until it
#                   drifts out of sync, which takes it about two seconds: the tone's short loops restart in sync
#                   before they can drift that far
STRETCH_TRACKS = (
    ("RSTONE", "SAMPLES/RS/TONE.WAV", 512, 4096, 14, ""),
    ("RSPERC", "SAMPLES/RS/PERC.WAV", 0, 32768, 128, ""),
    ("RSSLOW", "SAMPLES/RS/TONE.WAV", 512, 4096, 18, ""),
    ("RSFAST", "SAMPLES/RS/PERC.WAV", 0, 32768, 64, ""),
    ("RSCRAWL", "SAMPLES/RS/PERC.WAV", 0, 32768, 300, ""),
    ("RSREV", "SAMPLES/RS/TONE.WAV", 512, 3584, 16, ' reversed="1"'),
    ("RSPITCH", "SAMPLES/RS/TONE.WAV", 512, 4096, 15, ' transpose="2"'),
    ("RSCLAMP", "SAMPLES/RS/PERC.WAV", 0, 32768, 32, ""),
    ("RSHALT", "SAMPLES/RS/PERC.WAV", 0, 32768, 600, ""),
    ("RSDRIFT", "SAMPLES/RS/A.WAV", 0, RATE * SAMPLE_SECONDS, 1148, ""),
)
# RS_AUDIO_STRETCH_NATIVE's two clips are both native at 250 samples a tick (about 105.8 BPM): the tone from 596
# (3,500 frames over 14 ticks) and the percussive source from 1,018 (31,750 frames over 127 ticks). Their ratios fall
# in the window where nothing is stretched, but a loop end with a pre-margin to crossfade into has a time stretcher
# fudged in to crossfade it without a click when a render block starts within the crossfade's 100-frame reach of it,
# where the fudge is set up; a loop end no block start reaches plays unfudged. Neither loop is a whole number of
# 128-frame render blocks, so the block starts drift across the loop and most loop ends (88 of about 110 for the tone)
# are reached. Its twin plays the same frames from files that hold only them: no pre-margin, so no fudged crossfade.
TEMPO_STRETCH_NATIVE = 'timePerTimerTick="250" timerTickFraction="0"'
STRETCH_NATIVE_TRACKS = (
    ("RSTONE", "SAMPLES/RS/TONE.WAV", 596, 4096, 14, ""),
    ("RSPERC", "SAMPLES/RS/PERC.WAV", 1018, 32768, 127, ""),
)
STRETCH_NATIVE_DRY_TRACKS = (
    ("RSTONE", "SAMPLES/RS/TONE_TRIM.WAV", 0, 4096 - 596, 14, ""),
    ("RSPERC", "SAMPLES/RS/PERC_TRIM.WAV", 0, 32768 - 1018, 127, ""),
)
# RS_AUDIO_STRETCH_ARRANGED plays clips from arrangement instances that end without looping: the percussive source for
# two loops and a half, then once through, its whole file, so it has no waveform past its end and plays to the end
# unfaded; the tone forwards to 3,584 of its 4,096 frames for three loops and a half; and the reversed tone for twelve
# and a half, then on past the render's end, so the arrangement plays throughout. The two tones have waveform past the
# end they play to, so each fades out before its instance ends. Each instance is (position, length) in ticks.
ARRANGED_TRACKS = (
    (("RSPERC", "SAMPLES/RS/PERC.WAV", 0, 32768, 128, ""), ((0, 320), (384, 128))),
    (("RSTONE", "SAMPLES/RS/TONE.WAV", 0, 3584, 14, ""), ((0, 49),)),
    (
        ("RSREV", "SAMPLES/RS/TONE.WAV", 512, 3584, 16, ' reversed="1"'),
        ((0, 200), (208, 1700)),
    ),
)
# RS_AUDIO_STRETCH_EDGES's clips, at 120 BPM, each at an edge of the stretcher's paths or its caller's:
#   RSTIGHT   ~1.115  the tone from 8 to 3,592: a loop end's crossfade would start further back than its 8-frame
#                     pre-margin reaches, so that hop is placed as any other, and the phase search can take a hop back
#                     before the waveform's start
#   RSTIED    ~1.115  the tone 30 cents up with pitch and speed tied: a tied clip with a fine tune stretches
#   RSTRANS   ~0.927  the tone two semitones up with pitch and speed tied: a tied clip with a transpose stretches
#   RSPLAIN   ~1.115  the tone with pitch and speed tied and no pitch change: resampled, its loop crossfade fudged at
#                     its resampled rate
#   RSBACK    ~0.836  the tone reversed, pitch and speed tied: resampled backwards, its loop crossfade fudged into the
#                     pre-margin after its end marker
#   RS48K     ~1.115  the tone in a 48 kHz file: its playback rate alone keeps it in time, so it is stretched by the
#                     ratio less that rate
#   RSMONO    ~1.115  the tone in a mono file
#   RSPAST    ~0.998  the tone with its end marker 504 frames past the file's end: the clip does not loop at the
#                     stretcher's level
#   RSFLUSH   1       the percussive source from 2 at its native tempo: a 2-frame pre-margin, too short to fudge a
#                     loop crossfade into
#   RSSHALLOW 1       the percussive source from 50 at its native tempo: a pre-margin shorter than the crossfade, so a
#                     loop end is fudged only within its 50 frames
#   RSADJUST  ~1.115  the tone with the clip's pitch adjust raised: a manual pitch change the stretch ratio makes up
#   RSLONG    ~1.115  the first 2,129,920 frames of the mono tone repeated to 4,259,840 (LONG.WAV): a source this long
#                     keeps its percussiveness cache in clusters (from 2^21 frames with the card's 32 KB clusters)
#                     rather than in one block
#   RSLONGMID ~1.116  a 10,000-frame loop of LONG.WAV from 20,000: RSLONG, filling its percussiveness cache from 0,
#                     runs into this loop's zone, eats into the part of it due to be replaced, and merges with it
#   RSLONGRIM ~1.115  a 90,000-frame loop of LONG.WAV across frame 2^22, where one percussiveness cache cluster ends
#                     and the next begins
#   RSLONGREV ~1.115  the same loop reversed, so its percussiveness cache is filled backwards, across the same clusters
STRETCH_EDGE_TRACKS = (
    ("RSTIGHT", "SAMPLES/RS/TONE.WAV", 8, 3592, 14, ""),
    (
        "RSTIED",
        "SAMPLES/RS/TONE.WAV",
        512,
        4096,
        14,
        ' pitchSpeedIndependent="0" cents="30"',
    ),
    (
        "RSTRANS",
        "SAMPLES/RS/TONE.WAV",
        512,
        4096,
        15,
        ' pitchSpeedIndependent="0" transpose="2"',
    ),
    ("RSPLAIN", "SAMPLES/RS/TONE.WAV", 512, 4096, 14, ' pitchSpeedIndependent="0"'),
    (
        "RSBACK",
        "SAMPLES/RS/TONE.WAV",
        512,
        3584,
        16,
        ' pitchSpeedIndependent="0" reversed="1"',
    ),
    ("RS48K", "SAMPLES/RS/TONE48.WAV", 512, 4096, 14, ""),
    ("RSMONO", "SAMPLES/RS/TONE_MONO.WAV", 512, 4096, 14, ""),
    ("RSPAST", "SAMPLES/RS/TONE.WAV", 512, 4600, 18, ""),
    ("RSFLUSH", "SAMPLES/RS/PERC.WAV", 2, 14702, 64, ""),
    ("RSSHALLOW", "SAMPLES/RS/PERC.WAV", 50, 14750, 64, ""),
    ("RSADJUST", "SAMPLES/RS/TONE.WAV", 512, 4096, 14, ""),
    ("RSLONG", "SAMPLES/RS/LONG.WAV", 0, 520 * 4096, 8316, ""),
    ("RSLONGMID", "SAMPLES/RS/LONG.WAV", 20000, 30000, 39, ""),
    ("RSLONGRIM", "SAMPLES/RS/LONG.WAV", 4150000, 4240000, 351, ""),
    ("RSLONGREV", "SAMPLES/RS/LONG.WAV", 4150000, 4240000, 351, ' reversed="1"'),
)
# Each clip's extra params: the pitch adjust, and the LONG.WAV clips quieter, so the song's sum stays below full scale.
STRETCH_EDGE_PARAMS = {
    "RSADJUST": 'pitchAdjust="0x10000000"',
    **{
        name: 'volume="0x90000000"'
        for name in ("RSLONG", "RSLONGMID", "RSLONGREV", "RSLONGRIM")
    },
}
# Each track's volume, low enough that the clips' sum stays below full scale, so no clipping hides the stretch.
STRETCH_VOLUME = 'volume="0xA0000000"'
TEMPO_120 = 'timePerTimerTick="229" timerTickFraction="-1342177280"'


def stretch_source(path: str) -> bytes:
    """The committed sample golden source behind card path @p path, refusing one whose bytes have changed."""
    name, sha256 = STRETCH_SOURCES[path]
    data = (
        Path(__file__).resolve().parents[1] / "presets" / "sample" / name
    ).read_bytes()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise ValueError(
            f"{name} changed: update its sha256 in make_render_song.py's STRETCH_SOURCES"
        )
    return data


# A source's bytes in a file whose header gives another sample rate: (source card path, rate).
RERATED_SOURCES = {"SAMPLES/RS/TONE48.WAV": ("SAMPLES/RS/TONE.WAV", 48000)}


def rerated_source(path: str) -> bytes:
    """The source RERATED_SOURCES gives card path @p path, its fmt chunk's sample and byte rates set to its rate."""
    source, rate = RERATED_SOURCES[path]
    data = bytearray(stretch_source(source))
    pos = 12
    while data[pos : pos + 4] != b"fmt ":
        pos += 8 + struct.unpack_from("<I", data, pos + 4)[0]
    block_align = struct.unpack_from("<H", data, pos + 20)[0]
    struct.pack_into("<II", data, pos + 12, rate, rate * block_align)
    return bytes(data)


# A source's frames repeated end to end into one long file: (source card path, copies).
REPEATED_SOURCES = {"SAMPLES/RS/LONG.WAV": ("SAMPLES/RS/TONE_MONO.WAV", 1040)}


def repeated_source(path: str) -> bytes:
    """The source REPEATED_SOURCES gives card path @p path, its data chunk repeated, every other chunk kept."""
    source, copies = REPEATED_SOURCES[path]
    data = stretch_source(source)
    out = bytearray(data[:12])
    pos = 12
    while pos < len(data):
        tag, size = data[pos : pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8 : pos + 8 + size]
        if tag == b"data":
            body = body * copies
        out += (
            tag
            + struct.pack("<I", len(body))
            + body
            + (b"\0" if len(body) % 2 else b"")
        )
        pos += 8 + size + (size % 2)
    struct.pack_into("<I", out, 4, len(out) - 8)
    return bytes(out)


def trimmed_source(path: str) -> bytes:
    """The 16-bit stereo frames TRIMMED_SOURCES gives card path @p path, in a file of their own."""
    source, first = TRIMMED_SOURCES[path]
    data = stretch_source(source)
    pos = 12
    while data[pos : pos + 4] != b"data":
        pos += 8 + struct.unpack_from("<I", data, pos + 4)[0]
    size = struct.unpack_from("<I", data, pos + 4)[0]
    return wav(data[pos + 8 + first * 4 : pos + 8 + size])


def stretch_parts(
    tracks: tuple,
    independent: bool = True,
    instances: tuple = (),
    params: dict | None = None,
) -> tuple[str, str]:
    """An audio track and clip for each of @p tracks, pitch and speed independent unless not @p independent, each track
    given the arrangement instances in @p instances at its index, and each clip named in @p params its extra params."""
    tracks_xml = ""
    clips = ""
    for index, (name, path, start, end, ticks, attributes) in enumerate(tracks):
        extra = (params or {}).get(name, "")
        fx = _host(params=f"{STRETCH_VOLUME} {extra}" if extra else STRETCH_VOLUME)
        track_attributes = f'name="{name}"'
        if instances:
            codes = "".join(
                f"{pos:08X}{length:08X}{index:08X}" for pos, length in instances[index]
            )
            track_attributes += f' clipInstances="0x{codes}"'
        tracks_xml += AUDIO_TRACK.format(**fx).replace('name="RSAUD"', track_attributes)
        clips += (
            AUDIO_CLIP.format(
                frames=end,
                ticks=ticks,
                clip_attributes=(
                    attributes
                    if "pitchSpeedIndependent" in attributes
                    else f' pitchSpeedIndependent="{int(independent)}"{attributes}'
                ),
                params=fx["params"],
                param_tags=fx["param_tags"],
            )
            .replace('trackName="RSAUD"', f'trackName="{name}"')
            .replace("SAMPLES/RS/A.WAV", path)
            .replace('startSamplePos="0"', f'startSamplePos="{start}"')
        )
    return tracks_xml, clips


def stretch_song(
    tracks: tuple,
    independent: bool = True,
    native: bool = False,
    params: dict | None = None,
) -> str:
    """@p tracks alone at 120 BPM, or at 250 samples a tick when @p native, each clip named in @p params given its
    extra params."""
    instruments, clips = stretch_parts(tracks, independent, params=params)
    xml = song_xml([], instruments=instruments, clips=clips)
    return xml.replace(TEMPO_120, TEMPO_STRETCH_NATIVE) if native else xml


def stretch_arranged_song(independent: bool) -> str:
    """ARRANGED_TRACKS at 120 BPM in the arranger, the song opening in the arranger view so play starts it."""
    instruments, clips = stretch_parts(
        tuple(track for track, _ in ARRANGED_TRACKS),
        independent,
        tuple(instances for _, instances in ARRANGED_TRACKS),
    )
    return song_xml([], instruments=instruments, clips=clips).replace(
        'inputTickMagnitude="2"', 'inputTickMagnitude="2" inArrangementView="1"'
    )


# A bar at the song's 120 BPM, about 2 s: an arrangement take of RS_ARRANGE_TAKE's clips repeats each of them three
# times or more within the default render.
TAKE_TICKS = 384
# The synth's automation: its volume stepping down and back within the bar, and its low-pass cutoff ramping down to
# the middle of the bar and stepping back up at its end, so a repeat carries both stepped and interpolated nodes.
TAKE_SYNTH_VOLUME = _steps(0x4CCCCCA8, [(0x20000000, 96), (0x4CCCCCA8, 288)])
TAKE_SYNTH_LPF = (
    f"0x{0x7FFFFFFF:08X}{0x7FFFFFFF:08X}00000000{0x10000000:08X}{192 | 0x80000000:08X}"
    f"{0x7FFFFFFF:08X}{352:08X}"
)
# The kit's first drum's row: its cutoff stepping down for the second half of the bar.
TAKE_ROW_LPF = _steps(0x7FFFFFFF, [(0x20000000, 192)])
# The kit's second row is half a bar long, so it repeats at its own end: its cutoff stepping down for the second half of
# its length.
TAKE_SHORT_ROW_TICKS = TAKE_TICKS // 2
TAKE_SHORT_ROW_LPF = _steps(0x7FFFFFFF, [(0x30000000, TAKE_SHORT_ROW_TICKS // 2)])
# The audio clip's volume stepping up for the last quarter of the bar.
TAKE_AUDIO = _host(params=f'volume="{_steps(0x00000000, [(0x30000000, 288)])}"')


def arrange_take_song(voices: list[tuple[str, str, int]]) -> str:
    """One synth, a kit and an audio track, each playing a one-bar session clip with automation, for a gate that
    records them into the arrangement (RENDER_ARRANGE_RECORD) and turns their knobs while it does (RENDER_KNOB): each
    take's clip repeats its source clip's notes and automation at every loop end."""
    name, file, note = voices[0]
    frames = RATE * SAMPLE_SECONDS
    sound = SOUND.format(name=name, file=file, frames=frames)
    synth_clip = (
        CLIP.format(name=name, ticks=TAKE_TICKS, note=note)
        .replace('volume="0x4CCCCCA8"', f'volume="{TAKE_SYNTH_VOLUME}"')
        .replace('lpfFrequency="0x7FFFFFFF"', f'lpfFrequency="{TAKE_SYNTH_LPF}"')
    )
    drums = [
        _drum("RSD1", "SAMPLES/RS/A.WAV", [(0, 96), (192, 96)]),
        _drum("RSD2", "SAMPLES/RS/B.WAV", [(96, 96)]),
    ]
    kit, kit_clip = kit_parts(DRY, drums)
    # Each row's cutoff, in row order: the first soundParams in the clip is the first row's.
    kit_clip = kit_clip.replace(
        'lpfFrequency="0x7FFFFFFF"', f'lpfFrequency="{TAKE_ROW_LPF}"', 1
    )
    kit_clip = kit_clip.replace(
        'lpfFrequency="0x7FFFFFFF"', f'lpfFrequency="{TAKE_SHORT_ROW_LPF}"', 1
    )
    kit_clip = kit_clip.replace(
        '<noteRow drumIndex="1" ',
        f'<noteRow drumIndex="1" length="{TAKE_SHORT_ROW_TICKS}" ',
    )
    track, audio_clip = audio_parts(TAKE_AUDIO)
    clips = [kit_clip, audio_clip]
    clips = [
        c.replace(f'length="{CLIP_TICKS}"', f'length="{TAKE_TICKS}"') for c in clips
    ]
    return song_xml(
        [],
        instruments=sound + kit + track,
        clips=synth_clip + "".join(clips),
    )


SONGS = {
    "RS_ARRANGE_TAKE": arrange_take_song,
    "RS_KIT_FX": lambda v: kit_song(v, FX),
    "RS_KIT_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_FX": lambda v: audio_song(v, FX),
    "RS_AUDIO_DRY": lambda v: audio_song(v, DRY),
    "RS_AUDIO_CENTS": lambda v: audio_song(v, AUDIO_CENTS),
    "RS_AUDIO_CENTS_DRY": lambda v: audio_song(v, DRY),
    "RS_AUDIO_STRETCH": lambda v: stretch_song(STRETCH_TRACKS),
    # Every clip at 120 BPM as in RS_AUDIO_STRETCH, but pitch and speed tied: nine resample, and the transposed clip
    # still stretches, as a tied clip with a transpose does.
    "RS_AUDIO_STRETCH_DRY": lambda v: stretch_song(STRETCH_TRACKS, independent=False),
    "RS_AUDIO_STRETCH_NATIVE": lambda v: stretch_song(
        STRETCH_NATIVE_TRACKS, native=True
    ),
    "RS_AUDIO_STRETCH_NATIVE_DRY": lambda v: stretch_song(
        STRETCH_NATIVE_DRY_TRACKS, native=True
    ),
    "RS_AUDIO_STRETCH_ARRANGED": lambda v: stretch_arranged_song(independent=True),
    "RS_AUDIO_STRETCH_ARRANGED_DRY": lambda v: stretch_arranged_song(independent=False),
    "RS_AUDIO_STRETCH_EDGES": lambda v: stretch_song(
        STRETCH_EDGE_TRACKS, params=STRETCH_EDGE_PARAMS
    ),
    "RS_AUDIO_STRETCH_EDGES_DRY": lambda v: stretch_song(
        STRETCH_EDGE_TRACKS, independent=False, params=STRETCH_EDGE_PARAMS
    ),
    "RS_MASTER_FX": lambda v: master_song(v, FX),
    "RS_MASTER_DRY": lambda v: master_song(v, DRY),
    # RS_MASTER_FX under its own name, rendered in stereo, where the master's pan applies.
    "RS_MASTER_PAN": lambda v: master_song(v, FX),
    "RS_MASTER_PAN_DRY": lambda v: master_song(v, DRY),
    "RS_MASTER_GRAIN_OFF": lambda v: master_song(v, MASTER_GRAIN_OFF),
    "RS_MASTER_GRAIN_OFF_DRY": lambda v: master_song(v, DRY),
    "RS_MASTER_ARRANGER": lambda v: arranger_song(v, MASTER_ARRANGER),
    "RS_MASTER_ARRANGER_DRY": lambda v: arranger_song(v, DRY),
    "RS_KIT_FILTERS": lambda v: kit_song(v, KIT_FILTERS),
    "RS_KIT_FILTERS_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_FILTERS": lambda v: audio_song(v, AUDIO_FILTERS),
    "RS_AUDIO_FILTERS_DRY": lambda v: audio_song(v, DRY),
    "RS_MASTER_FILTERS": lambda v: master_song(v, MASTER_FILTERS),
    "RS_MASTER_FILTERS_DRY": lambda v: master_song(v, DRY),
    "RS_KIT_SVF_MORPH": lambda v: kit_song(v, KIT_SVF_MORPH),
    "RS_KIT_SVF_MORPH_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_DRIVE": lambda v: audio_song(v, AUDIO_DRIVE),
    "RS_AUDIO_DRIVE_DRY": lambda v: audio_song(v, DRY),
    "RS_KIT_FILTER_FADE": lambda v: kit_song(v, KIT_FILTER_FADE),
    "RS_KIT_FILTER_FADE_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_FILTER_FADE": lambda v: audio_song(v, AUDIO_FILTER_FADE),
    "RS_AUDIO_FILTER_FADE_DRY": lambda v: audio_song(v, DRY),
    "RS_AUDIO_DUCKED": lambda v: ducked_song(v, dry=False),
    "RS_AUDIO_DUCKED_DRY": lambda v: ducked_song(v, dry=True),
    "RS_DUCK_SHAPES": lambda v: duck_shapes_song(v, dry=False),
    "RS_DUCK_SHAPES_DRY": lambda v: duck_shapes_song(v, dry=True),
    "RS_KIT_COMP": lambda v: parts_song(v, [kit_parts(KIT_COMP, COMP_DRUMS)]),
    "RS_KIT_COMP_DRY": lambda v: parts_song(v, [kit_parts(DRY, COMP_DRUMS_DRY)]),
    "RS_DELAY_OWNERS": lambda v: parts_song(
        v,
        delay_owners_parts(dry=False),
        song_settings=master_settings(delay_owners_master(dry=False)),
    ),
    "RS_DELAY_OWNERS_DRY": lambda v: parts_song(
        v,
        delay_owners_parts(dry=True),
        song_settings=master_settings(delay_owners_master(dry=True)),
    ),
    "RS_TEMPO_DELAYS": lambda v: parts_song(v, tempo_delays_parts()),
    "RS_STUTTER_AUDIO": lambda v: parts_song(v, [audio_parts(DRY, STUTTER_FREE)]),
    # RS_STUTTER_AUDIO under its own name, so the gate that turns its rate keeps its own record.
    "RS_STUTTER_RATE": lambda v: parts_song(v, [audio_parts(DRY, STUTTER_FREE)]),
    # RS_STUTTER_AUDIO under its own name: its first synth stutters, with the song's stutter settings (quantized).
    "RS_STUTTER_SYNTH": lambda v: parts_song(v, [audio_parts(DRY, STUTTER_FREE)]),
    "RS_STUTTER_KIT": lambda v: parts_song(
        v, [kit_parts(DRY, PINGPONG_DRUMS, STUTTER_QUANT_REV)]
    ),
    "RS_STUTTER_MASTER": lambda v: parts_song(
        v,
        [audio_parts(DRY)],
        song_settings=master_settings(DRY) + f"\n\t{STUTTER_PINGPONG}",
    ),
    "RS_STUTTER_HANDOFF": lambda v: parts_song(
        v,
        [
            audio_parts(DRY, STUTTER_FREE),
            kit_parts(DRY, PINGPONG_DRUMS, STUTTER_QUANT_REV),
        ],
    ),
    "RS_KIT_DELAY": lambda v: parts_song(v, [kit_parts(KIT_DELAY, DELAY_DRUMS)]),
    "RS_KIT_DELAY_DRY": lambda v: parts_song(v, [kit_parts(DRY, DELAY_DRUMS)]),
    "RS_AUDIO_DELAY": lambda v: parts_song(v, [audio_parts(AUDIO_DELAY)]),
    "RS_AUDIO_DELAY_DRY": lambda v: parts_song(v, [audio_parts(DRY)]),
    "RS_KIT_PINGPONG": lambda v: parts_song(
        v, [kit_parts(KIT_PINGPONG, PINGPONG_DRUMS)]
    ),
    "RS_KIT_PINGPONG_DRY": lambda v: parts_song(v, [kit_parts(DRY, PINGPONG_DRUMS)]),
    "RS_AUDIO_DELAY_TOGGLE": lambda v: parts_song(v, [audio_parts(AUDIO_DELAY_TOGGLE)]),
    "RS_AUDIO_DELAY_TOGGLE_DRY": lambda v: parts_song(
        v, [audio_parts(AUDIO_DELAY_TOGGLE_DRY)]
    ),
    "RS_AUDIO_DELAY_SWITCH": lambda v: parts_song(v, [audio_parts(AUDIO_DELAY_SWITCH)]),
    "RS_PINGPONG_SWEEP": lambda v: parts_song(
        v, [kit_parts(KIT_DELAY, DELAY_DRUMS), audio_parts(AUDIO_PINGPONG_SWEEP)]
    ),
    "RS_PINGPONG_SWEEP_DRY": lambda v: parts_song(
        v, [kit_parts(DRY, DELAY_DRUMS), audio_parts(DRY)]
    ),
    "RS_AUDIO_DELAY_EXTREMES": lambda v: parts_song(
        v, [audio_parts(AUDIO_DELAY_EXTREMES)]
    ),
    "RS_AUDIO_DELAY_EXTREMES_DRY": lambda v: parts_song(v, [audio_parts(DRY)]),
    "RS_LOW_CLIP": lambda v: parts_song(
        v, [kit_parts(KIT_LOW_CLIP, PINGPONG_DRUMS), audio_parts(AUDIO_LOW_CLIP)]
    ),
    "RS_LOW_CLIP_DRY": lambda v: parts_song(
        v, [kit_parts(DRY, PINGPONG_DRUMS), audio_parts(DRY)]
    ),
    "RS_BITCRUSH": lambda v: parts_song(
        v,
        [kit_parts(KIT_BITCRUSH, PINGPONG_DRUMS), audio_parts(AUDIO_SRR_TOGGLE)],
        song_settings=master_settings(MASTER_BITCRUSH),
    ),
    "RS_BITCRUSH_DRY": lambda v: parts_song(
        v,
        [kit_parts(DRY, PINGPONG_DRUMS), audio_parts(DRY)],
        song_settings=master_settings(DRY),
    ),
    "RS_EQ_BANDS": lambda v: parts_song(
        v, [kit_parts(KIT_EQ_BASS, PINGPONG_DRUMS), audio_parts(AUDIO_EQ_TREBLE)]
    ),
    "RS_EQ_BANDS_DRY": lambda v: parts_song(
        v, [kit_parts(DRY, PINGPONG_DRUMS), audio_parts(DRY)]
    ),
    "RS_AUDIO_COMP_ONSET": lambda v: parts_song(v, [audio_parts(AUDIO_COMP_ONSET)]),
    "RS_AUDIO_COMP_ONSET_DRY": lambda v: parts_song(v, [audio_parts(DRY)]),
    "RS_AUDIO_COMP_RESET": lambda v: parts_song(v, [audio_parts(AUDIO_COMP_RESET)]),
    "RS_AUDIO_COMP_RESET_DRY": lambda v: parts_song(v, [audio_parts(DRY)]),
    "RS_AUDIO_COMP_BLEND": lambda v: parts_song(
        v, [loud_audio_parts(AUDIO_COMP_BLEND)]
    ),
    "RS_AUDIO_COMP_BLEND_DRY": lambda v: parts_song(
        v, [loud_audio_parts(AUDIO_COMP_BLEND_DRY)]
    ),
    "RS_MODFX_MONO": lambda v: mod_fx_song(
        v, _mod_fx("TapeWarble"), _mod_fx("dimension"), DRY
    ),
    "RS_MODFX_MONO_DRY": lambda v: mod_fx_song(v, DRY, DRY, DRY),
    "RS_MODFX_STEREO_A": lambda v: mod_fx_song(
        v,
        _mod_fx("flanger", MOD_FX_STEREO_A_VOLUME),
        _mod_fx("chorus", MOD_FX_STEREO_A_VOLUME),
        _mod_fx("phaser", MOD_FX_STEREO_A_VOLUME),
    ),
    "RS_MODFX_STEREO_A_DRY": lambda v: mod_fx_song(
        v,
        _host(params=MOD_FX_STEREO_A_VOLUME),
        _host(params=MOD_FX_STEREO_A_VOLUME),
        _host(params=MOD_FX_STEREO_A_VOLUME),
    ),
    "RS_MODFX_STEREO_B": lambda v: mod_fx_song(
        v,
        _mod_fx("StereoChorus", MOD_FX_STEREO_B_VOLUME),
        _mod_fx("TapeWarble", MOD_FX_STEREO_B_VOLUME),
        _mod_fx("dimension", MOD_FX_STEREO_B_VOLUME),
    ),
    "RS_MODFX_STEREO_B_DRY": lambda v: mod_fx_song(
        v,
        _host(params=MOD_FX_STEREO_B_VOLUME),
        _host(params=MOD_FX_STEREO_B_VOLUME),
        _host(params=MOD_FX_STEREO_B_VOLUME),
    ),
    "RS_MODFX_SOFT_OFF": lambda v: parts_song(
        v,
        [
            kit_parts(
                _mod_fx("phaser", f"{MOD_FX_SOFT_OFF_DEPTH} {MOD_FX_SOFT_OFF_VOLUME}"),
                PINGPONG_DRUMS,
            ),
            audio_parts(
                _mod_fx("flanger", f"{MOD_FX_SOFT_OFF_DEPTH} {MOD_FX_SOFT_OFF_VOLUME}")
            ),
        ],
    ),
    "RS_MODFX_SWITCH": lambda v: parts_song(v, [audio_parts(_mod_fx("TapeWarble"))]),
    "RS_MODFX_CLEAR": lambda v: parts_song(
        v, [audio_parts(_mod_fx("flanger", MOD_FX_CLEAR_VOLUME))]
    ),
    "RS_GRAIN_MONO": lambda v: mod_fx_song(
        v, _grain(GRAIN_MONO_VOLUME), DRY, _mod_fx("grainFX", GRAIN_MONO_VOLUME)
    ),
    "RS_GRAIN_MONO_DRY": lambda v: mod_fx_song(
        v, _host(params=GRAIN_MONO_VOLUME), DRY, _host(params=GRAIN_MONO_VOLUME)
    ),
    "RS_GRAIN_STEREO": lambda v: mod_fx_song(
        v,
        _grain(GRAIN_STEREO_VOLUME),
        _mod_fx("grainFX", GRAIN_STEREO_VOLUME),
        _grain(GRAIN_STEREO_VOLUME),
    ),
    "RS_GRAIN_STEREO_DRY": lambda v: mod_fx_song(
        v,
        _host(params=GRAIN_STEREO_VOLUME),
        _host(params=GRAIN_STEREO_VOLUME),
        _host(params=GRAIN_STEREO_VOLUME),
    ),
    "RS_GRAIN_SWITCH": lambda v: parts_song(
        v,
        [
            audio_parts(
                _mod_fx("chorus", f'modFXFeedback="0x7FFFFFFF" {GRAIN_SWITCH_VOLUME}')
            )
        ],
    ),
    "RS_GRAIN_OWNERS": lambda v: parts_song(
        v,
        [
            kit_parts(_grain(GRAIN_OWNERS_VOLUME), PINGPONG_DRUMS),
            grain_synth_held_parts(),
        ],
        song_settings=master_settings(_grain(GRAIN_OWNERS_VOLUME)),
    ),
    "RS_GRAIN_MASTER": lambda v: parts_song(
        v,
        [],
        song_settings=master_settings(
            _mod_fx("chorus", f'modFXFeedback="0x7FFFFFFF" {GRAIN_MASTER_VOLUME}')
        ),
    ),
    "RS_GRAIN_RESTART": lambda v: parts_song(
        v,
        [
            kit_parts(_grain(GRAIN_RESTART_VOLUME), GRAIN_RESTART_DRUMS),
            audio_parts(_grain(f"{MOD_FX_SOFT_OFF_DEPTH} {GRAIN_RESTART_VOLUME}")),
            grain_synth_parts(),
        ],
    ),
    "RS_GRAIN_RESTART_DRY": lambda v: parts_song(
        v,
        [
            kit_parts(_host(params=GRAIN_RESTART_VOLUME), GRAIN_RESTART_DRUMS),
            audio_parts(_host(params=GRAIN_RESTART_VOLUME)),
            grain_synth_dry_parts(),
        ],
    ),
    "RS_MODFX_SOFT_OFF_DRY": lambda v: parts_song(
        v,
        [
            kit_parts(_host(params=MOD_FX_SOFT_OFF_VOLUME), PINGPONG_DRUMS),
            audio_parts(_host(params=MOD_FX_SOFT_OFF_VOLUME)),
        ],
    ),
    # Freeverb with its width at half, the only setting where its right channel takes in the left (wet2 > 0).
    "RS_REVERB_FREEVERB": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(0, REVERB_TURNED_KNOBS)
    ),
    "RS_REVERB_FREEVERB_DRY": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(0, REVERB_DEFAULT_KNOBS)
    ),
    "RS_REVERB_MUTABLE": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(1, REVERB_TURNED_KNOBS)
    ),
    "RS_REVERB_MUTABLE_DRY": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(1, REVERB_DEFAULT_KNOBS)
    ),
    # Digital, rendered in stereo by its gate, its pan halfway left so the two channels' levels differ.
    "RS_REVERB_DIGITAL": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(2, REVERB_TURNED_KNOBS, pan=-1073741824)
    ),
    "RS_REVERB_DIGITAL_DRY": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(2, REVERB_DEFAULT_KNOBS)
    ),
    # Mutable at turned knobs, for its gate to switch the model and turn knobs mid-render (RENDER_REVERB_SWITCH);
    # no dry twin: the gate's twin is the same song rendered unswitched.
    "RS_REVERB_SWITCH": lambda v: parts_song(
        v, [audio_parts(REVERB_SEND)], _reverb(1, REVERB_TURNED_KNOBS)
    ),
    # RS_STREAM under its own name, for its gate to switch the metronome on mid-render (RENDER_METRONOME); no dry twin:
    # the gate's twin is the same song rendered unswitched.
    "RS_METRONOME": song_xml,
    "RS_REVERB_DUCK_OVERRIDE": lambda v: reverb_override_song(
        v, track_leads=False, dry=False
    ),
    "RS_REVERB_DUCK_OVERRIDE_DRY": lambda v: reverb_override_song(
        v, track_leads=False, dry=True
    ),
    "RS_REVERB_DUCK_OVERRIDE_TRACK": lambda v: reverb_override_song(
        v, track_leads=True, dry=False
    ),
    "RS_REVERB_DUCK_OVERRIDE_TRACK_DRY": lambda v: reverb_override_song(
        v, track_leads=True, dry=True
    ),
}


# Each sample file a card may hold, by its card path, and how to make it.
SAMPLE_FILES = {
    "SAMPLES/RS/A.WAV": lambda: wav(sample_frames(1, [110.0, 220.0, 330.0, 440.0])),
    "SAMPLES/RS/B.WAV": lambda: wav(sample_frames(2, [138.6, 277.2, 415.8])),
    LOUD_SAMPLE: lambda: wav(loud_sample_frames(1, [110.0, 220.0, 330.0, 440.0])),
    **{path: lambda path=path: stretch_source(path) for path in STRETCH_SOURCES},
    **{path: lambda path=path: trimmed_source(path) for path in TRIMMED_SOURCES},
    **{path: lambda path=path: rerated_source(path) for path in RERATED_SOURCES},
    **{path: lambda path=path: repeated_source(path) for path in REPEATED_SOURCES},
}
VOICES = [("RSA", "SAMPLES/RS/A.WAV", 60), ("RSB", "SAMPLES/RS/B.WAV", 64)]


def samples_for(name: str) -> list[str]:
    """The card paths of the samples song @p name plays."""
    paths = ["SAMPLES/RS/A.WAV", "SAMPLES/RS/B.WAV"]
    if name.startswith("RS_AUDIO_COMP_BLEND"):
        paths.append(LOUD_SAMPLE)
    # Every stretch song's card holds all the small stretch sources (TONE48.WAV and TONE_MONO.WAV among them, which only
    # RS_AUDIO_STRETCH_EDGES plays): a file a song does not name is never read. LONG.WAV, 8 MB, goes only on the card
    # that plays it.
    if name.startswith("RS_AUDIO_STRETCH"):
        paths += [*STRETCH_SOURCES, *TRIMMED_SOURCES, *RERATED_SOURCES]
    if name.startswith("RS_AUDIO_STRETCH_EDGES"):
        paths += [*REPEATED_SOURCES]
    return paths


def song_path(name: str) -> str:
    """The card path of song @p name."""
    return SONG if name == "RS_STREAM" else f"SONGS/{name}.XML"


def write_song(out: Path, name: str) -> None:
    """Write song @p name's XML into the card tree @p out."""
    (out / "SONGS").mkdir(parents=True, exist_ok=True)
    xml = song_xml(VOICES) if name == "RS_STREAM" else SONGS[name](VOICES)
    (out / song_path(name)).write_text(xml)


def main(argv: list[str]) -> int:
    name = "RS_STREAM"
    if len(argv) == 4 and argv[2] == "--song":
        name = argv[3]
    elif len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    if name != "RS_STREAM" and name not in SONGS:
        print(f"unknown song {name}", file=sys.stderr)
        return 2
    out = Path(argv[1])
    (out / "SAMPLES" / "RS").mkdir(parents=True, exist_ok=True)
    for path in samples_for(name):
        (out / path).write_bytes(SAMPLE_FILES[path]())
    write_song(out, name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
