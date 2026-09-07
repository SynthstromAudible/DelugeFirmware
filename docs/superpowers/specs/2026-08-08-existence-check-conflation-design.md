# Existence-check error conflation — design

**Status:** batch 1 IMPLEMENTED 2026-08-09 (see §6.1); batches 2-4 outstanding. Designed 2026-08-08 with Kate.
**Authorised:** independently of the R5a/R5b ladder (Kate, 2026-08-08, option C). This is wrong under
*any* architecture, so it does not wait on
`2026-08-08-storage-execution-model-end-state-design.md` and is listed there as out of scope.

---

## 1. The bug in one sentence

`StorageManager::fileExists` is `deluge::io::File::open(...).has_value()`, so it reports **"I could not
determine whether this file exists"** as **"this file does not exist."**

## 2. This completes R4 Phase C rather than adding machinery

The information needed to fix this **already exists**. `src/deluge/io/file.hpp:15` defines a granular
`Status`:

```
OK · ERR · PARAM · BUSY · TIMEOUT · IO · NODEV · UNSUPPORTED
NOT_FOUND · EXISTS · NO_SPACE · NO_FILESYSTEM · WRITE_PROTECTED · NO_MEMORY · NOT_EMPTY
```

and both `File::open` and `Directory::open` already return `std::expected<_, Status>`
(`src/deluge/io/file.cpp:184`). R4 Phase C built that granularity — including the commit that made the
efatfs C-ABI return granular `DelugeStatus` — but the **call sites were never migrated to consume it.**
They discard it with `.has_value()`.

So the fix is propagation, not invention. `NOT_FOUND` and `BUSY` are already distinguishable at every
site; nothing new needs to be plumbed from the filesystem.

## 3. Scope — 24 sites across 13 files

An earlier note in this project's memory recorded "six callers." **That was wrong** — it captured only
the save/load subset. Verified count: **22 `StorageManager::fileExists` call sites across 12 files,
plus 2 `Directory::open(...).has_value()`-as-boolean sites.**

Grouped by **what the `false` branch does**, which is the only grouping that matters:

| # | Class | Sites | Cost of reading "unknown" as "absent" |
|---|---|---|---|
| 1 | **Anti-clobber guards** | `audio_file_manager.cpp:376`, `:386`; `stem_export.cpp:1004` | Defeats a *deliberate* guard: overwrite an existing recording take; reuse an occupied stem-export folder |
| 2 | **Sticky wrong negative** | `audio_file_manager.cpp:594` | Caches `alternateLoadDirStatus = NOT_FOUND`, turning a transient `BUSY` into a permanent wrong answer for the session |
| 3 | **Settings bootstrap** (7) | `performance_view.cpp:1811`, `:1822`; `midi_follow.cpp:1812`; `midi_device_manager.cpp:533`, `:544`; `runtime_feature_settings.cpp:190`, `:201` | "absent → write defaults" overwrites the user's settings file |
| 4 | **Boot / failsafe** | `deluge.cpp:423`, `:428`, `:452`, `:463` | Wrong failsafe and default-song decisions at boot |
| 5 | **Save** | `save_song_ui.cpp:156`, `:444`; `save_instrument_preset_ui.cpp:120` | Overwrite-confirmation prompt skipped |
| 6 | **Load / browse** | `load_song_ui.cpp:561`; `load_instrument_preset_ui.cpp:481`, `:684`, `:1045`; `browser.cpp:460` | Silently skips a MIDI device definition; reports a present preset as missing |
| 7 | **Favourites** | `favourite_manager.cpp:70` | — |

Per-class counts, summing to 24: class 1 = 3 · class 2 = 1 · class 3 = 7 · class 4 = 4 · class 5 = 3 ·
class 6 = 5 · class 7 = 1.

**Eleven of the 24 sites perform a write on the `false` branch.** That is the case for treating this as
correctness work rather than tidying.

### 3.0 Class 3 is LIVE on the Rust BSP — and reveals a previously unrecorded device bug

