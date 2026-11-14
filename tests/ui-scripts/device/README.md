# Device scripts (the probe rig)

Level-3 UI scripts for the real Deluge on the probe rig, run with `tools/device_parity.py` on a `rig-harness` image
(`tools/bench_image.py --no-bench --features rig-harness`). They also run in delugemu, so the device can be compared
with the emulator running the same image. `d01` is compared (`./dbt test ui-device`); `d02` records only
(`ui-device-record`), since its measurement is the device's. A script with a `midi` step runs with `--record-only`.

The rig's SD card must hold the card the scripts' headers name first; `device_parity` images that card for delugemu
unless `--card`/`--sd` names another.

## The device checklist, by script

| Checklist item (`device-manual-checklist`) | Script |
|---|---|
| 1. The sound menu | `parity/p12-sound-menu` |
| 3. Shift + shortcut pad lands on the parameter | `parity/p07-shift-shortcuts` |
| 5. Shortcut pad flashes, pad brightness | `parity/p13-shortcut-blink`, `parity/p14-pad-brightness` |
| 10. Open/close stress | `device/d01-open-close-stress` |
| 2, 3 in the sound menu | `parity/sm01-*`, `parity/sm02-*` |

Still by hand: how the encoders feel (6), the OLED's look beyond its pixels (4), audio during browsing (7), MIDI
learn with a controller (8, until the rig's USB-MIDI enumerates), the screensaver (9), a 7-segment Deluge (11).
