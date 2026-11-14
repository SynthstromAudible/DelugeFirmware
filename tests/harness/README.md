# UI test harness

Level 0 of the UI test harness:
one script format, one snapshot format, dependency-free so they can be shared by levels 1-3
and compiled directly into the host BSP. **This directory must not include anything from
`src/shell`** -- it is compiled into the host BSP, which the shell is not.

- `ui_script.{h,cpp}` -- the script parser (`deluge::harness::parse_script`), plus a C façade
  (`ui_script_load` / `ui_script_next_due`) for the host BSP.
- `snapshot.{h,cpp}` -- the `.state` formatter (`StateLines`), an `oled_pbm` PBM writer, and
  `write_file`.
- `fixed_vector.h` -- a standalone copy of `src/shell/fixed_vector.h`'s shape (see above for why
  it isn't shared).

## Script format

deluge-sdk's headless format, one step per line: `<at_ms> <command> <args…>`; `#` comments.

```
0       board 128x48+5,128x64+0   # host-app level only: select the panel set
0       snapshot boot
500     button Clip down          # ButtonId names
520     button Clip up
600     encoder Select +1         # EncoderRole names; signed detents
700     pad 3 5 down 100          # grid coords; velocity optional (default 255)
750     pad 3 5 up
800     released                  # DELUGE_EVENT_NO_PRESSES
900     model ClipContent 3       # shell level only: raise Dirty::ClipContent for clip 3
950     notice Error 248          # shell level only: push a Notice
1000    snapshot after-clip
1200    quit
```

- Times are milliseconds on the harness clock: the shell's `now_ms` argument at level 2;
  golden_vt_render's virtual clock at level 3 (milliseconds since boot, the clock the app's timers run on).
- Parsing is one dependency-free unit, `tests/harness/ui_script.{h,cpp}`
  (`parse_script(string_view) -> FixedVector<Step, 256>`; `Step{at_ms, kind, args}`),
  shared by levels 1-2 and compiled into the host BSP for level 3.
- Unknown commands are errors, not warnings.
- MIDI steps, channels 1..16: `note CH NOTE VEL`, `noteoff CH NOTE`, `cc CH NUMBER VALUE`, `pc CH PROGRAM`,
  `bend CH VALUE` (14 bits, 8192 centred), and `midi <hex bytes>` for any other message (e.g.
  `2000 midi 90 3c 64`). `tools/emu_parity.py` sends them to delugemu's DIN MIDI input; on the probe rig
  (`tools/device_parity.py`) they go to the Deluge's USB-MIDI through `amidi` on the probe host.
  `ui_script.cpp` does not parse them.
- Level-3 scripts also run on the real Deluge on the probe rig, through a `rig-harness` image and
  `tools/device_parity.py`, compared with delugemu (`docs/dev/device_benchmarking.md`).

## Cards

A script that runs on a card (every directory under `tests/ui-scripts` except `shell/`) is named
`<letters><two digits>-<words>.txt` and names each card it runs on in a line of its own before its first step:

    # card: tests/fixtures/card                     a directory in this repository
    # card: tests/fixtures/card-dx-no-folder.card   a card manifest
    # card: content/sweep-bare.img                  a content card: a directory or FAT image in DELUGE_CONTENT_CARDS
                                                    (default .superpowers/emu-card), on a developer's machine only

Every runner reads them through `tools/ui_script_cards.py` and plays the script once per card. The `ui-scripts` gate
compares the first card's snapshots with the goldens and requires every further card to reproduce them, so its
scripts (`app/`, `selftest/`) name repository cards only. `tools/emu_parity.py <dir>` compares main's image with this
one on each card; `--card` or `--sd` runs every script on one card instead. `tools/ui_script_cards.py`'s test refuses
a script without a numbered name or a header.

Steps only the emulator runner takes: `load NAME` (opens the browser with Load, types NAME on the pad keyboard and
presses Select) and `random_colour X Y` (from then on pad (X, Y) compares as lit or unlit only, for a pad drawn in a
random row colour, DIFFERENCES.md D11). `ui_script.cpp` rejects both, so app scripts do not use them.

## Snapshot format

Per `snapshot <name>`, written into the output directory:

- `<name>.oled<N>.pbm` -- one **P1 PBM** per OLED surface, via `oled_pbm` (`snapshot.h`'s
  dependency-free re-implementation of `tests/unit/gfx/support/pbm.h`'s `page_major_to_pbm` --
  `tests/harness` may not include `tests/unit/` headers, since it is also compiled into the host
  BSP). A deliberate deviation from the SDK's PNG: no image codec in C, text diffs read in PRs.
  `scripts/pbm2png.py` converts for viewing.
- `<name>.state` -- text, one item per line, sorted so diffs are stable:

```
scene Popup/General
scene Session
pad 0 0 255 0 0
pad 4 3 10 20 30
led Play on
led Record blink 500
indicator upper 3 3 2 0
lock loading
notice Error 248
```

Only non-black pads, only lit/blinking LEDs, `scene` lines top→bottom, `lock` and
`notice` only when present.

