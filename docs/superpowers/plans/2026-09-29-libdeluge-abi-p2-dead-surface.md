# libdeluge ABI P2: delete the dead surface — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove every declared libdeluge function that nothing calls or nothing defines, the FatFs diskio exports,
the legacy USB and NE10 leftovers, and the orphaned scheduler headers, so that P3 binds only live surface.

**Architecture:** A small script, `scripts/check_abi_surface.py`, lists every declared function with its
definitions and callers. Each deletion task starts by showing, with the script, that its functions have no
callers. It then deletes the declaration, the Rust definition and any mock, and ends with the script's `--check`
(nothing declared is left undefined), the gates, and the device link.

**Tech Stack:** Python 3 (the script), Rust (`src/bsp/rust`, `crates`), C/C++ headers, CppUTest/CppSpec mocks.

**Spec:** `docs/superpowers/specs/2026-09-29-libdeluge-abi-overhaul-design.md` (§2 "The dead surface", A2, C5, D7;
§3 decision 4)

## Global Constraints

- Branch off `next` after P1 has merged: `git switch -c chore/libdeluge-dead-surface next`. Local merges go into
  `next`, never `main`. Never push.
- Commit after every task, without asking. No `Co-Authored-By`. Commit subjects name the change, not the process.
- If a pre-commit hook (clang-format, rustfmt, ruff) reformats files, re-add them and commit again. Never `--amend`.
- Comments state the current rationale only: no history, no "was", no "no longer".
- Don't touch the website (`website/`).
- **Device link check** (run where a task says so):
  ```bash
  mv src/bsp/rust/local-sdk.toml /tmp/local-sdk.toml.held 2>/dev/null; ./dbt build RelWithDebInfo; rc=$?; mv /tmp/local-sdk.toml.held src/bsp/rust/local-sdk.toml 2>/dev/null; git checkout -q src/bsp/rust/Cargo.lock; exit $rc
  ```
- **Host gates for a deletion:** `./dbt test unit bsp-host crates golden-vt-render`. The host app build links
  every Rust export the scenarios use, so an export that something still needed fails the link there.
- Deleting a Rust export can leave a Rust-internal helper unused. Delete that helper too if `cargo` warns
  `dead_code` about it; the build treats warnings as they are today, so read the warnings.

## Review Focus

- **A function the script calls dead that something still calls under another spelling.** Examples: a Rust
  `extern "C"` block naming it, `dlsym`, or a string in a Python harness. Each deletion task greps for the bare name
  across the whole repository, `.py` and `.toml` included, before deleting. Owner: each task's first step.
- **A scenario or unit mock that defines a deleted function.** It still links, because the mock is the definition,
  and it keeps a dead name alive. Each task deletes the mocks named in its file list. Owner: each task.
- **The NE10 stubs.** Deleting them must not break the device link. Task 6 proves this either way before deciding.
- **`usbLock`.** It is live (`midi_engine.cpp` gates USB servicing with it). Only its duplicate declarations go.
  Owner: Task 6's gate run includes `recorder-write` and the MIDI-driving scenarios.
- **The header left empty.** `block_device.h` loses four declarations. Whatever remains must still be included
  where it was. Owner: Task 4.

---

### Task 1: The ABI surface script

**Files:**
- Create: `scripts/check_abi_surface.py`
- Modify: `.pre-commit-config.yaml` (a hook that runs `--check` when a libdeluge header changes)

**Interfaces:**
- Produces:
  - `scripts/check_abi_surface.py [NAME...] [--dead] [--check]`;
  - its table columns: `function header def mock app tests rust`;
  - `--check`, which exits 1 when a declared function has no definition.

- [ ] **Step 1: Write the script**

`scripts/check_abi_surface.py`:

