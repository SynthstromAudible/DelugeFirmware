# All parameters

This folder contains the three setup `.json`-files

- `all_parameters1.json`
- `all_parameters2.json`
- `all_parameters3.json`

that together cover all MIDI Follow parameters (and their CC#) described in [*Appendix A - List of Deluge Parameters with Default Mapped CC's*](../../../../docs/features/midi_follow_mode.md#appendix-a---list-of-deluge-parameters-with-default-mapped-ccs).

The philosophy behind these three templates is to surface all available parameters so users can explore which parameters they might want to consolidate within, say, a single, more performance-oriented setup.

Below is a summary table, where `Position` is the knob's position across the 32 knobs and `Page` is the knob's page (1st, 2nd, 3rd, or 4th) in the setup.[^1]

| Parameter | Position | Page | Setup | CC# |
|-----------|----------|------|-------|----|
| **Master** | 1 | 1 | all_parameters1 | 7 |
| **Panning** | 2 | 1 | all_parameters1 | 10 |
| **Pitch** | 3 | 1 | all_parameters1 | 3 |
| **Portamento** | 4 | 1 | all_parameters1 | 5 |
| **LPF Cutoff** | 5 | 1 | all_parameters1 | 74 |
| **LPF Resonanc** | 6 | 1 | all_parameters1 | 71 |
| **HPF Cutoff** | 7 | 1 | all_parameters1 | 81 |
| **HPF Resonan** | 8 | 1 | all_parameters1 | 82 |
| **OSC1 Level** | 9 | 2 | all_parameters1 | 21 |
| **OSC1 Pitch** | 10 | 2 | all_parameters1 | 12 |
| **OSC1 PW** | 11 | 2 | all_parameters1 | 23 |
| **OSC1 WavePos** | 12 | 2 | all_parameters1 | 25 |
| **OSC1 Feedb** | 13 | 2 | all_parameters1 | 24 |
| **FM1 Level** | 14 | 2 | all_parameters1 | 54 |
| **FM1 Pitch** | 15 | 2 | all_parameters1 | 14 |
| **FM1 Feedback** | 16 | 2 | all_parameters1 | 55 |
| **OSC2 Level** | 17 | 3 | all_parameters1 | 26 |
| **OSC2 Pitch** | 18 | 3 | all_parameters1 | 13 |
| **OSC2 PW** | 19 | 3 | all_parameters1 | 28 |
| **OSC2 WavePos** | 20 | 3 | all_parameters1 | 30 |
| **OSC2 Feedb** | 21 | 3 | all_parameters1 | 29 |
| **FM2 Level** | 22 | 3 | all_parameters1 | 56 |
| **FM2 Pitch** | 23 | 3 | all_parameters1 | 15 |
| **FM2 Feedback** | 24 | 3 | all_parameters1 | 57 |
| **ENV1 Attack** | 25 | 4 | all_parameters1 | 73 |
| **ENV1 Decay** | 26 | 4 | all_parameters1 | 75 |
| **ENV1 Sustain** | 27 | 4 | all_parameters1 | 76 |
| **ENV1 Release** | 28 | 4 | all_parameters1 | 72 |
| **ENV2 Attack** | 29 | 4 | all_parameters1 | 77 |
| **ENV2 Decay** | 30 | 4 | all_parameters1 | 78 |
| **ENV2 Sustain** | 31 | 4 | all_parameters1 | 79 |
| **ENV2 Release** | 32 | 4 | all_parameters1 | 80 |
| **EQ-Bass Freq** | 1 | 1 | all_parameters2 | 84 |
| **EQ-Bass Gain** | 2 | 1 | all_parameters2 | 86 |
| **EQ-Treb Freq** | 3 | 1 | all_parameters2 | 85 |
| **EQ-Treb Gain** | 4 | 1 | all_parameters2 | 87 |
| **Noise** | 5 | 1 | all_parameters2 | 41 |
| **REVERB Amoun** | 6 | 1 | all_parameters2 | 91 |
| **DELAY Amount** | 7 | 1 | all_parameters2 | 52 |
| **DELAY Rate** | 8 | 1 | all_parameters2 | 53 |
| **MODFX Rate** | 9 | 2 | all_parameters2 | 16 |
| **MODFX Depth** | 10 | 2 | all_parameters2 | 93 |
| **MODFX Feedb** | 11 | 2 | all_parameters2 | 17 |
| **MODFX Offset** | 12 | 2 | all_parameters2 | 18 |
| **Wavefold** | 13 | 2 | all_parameters2 | 19 |
| **Bitcrush** | 14 | 2 | all_parameters2 | 62 |
| **Decimation** | 15 | 2 | all_parameters2 | 63 |
|  | 16 | 2 | all_parameters2 |  |
| **LFO1 Rate** | 17 | 3 | all_parameters2 | 58 |
| **LFO2 Rate** | 18 | 3 | all_parameters2 | 59 |
| **LPF Morph** | 19 | 3 | all_parameters2 | 70 |
| **HPF Morph** | 20 | 3 | all_parameters2 | 83 |
| **STUTTER Rate** | 21 | 3 | all_parameters2 | 20 |
| **SIDECH Level** | 22 | 3 | all_parameters2 | 61 |
| **SIDECH Shape** | 23 | 3 | all_parameters2 | 60 |
| **Comp Tresh** | 24 | 3 | all_parameters2 | 27 |
| **ENV3 Attack** | 25 | 4 | all_parameters2 | 102 |
| **ENV3 Decay** | 26 | 4 | all_parameters2 | 103 |
| **ENV3 Sustain** | 27 | 4 | all_parameters2 | 104 |
| **ENV3 Release** | 28 | 4 | all_parameters2 | 105 |
| **ENV4 Attack** | 29 | 4 | all_parameters2 | 106 |
| **ENV4 Decay** | 30 | 4 | all_parameters2 | 107 |
| **ENV4 Sustain** | 31 | 4 | all_parameters2 | 108 |
| **ENV4 Release** | 32 | 4 | all_parameters2 | 109 |
| **ARP Rate** | 1 | 1 | all_parameters3 | 51 |
| **ARP Gate** | 2 | 1 | all_parameters3 | 50 |
| **ARP GateSpr** | 3 | 1 | all_parameters3 | 40 |
| **ARP OctSpr** | 4 | 1 | all_parameters3 | 39 |
| **ARP VelSpr** | 5 | 1 | all_parameters3 | 37 |
| **ARP Rhythm** | 6 | 1 | all_parameters3 | 42 |
| **ARP SeqLen** | 7 | 1 | all_parameters3 | 43 |
| **ARP RevProb** | 8 | 1 | all_parameters3 | 36 |
| **ARP ChoPoly** | 9 | 2 | all_parameters3 | 44 |
| **ARP ChoProb** | 10 | 2 | all_parameters3 | 48 |
| **ARP RatchAm** | 11 | 2 | all_parameters3 | 45 |
| **ARP RatchPr** | 12 | 2 | all_parameters3 | 49 |
| **ARP NoteProb** | 13 | 2 | all_parameters3 | 46 |
| **ARP BassProb** | 14 | 2 | all_parameters3 | 47 |
|  | 15 | 2 | all_parameters3 |  |
|  | 16 | 2 | all_parameters3 |  |
|  | 17 | 3 | all_parameters3 |  |
|  | 18 | 3 | all_parameters3 |  |
|  | 19 | 3 | all_parameters3 |  |
|  | 20 | 3 | all_parameters3 |  |
|  | 21 | 3 | all_parameters3 |  |
|  | 22 | 3 | all_parameters3 |  |
|  | 23 | 3 | all_parameters3 |  |
|  | 24 | 3 | all_parameters3 |  |
|  | 25 | 4 | all_parameters3 |  |
|  | 26 | 4 | all_parameters3 |  |
|  | 27 | 4 | all_parameters3 |  |
|  | 28 | 4 | all_parameters3 |  |
|  | 29 | 4 | all_parameters3 |  |
|  | 30 | 4 | all_parameters3 |  |
|  | 31 | 4 | all_parameters3 |  |
|  | 32 | 4 | all_parameters3 |  |

MIDI channel: 15


[^1]: Unassigned knobs show up in the table with no parameter name and CC#.

    The table was automatically produced by calling

    ```python
    python3 roto-control_summarize.py "all_parameters/all_parameters1.json" "all_parameters/all_parameters2.json" "all_parameters/all_parameters3.json"
    ```

    from the `roto_control` directory one level above. So barring any errors in the script, the mappings summarized in the table should, by construction, match with the actual setup information encoded in the respective `.json` files.
