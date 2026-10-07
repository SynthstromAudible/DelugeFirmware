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
routes: only the filters are on, each with its own `_DRY` twin.
"""

import math
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


def sample_frames(seed: int, partials: list[float]) -> bytes:
    """Six seconds of a slowly beating chord with a little noise, 16-bit stereo, the same bytes on every run."""
    frames = RATE * SAMPLE_SECONDS
    state = seed
    out = bytearray()
    for i in range(frames):
        t = i / RATE
        swell = 0.6 + 0.4 * math.sin(2 * math.pi * 0.5 * t)
        value = (
            sum(math.sin(2 * math.pi * f * t) / (k + 1) for k, f in enumerate(partials))
            * swell
        )
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        noise = (state / 0x7FFFFFFF - 0.5) * 0.02
        left = int(max(-1.0, min(1.0, 0.3 * value + noise)) * 32767)
        right = int(max(-1.0, min(1.0, 0.3 * value - noise)) * 32767)
        out += struct.pack("<hh", left, right)
    return bytes(out)


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
		<audioClip trackName="RSAUD" filePath="SAMPLES/RS/A.WAV" startSamplePos="0" endSamplePos="{frames}" isPlaying="1" length="{ticks}" section="0">
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


def audio_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    return song_xml(
        voices,
        instruments=AUDIO_TRACK.format(**fx),
        clips=AUDIO_CLIP.format(frames=RATE * SAMPLE_SECONDS, ticks=CLIP_TICKS, **fx),
    )


def master_song(voices: list[tuple[str, str, int]], fx: dict) -> str:
    settings = (
        f" {fx['attributes']}>\n\t{fx['delay']}\n\t{fx['compressor']}\n"
        f"\t<songParams {fx['params']}>{fx['param_tags']}</songParams>"
    )
    return song_xml(voices, song_settings=settings)


SONGS = {
    "RS_KIT_FX": lambda v: kit_song(v, FX),
    "RS_KIT_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_FX": lambda v: audio_song(v, FX),
    "RS_AUDIO_DRY": lambda v: audio_song(v, DRY),
    "RS_MASTER_FX": lambda v: master_song(v, FX),
    "RS_MASTER_DRY": lambda v: master_song(v, DRY),
    "RS_KIT_FILTERS": lambda v: kit_song(v, KIT_FILTERS),
    "RS_KIT_FILTERS_DRY": lambda v: kit_song(v, DRY),
    "RS_AUDIO_FILTERS": lambda v: audio_song(v, AUDIO_FILTERS),
    "RS_AUDIO_FILTERS_DRY": lambda v: audio_song(v, DRY),
    "RS_MASTER_FILTERS": lambda v: master_song(v, MASTER_FILTERS),
    "RS_MASTER_FILTERS_DRY": lambda v: master_song(v, DRY),
}


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
    (out / "SONGS").mkdir(parents=True, exist_ok=True)
    (out / "SAMPLES" / "RS").mkdir(parents=True, exist_ok=True)
    voices = [("RSA", "SAMPLES/RS/A.WAV", 60), ("RSB", "SAMPLES/RS/B.WAV", 64)]
    (out / "SAMPLES/RS/A.WAV").write_bytes(
        wav(sample_frames(1, [110.0, 220.0, 330.0, 440.0]))
    )
    (out / "SAMPLES/RS/B.WAV").write_bytes(wav(sample_frames(2, [138.6, 277.2, 415.8])))
    if name == "RS_STREAM":
        (out / SONG).write_text(song_xml(voices))
    else:
        (out / "SONGS" / f"{name}.XML").write_text(SONGS[name](voices))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