```python
#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The libdeluge C ABI's surface: every declared function, where it is defined, and who calls it.

Declarations are read from ``include/libdeluge/*.h`` and ``src/OSLikeStuff/scheduler_api.h``. A function is
*defined* by a Rust ``fn`` in ``src/bsp/rust/src`` or ``crates/*/src``, or by a C/C++ body in ``src/``. Mocks in
``tests/`` are reported apart, because a mock of a function nobody calls keeps nothing alive.

Callers are counted by name, per area:
- ``app``: ``src/deluge`` and ``src/OSLikeStuff``;
- ``tests``: ``tests/``, excluding the mocks' own definitions;
- ``rust``: ``src/bsp/rust`` and ``crates``, excluding the definitions.

Comments are ignored.

Usage:
    scripts/check_abi_surface.py              # the table
    scripts/check_abi_surface.py --dead       # declared functions nothing calls
    scripts/check_abi_surface.py --check      # exit 1 if a declared function has no definition
    scripts/check_abi_surface.py NAME...      # the rows for these functions
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = sorted((ROOT / "include" / "libdeluge").glob("*.h")) + [ROOT / "src" / "OSLikeStuff" / "scheduler_api.h"]

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
# A declaration: one to four type words or stars, a name, a parameter list, an optional noexcept spelling, a `;`.
DECLARATION = re.compile(
    r"(?m)^[ \t]*((?:[A-Za-z_]\w*[ \t\*]+){1,4})([A-Za-z_]\w*)\s*\(([^;{()]|\([^;{()]*\))*\)\s*(?:DELUGE_NOEXCEPT|noexcept)?\s*;"
)
NOT_A_RETURN_TYPE = {"typedef", "return", "if", "while", "sizeof", "define"}


def strip_comments(text: str) -> str:
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def declared() -> dict[str, Path]:
    names: dict[str, Path] = {}
    for header in HEADERS:
        for match in DECLARATION.finditer(strip_comments(header.read_text())):
            words = match.group(1).split()
            if words and words[0] not in NOT_A_RETURN_TYPE:
                names.setdefault(match.group(2), header)
    return names


def source_files(*roots: str, suffixes: tuple[str, ...]) -> list[Path]:
    files = []
    for root in roots:
        for path in (ROOT / root).rglob("*"):
            if path.suffix in suffixes and "target" not in path.parts and path.is_file():
                files.append(path)
    return files


RUST = source_files("src/bsp/rust/src", "src/bsp/rust/harness", "crates", suffixes=(".rs",))
CXX_APP = source_files("src/deluge", "src/OSLikeStuff", suffixes=(".c", ".cpp", ".h", ".hpp"))
CXX_TESTS = source_files("tests", suffixes=(".c", ".cpp", ".h", ".hpp", ".rs"))


def cxx_definition(name: str) -> re.Pattern[str]:
    """A C/C++ body for `name`: a type-like prefix at the start of a line, the name, a parameter list, a `{`."""
    return re.compile(
        r'(?m)^[ \t]*(?:extern\s+"C"\s+)?(?:[A-Za-z_][\w:<>]*[ \t\*&]+)+' + re.escape(name)
        + r"\s*\([^;{}]*\)\s*(?:noexcept|DELUGE_NOEXCEPT)?\s*\{"
    )


def rust_definition(name: str) -> re.Pattern[str]:
    """A Rust `fn name`, or any item exported as `name` (`yield` is a Rust keyword, so it is exported by name)."""
    return re.compile(r"\bfn\s+" + re.escape(name) + r"\b|export_name\s*=\s*\"" + re.escape(name) + r"\"")


def scan(name: str) -> dict[str, int]:
    word = re.compile(r"\b" + re.escape(name) + r"\b")
    row = {"rust_def": 0, "cxx_def": 0, "mock": 0, "app": 0, "tests": 0, "rust": 0}
    areas = [(p, "rust") for p in RUST] + [(p, "app") for p in CXX_APP] + [(p, "tests") for p in CXX_TESTS]
    for path, area in areas:
        if path in HEADERS:
            continue
        text = path.read_text(errors="replace")
        if name not in text:
            continue
        text = strip_comments(text)
        definition = rust_definition(name) if path.suffix == ".rs" else cxx_definition(name)
        defs = len(definition.findall(text))
        uses = len(word.findall(text)) - defs
        if area == "tests":
            row["mock"] += defs
        else:
            row["rust_def" if path.suffix == ".rs" else "cxx_def"] += defs
        row[area] += uses
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*")
    parser.add_argument("--dead", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    decls = declared()
    names = args.names or sorted(decls)
    undefined = []
    print(f"{'function':48} {'header':22} def mock  app tests rust")
    for name in names:
        row = scan(name)
        defined = row["rust_def"] + row["cxx_def"]
        callers = row["app"] + row["tests"] + row["rust"]
        if not defined:
            undefined.append(name)
        if args.dead and callers:
            continue
        if args.check and defined:
            continue
        header = decls[name].name if name in decls else "-"
        print(f"{name:48} {header:22} {defined:3} {row['mock']:4} {row['app']:4} {row['tests']:5} {row['rust']:4}")
    if args.check and undefined:
        print(f"{len(undefined)} declared function(s) with no definition")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

`chmod +x scripts/check_abi_surface.py`.

- [ ] **Step 2: Run it against facts checked by hand**

Run: `scripts/check_abi_surface.py deluge_audio_stop yield deluge_audio_load_resolve_alternate_dir getAverageRunTimeForTask`
Expected, these four rows:
- `deluge_audio_stop`: def 1, and 0 in app, tests and rust;
- `yield`: def 1, app ≥ 10 (it is exported with `export_name`, so `def` must not be 0);
- `deluge_audio_load_resolve_alternate_dir`: app 1 (the `&&`-prefixed call in `audio_file_manager.cpp` is a
  call, not a definition);
- `getAverageRunTimeForTask`: app 1.

Run: `scripts/check_abi_surface.py --check`
Expected: exit 1, listing exactly `deluge_app_on_event`, `deluge_block_init`, `deluge_block_sector_count`,
`deluge_block_sector_size` and `deluge_block_sync`. Tasks 4 and 5 bring it to exit 0.

Run: `scripts/check_abi_surface.py --dead`
Expected: the list in spec §2 "The dead surface". `deluge_file_directory_async_active` is also listed; P3
deletes it with its siblings. If P1 has merged, `deluge_trigger_clock_set_handler` is absent.

- [ ] **Step 3: Note the hook Task 5 adds**

`--check` fails until Task 5 removes the last undefined declarations, so the pre-commit hook lands there. Its
text, for Task 5 to add beside the `check_platform_boundary` hook in `.pre-commit-config.yaml`, with the same
indentation:

```yaml
      - id: check-abi-surface
        name: every declared libdeluge function has a definition
        entry: python3 scripts/check_abi_surface.py --check
        language: system
        pass_filenames: false
        files: ^(include/libdeluge/|src/OSLikeStuff/scheduler_api\.h|scripts/check_abi_surface\.py)
```

- [ ] **Step 4: Commit**

```bash
git add scripts/check_abi_surface.py
git commit -m "chore: add a script that lists the libdeluge ABI's definitions and callers"
```


---

### Task 2: Dead platform services

`audio_io`, `clock`, `memory`, `midi_io`, `system`, `display` and `control_surface` functions nothing calls.

**Files:**
- Modify: `include/libdeluge/audio_io.h`, `clock.h`, `memory.h`, `midi_io.h`, `system.h`, `display.h`,
  `control_surface.h`
- Modify: `src/bsp/rust/src/audio.rs`, `audio_host.rs`, `services.rs`, `midi.rs`, `display.rs`, `control.rs`
- Modify: `tests/unit/cpputest/mocks/timer_mocks.cpp` (the `deluge_clock_monotonic` mock),
  `tests/support/mocks/mock_libdeluge.cpp` (the `deluge_memory_external_end` and `deluge_memory_internal_begin`
  mocks)

The functions:
- `deluge_audio_stop`, `deluge_audio_max_block_frames`, `deluge_audio_sample_rate`,
  `deluge_audio_output_latency_frames`;
- `deluge_clock_ticks_per_second`, `deluge_clock_monotonic_hz`, `deluge_clock_monotonic`;
- `deluge_memory_external_end`, `deluge_memory_internal_begin`;
- `deluge_cache_line_size`, `deluge_cache_clean`, `deluge_cache_invalidate`;
- `deluge_midi_port_kind`, `deluge_midi_read`, `deluge_midi_port_connected`;
- `deluge_platform_init`;
- `deluge_display_busy`;
- `deluge_control_set_pad`.

- [ ] **Step 1: Show they are dead**

Run:
```bash
scripts/check_abi_surface.py deluge_audio_stop deluge_audio_max_block_frames deluge_audio_sample_rate deluge_audio_output_latency_frames deluge_clock_ticks_per_second deluge_clock_monotonic_hz deluge_clock_monotonic deluge_memory_external_end deluge_memory_internal_begin deluge_cache_line_size deluge_cache_clean deluge_cache_invalidate deluge_midi_port_kind deluge_midi_read deluge_midi_port_connected deluge_platform_init deluge_display_busy deluge_control_set_pad
git grep -n -w -e deluge_audio_sample_rate -e deluge_clock_monotonic -e deluge_midi_read -e deluge_platform_init -- '*.py' '*.toml' '*.md' ':!docs/superpowers'
```
Expected: every row reads 0 in `app`, `tests` and `rust`. The grep lists only comments or docs; rewrite each of
those in Step 3.

- [ ] **Step 2: Delete them**

For each function:
- delete its declaration and doc comment from the header;
- delete its `#[unsafe(no_mangle)] pub extern "C" fn` item and doc comment from the Rust file (both
  `audio.rs` and `audio_host.rs` for `max_block_frames` and `sample_rate`);
