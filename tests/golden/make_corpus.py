#!/usr/bin/env python3
"""Authoring tool for the golden-master corpus: emits the preset XMLs in corpus/.

The committed XMLs are the source of truth (run.py reads only those); this
script exists so the family of presets stays consistent when extending the
corpus. Re-run it after editing and commit the changed XMLs:

    python3 tests/golden/make_corpus.py

Preset values are q31 hex strings as serialized by Sound::writeParamsToFile.
Key reference points: 0x7FFFFFFF = max, 0x80000000 = min/off, 0x00000000 =
center/neutral, 0x4CCCCCA8 = the default synth volume.
"""

import pathlib

CORPUS = pathlib.Path(__file__).parent / "corpus"

# Attributes of <sound>. inputMidiChannel="0" (MIDI channel 1) is what binds the
# loaded preset to the harness's scripted DIN input — every case needs it.
BASE_SOUND_ATTRS = {
    "firmwareVersion": "c1.3.0",
    "earliestCompatibleFirmware": "4.1.0-alpha",
    "polyphonic": "poly",
    "inputMidiChannel": "0",
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

BASE_ENV1 = {"attack": "0x80000000", "decay": "0xE6666654", "sustain": "0x7FFFFFFF", "release": "0x80000000"}
BASE_ENV2 = {"attack": "0xE6666654", "decay": "0xE6666654", "sustain": "0xFFFFFFE9", "release": "0xE6666654"}
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
        lines.append(f"\t<modulator1 {attrs(merged({'transpose': '0', 'cents': '0', 'retrigPhase': '-1'}, mod1))} />")
        lines.append(
            f"\t<modulator2 {attrs(merged({'transpose': '0', 'cents': '0', 'retrigPhase': '-1', 'toModulator1': '0'}, mod2))} />"
        )
    lines.append(f"\t<unison {attrs(merged(BASE_UNISON, unison))} />")
    lines.append('\t<delay pingPong="1" analog="0" syncLevel="7" />')
    lines.append(f"\t<defaultParams {attrs(merged(BASE_PARAMS, params))}>")
    lines.append(f"\t\t<envelope1 {attrs(merged(BASE_ENV1, env1))} />")
    lines.append(f"\t\t<envelope2 {attrs(merged(BASE_ENV2, env2))} />")
    lines.append("\t\t<patchCables>")
    for source, destination, amount in (cables if cables is not None else BASE_CABLES):
        lines.append(f'\t\t\t<patchCable source="{source}" destination="{destination}" amount="{amount}" />')
    lines.append("\t\t</patchCables>")
    lines.append(
        '\t\t<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />'
    )
    lines.append("\t</defaultParams>")
    lines.append("</sound>")
    (CORPUS / f"{name}.xml").write_text("\n".join(lines) + "\n")


# --- basic waveforms ---------------------------------------------------------
case("saw_basic")
case("sine_basic", osc1={"type": "sine"})
case("triangle_basic", osc1={"type": "triangle"})
case("square_basic", osc1={"type": "square"})
case("square_pw", osc1={"type": "square"}, params={"oscAPulseWidth": "0x30000000"})
case("analog_osc", osc1={"type": "analogSaw"}, osc2={"type": "analogSquare"}, params={"oscBVolume": "0x7FFFFFFF"})
case("wavefold_sine", osc1={"type": "sine"}, params={"waveFold": "0x40000000"})

# --- source combinations -----------------------------------------------------
case(
    "two_osc_detune",
    osc2={"type": "square", "transpose": "-12", "cents": "30"},
    params={"oscBVolume": "0x7FFFFFFF"},
)
case("noise_only", params={"oscAVolume": "0x80000000", "noiseVolume": "0x7FFFFFFF"})
case("ringmod", sound={"mode": "ringmod"}, osc1={"type": "sine"}, osc2={"type": "square", "transpose": "7"})
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
    env1={"attack": "0x20000000", "decay": "0x30000000", "sustain": "0x20000000", "release": "0x10000000"},
)
case(
    "lpf_24db_env2",
    params={"lpfFrequency": "0x10000000", "lpfResonance": "0x30000000"},
    env2={"attack": "0x18000000", "decay": "0x28000000", "sustain": "0x80000000", "release": "0x28000000"},
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

# --- note-event behaviours (paired with dedicated .notes scripts) -------------
case("poly_chord")
case("pitch_bend")
case("mono_legato", sound={"polyphonic": "legato"})
case("porta", params={"portamento": "0x30000000"})

print(f"wrote {len(list(CORPUS.glob('*.xml')))} presets to {CORPUS}")