All four class-3 readers are called in sequence from `deluge.cpp:735-739`:
`runtimeFeatureSettings.readSettingsFromFile()`, `MIDIDeviceManager::readDevicesFromFile()`,
`midiFollow.readDefaultsFromFile()` (and `PerformanceView::readDefaultsFromFile()` from `:285`/`:1779`).

Those run inside `deluge_app_init`, which the Rust BSP calls at `src/bsp/rust/src/main.rs:552`
**directly inside `app_task`** — a plain Embassy task on MAIN. The worker-fiber pump does not start
until *after* `deluge_app_init` returns (`main.rs:556`). So at boot `on_fiber()` is false and every one
of these `fileExists` calls is rejected with `DELUGE_ERR_BUSY`, which the conflation reads as *absent*.

**Consequence, on every boot of the Rust BSP: community-feature settings, MIDI devices, MIDI-follow
defaults and the performance-view layout all silently fail to load.** This is user-visible, has the same
root cause as the save and pre-scan defects (the 29 rejection gates), and appears not to have been
recorded anywhere — consistent with the R4 arc never having run on hardware.

`midi_device_manager.cpp:526-528` half-anticipates this: it documents the inline call as deliberate
("dispatching onto the owner here could queue an op nothing pumps yet") and notes the
"FatFS entered only on the owner" audit assert must whitelist the site. That comment predates the gates.

**Why it is not also destructive today — and why that is fragile.** The dangerous branches are writes:
`midi_follow.cpp` would `writeDefaultsToFile()` over the user's config, and the three legacy-migration
sites would `rename` a stale root-level `*.XML` over the current settings. Those do not fire because
`deluge_efatfs_mkdir`, `_rename`, `_unlink` and `_file_open` are **all** gated off-fiber too
(`efatfs_fs.rs:898`, `:938`, `:918`, `:530`). `mkdir` returns `BUSY`, which is neither success nor
`EXISTS`, so the guard fails closed and the write is skipped.

So **today's safety is accidental: it depends on the check gate and the write gate failing together.**
R5a Phase 2 removes both at once, which is safe. But any partial, reordered, or per-op rollout — or any
future path where a write succeeds while an existence check can still return `BUSY` — recreates the
destructive combination. Fixing the conflation removes the dependence on that coincidence entirely,
which is the strongest available argument for doing it independently of the ladder.

### 3.1 The class-1 destructive shapes are latent, not live

`audio_file_manager.cpp:376/:386` sits in `getUnusedAudioRecordingFilePath`, whose whole purpose is
finding a free path, and reads:

```cpp
while (StorageManager::fileExists(namedPath)) { /* bump i */ }
```

A "couldn't check" answer exits the loop at `i == 0` and the recording is written over
`<song>_000.wav`. `stem_export.cpp:1004`'s guard is explicitly commented as existing so the code
"never reuse[s] an occupied stem-export folder" now that efatfs `mkdir` is idempotent.

**Neither is reachable today, and this was checked rather than assumed.**
`getUnusedAudioRecordingFilePath` is called from `SampleRecorder::cardRoutine`, which runs on the
worker fiber; `stem_export.cpp:93` dispatches through `Owner::run`. On-fiber callers are not rejected,
so `fileExists` answers correctly at both. R5a Phase 2 removes the rejection gates entirely, making it
moot from the other direction.

**The real argument for fixing it is therefore not "data loss today."** It is that correctness at these
sites rests on an **invisible calling-context invariant** — nothing in the signature, the call, or the
surrounding code says "this is only correct on the storage owner," and a future dispatch change
silently converts a latent hazard into a live one. That is precisely the failure mode that produced
this whole investigation.

## 4. The design

### 4.1 Signature

```cpp
// storage_manager.h
std::expected<bool, deluge::io::Status> fileExists(char const* pathName);
```