- delete its mock from the two mock files.

In `audio.rs`, P1 made `deluge_audio_sample_rate` return `SAMPLE_RATE_HZ`. Keep `SAMPLE_RATE_HZ` (P1's
`capture_stamp` uses it) and delete the function.

`deluge_platform_init`'s deletion also removes the `i32`/`DelugeStatus` width mismatch (spec §2 A). Rewrite the
two sentences that name it: `system.h:20-21` ("The platform calls `deluge_platform_init` before…") and
`board.h:66` ("Valid after `deluge_platform_init`"). They become "The platform is up before `deluge_app_init` runs."
and "Valid from `deluge_app_init` on."

- [ ] **Step 3: Rewrite what still names them**

Run: `git grep -n -w -e deluge_audio_stop -e deluge_audio_max_block_frames -e deluge_audio_sample_rate -e deluge_audio_output_latency_frames -e deluge_clock_ticks_per_second -e deluge_clock_monotonic_hz -e deluge_clock_monotonic -e deluge_memory_external_end -e deluge_memory_internal_begin -e deluge_cache_line_size -e deluge_cache_clean -e deluge_cache_invalidate -e deluge_midi_port_kind -e deluge_midi_read -e deluge_midi_port_connected -e deluge_platform_init -e deluge_display_busy -e deluge_control_set_pad -- ':!docs/superpowers' ':!website'`
Expected after the edits: no output. Rewrite each comment that names one of these so it states what is true now,
or delete the comment if it only described the function.