`diff -r got/ golden/` is the assertion. `./dbt test --update <test>` re-records
(tests/README.md). A snapshot that is identical to the previous one in the same script is
still written -- silence is not a pass.

### `sorted_text()` ordering, precisely

Lines are emitted in a **fixed category order**, by first word: `scene`, `led`, `pad`,
`indicator`, `lock`, `notice`, then anything with an unrecognized first word, last. Within a
category, entries are ASCII-sorted -- except `scene`, which keeps insertion order (so a
script's nested-scene breadcrumb reads top-down as written). So two `indicator` lines always
come out `lower` before `upper` regardless of the order they were added in, and a `notice`
line always comes out after a `lock` line even if the `notice` was added first.

Lines are at most 96 bytes (`kStateLineBytes`, `snapshot.h`) and truncated, not rejected, past
that: `"notice "` (7) + the longest `Notice::Kind` name (`ClearWorking`, 12) + a space + a
47-character popup text (`Notice::text`/`PopupScene::text_` are both 48-byte buffers) is 67
characters, and 96 leaves headroom for a longer kind name or a wider surface without another
audit.

## Three levels

| Level | Drives | Location |
|---|---|---|
| 1 -- toolkit | one widget/component directly (literal data + POD state -> golden frame); no script | `tests/unit/ui/*_spec.cpp` |
| 2 -- shell + scenes | the real `Shell` over a fake `SurfaceSet` (`FakeSurfaceSet`), scripts may use every command including `model`/`notice` | `tests/unit/shell/scripts_spec.cpp` + `tests/ui-scripts/shell/*.txt`, goldens `tests/golden/ui/shell/` |
| 3 -- whole app | the real app on `golden_vt_render`, injected on its virtual clock | `tests/ui-scripts/app/*.txt` on its `ui_script` scenario (the `ui-scripts` gate, `tests/integration/checks/ui_scripts.py`), goldens `tests/golden/ui/app/` |

Levels 2-3 share the same script and snapshot format; that is the whole point of building both
on top of level 0. A migrated scene's script is meant to run, unmodified, at level 2
against both the legacy adapter and the new scene -- see "Migration hook" below.

## Adding a script

1. Write `tests/ui-scripts/shell/<name>.txt` (level 2) or `tests/ui-scripts/app/<aNN>-<name>.txt`
   (level 3, numbered, with its `# card:` lines; see "Cards" above) in the format above.
   End it with `quit` -- level 3 has no other way to stop, and
   level 2's last tick is the last step's `at_ms` either way, so a `quit` costs nothing and
   guards against a script that grows a later step by accident.
2. **Level 2**: call `uitest::expect_script_goldens(self, "<name>", registry, services, opts)`
   from an `it(...)` block in `tests/unit/shell/scripts_spec.cpp` (or a spec beside it under
   `tests/unit/shell/`); it reads the script by name from `UI_SCRIPTS_DIR` and diffs
   `UI_GOT_DIR` against `GOLDEN_ROOT`'s `ui/shell/` (all three set in `tests/trees/host/CMakeLists.txt`;
   `GOLDEN_ROOT` from `tests/support/cmake/golden.cmake`).
   **Level 3**: nothing to wire up -- the `ui-scripts` gate runs every script in
   `tests/ui-scripts/app` on each of its cards; record its goldens with `./dbt test --update ui-scripts`.
3. Run once with no golden recorded -- it fails, naming the missing golden directory. That
   failure is the point: it proves the run itself succeeds (no parse/run error, the right
   number of snapshots) *before* any golden exists to hide behind.
4. Asserting `snapshots == N` and diffing against the golden pins the *count*, but not what the
   golden actually says (M-10): a golden that was recorded wrong the first time then passes
   forever against itself. Where practical, also read a fact back out of the run's own `.state`/
   `.pbm` output (`tests/unit/shell/scripts_spec.cpp`'s `"lowers pad, encoder and model steps"`
   case is the pattern to follow -- it checks the pressed pad's colour, the two LEDs' declared
   states, and that each counter step actually redrew the OLED) rather than trusting the golden
   diff alone the way `"boots to the splash..."`/`"navigates, shows a popup..."` do.

