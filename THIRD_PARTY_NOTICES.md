# Licensing and third-party attribution

Published: 2026-10-11. This is a modified, unofficial distribution.

## Deluge

Original firmware: Synthstrom Audible Limited and Deluge Community Firmware contributors.
Upstream: https://github.com/SynthstromAudible/DelugeFirmware
Base commit: `3e06e01d713001e546480a3153f01570c0d05fa0`.
The combined firmware is provided under GNU GPL version 3 or later, with the full GPL v3 text in LICENSE. Original file copyright notices, warranty disclaimers and the upstream Git history remain intact. Other bundled components retain their own notices; this document does not replace them.

## Plaits and stmlib

Author: Emilie Gillet (Mutable Instruments).
Plaits source: https://github.com/pichenettes/eurorack at `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4`.
stmlib source: https://github.com/pichenettes/stmlib at `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`.
The imported subset is under MIT, with original copyright and permission text retained in its source files and an additional readable copy in src/plaits_vendor/LICENSE-MIT.txt.

Changes to the imported engines: optional null AUX output to skip unused auxiliary synthesis in FM, Waveshaping and the selected Virtual Analog variant; custom compilation configuration. The Deluge adapter, mixing, menu and modulation integration are separate custom integration code. Changes published 2026-10-11; see CUSTOM_CHANGES.md and docs/custom/PLAITS_PERFORMANCE_20261011.md.

## MDD Snake inspiration

The sequencer design is inspired by MDD Snake by Maxime Dangles. The MDD implementation published here is the custom C++ implementation in src/deluge/sequencer and the Deluge integration, not a redistribution of the Max for Live device or its artwork. This attribution does not assert permission to redistribute that separate device or imply its author's endorsement.

## Source and binaries

The repository includes the editable custom source, vendored dependencies and build scripts. Any future binary release should identify its exact source commit and provide access to that matching source. This source-only audit update is newer than the previously distributed firmware binaries and must not be described as their exact source snapshot.