- [ ] **Step 4: Run the gates**

Run: `./dbt test unit bsp-host crates golden-vt-render`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check`
Expected: exit 1, with the same five undefined names as Task 1. Nothing new is undefined.

Then run the device link check.
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add include/libdeluge src/bsp/rust/src tests/unit/cpputest/mocks/timer_mocks.cpp tests/support/mocks/mock_libdeluge.cpp
git commit -m "chore: delete the platform services nothing calls"
```

---

### Task 3: Dead storage and sample functions

**Files:**
- Modify: `include/libdeluge/sample_reader.h`, `streaming_fill.h`, `file_io.h`, `stream_io.h`, `tier.h`
- Modify: `crates/deluge_sample_reader/src/reservation.rs`, `src/bsp/rust/src/streaming_loader.rs`,
  `src/bsp/rust/src/efatfs_fs.rs`, `src/bsp/rust/src/efatfs_host_shim.rs`, `src/bsp/rust/src/tier_meter.rs`
- Modify: `tests/unit/io/mock_file_io.cpp` (the `deluge_efatfs_file_read` and `deluge_efatfs_file_truncate`
  mocks), `tests/unit/io/mock_file_io_spec.cpp` (the cases that call the `file_read` mock)

The functions:
- `deluge_sample_reserve_asset`, `deluge_sample_reserve_covered_count`, `deluge_sample_reserve_covered_index`,
  `deluge_sample_reserve_leased_count`;
