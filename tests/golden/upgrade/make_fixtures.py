#!/usr/bin/env python3
"""Writes the upgrader's legacy fixtures: each a current-form preset given one old form (or one ordering of two), and
stamped so that only the conversion it pins applies to it. Their round-trip hashes, recorded with a firmware that
still reads the old forms, are in tests/golden/upgrade_fixtures.json (tests/golden/upgrade_run.py --record).

    python3 tests/golden/upgrade/make_fixtures.py
"""

import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
PRESETS = HERE.parents[1] / "integration" / "golden" / "presets"
SYNTH = (PRESETS / "community" / "Basic Bass.XML").read_text()


def restamp(text: str, stamp: str) -> str:
    return re.sub(
        r'firmwareVersion="[^"]*"', f'firmwareVersion="{stamp}"', text, count=1
    )


def without(text: str, *attributes: str) -> str:
    for name in attributes:
        text = re.sub(rf'\n\s*{name}="[^"]*"', "", text, count=1)
    return text


def element_span(text: str, name: str) -> tuple[int, int]:
    """Where the first `name` element starts and ends: a self-closing tag, or through its closing tag."""
    start = re.search(rf"<{name}\b", text).start()
    tag_end = text.index(">", start)
    end = (
        tag_end + 1
        if text[tag_end - 1] == "/"
        else text.index(f"</{name}>", start) + len(f"</{name}>")
    )
    return start, end


def without_element(text: str, name: str) -> str:
    start, end = element_span(text, name)
    while start > 0 and text[start - 1] in " \t":
        start -= 1
    if start > 0 and text[start - 1] == "\n":
        start -= 1
    return text[:start] + text[end:]


def with_attributes(text: str, attributes: str) -> str:
    """Adds attributes to the root <sound>, ahead of its own."""
    return text.replace("<sound\n", f"<sound\n\t{attributes}\n", 1)


def after(text: str, element: str, added: str) -> str:
    """Adds elements after the first `element` element closes."""
    _, end = element_span(text, element)
    return text[:end] + "\n\t" + added + text[end:]


def before(text: str, element: str, added: str) -> str:
    at = text.index(f"<{element}")
    return text[:at] + added + "\n\t" + text[at:]


OFFICIAL = restamp(SYNTH, "4.1.4-alpha")
NO_PHASES = re.sub(r'\n\s*retrigPhase="[^"]*"', "", OFFICIAL)


def with_param(text: str, name: str, value: str) -> str:
    """Sets one defaultParams attribute."""
    return re.sub(rf'(\n\t\t{name}=)"[^"]*"', rf'\1"{value}"', text, count=1)


def without_cable(text: str, source: str) -> str:
    """Removes the first cable from `source`."""
    return re.sub(rf'\n\s*<patchCable\s+source="{source}"[^>]*/>', "", text, count=1)


def with_cables(text: str, cables: str) -> str:
    """Adds cables at the end of the sound's patchCables."""
    return text.replace("</patchCables>", cables + "\n\t\t</patchCables>", 1)


def cable(source: str, destination: str, amount: str, extra: str = "") -> str:
    return f'\n\t\t\t<patchCable source="{source}" destination="{destination}" amount="{amount}"{extra} />'


PRE_4 = restamp(SYNTH, "3.1.0")
NO_EXPRESSION = without_cable(without_cable(PRE_4, "aftertouch"), "y")
FM_NO_EXPRESSION = re.sub(
    r'hpfMode="[^"]*"',
    'hpfMode="Off"',
    re.sub(
        r'lpfMode="[^"]*"',
        'lpfMode="Off"',
        NO_EXPRESSION.replace('mode="subtractive"', 'mode="fm"', 1),
        count=1,
    ),
    count=1,
)


# What official firmware read each form already gone from the firmware as, written in the current form: the recorded
# hash of a fixture with an oracle is its oracle's.
ORACLES = {
    "sound/lpf_mode_fx_type.synth.XML": with_attributes(
        without(OFFICIAL, "modFXType", "lpfMode"),
        'modFXType="flanger"\n\tlpfMode="12dB"',
    ),
    "sound/delay_rate_then_feedback.synth.XML": with_param(
        OFFICIAL, "delayFeedback", "0x22000000"
    ),
    "sound/delay_feedback_then_rate.synth.XML": with_param(
        OFFICIAL, "delayFeedback", "0x11000000"
    ),
}