**A `snapshot` step at the same `at_ms` as an input step orders differently at the two levels**
(M-4): level 3 applies every due step and then writes pending snapshots in the same pump call
(`host_script.c`'s `host_script_pump`), so a `snapshot` that shares the input's own `at_ms` sees
the *pre*-input screen -- the app has not yet consumed the event it just queued. Level 2 ticks the
shell (which consumes the input) before writing the snapshot, so the same script sees the
*post*-input screen at level 2. Neither is wrong -- it is what each level can observe in one pump
-- but a script whose first `snapshot` shares an `at_ms` with a button/pad/encoder/released step
will golden the wrong frame at whichever level and look correct doing it. Give an input step a
`snapshot` a tick or more later if you want to see its effect at level 3.

## Re-recording goldens

- **Level 2**: `./dbt test --update unit -- -R <spec>` (the spec binary that hosts the script). Recording asserts the
  run produced at least one file first -- an empty directory would otherwise "pass" forever with nothing in it.
- **Level 3**: `./dbt test --update ui-scripts`.
- Either way the runner runs the test again without the switch straight after recording, so a fresh golden that
  does not pass is put back.
- Run the *whole* suite a second and third time and diff (or checksum) the output trees against
  each other: a script and a scene are accepted on **byte-identical** frames, so any run-to-run
  wobble is a determinism bug in the scene or the harness, not something to average away.

## Reading a diff

A failing `expect_script_goldens` assertion (level 2) or `ui-scripts` gate (level 3, through
`scripts/ui_snapshot_diff.py`) prints both sides of
the mismatching file under a `--- <script>/<file> ---` header (or the differ's own summary at
level 3): a `.state` mismatch is two texts, directly comparable; a `.pbm` mismatch is two P1
PBMs -- pipe either side through `scripts/pbm2png.py` to view it as an image, or diff them as
text (a P1 PBM's ASCII rows already read as a coarse picture, one character per pixel).

## Level 3 limitations

The headless app has no shell-level channels and no way to report what it cannot see, so its
`.state` is a strict subset of level 2's (documented at length in `host_script.c`'s file
header):

- **No `model`/`notice` commands.** Both are shell-only (`notify::Dirty`, `notify::Notice`);
  a level-3 script that uses either is rejected at load with "shell-level command in an app
  script", exit 2 -- not silently ignored.
- **No `scene` lines.** The legacy UI has no scene names to report.
- **No `lock`/`notice` lines.** Neither the input lock nor the notice queue reaches the BSP.
- **LEDs are `on`-only, never `blink`.** Legacy blinking is the *app* toggling
  `deluge_control_set_led` on a timer; the BSP only ever sees the resolved boolean for this
  instant and cannot tell a steady LED from a blinking one caught in its on phase. Level 2, by
  contrast, reads the shell's own *declared* `LedFrame` (`Shell::derive_leds`'s composed state,
  before it is flattened to a boolean), so a level-2 `.state` can and does say
  `led Record blink 500` -- the period is only ever visible there.
- **`indicator lower|upper`** at level 3 reports the wire's own numbering directly
  (`which == 0` is the mod-encoder-0 = **lower** gold knob); level 2's `IndicatorId` is ordered
  `{Lower = 0, Upper = 1}` to agree, so both levels name the same physical knob the same way.

None of this is a gap to close -- it is what the BSP can physically observe. A migrated
scene's script should still pass at level 2 against both the legacy adapter and the new scene
(the migration hook below); level 3 exists to prove the *legacy* boundary continues to work
exactly as scripted while that migration is in flight.

## Migration hook

`RunOptions::second_registry` (level 2 only) points `expect_script_goldens` at a second scene
table. When set, the same script is run twice -- once against the call's own `registry`
argument, once against `*second_registry` -- into sibling `got/<name>/` and
`got/<name>-second/` trees, and those two trees are diffed **against each other**, not against
a recorded golden (there is nothing to record ahead of time when the whole point is that two
independent sources must agree). A failure names the first file, in sorted order, that
differs or is missing on either side. This is the acceptance test for a migrated scene:
the same script against `LegacySceneAdapter` and the new scene must produce identical
snapshots.

## Deadline (level 3 only)

A level-3 run only ends when the script's own `quit` step fires; anything that stops the app polling for input also
stops the pump. `host_script.c` therefore gives the run the script's last `at_ms` plus a 5 **virtual-second** margin
(`HOST_SCRIPT_DEADLINE_MARGIN_S`). Passing it exits 2 with a message naming the time reached and the script's last
step -- the useful diagnosis. A `quit` fires within a pump of the last step, so the margin is slack, not budget, and
`tests/ui-scripts/selftest/t03-no-quit.txt` pins the failure.

## The first snapshot (level 3)

The app's first OLED blit happens from inside its main loop, *after* its first input poll -- there is no frame to
snapshot at `at_ms == 0`. A level-3 script's first `snapshot` waits until the app has drawn: the committed scripts
take theirs at 50 ms or later. Snapshotting a panel that has never blitted is a hard failure (exit 2, "has never blitted"),
not a black `.pbm` or a silently-missing file -- either of those would be a golden that cannot tell "blank screen"
from "never drawn". `tests/ui-scripts/selftest/t02-early-snapshot.txt` pins it.

## The fixture card

`tests/fixtures/card/` is the card the level-3 scripts boot on unless their header names another: `KITS/`, `SONGS/`
and `SYNTHS/` empty but for a `.keep`, `SAMPLES/` with `TONE.WAV` (`scripts/make_fixture_sample.py` computes it),
and `SETTINGS/` with `MIDIFollow.XML` and `FAVOURITES/SAMPLES_Bank0.xml`. The app writes both settings files on its
first boot when they are missing, so they are committed: the card is a fixed point and a run only reads it.

The cards that differ from it by one file are card manifests over it (`tests/fixtures/card-<name>.card`, the format
in `src/bsp/rust/src/storage/card_manifest.rs`): `base tests/fixtures/card` and the one difference.
