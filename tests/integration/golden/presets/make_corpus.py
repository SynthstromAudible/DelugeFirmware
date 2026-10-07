#!/usr/bin/env python3
"""Authoring tool for the golden-master corpus: emits the synthesized preset XMLs beside it.

The committed XMLs are the source of truth (the render-preset gate reads only those); this
script exists so the family of presets stays consistent when extending the
corpus. Re-run it after editing and commit the changed XMLs:

    python3 tests/integration/golden/presets/make_corpus.py

Preset values are q31 hex strings as serialized by Sound::writeParamsToFile.
Key reference points: 0x7FFFFFFF = max, 0x80000000 = min/off, 0x00000000 =
center/neutral, 0x4CCCCCA8 = the default synth volume.
"""

import pathlib
import subprocess
import tempfile

CORPUS = pathlib.Path(__file__).parent

# Attributes of <sound>. No inputMidiChannel: the note scripts reach a case through MIDI follow, and a learned input
# on the same channel would deliver every note twice.
BASE_SOUND_ATTRS = {
    "firmwareVersion": "c1.3.0",
    "earliestCompatibleFirmware": "4.1.0-alpha",
    "polyphonic": "poly",
    "voicePriority": "1",
    "mode": "subtractive",
    "lpfMode": "24dB",
    "modFXType": "none",
}

BASE_PARAMS = {
    "arpeggiatorGate": "0x00000000",
    "portamento": "0x80000000",
    "compressorShape": "0xDC28F5B2",
    "oscAVolume": "0x7FFFFFFF",
    "oscAPulseWidth": "0x00000000",
    "oscBVolume": "0x80000000",
    "oscBPulseWidth": "0x00000000",
    "noiseVolume": "0x80000000",
    "volume": "0x4CCCCCA8",
    "pan": "0x00000000",
    "lpfFrequency": "0x7FFFFFFF",
    "lpfResonance": "0x80000000",
    "hpfFrequency": "0x80000000",
    "hpfResonance": "0x80000000",
    "lfo1Rate": "0x1999997E",
    "lfo2Rate": "0x00000000",
    "modulator1Amount": "0x80000000",
    "modulator1Feedback": "0x80000000",
    "modulator2Amount": "0x80000000",
    "modulator2Feedback": "0x80000000",
    "carrier1Feedback": "0x80000000",
    "carrier2Feedback": "0x80000000",
    "modFXRate": "0x00000000",
    "modFXDepth": "0x00000000",
    "delayRate": "0x00000000",
    "delayFeedback": "0x80000000",
    "reverbAmount": "0x80000000",
    "arpeggiatorRate": "0x00000000",
    "stutterRate": "0x00000000",
    "sampleRateReduction": "0x80000000",
    "bitCrush": "0x80000000",
    "modFXOffset": "0x00000000",
    "modFXFeedback": "0x00000000",
    "waveFold": "0x00000000",
}

BASE_ENV1 = {
    "attack": "0x80000000",
    "decay": "0xE6666654",
    "sustain": "0x7FFFFFFF",
    "release": "0x80000000",
}
BASE_ENV2 = {
    "attack": "0xE6666654",
    "decay": "0xE6666654",
    "sustain": "0xFFFFFFE9",
    "release": "0xE6666654",
}
BASE_CABLES = [("velocity", "volume", "0x3FFFFFE8")]

BASE_OSC1 = {"type": "saw", "transpose": "0", "cents": "0", "retrigPhase": "-1"}
BASE_OSC2 = {"type": "square", "transpose": "0", "cents": "0", "retrigPhase": "-1"}
BASE_LFO1 = {"type": "triangle", "syncLevel": "0"}
BASE_UNISON = {"num": "1", "detune": "8"}