FIXTURES = {
    # Pre-mid-2016 oscillators, read after the params (their volumes win) and before them (the params' win).
    "sound/old_oscillators_after_params.synth.XML": after(
        without_element(without_element(OFFICIAL, "osc1"), "osc2"),
        "defaultParams",
        '<oscillatorA type="square" note="64" volume="0x40000000" phaseWidth="0x20000000" />\n\t'
        '<oscillatorB type="saw" transpose="3" cents="-5" volume="0x30000000" phaseWidth="0x10000000" />',
    ),
    "sound/old_oscillators_before_params.synth.XML": before(
        without_element(without_element(OFFICIAL, "osc1"), "osc2"),
        "defaultParams",
        '<oscillatorA type="triangle" note="50" volume="0x40000000" />\n\t<oscillatorB type="sine" />',
    ),
    "sound/ringmod_then_mode.synth.XML": with_attributes(
        without(OFFICIAL, "mode"), 'ringMod="1"\n\tmode="fm"'
    ),
    "sound/mode_then_ringmod.synth.XML": with_attributes(
        without(OFFICIAL, "mode"), 'mode="fm"\n\tringMod="1"'
    ),
    "sound/ringmod_off.synth.XML": with_attributes(OFFICIAL, 'ringMod="0"'),
    "sound/early_sample.synth.XML": after(
        without_element(OFFICIAL, "osc1"),
        "defaultParams",
        "<fileName>SAMPLES/EARLY.WAV</fileName>\n\t<continuous>1</continuous>\n\t<reversed>1</reversed>\n\t"
        '<zone startSeconds="1" startMilliseconds="250" endSeconds="2" endMilliseconds="5" />',
    ),
    "sound/early_sample_then_params.synth.XML": before(
        without_element(OFFICIAL, "osc1"),
        "defaultParams",
        "<fileName>SAMPLES/EARLY.WAV</fileName>",
    ),
    "sound/sound_level_params.synth.XML": after(
        OFFICIAL,
        "defaultParams",
        "<noiseVolume>0x10000000</noiseVolume>\n\t<portamento>0x20000000</portamento>\n\t"
        "<oscAPitchAdjust>0x01000000</oscAPitchAdjust>\n\t<oscBPitchAdjust>0x02000000</oscBPitchAdjust>\n\t"
        "<mod1PitchAdjust>0x03000000</mod1PitchAdjust>\n\t<mod2PitchAdjust>0x04000000</mod2PitchAdjust>\n\t"
        "<volume>0x30000000</volume>\n\t<pan>0x05000000</pan>\n\t<pitchAdjust>0x06000000</pitchAdjust>\n\t"
        "<reverbAmount>0x07000000</reverbAmount>\n\t<waveFold>0x08000000</waveFold>\n\t"
        '<modulator1 volume="0x09000000" />\n\t<modulator2 volume="0x0A000000" />\n\t'
        '<lfo1 rate="0x0B000000" />\n\t<lfo2 rate="0x0C000000" />\n\t'
        '<lpf frequency="0x0D000000" resonance="0x0E000000" morph="0x0F000000" />\n\t'
        '<hpf frequency="0x11000000" resonance="0x12000000" morph="0x13000000" />\n\t'
        '<envelope1 attack="0x14000000" decay="0x15000000" sustain="0x16000000" release="0x17000000" />\n\t'
        '<envelope2 attack="0x18000000" decay="0x19000000" sustain="0x1A000000" release="0x1B000000" />',
    ),
    "sound/sound_level_params_before_params.synth.XML": before(
        OFFICIAL,
        "defaultParams",
        "<volume>0x30000000</volume>\n\t<pan>0x05000000</pan>",
    ),
    "sound/filter_status_off.synth.XML": after(
        OFFICIAL,
        "defaultParams",
        '<lpf status="0" frequency="0x0D000000" />\n\t<hpf status="0" />',
    ),
    "sound/old_fx.synth.XML": after(
        OFFICIAL,
        "defaultParams",
        '<fx rate="0x10000000" depth="0x20000000" feedback="1073741824" offset="4194304" />',
    ),
    "sound/lpf_mode_fx_type.synth.XML": after(
        without(OFFICIAL, "modFXType", "lpfMode"),
        "defaultParams",
        '<lpf mode="12dB" />\n\t<fx type="flanger" />',
    ),
    "sound/lpf_mode_fx_type_with_keys.synth.XML": after(
        OFFICIAL, "defaultParams", '<lpf mode="12dB" />\n\t<fx type="flanger" />'
    ),
    "sound/delay_rate_then_feedback.synth.XML": after(
        without_element(OFFICIAL, "delay"),
        "defaultParams",
        '<delay rate="0x11000000" feedback="0x22000000" pingPong="1" analog="0" syncLevel="7" />',
    ),
    "sound/delay_feedback_then_rate.synth.XML": after(
        without_element(OFFICIAL, "delay"),
        "defaultParams",
        '<delay feedback="0x22000000" rate="0x11000000" pingPong="1" analog="0" syncLevel="7" />',
    ),
    "sound/oscillator_reset_off.synth.XML": with_attributes(
        NO_PHASES, 'oscillatorReset="0"'
    ),
    "sound/oscillator_reset_then_phase.synth.XML": before(
        OFFICIAL, "osc1", "<oscillatorReset>0</oscillatorReset>"
    ),
    "sound/oscillator_reset_on.synth.XML": with_attributes(
        OFFICIAL, 'oscillatorReset="1"'
    ),
    "sound/polyphony_0.synth.XML": OFFICIAL.replace(
        'polyphonic="mono"', 'polyphonic="0"', 1
    ),
    "sound/polyphony_1.synth.XML": OFFICIAL.replace(
        'polyphonic="mono"', 'polyphonic="1"', 1
    ),
    "sound/polyphony_2.synth.XML": OFFICIAL.replace(
        'polyphonic="mono"', 'polyphonic="2"', 1
    ),
}