- `deluge_streaming_chunk_ready`;
- `deluge_efatfs_file_read`, `deluge_efatfs_file_truncate`, `deluge_efatfs_is_mounted`,
  `deluge_efatfs_stream_flush`;
- `deluge_tier_sync_total`.

Kept: `deluge_sample_reserve_open`/`move` (the app calls them), the other `deluge_tier_sync_*` (four scenarios),
and `deluge_efatfs_unmount` (`song_preview_ab` ejects with it).

- [ ] **Step 1: Show they are dead**

Run: `scripts/check_abi_surface.py deluge_sample_reserve_asset deluge_sample_reserve_covered_count deluge_sample_reserve_covered_index deluge_sample_reserve_leased_count deluge_streaming_chunk_ready deluge_efatfs_file_read deluge_efatfs_file_truncate deluge_efatfs_is_mounted deluge_efatfs_stream_flush deluge_tier_sync_total`

Expected:
- every row reads 0 in `app` and `rust`;
- `deluge_efatfs_file_read` reads `tests 5`: these are `mock_file_io_spec.cpp`'s own calls to the mock;
- every other row reads 0 in `tests`.

Run: `grep -n 'file_read\b' tests/unit/io/mock_file_io_spec.cpp`
Expected: the five calls at lines 47, 146, 167, 173 and 197. Each is inside a case that tests the mock's own
read. Those cases go, and the cases that test `file_read_exact` stay.

- [ ] **Step 2: Delete them**

For each function:
- delete the declaration and its doc comment;
- delete the Rust definition and its doc comment (both `efatfs_fs.rs` and `efatfs_host_shim.rs` for the efatfs
  four);
- delete the mocks.

In `mock_file_io_spec.cpp`, delete each `it(...)` block whose body calls `deluge_efatfs_file_read(`, and keep the
rest. In `streaming_loader.rs`, keep the private readiness function the two doc comments at lines 126 and 162
mention, if the loader still calls it. Delete it only if `cargo` reports it unused. Rewrite those doc comments so
they no longer link to the deleted export.

- [ ] **Step 3: Rewrite what still names them**

Run: `git grep -n -w -e deluge_sample_reserve_asset -e deluge_sample_reserve_covered_count -e deluge_sample_reserve_covered_index -e deluge_sample_reserve_leased_count -e deluge_streaming_chunk_ready -e deluge_efatfs_file_read -e deluge_efatfs_file_truncate -e deluge_efatfs_is_mounted -e deluge_efatfs_stream_flush -e deluge_tier_sync_total -- ':!docs/superpowers' ':!website'`

Expected after the edits: no output. One comment names `deluge_efatfs_is_mounted()` as a behaviour:
`audio_file_manager.cpp:1555`. Rewrite it in terms of what `initSD()` observes now: the mount state that
`StorageManager` tracks.

- [ ] **Step 4: Run the gates**

Run: `./dbt test unit bsp-host crates golden-vt-render song-preview-ab streaming-margin`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check`
Expected: the same five undefined names as Task 1.

Then run the device link check.
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add include/libdeluge crates/deluge_sample_reader src/bsp/rust/src tests/unit/io
git commit -m "chore: delete the storage and sample functions nothing calls"
```

