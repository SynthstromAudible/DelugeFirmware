# All parameters

This folder contains the three setup `.json`-files

- `Deluge P1.json`
- `Deluge P2.json`
- `Deluge P3.json`

that together cover all MIDI Follow parameters (and their CC#) described in [*Appendix A - List of Deluge Parameters with Default Mapped CC's*](../../../../docs/features/midi_follow_mode.md#appendix-a---list-of-deluge-parameters-with-default-mapped-ccs).

The philosophy behind these three templates is to surface all available parameters so users can explore which parameters they might want to consolidate in within, say, a single, more performance-oriented setup.

Below is a summary table, where `Position` is the knob's position and `Page` is a knob's page in the corresponding setup file.[^1]

| Parameter | Position | Page | Setup | CC# |
|-----------|----------|------|-------|----|
| **Master** | 1 | 1 | Deluge P1 | 7 |
| **Panning** | 2 | 1 | Deluge P1 | 10 |
| **Pitch** | 3 | 1 | Deluge P1 | 3 |
| **Portamento** | 4 | 1 | Deluge P1 | 5 |
| **LPF Cutoff** | 5 | 1 | Deluge P1 | 74 |
| **LPF Resonanc** | 6 | 1 | Deluge P1 | 71 |
| **HPF Cutoff** | 7 | 1 | Deluge P1 | 81 |
| **HPF Resonan** | 8 | 1 | Deluge P1 | 82 |
| **OSC1 Level** | 9 | 2 | Deluge P1 | 21 |
| **OSC1 Pitch** | 10 | 2 | Deluge P1 | 12 |
| **OSC1 PW** | 11 | 2 | Deluge P1 | 23 |
| **OSC1 WavePos** | 12 | 2 | Deluge P1 | 25 |
| **OSC1 Feedb** | 13 | 2 | Deluge P1 | 24 |
| **FM1 Level** | 14 | 2 | Deluge P1 | 54 |
| **FM1 Pitch** | 15 | 2 | Deluge P1 | 14 |
| **FM1 Feedback** | 16 | 2 | Deluge P1 | 55 |
| **OSC2 Level** | 17 | 3 | Deluge P1 | 26 |
| **OSC2 Pitch** | 18 | 3 | Deluge P1 | 13 |
| **OSC2 PW** | 19 | 3 | Deluge P1 | 28 |
| **OSC2 WavePos** | 20 | 3 | Deluge P1 | 30 |
| **OSC2 Feedb** | 21 | 3 | Deluge P1 | 29 |
| **FM2 Level** | 22 | 3 | Deluge P1 | 56 |
| **FM2 Pitch** | 23 | 3 | Deluge P1 | 15 |
| **FM2 Feedback** | 24 | 3 | Deluge P1 | 57 |
| **ENV1 Attack** | 25 | 4 | Deluge P1 | 73 |
| **ENV1 Decay** | 26 | 4 | Deluge P1 | 75 |
| **ENV1 Sustain** | 27 | 4 | Deluge P1 | 76 |
| **ENV1 Release** | 28 | 4 | Deluge P1 | 72 |
| **ENV2 Attack** | 29 | 4 | Deluge P1 | 77 |
| **ENV2 Decay** | 30 | 4 | Deluge P1 | 78 |
| **ENV2 Sustain** | 31 | 4 | Deluge P1 | 79 |
| **ENV2 Release** | 32 | 4 | Deluge P1 | 80 |
| **EQ-Bass Freq** | 1 | 1 | Deluge P2 | 84 |
| **EQ-Bass Gain** | 2 | 1 | Deluge P2 | 86 |
| **EQ-Treb Freq** | 3 | 1 | Deluge P2 | 85 |
| **EQ-Treb Gain** | 4 | 1 | Deluge P2 | 87 |
| **Noise** | 5 | 1 | Deluge P2 | 41 |
| **REVERB Amoun** | 6 | 1 | Deluge P2 | 91 |
| **DELAY Amount** | 7 | 1 | Deluge P2 | 52 |
| **DELAY Rate** | 8 | 1 | Deluge P2 | 53 |
| **MODFX Rate** | 9 | 2 | Deluge P2 | 16 |
| **MODFX Depth** | 10 | 2 | Deluge P2 | 93 |
| **MODFX Feedb** | 11 | 2 | Deluge P2 | 17 |
| **MODFX Offset** | 12 | 2 | Deluge P2 | 18 |
| **Wavefold** | 13 | 2 | Deluge P2 | 19 |
| **Bitcrush** | 14 | 2 | Deluge P2 | 62 |
| **Decimation** | 15 | 2 | Deluge P2 | 63 |
|  | 16 | 2 | Deluge P2 |  |
| **LFO1 Rate** | 17 | 3 | Deluge P2 | 58 |
| **LFO2 Rate** | 18 | 3 | Deluge P2 | 59 |
| **LPF Morph** | 19 | 3 | Deluge P2 | 70 |
| **HPF Morph** | 20 | 3 | Deluge P2 | 83 |
| **STUTTER Rate** | 21 | 3 | Deluge P2 | 20 |
| **SIDECH Level** | 22 | 3 | Deluge P2 | 61 |
| **SIDECH Shape** | 23 | 3 | Deluge P2 | 60 |
| **Comp Tresh** | 24 | 3 | Deluge P2 | 27 |
| **ENV3 Attack** | 25 | 4 | Deluge P2 | 102 |
| **ENV3 Decay** | 26 | 4 | Deluge P2 | 103 |
| **ENV3 Sustain** | 27 | 4 | Deluge P2 | 104 |
| **ENV3 Release** | 28 | 4 | Deluge P2 | 105 |
| **ENV4 Attack** | 29 | 4 | Deluge P2 | 106 |
| **ENV4 Decay** | 30 | 4 | Deluge P2 | 107 |
| **ENV4 Sustain** | 31 | 4 | Deluge P2 | 108 |
| **ENV4 Release** | 32 | 4 | Deluge P2 | 109 |
| **ARP Rate** | 1 | 1 | Deluge P3 | 51 |
| **ARP Gate** | 2 | 1 | Deluge P3 | 50 |
| **ARP GateSpr** | 3 | 1 | Deluge P3 | 40 |
| **ARP OctSpr** | 4 | 1 | Deluge P3 | 39 |
| **ARP VelSpr** | 5 | 1 | Deluge P3 | 37 |
| **ARP Rhythm** | 6 | 1 | Deluge P3 | 42 |
| **ARP SeqLen** | 7 | 1 | Deluge P3 | 43 |
| **ARP RevProb** | 8 | 1 | Deluge P3 | 36 |
| **ARP ChoPoly** | 9 | 2 | Deluge P3 | 44 |
| **ARP ChoProb** | 10 | 2 | Deluge P3 | 48 |
| **ARP RatchAm** | 11 | 2 | Deluge P3 | 45 |
| **ARP RatchPr** | 12 | 2 | Deluge P3 | 49 |
| **ARP NoteProb** | 13 | 2 | Deluge P3 | 46 |
| **ARP BassProb** | 14 | 2 | Deluge P3 | 47 |
|  | 15 | 2 | Deluge P3 |  |
|  | 16 | 2 | Deluge P3 |  |
|  | 17 | 3 | Deluge P3 |  |
|  | 18 | 3 | Deluge P3 |  |
|  | 19 | 3 | Deluge P3 |  |
|  | 20 | 3 | Deluge P3 |  |
|  | 21 | 3 | Deluge P3 |  |
|  | 22 | 3 | Deluge P3 |  |
|  | 23 | 3 | Deluge P3 |  |
|  | 24 | 3 | Deluge P3 |  |
|  | 25 | 4 | Deluge P3 |  |
|  | 26 | 4 | Deluge P3 |  |
|  | 27 | 4 | Deluge P3 |  |
|  | 28 | 4 | Deluge P3 |  |
|  | 29 | 4 | Deluge P3 |  |
|  | 30 | 4 | Deluge P3 |  |
|  | 31 | 4 | Deluge P3 |  |
|  | 32 | 4 | Deluge P3 |  |

MIDI channel: 15


[^1]: Unassigned knobs show up in the table with no parameter name and CC#. The table was automatically produced by calling

    ```python
    python3 roto-control_summarize.py "all_parameters/Deluge P1.json" "all_parameters/Deluge P2.json" "all_parameters/Deluge P3.json"
    ```

    from the `roto_control` directory one level above. So barring any errors in the script, the mappings summarized in the table should, by construction, match with the actual setup information encoded in the respective `.json` files.
