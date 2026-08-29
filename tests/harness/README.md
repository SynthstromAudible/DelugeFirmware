# UI test harness

Level 0 of the UI test harness (spec G, `docs/superpowers/specs/2026-08-26-ui-test-harness-design.md`):
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

(Copied from spec G §2.)

deluge-sdk's headless format, one step per line: `<at_ms> <command> <args…>`; `#` comments.

```
0       board 128x48+5,128x64+0   # host-app level only: select the panel set
0       snapshot boot
500     button Clip down          # ButtonId names (spec D §2)
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
  the deterministic frame-count clock at level 3 (`ms = frames * 1000 / sample_rate`).
- Parsing is one dependency-free unit, `tests/harness/ui_script.{h,cpp}`
  (`parse_script(string_view) -> FixedVector<Step, 256>`; `Step{at_ms, kind, args}`),
  shared by levels 1-2 and compiled into the host BSP for level 3.
- Unknown commands are errors, not warnings.

## Snapshot format

(Copied from spec G §3.)

Per `snapshot <name>`, written into the output directory:

- `<name>.oled<N>.pbm` -- one **P1 PBM** per OLED surface, via `page_major_to_pbm`. A
  deliberate deviation from the SDK's PNG: no image codec in C, text diffs read in PRs.
  `scripts/pbm2png.py` converts for viewing.
- `<name>.state` -- text, one item per line, sorted so diffs are stable:

```
scene Session
scene Popup/General
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

`diff -r got/ golden/` is the assertion. `GFX_GOLDEN_UPDATE=1` re-records (the helper from
the gfx plan). A snapshot that is identical to the previous one in the same script is
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
| 1 -- toolkit | one widget/component directly (literal data + POD state -> golden frame); no script | `tests/spec/ui/*_spec.cpp` |
| 2 -- shell + scenes | the real `Shell` over a fake `SurfaceSet` (`FakeSurfaceSet`), scripts may use every command including `model`/`notice` | `tests/spec/shell/scripts_spec.cpp` + `tests/ui-scripts/shell/*.txt`, goldens `tests/golden/ui/shell/` |
| 3 -- whole app | the real headless `deluge_host`, injected on its deterministic frame clock | `tests/ui-scripts/app/*.txt`, goldens `tests/golden/ui/app/`, ctest `ui_app_scripts_<name>` / `_diff` |

Levels 2-3 share the same script and snapshot format; that is the whole point of building both
on top of level 0. A migrated scene's script (spec F) is meant to run, unmodified, at level 2
against both the legacy adapter and the new scene -- see "Migration hook" below.

## Adding a script

1. Write `tests/ui-scripts/shell/<name>.txt` (level 2) or `tests/ui-scripts/app/<name>.txt`
   (level 3) in the format above. End it with `quit` -- level 3 has no other way to stop, and
   level 2's last tick is the last step's `at_ms` either way, so a `quit` costs nothing and
   guards against a script that grows a later step by accident.
2. **Level 2**: call `uitest::expect_script_goldens(self, "<name>", registry, services, opts)`
   from an `it(...)` block in `tests/spec/shell/scripts_spec.cpp` (or a new `*_spec.cpp` nested
   under `tests/spec/shell/`); it reads the script by name from `UI_SCRIPTS_DIR` and diffs
   `UI_GOT_DIR` against `UI_GOLDEN_DIR` (both set in `tests/spec/CMakeLists.txt`).
   **Level 3**: nothing to wire up -- `sim/CMakeLists.txt` globs
   `tests/ui-scripts/app/*.txt` (`CONFIGURE_DEPENDS`, so a fresh build picks up a new file with
   no manual re-run) and creates the `ui_app_scripts_<name>` / `_diff` pair automatically.
3. Run once with no golden recorded -- it fails, naming the missing golden directory. That
   failure is the point: it proves the run itself succeeds (no parse/run error, the right
   number of snapshots) *before* any golden exists to hide behind.

## Re-recording goldens

