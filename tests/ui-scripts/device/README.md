# Device scripts (the probe rig)

Level-3 UI scripts for the real Deluge on the probe rig, run with `tools/device_parity.py` on a `rig-harness`
image (`tools/bench_image.py --no-bench --features rig-harness`). They also run in delugemu, so the device can be
compared with the emulator running the same image. Scripts with a `midi` step run with `--record-only`.

The rig's SD card must hold what `--card`/`--sd` describes; the backup-card image `device_parity` is usually
run with is the rig's card.

## The device checklist, by script

| Checklist item (`device-manual-checklist`) | Script |
|---|---|
| 1. Legacy menus with ShellMenu off | `parity/p12-sound-menu` |
| 3. Shift + shortcut pad lands on the parameter | `parity/p07-shift-shortcuts` |
| 5. Shortcut pad flashes, pad brightness | `parity/p13-shortcut-blink`, `parity/p14-pad-brightness` |
| 10. Open/close stress | `device/d01-open-close-stress` |
| 2, 3 with ShellMenu on | `parity/sm01-*`, `parity/sm02-*` (needs the ShellMenu card on the rig) |

Still by hand: how the encoders feel (6), the OLED's look beyond its pixels (4), audio during browsing (7), MIDI
learn with a controller (8, until the rig's USB-MIDI enumerates), the screensaver (9), a 7-segment Deluge (11).