---

### Task 4: `block_device.h` and the FatFs diskio exports

`block_device.h` declares four functions no one defines. `sd.rs` still exports FatFs's diskio (`disk_initialize`,
`disk_status`, `disk_ioctl`, `disk_timerproc` and `get_fattime`), but FatFs is gone. `deluge_block_read`/`write`
stay, because Rust still calls them.

**Files:**
- Modify: `include/libdeluge/block_device.h` (delete `deluge_block_init`, `deluge_block_sector_count`,
  `deluge_block_sector_size` and `deluge_block_sync`)
- Modify: `src/bsp/rust/src/sd.rs` (delete `disk_initialize` ×2, `disk_status` ×2, `disk_ioctl`,
  `disk_timerproc` and `get_fattime`, at lines 336, 354, 369, 379, 409, 860 and 865 today)
- Modify: `src/bsp/rust/src/ffi_extra.rs:66-67` (the comment that points at the diskio glue)

- [ ] **Step 1: Show they are dead**

Run: `git grep -n -w -e disk_initialize -e disk_status -e disk_ioctl -e disk_timerproc -e get_fattime -e deluge_block_init -e deluge_block_sector_count -e deluge_block_sector_size -e deluge_block_sync -- ':!docs/superpowers' ':!website'`
Expected: only the definitions in `sd.rs`, the declarations in `block_device.h`, comments, and the vendored
FatFs-derived sources if any remain under `src/`. List any such source. If the device link includes one, stop and
report: it would still reference `disk_*`.

- [ ] **Step 2: Delete them**

Delete the declarations and their doc comments. Delete the seven Rust items and their doc comments, and any
`cfg`-paired twin. Delete the `// FatFS diskio glue …` comment in `ffi_extra.rs`. If `sd.rs` has a private helper
that only `disk_ioctl` used (a sector-count or sync helper), delete it when `cargo` reports it unused.

- [ ] **Step 3: Run the gates**

Run: `./dbt test unit bsp-host crates golden-vt-render card-reinsert embedded-fatfs block-device-adapters block-device-driver`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check`
Expected: exit 1, with only `deluge_app_on_event` left.

Then run the device link check.
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add include/libdeluge/block_device.h src/bsp/rust/src/sd.rs src/bsp/rust/src/ffi_extra.rs
git commit -m "chore: delete the FatFs diskio exports and the block functions nothing defines"
```

---

### Task 5: The app callback nobody defines, the Rust-only boot mark, and the pre-commit check

**Files:**
- Modify: `include/libdeluge/app.h` (delete `deluge_app_on_event` and any type only it uses)
- Modify: `include/libdeluge/boot.h` and `src/bsp/rust/src/boot.rs:17-21` (delete
  `deluge_boot_mark_finished`; `main.rs` calls `boot::mark_finished()` directly)
- Modify: `src/deluge/util/boot.h:31` (the comment naming the export)
- Modify: `.pre-commit-config.yaml` (the hook from Task 1, Step 3)

- [ ] **Step 1: Show they are dead**

Run: `scripts/check_abi_surface.py deluge_app_on_event deluge_boot_mark_finished`
Expected: both rows read 0 in `app`, `tests` and `rust`. `deluge_app_on_event` reads `def 0`.

Run: `grep -n 'DelugeAppEvent\|deluge_app_on_event' -r include src tests | grep -v /target/`
Expected: only `app.h`. If an event type exists and nothing else uses it, delete it too.

- [ ] **Step 2: Delete them**

Delete the two declarations, the event type if it is unused, and `boot.rs`'s `deluge_boot_mark_finished`.
Rewrite the `src/deluge/util/boot.h:31` comment to say that the platform marks boot finished (on the Rust BSP,
`main.rs` does so after the first app tick) and that the app only reads it.

- [ ] **Step 3: Turn on the check**

Add the hook from Task 1, Step 3 to `.pre-commit-config.yaml`.

Run: `scripts/check_abi_surface.py --check; echo exit=$?`
Expected: the header line only, then `exit=0`.