FIXTURES |= {
    # Pre-3.2 "range" cables: each controls the depth of the last rangeAdjustable cable; with none, nothing.
    "cables/range_cables.synth.XML": with_cables(
        OFFICIAL,
        cable("lfo1", "range", "0x10000000")
        + cable("envelope1", "pitch", "0x20000000", ' rangeAdjustable="1"')
        + cable("velocity", "range", "0x30000000")
        + cable("note", "lpfFrequency", "0x05000000", ' rangeAdjustable="1"')
        + cable("aftertouch", "range", "0x40000000"),
    ),
    "cables/range_cables_none_adjustable.synth.XML": with_cables(
        OFFICIAL,
        cable("lfo1", "range", "0x10000000") + cable("random", "range", "0x30000000"),
    ),
    "cables/range_mod_knob.synth.XML": re.sub(
        r"(<modKnobs>(?:\s*<modKnob[^>]*/>){12}\s*)<modKnob[^>]*/>",
        r'\1<modKnob controlsParam="range" />',
        OFFICIAL,
        count=1,
    ),
    "cables/range_midi_knob.synth.XML": OFFICIAL.replace(
        "<midiKnobs>",
        '<midiKnobs>\n\t\t<midiKnob channel="3" ccNumber="20" relative="0" controlsParam="range" />',
        1,
    ),
    # Before official 4.0 a sound was given aftertouch to volume and Y to its filter (FM: its first modulator).
    "cables/default_expression.synth.XML": NO_EXPRESSION,
    "cables/default_expression_fm.synth.XML": FM_NO_EXPRESSION,
    "cables/default_expression_with_x.synth.XML": with_cables(
        NO_EXPRESSION, cable("x", "pan", "0x10000000")
    ),
    "cables/default_expression_with_own.synth.XML": PRE_4,
}


MONO = (PRESETS / "sample" / "mono.xml").read_text()


def ms_zone(start: int, end: int, reversed_: bool = False) -> str:
    """The sample case `mono`, stamped as official 2.0 with its zone in milliseconds only."""
    text = MONO.replace('firmwareVersion="c1.3.0"', 'firmwareVersion="2.0.0"', 1)
    text = text.replace('reversed="0"', f'reversed="{int(reversed_)}"', 1)
    return text.replace(
        '<zone startSamplePos="0" endSamplePos="4096" />',
        f'<zone startMilliseconds="{start}" endMilliseconds="{end}" />',
        1,
    )


# The golden render cases presets/upgrade/<name> were recorded from these originals (the renderer stages a case's .wav
# as SAMPLES/WT.WAV): before official 2.1 a zone in milliseconds became sample positions once its sample loaded.
RENDER_CASES = {
    "ms_zone": ms_zone(20, 80),
    "ms_zone_end_only": ms_zone(0, 50),
    "ms_zone_reversed": ms_zone(10, 60, reversed_=True),
    "ms_zone_overshoot": ms_zone(5, 93),
    "ms_zone_far_overshoot": ms_zone(5, 100),
}


SYNTHETIC = HERE.parent / "synthetic"

# Songs stamped as old official firmware: 1.4.0 (every song rule from before 1.5 up to community 1.3) and 1.1.0 (the
# pre-1.2 rules too).
FIXTURES |= {
    f"songs/{song.stem}_{stamp.replace('.', '_')}.song.XML": restamp(
        song.read_text(), stamp
    )
    for song in sorted(SYNTHETIC.glob("*.XML"))
    for stamp in ("1.4.0", "1.1.0")
}


def main() -> None:
    for name, text in FIXTURES.items():
        path = HERE / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    # The golden corpus holds these cases upgraded (presets/upgrade/*.json, rendering as their records); the originals
    # they were recorded from are kept here.
    render = HERE / "render"
    render.mkdir(exist_ok=True)
    for name, text in RENDER_CASES.items():
        (render / f"{name}.xml").write_text(text)
        (render / f"{name}.wav").write_bytes(
            (PRESETS / "sample" / "mono.wav").read_bytes()
        )
    for name, text in ORACLES.items():
        (HERE / name.replace(".XML", ".oracle.xml")).write_text(text)
    print(f"wrote {len(FIXTURES)} fixtures")


if __name__ == "__main__":
    main()
