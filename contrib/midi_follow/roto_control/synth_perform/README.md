# synth_perform (MIDI Follow Mode Template for Melbourne Instruments ROTO-CONTROL)

This setup (`synth_perform.json`) surfaces parameters useful for performing with subtractive or FM synths on the Synthstrom Deluge. It uses the same set of parameters surfaced in [Sean Ditny's "synth perform" page for Electa One](https://app.electra.one/preset/vZ6WBYb4xDpMGcpChejY), but ordered somewhat differently to ensure that related parameters appear on the same page of eight knobs.

Below is a summary table, where `Position` is the knob's position across the 32 knobs and `Page` is the knob's page (1st, 2nd, 3rd, or 4th) in the setup.[^1]

| Parameter | Position | Page | CC# |
|-----------|----------|------|----|
| **LPF Cutoff** | 1 | 1 | 74 |
| **LPF Resonanc** | 2 | 1 | 71 |
| **HPF Cutoff** | 3 | 1 | 81 |
| **HPF Resonan** | 4 | 1 | 82 |
| **EQ-Treb Freq** | 5 | 1 | 85 |
| **EQ-Treb Gain** | 6 | 1 | 87 |
| **EQ-Bass Freq** | 7 | 1 | 84 |
| **EQ-Bass Gain** | 8 | 1 | 86 |
| **FM1 Level** | 9 | 2 | 54 |
| **FM1 Feedback** | 10 | 2 | 55 |
| **FM2 Level** | 11 | 2 | 56 |
| **FM2 Feedback** | 12 | 2 | 57 |
| **OSC1 Level** | 13 | 2 | 21 |
| **OSC1 Feedb** | 14 | 2 | 24 |
| **OSC2 Level** | 15 | 2 | 26 |
| **OSC2 Feedb** | 16 | 2 | 29 |
| **REVERB Amoun** | 17 | 3 | 91 |
| **Wavefold** | 18 | 3 | 19 |
| **DELAY Amount** | 19 | 3 | 52 |
| **DELAY Rate** | 20 | 3 | 53 |
| **MODFX Rate** | 21 | 3 | 16 |
| **MODFX Depth** | 22 | 3 | 93 |
| **MODFX Feedb** | 23 | 3 | 17 |
| **MODFX Offset** | 24 | 3 | 18 |
| **Master Vol** | 25 | 4 | 7 |
| **Panning** | 26 | 4 | 10 |
| **Master Pitch** | 27 | 4 | 3 |
| **Noise** | 28 | 4 | 41 |
| **ENV1 Attack** | 29 | 4 | 73 |
| **ENV1 Decay** | 30 | 4 | 75 |
| **ENV1 Sustain** | 31 | 4 | 76 |
| **ENV1 Release** | 32 | 4 | 72 |

MIDI channel: 15

[^1]: Unassigned knobs show up in the table with no parameter name and CC#.

    The table was automatically produced by calling

    ```python
    python3 roto-control_summarize.py "synth_perform/D_synth_perform.json"
    ```

    from the `roto_control` directory one level above.