- `*result == true` — the path exists.
- `*result == false` — the path is **known absent** (`Status::NOT_FOUND` from the port).
- `!result` — existence **could not be determined**; `result.error()` says why.

This matches the port's own idiom (`File::open`, `Directory::open`), matches the project's
C++23-for-new-code rule, and forces the decision at every call site. The churn is the forcing
function, not a side effect.

`initSD()` failure, which today also returns a bare `false`, becomes an error rather than an absence.

The two `Directory::open(...).has_value()` sites need no API change — they already hold the
`expected`. They stop calling `.has_value()` as a boolean and branch on `Status` instead.

### 4.2 The decision rule

Every site is migrated under one rule, stated once so it does not have to be re-argued 24 times:

> **Unknown is never absence.** A site may treat unknown as *present*, may surface an error, or may
> refuse to act — but it may never take the "absent" branch.

Class-level rulings, with per-site specifics settled at plan time (§6):

| Class | Ruling |
|---|---|
| 1 — anti-clobber | **Refuse to act.** Abort the path/folder search and propagate the error. Both callers already return `Error`. Refusing to record beats overwriting a take. Do **not** "treat as occupied and keep searching" — a persistent `BUSY` would spin. |
| 2 — sticky negative | **Do not cache.** Leave `alternateLoadDirStatus` at `MIGHT_EXIST` so a later attempt re-checks; return the error for this attempt. Only a genuine `NOT_FOUND` caches the negative. |
| 3 — settings bootstrap | **Do not write.** Use in-memory defaults for this session without persisting. Never overwrite a settings file whose absence is unconfirmed. |
| 4 — boot / failsafe | Per-site; bias toward the **non-destructive** branch. Boot must still complete, so a site that cannot determine state should proceed as if the file were present rather than recreate it. |
| 5 — save | **Surface the error and do not save.** An unconfirmed absence must not silently skip the overwrite prompt. |
| 6 — load / browse | **Surface the error.** "Could not read the card" is a different message from "not found", and the user needs to be able to tell them apart. |
| 7 — favourites | Per-site; non-destructive branch. |

### 4.3 Explicitly not in scope

- **No retry loop, no backoff.** Callers surface or refuse; they do not paper over `BUSY`.
- **No change to why `BUSY` happens.** That is R5a Phase 2's job.
- **No new `Status` values.** The enum is sufficient.

## 5. Verification

This project's record is that review and unit specs alone have missed real storage bugs, and that
**real execution** is what catches them. So the gate is a fault-injecting run, not an inspection.

| Gate | What it proves |
|---|---|
| **Fault-injecting spec (new, primary)** | A backing that returns `Status::BUSY` for a path that **does exist** drives each class and asserts the site takes neither the absent branch nor a write. Without this the fix is unverified — a `NOT_FOUND`-only test passes vacuously against the current code. |
| **`io_specs` + host suite** | No regression in the existing port specs |
| **`deluge_loadcheck` RUN** | Real execution, not just linking |
| **`scripts/golden_embassy_diff.sh`** (cordae / highsiderr / icoustic) | Byte-identical. The happy path is unchanged, so any divergence means the refactor altered behaviour it should not have. Note icoustic has a **known pre-existing** divergence — compare against its current state, not against green. |
| **Device link** | `dbt build Debug` |

The negative control matters: the fault-injecting spec must **fail against today's code**. If it passes
before the fix, it is not testing the conflation.

## 6. Batches

Risk-first, each its own commit with its per-site rulings recorded in the commit message. Per-site code
reading happens when that batch's plan is written — the rulings above are class-level, and this spec
does not pretend to have read all 24 sites.

| Batch | Sites | Content |
|---:|---:|---|
| **1** | 11 | The signature change plus classes 1–3 — every site with a destructive `false` branch. Ships the fault-injecting spec. Retires the actual risk. |
| **2** | 3 | Class 5, save. The overwrite prompt made honest. |
| **3** | 4 | Class 4, boot / failsafe. |
| **4** | 6 | Classes 6–7, load / browse / favourites. Mostly message-quality. |