def case(
    name,
    sound=None,
    params=None,
    osc1=None,
    osc2=None,
    lfo1=None,
    unison=None,
    env1=None,
    env2=None,
    cables=None,
    modulators=None,  # (mod1_attrs, mod2_attrs) — FM-mode cases only
    json=False,  # write the case as the firmware's JSON, through the upgrader
):
    def merged(base, over):
        return {**base, **(over or {})}

    def attrs(d):
        return " ".join(f'{k}="{v}"' for k, v in d.items())

    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    lines.append(f"<sound {attrs(merged(BASE_SOUND_ATTRS, sound))}>")
    lines.append(f"\t<osc1 {attrs(merged(BASE_OSC1, osc1))} />")
    lines.append(f"\t<osc2 {attrs(merged(BASE_OSC2, osc2))} />")
    lines.append(f"\t<lfo1 {attrs(merged(BASE_LFO1, lfo1))} />")
    lines.append('\t<lfo2 type="triangle" />')
    if modulators is not None:
        mod1, mod2 = modulators
        lines.append(
            f"\t<modulator1 {attrs(merged({'transpose': '0', 'cents': '0', 'retrigPhase': '-1'}, mod1))} />"
        )
        lines.append(
            f"\t<modulator2 {attrs(merged({'transpose': '0', 'cents': '0', 'retrigPhase': '-1', 'toModulator1': '0'}, mod2))} />"
        )
    lines.append(f"\t<unison {attrs(merged(BASE_UNISON, unison))} />")
    lines.append('\t<delay pingPong="1" analog="0" syncLevel="7" />')
    lines.append(f"\t<defaultParams {attrs(merged(BASE_PARAMS, params))}>")
    lines.append(f"\t\t<envelope1 {attrs(merged(BASE_ENV1, env1))} />")
    lines.append(f"\t\t<envelope2 {attrs(merged(BASE_ENV2, env2))} />")
    lines.append("\t\t<patchCables>")
    for source, destination, amount in cables if cables is not None else BASE_CABLES:
        lines.append(
            f'\t\t\t<patchCable source="{source}" destination="{destination}" amount="{amount}" />'
        )
    lines.append("\t\t</patchCables>")
    lines.append(
        '\t\t<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />'
    )
    lines.append("\t</defaultParams>")
    lines.append("</sound>")
    text = "\n".join(lines) + "\n"
    if not json:
        (CORPUS / f"{name}.xml").write_text(text)
        return
    # The firmware saves presets as JSON. The upgrader (tools/deluge-upgrade) writes a document exactly as the firmware
    # would, so the case is authored here in the same terms as the others and converted.
    target = (CORPUS / name).parent
    target.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        source = pathlib.Path(scratch) / f"{pathlib.Path(name).name}.xml"
        source.write_text(text)
        subprocess.run([str(upgrader()), str(source), "--out", str(target)], check=True)


def upgrader() -> pathlib.Path:
    """The upgrader, built if it is not current."""
    tool = CORPUS.resolve().parents[3] / "tools" / "deluge-upgrade"
    subprocess.run(["cargo", "build", "--release", "--quiet"], cwd=tool, check=True)
    return tool / "target" / "release" / "deluge-upgrade"


# --- basic waveforms ---------------------------------------------------------
case("saw_basic")
case("sine_basic", osc1={"type": "sine"})
case("triangle_basic", osc1={"type": "triangle"})
case("square_basic", osc1={"type": "square"})
case("square_pw", osc1={"type": "square"}, params={"oscAPulseWidth": "0x30000000"})
# low pitch reaches the band-limited table pulse path (waveRenderingFunctionPulse)
# rather than the analytic square; has its own .notes at C1
case("square_pw_low", osc1={"type": "square"}, params={"oscAPulseWidth": "0x30000000"})
case(
    "analog_osc",
    osc1={"type": "analogSaw"},
    osc2={"type": "analogSquare"},
    params={"oscBVolume": "0x7FFFFFFF"},
)
case("wavefold_sine", osc1={"type": "sine"}, params={"waveFold": "0x40000000"})

# --- source combinations -----------------------------------------------------
case(
    "two_osc_detune",
    osc2={"type": "square", "transpose": "-12", "cents": "30"},
    params={"oscBVolume": "0x7FFFFFFF"},
)
case("noise_only", params={"oscAVolume": "0x80000000", "noiseVolume": "0x7FFFFFFF"})
case(
    "ringmod",
    sound={"mode": "ringmod"},
    osc1={"type": "sine"},
    osc2={"type": "square", "transpose": "7"},
)
case("unison_spread", unison={"num": "4", "detune": "30", "spread": "20"})

# --- FM ----------------------------------------------------------------------
case(
    "fm_2op",
    sound={"mode": "fm"},
    osc1={"type": "sine"},
    osc2={"type": "sine"},
    modulators=({}, {}),
    params={"modulator1Amount": "0x30000000"},
)
case(
    "fm_feedback",
    sound={"mode": "fm"},
    osc1={"type": "sine"},
    osc2={"type": "sine"},
    modulators=({"transpose": "12"}, {}),
    params={
        "modulator1Amount": "0x28000000",
        "modulator1Feedback": "0x20000000",
        "carrier1Feedback": "0x20000000",
    },
)

# --- envelopes / filters / modulation ----------------------------------------
case(
    "env_shape",
    env1={
        "attack": "0x20000000",
        "decay": "0x30000000",
        "sustain": "0x20000000",
        "release": "0x10000000",
    },
)
case(
    "lpf_24db_env2",
    params={"lpfFrequency": "0x10000000", "lpfResonance": "0x30000000"},
    env2={
        "attack": "0x18000000",
        "decay": "0x28000000",
        "sustain": "0x80000000",
        "release": "0x28000000",
    },
    cables=BASE_CABLES + [("envelope2", "lpfFrequency", "0x30000000")],
)
case(
    "svf_band",
    sound={"lpfMode": "SVF_Band"},
    params={"lpfFrequency": "0x20000000", "lpfResonance": "0x28000000"},
)
case(
    "lfo_vibrato",
    lfo1={"type": "sine"},
    params={"lfo1Rate": "0x30000000"},
    cables=BASE_CABLES + [("lfo1", "pitch", "0x08000000")],
)
case("velocity_dynamics", cables=[("velocity", "volume", "0x50000000")])

