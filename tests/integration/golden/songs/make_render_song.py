#! /usr/bin/env python3
"""Generate the card tree `render_song` plays in CI: a small song whose playback streams samples from the card.

Two synths, each playing its own six-second stereo sample through a long note in a looping session clip. Each
sample is about 32 clusters, so playback has to stream it cluster by cluster rather than finding it resident. Both
samples are generated here, deterministically, so nothing binary is committed and every run plays the same bytes.

    make_render_song.py <out-dir>

writes <out-dir>/SONGS/RS_STREAM.XML and <out-dir>/SAMPLES/RS/{A,B}.WAV.
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


def song_xml(voices: list[tuple[str, str, int]]) -> str:
    frames = RATE * SAMPLE_SECONDS
    sounds = "".join(
        SOUND.format(name=name, file=file, frames=frames) for name, file, _ in voices
    )
    clips = "".join(
        CLIP.format(name=name, ticks=CLIP_TICKS, note=note) for name, _, note in voices
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        # The tempo, as the firmware writes it for a new song. Without it the song's clock runs so fast that every
        # note retriggers within a fraction of a second, and only the start of each sample ever plays.
        '<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" '
        'timerTickFraction="-1342177280" inputTickMagnitude="2">\n'
        f"\t<instruments>\n{sounds}\t</instruments>\n"
        f"\t<sessionClips>\n{clips}\t</sessionClips>\n"
        "</song>\n"
    )


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(argv[1])
    (out / "SONGS").mkdir(parents=True, exist_ok=True)
    (out / "SAMPLES" / "RS").mkdir(parents=True, exist_ok=True)
    voices = [("RSA", "SAMPLES/RS/A.WAV", 60), ("RSB", "SAMPLES/RS/B.WAV", 64)]
    (out / "SAMPLES/RS/A.WAV").write_bytes(
        wav(sample_frames(1, [110.0, 220.0, 330.0, 440.0]))
    )
    (out / "SAMPLES/RS/B.WAV").write_bytes(wav(sample_frames(2, [138.6, 277.2, 415.8])))
    (out / SONG).write_text(song_xml(voices))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