Neutering check: add `void deluge_nothing_defines_this(void);` to `include/libdeluge/boot.h`.
Run: `pre-commit run check-abi-surface --all-files`
Expected: FAIL, naming `deluge_nothing_defines_this`. Remove the line.

- [ ] **Step 4: Run the gates**

Run: `./dbt test unit bsp-host golden-vt-render`
Expected: PASS. Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add include/libdeluge/app.h include/libdeluge/boot.h src/bsp/rust/src/boot.rs src/deluge/util/boot.h .pre-commit-config.yaml
git commit -m "chore: delete the ABI's undefined app callback and Rust-only boot mark, and check every declaration is defined"
```

---

### Task 6: Scheduler leftovers, USB leftovers and the NE10 stubs

**Files:**
- Modify: `src/OSLikeStuff/scheduler_api.h` and `src/bsp/rust/src/scheduler.rs`. Delete `addOnceTask`,
  `getAverageRunTimeforCurrentTask`, `setNextRunTimeforCurrentTask`, `removeTask`, `boostTask`,
  `yieldWithTimeout` and `yieldToIdle`, and the scheduler state only they used (`scheduler.rs:115-126`
  describes a boost flag and a remove flag).
- Delete: `src/OSLikeStuff/task_scheduler/task.h` and `src/OSLikeStuff/task_scheduler/resource_checker.h`. Nothing
  includes `task.h`, and only `task.h` includes `resource_checker.h`.
- Modify: `src/deluge/deluge.cpp`. Delete:
  - the four USB `extern "C"` declarations (221-224);
  - `extern uint8_t anythingInitiallyAttachedAsUSBHost;` (206);
  - the peripheral/host switching block (299-301);
  - the `openUSBHost()`/`closeUSBHost()`/`openUSBPeripheral()` calls and their `anythingInitiallyAttachedAsUSBHost`
    test (643-656), keeping the `usbLock` set and clear only if something between them still needs it (read it
    first);
  - the three `usb_*` declarations and `extern "C" volatile uint32_t usbLock;` (426-431).
- Modify: `src/deluge/io/midi/midi_engine.h` (declare `extern volatile uint32_t usbLock;` once) and
  `midi_engine.cpp:50` (the definition stays)
- Modify: `src/bsp/rust/src/ffi_extra.rs` (delete `anythingInitiallyAttachedAsUSBHost`, `openUSBHost`,
  `closeUSBHost`, `openUSBPeripheral`, and wherever `closeUSBPeripheral` is defined: find it with
  `git grep -n closeUSBPeripheral`)
- Modify: `tests/component/differential/region_fill_differential/tests/host_end_to_end.rs:15` (the comment
  listing `openUSBHost`)

- [ ] **Step 1: Show the scheduler functions are dead**

Run: `scripts/check_abi_surface.py addOnceTask getAverageRunTimeforCurrentTask setNextRunTimeforCurrentTask removeTask boostTask yieldWithTimeout yieldToIdle`
Expected: every row reads 0 in `app`, `tests` and `rust`.

- [ ] **Step 2: Delete them and the orphaned headers**

Delete the seven declarations, their Rust definitions, the state only they touched, and the two orphaned headers.
Keep `getAverageRunTimeForTask` (capital F; `audio_engine.cpp:833` calls it).

Run: `./dbt test unit bsp-host golden-vt-render`
Expected: PASS.

- [ ] **Step 3: Delete the USB leftovers**

Read `deluge.cpp:640-660` first. The Rust BSP brings USB up itself, so `openUSBHost`/`closeUSBHost`/
`openUSBPeripheral` are empty on it. If `usbLock = 1; … usbLock = 0;` brackets only those calls, delete the
bracket too. Otherwise keep it around what remains.

Make the edits listed under Files. Every file that uses `usbLock` (`deluge.cpp` if it still does, and
`midi_engine.cpp`) includes `io/midi/midi_engine.h` for the one declaration.

Run: `git grep -n -w -e openUSBHost -e closeUSBHost -e openUSBPeripheral -e closeUSBPeripheral -e anythingInitiallyAttachedAsUSBHost -e usb_pstd_pcd_task -e usb_cstd_usb_task -e usb_main_host -- ':!docs/superpowers' ':!website'`
Expected: no output.

Run: `git grep -n 'usbLock' -- src`
Expected: the definition in `midi_engine.cpp`, the declaration in `midi_engine.h`, and uses. There must be no
other `extern` declaration.

- [ ] **Step 4: Settle the NE10 stubs**

The app calls only NE10's int32 FFT (`ne10_fft_alloc_r2c_int32`, `ne10_fft_r2c_1d_int32_neon`,
`ne10_fft_c2r_1d_int32_neon`), which `libNE10.a` defines. The 19 stubs in `ffi_extra.rs` are float, int16, FIR and
IIR entry points that return 0.

Delete the `ne10_stub!` macro and its invocation, then run the device link check.

- **If it exits 0:** nothing needed them. Keep the deletion.
- **If it fails with undefined `ne10_*` references:** restore the stubs. Replace the TODO comment above them with
  one that names the NE10 object requiring them (from the linker error, for example "`NE10_init.c.o`'s dispatch
  table references these; the app never calls them"), and keep the names the link actually needs, dropping the
  rest.

Either way, check the device image calls the real int32 FFT:

Run: `arm-none-eabi-nm build/RelWithDebInfo/deluge.elf | grep -E ' T ne10_fft_(r2c|c2r)_1d_int32_neon'`
Expected: both symbols, type `T`.

- [ ] **Step 5: Run the gates**

Run: `./dbt test unit bsp-host crates golden-vt-render golden-synth recorder-write sample-claim`
Expected: PASS. `sample-claim` runs the pitch detection that uses the FFT.

Then run the device link check.
Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add -A src/OSLikeStuff src/bsp/rust/src/scheduler.rs src/bsp/rust/src/ffi_extra.rs src/deluge/deluge.cpp \
  src/deluge/io/midi tests/component/differential/region_fill_differential/tests/host_end_to_end.rs
git commit -m "chore: delete the scheduler, USB and NE10 leftovers of the legacy BSP"
```

