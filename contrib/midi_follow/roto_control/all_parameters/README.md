# All parameters (MIDI Follow Mode Template for Melbourne Instruments ROTO-CONTROL)

This folder contains the three setup `.json`-files

- `D_1.json`
- `D_2.json`
- `D_3.json`

that together cover all MIDI Follow parameters (and their CC#) described in [*Appendix A - List of Deluge Parameters with Default Mapped CC's*](../../../../website/src/content/docs/features/midi_follow_mode.md#appendix-a---list-of-deluge-parameters-with-default-mapped-ccs).

The philosophy behind these three templates is to surface all available parameters so users can explore which parameters they might want to consolidate within, say, a single, more performance-oriented setup.

Below is a summary table, where `Position` is the knob's position across the 32 knobs and `Page` is the knob's page (1st, 2nd, 3rd, or 4th) in the setup.[^1]

| Parameter | Position | Page | Setup | CC# |
|-----------|----------|------|-------|----|
| **Master Vol** | 1 | 1 | D_1 | 7 |
| **Panning** | 2 | 1 | D_1 | 10 |
| **Master Pitch** | 3 | 1 | D_1 | 3 |
| **Portamento** | 4 | 1 | D_1 | 5 |
| **LPF Cutoff** | 5 | 1 | D_1 | 74 |
| **LPF Resonanc** | 6 | 1 | D_1 | 71 |
| **HPF Cutoff** | 7 | 1 | D_1 | 81 |
| **HPF Resonan** | 8 | 1 | D_1 | 82 |
| **OSC1 Level** | 9 | 2 | D_1 | 21 |
| **OSC1 Pitch** | 10 | 2 | D_1 | 12 |
| **OSC1 PW** | 11 | 2 | D_1 | 23 |
| **OSC1 WavePos** | 12 | 2 | D_1 | 25 |
| **OSC1 Feedb** | 13 | 2 | D_1 | 24 |
| **FM1 Level** | 14 | 2 | D_1 | 54 |
| **FM1 Pitch** | 15 | 2 | D_1 | 14 |
| **FM1 Feedback** | 16 | 2 | D_1 | 55 |
| **OSC2 Level** | 17 | 3 | D_1 | 26 |
| **OSC2 Pitch** | 18 | 3 | D_1 | 13 |
| **OSC2 PW** | 19 | 3 | D_1 | 28 |
| **OSC2 WavePos** | 20 | 3 | D_1 | 30 |
| **OSC2 Feedb** | 21 | 3 | D_1 | 29 |
| **FM2 Level** | 22 | 3 | D_1 | 56 |
| **FM2 Pitch** | 23 | 3 | D_1 | 15 |
| **FM2 Feedback** | 24 | 3 | D_1 | 57 |
| **ENV1 Attack** | 25 | 4 | D_1 | 73 |
| **ENV1 Decay** | 26 | 4 | D_1 | 75 |
| **ENV1 Sustain** | 27 | 4 | D_1 | 76 |
| **ENV1 Release** | 28 | 4 | D_1 | 72 |
| **ENV2 Attack** | 29 | 4 | D_1 | 77 |
| **ENV2 Decay** | 30 | 4 | D_1 | 78 |
| **ENV2 Sustain** | 31 | 4 | D_1 | 79 |
| **ENV2 Release** | 32 | 4 | D_1 | 80 |
| **EQ-Bass Freq** | 1 | 1 | D_2 | 84 |
| **EQ-Bass Gain** | 2 | 1 | D_2 | 86 |
| **EQ-Treb Freq** | 3 | 1 | D_2 | 85 |
| **EQ-Treb Gain** | 4 | 1 | D_2 | 87 |
| **Noise** | 5 | 1 | D_2 | 41 |
| **REVERB Amoun** | 6 | 1 | D_2 | 91 |
| **DELAY Amount** | 7 | 1 | D_2 | 52 |
| **DELAY Rate** | 8 | 1 | D_2 | 53 |
| **MODFX Rate** | 9 | 2 | D_2 | 16 |
| **MODFX Depth** | 10 | 2 | D_2 | 93 |
| **MODFX Feedb** | 11 | 2 | D_2 | 17 |
| **MODFX Offset** | 12 | 2 | D_2 | 18 |
| **Wavefold** | 13 | 2 | D_2 | 19 |
| **Bitcrush** | 14 | 2 | D_2 | 62 |
| **Decimation** | 15 | 2 | D_2 | 63 |
|  | 16 | 2 | D_2 |  |
| **LFO1 Rate** | 17 | 3 | D_2 | 58 |
| **LFO2 Rate** | 18 | 3 | D_2 | 59 |
| **LPF Morph** | 19 | 3 | D_2 | 70 |
| **HPF Morph** | 20 | 3 | D_2 | 83 |
| **STUTTER Rate** | 21 | 3 | D_2 | 20 |
| **SIDECH Level** | 22 | 3 | D_2 | 61 |
| **SIDECH Shape** | 23 | 3 | D_2 | 60 |
| **Comp Tresh** | 24 | 3 | D_2 | 27 |
| **ENV3 Attack** | 25 | 4 | D_2 | 102 |
| **ENV3 Decay** | 26 | 4 | D_2 | 103 |
| **ENV3 Sustain** | 27 | 4 | D_2 | 104 |
| **ENV3 Release** | 28 | 4 | D_2 | 105 |
| **ENV4 Attack** | 29 | 4 | D_2 | 106 |
| **ENV4 Decay** | 30 | 4 | D_2 | 107 |
| **ENV4 Sustain** | 31 | 4 | D_2 | 108 |
| **ENV4 Release** | 32 | 4 | D_2 | 109 |
| **ARP Rate** | 1 | 1 | D_3 | 51 |
| **ARP Gate** | 2 | 1 | D_3 | 50 |
| **ARP GateSpr** | 3 | 1 | D_3 | 40 |
| **ARP OctSpr** | 4 | 1 | D_3 | 39 |
| **ARP VelSpr** | 5 | 1 | D_3 | 37 |
| **ARP Rhythm** | 6 | 1 | D_3 | 42 |
| **ARP SeqLen** | 7 | 1 | D_3 | 43 |
| **ARP RevProb** | 8 | 1 | D_3 | 36 |
| **ARP ChoPoly** | 9 | 2 | D_3 | 44 |
| **ARP ChoProb** | 10 | 2 | D_3 | 48 |
| **ARP RatchAm** | 11 | 2 | D_3 | 45 |
| **ARP RatchPr** | 12 | 2 | D_3 | 49 |
| **ARP NoteProb** | 13 | 2 | D_3 | 46 |
| **ARP BassProb** | 14 | 2 | D_3 | 47 |
|  | 15 | 2 | D_3 |  |
|  | 16 | 2 | D_3 |  |
|  | 17 | 3 | D_3 |  |
|  | 18 | 3 | D_3 |  |
|  | 19 | 3 | D_3 |  |
|  | 20 | 3 | D_3 |  |
|  | 21 | 3 | D_3 |  |
|  | 22 | 3 | D_3 |  |
|  | 23 | 3 | D_3 |  |
|  | 24 | 3 | D_3 |  |
|  | 25 | 4 | D_3 |  |
|  | 26 | 4 | D_3 |  |
|  | 27 | 4 | D_3 |  |
|  | 28 | 4 | D_3 |  |
|  | 29 | 4 | D_3 |  |
|  | 30 | 4 | D_3 |  |
|  | 31 | 4 | D_3 |  |
|  | 32 | 4 | D_3 |  |

MIDI channel: 15

[^1]: Unassigned knobs show up in the table with no parameter name and CC#.

    The table was automatically produced by calling

    ```python
    python3 roto-control_summarize.py "all_parameters/D_1.json" "all_parameters/D_2.json" "all_parameters/D_3.json"
    ```

    from the `roto_control` directory one level above.
