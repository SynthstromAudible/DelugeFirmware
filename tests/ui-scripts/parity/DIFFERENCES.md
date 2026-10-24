# Known UI differences from `main` (tracker)

Every way this branch's UI looks or behaves differently from current `main`, as found by `tools/emu_parity.py` (or
on the device). Each one gets fixed or explicitly accepted. This file goes away when the list is empty.

Status: **open** (cause unknown), **explained** (cause known, nothing changed yet), **accepted**, **fixed**.

| # | Difference | Status | Origin | Evidence | Next step |
|---|---|---|---|---|---|
| D1 | One encoder detent does nothing; each turn reads one detent short until the knob next moves (Tempo +1 shows no popup) | explained | Rust BSP: deluge-sdk `QuadratureDecoder::step` confirms an edge only when the next edge arrives | `p02-encoders-clip` | One Tempo click on the device |
| D2 | Tap tempo: 119.99 vs `main`'s 119.8 for taps 500 ms apart | explained (inferred) | When each firmware samples `AudioEngine::audioSampleTimer` relative to the press | `p03-transport/tapped` | Trace the timer reads |
| D3 | Song Load browser lights the favourites row (0,2,2) on ours | open | — | `p10-load-save/load` | Isolate draw order vs `qwertyVisible` |
| D4 | Device only: audition column red at boot | explained (rig only) | J-Link loads the image after the flash's firmware has run; `main`'s `R_SFLASH_ByteRead` leaves SPIBSC in SPI mode (`CMNCR.MD` = 1), J-Link's reset doesn't reset SPIBSC, and ours reads settings memory-mapped (`deluge_flash_read`, 0x5807F000), so it reads zeros: byte 74 = 0 = RED. In memory-mapped mode the same sector reads byte 74 = 1 (GREEN). A boot from flash runs the bootloader, which leaves memory-mapped mode on | rig: `CMNCR` 0x81FFF320 → zeros; 0x01FFF320 → real settings | Ours assumes memory-mapped mode; decide whether reads should not |
| D5 | Boot shows the default synth "0" instead of the first SYNTHS preset | explained | `next` `d811680fe`: the preset arrives later (swap confirmed with gdb: `completeBlankPreset` returns true) and nothing redraws the OLED; the next redraw shows "000 RICH SAW BASS" | `parity-songs/*/boot` | — |
| D6 | Loading a non-song file keeps the current song and shows FILE CORRUPTED; `main` discards the song | explained | `next` `541caa5cf`: record-first song load | `parity-songs/clickybasswobble` | Kate: accept? |
| D7 | Some of our emulated VMs stopped advancing during playback in one `-j 3` run | open | — | first songs run | Reproduce with `stall.txt` |
| D8 | A long song name scrolls out of phase with `main` (~0.2 s) | explained (likely) | Load finishes at a different time | `parity-songs/midi-to-deluge` | Time load → first draw on each side |
| D9 | Audio: playing Kitchen Clave, ours has ~+10 dB above ~16 kHz; envelope and the rest of the spectrum match | open | Not found. Ruled out: noise floor/dither (stopped: identical ±1 LSB), interpolation mode (no `linearInterpolation`, nothing transposed), the voice- and kit-level rules that skip a fully open filter (same on both). A 2-tap average of ours brings the top bands to within +2–5 dB, but `main` has no such average in its voice or sample path | `--audio-wav`, Kitchen Clave; `main` vs itself: ±0.7 dB per band | Runtime A/B of one drum hit (interpolation mode, phase increment, filter path) on each side |
| D10 | Song browser: opening it, ours lights two favourites rows where `main` lights one; stepping to a song, ours draws that song's preview at once while `main` keeps the previous image for the whole 1 s window | open | Not found. `qwertyAlwaysVisible` is cleared on both; `main` does draw a preview later (back in the browser after a load) | `parity-card/c01-song-browser`, `c06-save-delete/load-browser` | Time `main`'s preview draw; compare the favourites/banks layout each side reads |
| D11 | A new kit row (or clip) gets a different colour | explained | Colours are random on both (`NoteRow` `random(71)`, new clips `random(72)`); the firmwares make different numbers of draws before it | `parity-card/c03-kit/kit-note` | Kate: accept? |
| D12 | Kit: after a drum is selected, the upper gold ring reads half on ours, empty on `main`; after the kit browser closes, ours lights a mute pad at the top row that `main` doesn't | open | Not found; first suspect is this branch's knob-focus rework (2e-2, 2e-3b) | `parity-card/c03-kit/kit-note` … `kit-closed` | Read which param each side's upper knob shows; compare kit scroll position after the browser |

## Coverage

Covered: views, encoders, transport, instrument buttons, grid, mod buttons, shift shortcuts, settings menu, song
menu, load/save browsers, toggles, sound menu (`parity/`); loading and playing eleven real songs (`parity-songs/`).

Not covered yet:

- [ ] ShellMenu on
- [x] Sample browser (matches); song browser from the clip view (D10); save and delete (match except D10's rows). Not yet: the synth preset browser (Load in the clip view opens the song browser on both; the preset browser needs Shift+Load or the instrument's name), rename
- [x] Editing inside a loaded song: notes, undo/redo, zoom, clip length, scroll (all match). Not yet: automation
- [x] Arranger: open, scroll, play, stop (all match)
- [x] Kit views: convert, notes, audition, scroll, kit browser (D11, D12). Not yet: kit menus
- [ ] Non-default settings (a saved settings sector that isn't factory)
- [~] Audio output: `--audio-wav` captures it; envelope and spectrum compare (the capture is not sample-exact, so waveforms do not)
