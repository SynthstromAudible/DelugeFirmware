# Known UI differences from `main` (tracker)

Every way this branch's UI looks or behaves differently from current `main`, as found by `tools/emu_parity.py` (or
on the device). Each one gets fixed or explicitly accepted. This file goes away when the list is empty.

Status: **open** (cause unknown), **explained** (cause known, nothing changed yet), **accepted**, **fixed**.

| # | Difference | Status | Origin | Evidence | Next step |
|---|---|---|---|---|---|
| D1 | One encoder detent does nothing; each turn reads one detent short until the knob next moves (Tempo +1 shows no popup) | explained | Rust BSP: deluge-sdk `QuadratureDecoder::step` confirms an edge only when the next edge arrives | `p02-encoders-clip` | One Tempo click on the device |
| D2 | Tap tempo: 119.99 vs `main`'s 119.8 for taps 500 ms apart | explained (inferred) | When each firmware samples `AudioEngine::audioSampleTimer` relative to the press | `p03-transport/tapped` | Trace the timer reads |
| D3 | Song Load browser lights the favourites row (0,2,2) on ours | open | — | `p10-load-save/load` | Isolate draw order vs `qwertyVisible` |
| D4 | Device only: audition column red at boot | fixed (SDK branch, not yet pinned) | The firmware in flash (`main`'s `R_SFLASH_ByteRead`) leaves SPIBSC in manual mode (`CMNCR.MD` = 1); J-Link's reset doesn't reset SPIBSC, and our memory-mapped settings read then returns zeros (0 = RED). Fixed in deluge-sdk `fix/warm-boot-spibsc` `8a172b3a`: `warm_start::quiesce` puts SPIBSC back into memory-mapped reads | rig, from `CMNCR` 0x81FFF320: before, colours 00 00 00 00; after, CMNCR 0x01FFF320 and colours 01 00 03 02 (the stored settings) | Merge the SDK branch, bump the firmware's deluge-sdk pin |
| D5 | Boot shows the default synth "0" instead of the first SYNTHS preset | explained | `next` `d811680fe`: the preset arrives later (swap confirmed with gdb: `completeBlankPreset` returns true) and nothing redraws the OLED; the next redraw shows "000 RICH SAW BASS" | `parity-songs/*/boot` | — |
| D6 | Loading a non-song file keeps the current song and shows FILE CORRUPTED; `main` discards the song | explained | `next` `541caa5cf`: record-first song load | `parity-songs/clickybasswobble` | Kate: accept? |
| D7 | Some of our emulated VMs stopped advancing during playback in one `-j 3` run | open (not reproduced) | Not found. Not seen again in four runs (`-j 2`; five of ours at once; all songs at `-j 3` beside up to 14 VMs). The interrupt log of the stalled run showed normal traffic, no fault | first songs run (2026-10-23) | `emu_parity` writes `stall.txt` (clock + registers) if it recurs |
| D8 | A long song name scrolls out of phase with `main` (~0.2 s) | explained (likely emulator timing) | The scroll starts when the load ends; the frame counts per window vary between runs on `main` too (stopped: 81, then 79), so load timing jitters in the emulator | `parity-songs/midi-to-deluge` | Time load → first draw on each side over several runs |
| D9 | Audio: playing Kitchen Clave, ours has ~+10 dB above ~16 kHz; envelope and the rest of the spectrum match | open | Not found. Ruled out: noise floor/dither (stopped: identical ±1 LSB), interpolation mode (no `linearInterpolation`, nothing transposed), the voice- and kit-level rules that skip a fully open filter (same on both). A 2-tap average of ours brings the top bands to within +2–5 dB, but `main` has no such average in its voice or sample path | `--audio-wav`, Kitchen Clave; `main` vs itself: ±0.7 dB per band | Runtime A/B of one drum hit (interpolation mode, phase increment, filter path) on each side |
| D10 | Song browser: opening it, ours lights two favourites rows where `main` lights one; stepping to a song, ours draws that song's preview at once while `main` keeps the previous image for the whole 1 s window | open | Not found. `qwertyAlwaysVisible` is cleared on both; `main` does draw a preview later (back in the browser after a load) | `parity-card/c01-song-browser`, `c06-save-delete/load-browser` | Time `main`'s preview draw; compare the favourites/banks layout each side reads |
| D11 | A new kit row (or clip) gets a different colour | explained | Colours are random on both (`NoteRow` `random(71)`, new clips `random(72)`); the firmwares make different numbers of draws before it | `parity-card/c03-kit/kit-note` | Kate: accept? |
| D12 | Kit from a factory preset (`000 TR-808`, no `firmwareVersion`): the drum's LPF resonance ring reads half on ours, empty on `main` | explained | `main` treats an unversioned file as pre-1.2 and, on load, sets a 50% resonance (0x00000000) with the LPF fully open to 0% (`Sound::compensateVolumeForResonance` and its `GlobalEffectable` twin; the same step can shift `GLOBAL_VOLUME_POST_FX`). Ours reads only current formats and leaves legacy fix-ups to `tools/deluge-upgrade` (`passes/params.rs`), so an un-upgraded factory preset keeps 50%. Factory presets on users' cards are in this form | `parity-card/c03-kit`, `c08-kit-menu` | Decide: upgrade presets on the card, or apply the fix-up at load; check the volume side |
| D13 | Kit: after the kit browser closes, ours lights a mute pad at the top row that `main` doesn't | open | Not found | `parity-card/c03-kit/kit-closed` | Compare the kit's vertical scroll after the browser |
| D14 | Some pad colours differ by one or two steps (e.g. a clip pad (68,11,11) vs (68,12,12); dimmed menu pads (19,8,8) vs (20,8,8)) | open | Not found: rounding in a colour computation (dimming / blending) | `devsettings` run: `p01-views/song`, `p09-song-menu/*`; `parity-card/c01-song-browser/preset-loaded` | Find the dim/blend arithmetic on each side |
| D15 | ShellMenu on only: with a menu open, ours blanks the mute and audition columns where `main` keeps them lit; the Back LED differs; the sound menu lights one extra pad on an item and its submenu text differs by 5 pixels; Shift+Session shows one more panel state | open | Not analysed yet | `shellmenu` run: `p08`, `p09`, `p12`, `p01/shift-session` | Analyse before step 3c (the ShellMenu default flip) |

## Coverage

Covered: views, encoders, transport, instrument buttons, grid, mod buttons, shift shortcuts, settings menu, song
menu, load/save browsers, toggles, sound menu (`parity/`); loading and playing eleven real songs (`parity-songs/`).

Not covered yet:

- [x] ShellMenu on: all 12 basic scripts (D15)
- [x] Sample browser (matches); song browser from the clip view (D10); save and delete (match except D10's rows). synth preset browser (pads differ as D10; loading a preset matches). Not yet: rename
- [x] Editing inside a loaded song: notes, undo/redo, zoom, clip length, scroll (all match); automation view (matches)
- [x] Arranger: open, scroll, play, stop (all match)
- [x] Kit views: convert, notes, audition, scroll, kit browser, drum menu, kit menu (D11, D12, D13)
- [x] Non-default settings: the device's own settings sector as the starting flash, all 12 basic scripts (D14)
- [~] Audio output: `--audio-wav` captures it; envelope and spectrum compare (the capture is not sample-exact, so waveforms do not)