---

### Task 7: Stale wording

The comments still describe a build that links weak C++ fallbacks, or a C++-only BSP (spec §2 C5).

**Files:** whatever the greps below list, outside `docs/superpowers` and `website/`.

- [ ] **Step 1: Find them**

Run:
```bash
git grep -n -i -e 'weak defin' -e 'C++-only build' -e 'C++-only BSP' -e 'legacy BSP' -e 'RZA1 BSP' -e 'deluge-embassy' -e 'weak fallback' -- src include tests crates scripts ':!website' ':!docs/superpowers'
```

- [ ] **Step 2: Rewrite each**

Rewrite each hit to state what is true on the one platform that exists, or delete it if it only explained the
other platforms. The `*_async_active` doc comments and definitions are exempt: P3 deletes them. The
`include/libdeluge/README.md` hit (`~/GitHub/deluge-embassy`) is exempt as well: P3 rewrites the README.

Run the Step 1 grep again.
Expected: only the exempt hits.

- [ ] **Step 3: Run the gates and commit**

Run: `./dbt test unit bsp-host golden-vt-render`
Expected: PASS.

```bash
git add -u
git commit -m "chore: correct comments that still describe the retired platforms"
```

---

### Task 8: Whole-suite check and merge

- [ ] **Step 1:** Run `./dbt test`.
  Expected: every entry PASS, apart from entries whose registry status is `unavailable`.
- [ ] **Step 2:** Run `scripts/check_abi_surface.py --dead`.
  Expected: only the `*_async_active` functions, which P3 deletes.
- [ ] **Step 3:** Run the device link check.
  Expected: exit 0.
- [ ] **Step 4:** `git switch next && git merge --no-ff chore/libdeluge-dead-surface`