Batch 1 carries the signature change, so batches 2–4 are pure per-site semantics. If work stops after
batch 1, the remaining sites still compile — they will have been migrated mechanically to
`unknown → absent` with an explicit comment marking them as owed, which is no worse than today and is
now *visible* rather than implicit.

## 6.1 What batch 1 actually landed (2026-08-09)

Branch `feat/existence-check-conflation`, 7 commits on `next` @ `be90842cb`. All 11 batch-1 sites fixed;
every task independently reviewed, all spec-✅ with zero Critical/Important findings.

| Commit | Content |
|---|---|
| `e02ad1efb` | per-path fault injection in the file-io mock + the gate spec |
| `500ea1f79` | `fileExists` → `std::expected<bool, Status>`; 21 sites migrated mechanically |
| `77eb409fc` | `deluge::io::presence_from_open` — made the gate executable |
| `63b97df1d` | `deluge::storage::existence_policy` — `Presence` + `Bootstrap` |
| `8648ec5f4` | class 1: 3 anti-clobber guards (+ a `Directory` overload) |
| `052f5fc20` | class 2: the sticky alternate-dir negative |
| `181317571` | class 3: 7 settings-bootstrap sites |

**Final gate:** markers 0 batch-1 / 12 remaining · ctest 100% of 38 · `./dbt rust` clean ·
goldens cordae `db63b128be9748ed…` + icoustic `a565391b293e9d88…` both PASS.

### Corrections to this spec, found during execution

- **21 live `fileExists` sites, not 22** — `save_instrument_preset_ui.cpp:120` sits inside a `/* */`
  block. Total is **23** (21 + 2 `Directory::open`), not 24. Class 3 has **7** sites, so batch 1 was
  **11**, and the batch-1 marker count was only ever **9** (the 2 `Directory::open` sites call the port
  directly, so the `fileExists` migration never marked them).
- **§5's "primary" fault-injecting spec could not reach `fileExists`.** No test target links
  `storage_manager` — or any of the six batch-1 modules. Resolved (Kate, 2026-08-08) by extracting each
  decision into a pure, testable function: `presence_from_open`, then `presence_of`/`bootstrap_action`.
  Class 3's seven sites now share ONE unit-tested rule, and switching on a named three-state enum makes a
  missing "undeterminable" case a `-Wswitch` error rather than a review-attention problem.
- **The device gate is the Rust-BSP build, not `dbt build Debug`.** `ed57e8f5c` deliberately fails the
  retired RZA1 `deluge.elf` link.

### Three gate holes found while building the gate

Worth recording, because each would have produced a green test that proved nothing:
1. the conflation spec was **inert** — `io_specs` was entirely "Not Run" (ctest 32/38 → 38/38 once fixed);
2. the mock's `deluge_efatfs_dir_open` returned OK for **any** path, making "missing directory → absent"
   vacuous;
3. a **compile** failure and a **link** failure look alike in a build log and mean opposite things — one
   is a valid negative control, the other is a gate that never ran.

The rule was watched failing under deliberate sabotage three times, once with the failure isolated to
exactly the sabotaged mapping.

### Deferred out of batch 1

- 12 markers remain: batch 2 (save, 2) · batch 3 (boot/failsafe, 4) · batch 4 (load/browse/favourites, 6).
- `src/deluge/util/semver.h` has no include guard (real, dormant, unrelated).
- `midi_follow.cpp`'s pre-existing mkdir fall-through: when `mkdir` fails, control still reaches
  `openXMLFile` for a file just judged absent. Preserved deliberately.

### Fix round 1 (2026-08-09): mock fidelity, batch 5 inventory, three accepted outcomes

A whole-branch review of batch 1 returned MERGEABLE: yes and asked for one tightening pass, not a rescue.