- **Level 2**: `GFX_GOLDEN_UPDATE=1 ctest --test-dir build/tests -R scripts_spec` (or whichever
  spec binary hosts the script). Recording asserts the run produced at least one file first --
  an empty directory would otherwise "pass" forever with nothing in it.
- **Level 3**: `GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R ui_app_scripts`
  (`scripts/ui_snapshot_diff.py`'s convention -- the exact string `"1"`, same as level 2).
- Either way, **run again without the flag** immediately after recording, to confirm the fresh
  golden actually passes rather than merely having been written.
- Run the *whole* suite a second and third time and diff (or checksum) the output trees against
  each other: a script and a scene are accepted on **byte-identical** frames, so any run-to-run
  wobble is a determinism bug in the scene or the harness, not something to average away.

## Reading a diff

A failing `expect_script_goldens`/`ui_app_scripts_<name>_diff` assertion prints both sides of
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

## Migration hook (spec G §6)

`RunOptions::second_registry` (level 2 only) points `expect_script_goldens` at a second scene
table. When set, the same script is run twice -- once against the call's own `registry`
argument, once against `*second_registry` -- into sibling `got/<name>/` and
`got/<name>-second/` trees, and those two trees are diffed **against each other**, not against
a recorded golden (there is nothing to record ahead of time when the whole point is that two
independent sources must agree). A failure names the first file, in sorted order, that
differs or is missing on either side. This is spec F's acceptance test for a migrated scene:
the same script against `LegacySceneAdapter` and the new scene must produce identical
snapshots.

## Timeouts (level 3 only)

A level-3 run only ends when the script's own `quit` step fires; anything that stops the app
polling for input also stops the pump, so two backstops exist, deliberately in different
units:

- **In-process** (`host_script.c`): the offline audio capture's own deadline is moved to the
  script's last `at_ms` plus a 5 **audio-second** margin. Expiring it exits 2 with a message
  naming the last step reached -- the useful diagnosis.
- **`ctest` `TIMEOUT`** (`sim/CMakeLists.txt`, computed per script from its own last `at_ms`):
  `8 * (last_s + 5) + 30` **wall seconds**. The `8 ×` converts the in-process, audio-second
  deadline into wall time at a render rate as slow as 0.125x (measured at ~0.26x on a
  `-DDELUGE_SIM_X64=ON` build, so better than 2x of headroom), and the `+ 30` covers process
  start-up and a cold CI runner. Scaling rather than adding keeps the in-process deadline
  winning the race for scripts of any length -- a flat wall margin only dominates for a short
  script and inverts past roughly 4-5 seconds of script timeline, at which point `ctest` would
  kill the process first and replace the useful message with a bare "Timeout".

Change `HOST_SCRIPT_DEADLINE_MARGIN_S` (currently 5) and the `8 * (last_s + 5)` in
`sim/CMakeLists.txt` together -- the CMake expression is the C constant seen from the wall-clock
side of the same conversion.

## The 50 ms first-snapshot rule (level 3)

The legacy app's first OLED blit happens from inside its main loop, *after* its first input
poll -- there is no frame to snapshot at `at_ms == 0`. A level-3 script's first `snapshot`
should be no earlier than **50 ms** (empirically 5-10x past the observed first blit on this
build). Snapshotting a panel that has never blitted is a hard failure (exit 2, "snapshot taken
before the app's first frame"), not a black `.pbm` or a silently-missing file -- either of
those would be a golden that cannot tell "blank screen" from "never drawn".

## The fixture card

`tests/fixtures/card/` (`DELUGE_SD_ROOT` for level 3) is committed with four empty directories
(`KITS/`, `SAMPLES/`, `SONGS/`, `SYNTHS/`, each a `.keep`) plus one real file,
`SETTINGS/MIDIFollow.XML`. The app boots to the session screen fine on the four empty
directories alone; `MIDIFollow.XML` is committed anyway because the app *writes* its own
built-in defaults there on first boot when it is absent (byte-identical every run) -- leaving
it uncommitted would mean every `ctest` run dirties the working tree with a new untracked file,
where committing it makes the card a fixed point and the run a read.