# --- filter modes, routes and transitions (JSON, as the firmware saves) -------
# The SVF's notch in the low-pass slot.
case(
    "filter_svf_notch",
    sound={"lpfMode": "SVF_Notch"},
    params={"lpfFrequency": "0x20000000", "lpfResonance": "0x28000000"},
    json=True,
)
# The parallel route with only its low-pass filter on: the dry signal is summed with the filtered one.
case(
    "filter_parallel_lpf_only",
    sound={"lpfMode": "12dB", "filterRoute": "PARA"},
    params={"lpfFrequency": "0x18000000", "lpfResonance": "0x30000000"},
    json=True,
)
# Each filter starts off (the low-pass fully open, the high-pass fully closed) and is switched on mid-note by its
# frequency's MIDI-follow CC (74, 81) in the case's note script, so it fades in from dry over its first blocks. The
# filter never ran before it fades in, so it starts from the cleared state of a new voice.
case(
    "filter_fade_lpf_ladder_hpf_svf",
    sound={"lpfMode": "24dB", "hpfMode": "SVF_Notch"},
    params={"lpfResonance": "0x30000000", "hpfResonance": "0x20000000"},
    json=True,
)
case(
    "filter_fade_lpf_svf_hpf_ladder",
    sound={"lpfMode": "SVF_Band", "hpfMode": "HPLadder"},
    params={"lpfResonance": "0x30000000", "hpfResonance": "0x20000000"},
    json=True,
)

# --- stereo voices -------------------------------------------------------------
# The cases under stereo/ render with headphones plugged in, so the app renders in stereo. A voice renders (and
# filters) both channels when a source of it is stereo: here, two unison voices spread across the field.
STEREO_UNISON = {"num": "2", "detune": "10", "spread": "40"}


def stereo_case(name, sound=None, params=None, **rest):
    case(
        f"stereo/{name}",
        sound=sound,
        params=params,
        unison=STEREO_UNISON,
        json=True,
        **rest,
    )


# Each route with both filters on, between them every ladder and SVF in each slot.
stereo_case(
    "lpf_12db_hpf_ladder_h2l",
    sound={"lpfMode": "12dB", "hpfMode": "HPLadder", "filterRoute": "H2L"},
    params={
        "lpfFrequency": "0x18000000",
        "lpfResonance": "0x30000000",
        "hpfFrequency": "0xC0000000",
        "hpfResonance": "0x20000000",
    },
)
stereo_case(
    "lpf_24db_hpf_svf_band_l2h",
    sound={"lpfMode": "24dB", "hpfMode": "SVF_Band", "filterRoute": "L2H"},
    params={
        "lpfFrequency": "0x18000000",
        "lpfResonance": "0x30000000",
        "hpfFrequency": "0xC0000000",
        "hpfResonance": "0x20000000",
    },
)
stereo_case(
    "lpf_svf_band_hpf_svf_notch_parallel",
    sound={"lpfMode": "SVF_Band", "hpfMode": "SVF_Notch", "filterRoute": "PARA"},
    params={
        "lpfFrequency": "0x20000000",
        "lpfResonance": "0x28000000",
        "hpfFrequency": "0xC0000000",
        "hpfResonance": "0x20000000",
    },
)
# The SVF notch alone in the parallel route: the dry signal is summed with it.
stereo_case(
    "lpf_svf_notch_parallel",
    sound={"lpfMode": "SVF_Notch", "filterRoute": "PARA"},
    params={"lpfFrequency": "0x20000000", "lpfResonance": "0x28000000"},
)
# The drive filter high and resonant enough to oversample.
stereo_case(
    "lpf_drive_oversampled",
    sound={"lpfMode": "24dBDrive"},
    params={"lpfFrequency": "0x60000000", "lpfResonance": "0x60000000"},
)
# Both filters switched on mid-note by the note script (the mono case's), so each fades in from dry.
stereo_case(
    "filter_fade_lpf_ladder_hpf_svf",
    sound={"lpfMode": "24dB", "hpfMode": "SVF_Notch"},
    params={"lpfResonance": "0x30000000", "hpfResonance": "0x20000000"},
)
# The delay in ping-pong, which alternates its echoes between the channels only in stereo.
stereo_case(
    "delay_pingpong",
    params={"delayRate": "0x00000000", "delayFeedback": "0x00000000"},
)

# --- note-event behaviours (paired with dedicated .notes scripts) -------------
case("poly_chord")
case("pitch_bend")
case("mono_legato", sound={"polyphonic": "legato"})
case("porta", params={"portamento": "0x30000000"})

print(f"wrote {len(list(CORPUS.glob('*.xml')))} presets to {CORPUS}")