- **Mock fidelity.** `tests/spec_io/mock_file_io.cpp`'s `deluge_efatfs_dir_open` conflated "no such entry"
  and "entry exists but isn't a directory" into a single `DELUGE_ERR_NOT_FOUND` (Absent). Real efatfs
  reports these separately (`crates/embedded-fatfs/src/dir.rs:252-256`: `Error::NotFound` vs.
  `Error::InvalidInput`); the latter maps to `DELUGE_ERR_PARAM`, i.e. Undeterminable. The mock was erring
  in the dangerous direction — it let a spec "prove" a safe absence the real device would never report.
  Split the two cases and pinned it with a new case in `existence_conflation_spec.cpp`. Verified no
  pre-existing spec depended on the old conflation: every existing `Directory::open`/`dir_open` call in
  `tests/spec_io/` and `tests/spec_host_passthrough/` targets either a genuine directory or a genuinely
  missing path, never a file path.

- **Batch 5 (new): three unmarked residual sites.** The review found three more sites carrying the same
  unknown-treated-as-absent shape as batches 1–4, but never entered in the original 24-site inventory, so
  they had no `TODO(conflation-batch-N)` marker — `getUnusedAudioRecordingFilePath` and
  `resolveFileSize`'s two lambdas (`tryAlternateName`, `tryRegularPath`) in
  `src/deluge/storage/audio/audio_file_manager.cpp`. These are unreachable today,
  same as every other site fixed in batch 1 — the problem is that `getUnusedAudioRecordingFilePath` now
  *looks* fully hardened while part of it is not, which is a false-honesty risk to future readers. Marked,
  not fixed: fixing site 1 means deciding what the REC-number scan should do about a stale
  `highestUsedAudioRecordingNumberNeedsReChecking[folderID]` (sticky, class 2 shape) and a stale
  `highestUsedAudioRecordingNumber[folderID]` (class 1 shape) after a skipped scan — a real design decision
  that needs its own gate, not a mechanical marker-batch fix.

- **An accepted behaviour change.** If `SAMPLES/EXPORTS/<song>/TRACKS` exists as a *file* (not a
  directory), the old code advanced straight to `TRACKS-00`; batch 1's code
  (`stem_export.cpp:1009-1052`) now classifies the `Directory::open` failure as `Presence::Undeterminable`
  and aborts the export with `Error::SD_CARD`. This is non-destructive and consistent with the decision
  rule, but it is not byte-identical to the old behaviour — and correctly so, since this call sits outside
  the Present/Absent branch and was never covered by the "must be byte-identical" guarantee.

- **A known minor.** Each refused named-song recording attempt still increments
  `highestUsedAudioRecordingNumber[folderID]` before failing, so N refused attempts inflate the counter by
  N, producing gaps in recording numbers. Verified this is not an out-param hazard: `*getNumber`,
  `filePath`, and `*tempFilePathForRecording` are all caller-local to `sample_recorder.cpp`'s calling
  scope and are never read on the `goto gotError` path (`sample_recorder.cpp:471-472` declares them
  local; `:599` is the unconditional `gotError:` landing pad that only sets `hadCardError`).

## 7. Risks

1. **The mechanical migration in batch 1 could silently change a hot path.** `deluge.cpp`'s boot sites
   and `audio_file_manager`'s recording path are both sensitive. The golden gate is the net.
2. **`getUnusedAudioRecordingFilePath` returning a new error** means callers must handle a refusal to
   record. Verify `sample_recorder.cpp:498`'s existing error path actually surfaces it rather than
   dropping it.
3. **Per-site rulings for classes 4 and 7 are genuinely open** and settled at plan time. If any turns
   out to need behaviour beyond the decision rule, stop and re-decide rather than improvise.

## 8. Related

- `docs/superpowers/specs/2026-08-08-storage-execution-model-end-state-design.md` — lists this as out
  of scope; §3 there explains why `BUSY` happens at all
- `src/deluge/io/file.hpp` / `file.cpp` — the `Status` enum and the `expected`-returning port
- `docs/dev/sd_busy_audit.md` — the sibling audit that found this class of defect
