# Durable-ID Phase −1: Rename `serialId` → `id` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mechanically rename the durable-identity member `serialId`/`serial_id_` to plain `id`/`id_` across `Clip`/`Output`/`Drum`, `OwnerId`, and `ParamAddress`, with zero behaviour change — so every later semantic diff in the durable-id program reads clean.

**Architecture:** Pure token rename. No logic, control flow, storage, or file-format change. Verified behaviour-preserving by the golden nets (byte-identical save) and a firmware link. This is Phase −1 of the durable-id unification (spec: `docs/superpowers/specs/2026-07-08-durable-id-unification-design.md`).

**Tech Stack:** C++23 firmware; host-sim golden harnesses (`song_roundtrip`, `param_record_diff`, `edit_undo_redo`) built via CMake/Ninja into `build-sim`; firmware via `./dbt build Debug`.

## Global Constraints

- Behaviour-preserving, golden bit-identical: `songs` `OK=15 REGRESS=0`, `param_record` `OK=3`, `edit_undo_redo` `OK=40`, firmware Debug links. (Copied from the spec's *Testing & gates*.)
- No file-format change: the on-disk attribute is already `"id"`; do **not** touch string literals.
- New/renamed C++ is idiomatic C++23; this task changes only identifiers.
- Commit with a single fresh `git commit` — never chain `--amend` (the pre-commit clang-format hook reformats + fails the first commit; recover with a *new* commit, not `--amend`).
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

## Rename map (exact)

| token (before) | token (after) | where |
| --- | --- | --- |
| `setSerialId` | `setId` | all owners |
| `serialId` | `id` | accessor + all call sites |
| `serial_id` | `id` | field `serial_id_`→`id_`; `OwnerId::serial_id`→`id` |
| `clip_serial` | `clip_id` | `ParamAddress::clip_serial` |

Ordering note: `serialId` (lowercase `s`) and `setSerialId` (capital `S` in `SerialId`) are distinct tokens, so their replacements don't interfere. Replacing the substring `serial_id` turns both `serial_id_`→`id_` and `serial_id`→`id` in one pass. `clip_serial` shares no substring with the others and is done independently. Leave `clip_is_session` / `is_session` untouched (deleted in a later phase).

---

### Task 1: Scripted rename + golden gate

**Files (modify — all occurrences; ~207 across 32 files):**
- `src/deluge/model/clip/clip.h` (field `serial_id_`, `serialId()`, `setSerialId()`)
- `src/deluge/model/output.h`, `src/deluge/model/drum/drum.h` (same trio)
- `src/deluge-project/param_address.h` (`OwnerId::serial_id`, `OwnerId::*` ctors, `ParamAddress::clip_serial`)
- `src/deluge-project/clip_address.h` (comment mentions `serialId()`)
- every `.cpp`/`.h` under `src/` and `tests/` referencing the tokens (call sites)

**Interfaces:**
- Consumes: nothing.
- Produces: `Clip/Output/Drum::id()` → `std::optional<uint32_t>`, `setId(uint32_t)`; `OwnerId{kind, id, is_session}`; `ParamAddress::clip_id`. (Types unchanged from the old `serialId`/`serial_id`; only names change. `is_session` stays for now.)

- [ ] **Step 1: Guard against a string-literal or collision rename**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
grep -rn '"serialId"\|"serial_id"\|"clip_serial"' src/ tests/ ; echo "literals-exit=$?"
grep -rIn '\bid_\b\|\bid()\|\bsetId(' src/deluge/model/clip/clip.h src/deluge/model/output.h src/deluge/model/drum/drum.h ; echo "collision-exit=$?"
```
Expected: `literals-exit=1` (no such string literals — nothing to accidentally break) and `collision-exit=1` (no pre-existing `id_`/`id()`/`setId` on the owner types). If either exits `0`, STOP and resolve before renaming.

- [ ] **Step 2: Apply the rename across `src/` and `tests/`**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
python3 - <<'PY'
import os
subs = [("setSerialId","setId"), ("serialId","id"), ("serial_id","id"), ("clip_serial","clip_id")]
n_files = 0
for root in ("src","tests"):
    for dp,_,fns in os.walk(root):
        for fn in fns:
            if not fn.endswith((".cpp",".h",".hpp")): continue
            p = os.path.join(dp,fn)
            s = open(p).read()
            o = s
            for a,b in subs:
                s = s.replace(a,b)
            if s != o:
                open(p,"w").write(s); n_files += 1
print("rewrote", n_files, "files")
PY
grep -rIn "serialId\|serial_id\|clip_serial\|setSerialId" src/ tests/ ; echo "residual-exit=$?"
```
Expected: prints `rewrote <N> files`, and `residual-exit=1` (no remaining occurrences of any old token).

- [ ] **Step 3: Build the sim harness**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
cmake --build build-sim --target edit_undo_redo song_roundtrip param_record_diff 2>&1 | grep -E "error:|Built target|Linking" | tail -8
```
Expected: links all three, no `error:`. If a compile error appears (e.g. a token the script missed inside a macro), fix it and re-run.

- [ ] **Step 4: Run all three golden nets — must be behaviour-identical**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
```
Expected, exactly:
```
OK=40 FAIL=0  (corpus=…)
OK=15 REGRESS=0 ABSENT=0  (corpus=…, 15 fixtures)
OK=3 REGRESS=0 ABSENT=0  (corpus=…, 3 baselines)
```
Any `REGRESS`/`FAIL` > 0 means the rename changed behaviour (it must not) — investigate before proceeding.

- [ ] **Step 5: Build firmware**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
./dbt build Debug 2>&1 | tail -2
```
Expected: prints the `text data bss dec hex filename` line for `build/Debug/deluge.elf` (a successful link).

- [ ] **Step 6: Commit (single fresh commit — no `--amend`)**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
git add -A
git commit -q -m "dom: rename Clip/Output/Drum serialId -> id (durable-id Phase -1)

Pure mechanical rename ahead of the durable-id unification (spec
2026-07-08-durable-id-unification-design.md): serial_id_->id_, serialId()->id(),
setSerialId->setId, OwnerId::serial_id->id, ParamAddress::clip_serial->clip_id.
The 'serial' qualifier only distinguished against the soon-deleted sessionId; the
single durable identity is now plain 'id'. Zero behaviour change: songs OK=15
REGRESS=0, param_record OK=3, edit_undo_redo OK=40; firmware Debug links."
git log --oneline -1
```
If the commit FAILS with a clang-format hook message ("files were modified by this hook"), the files were reformatted and nothing was committed — re-run `git add -A && git commit …` as a **fresh** command (do not use `--amend`).

Expected: the log line shows the new commit at HEAD.

---

## Self-Review

**1. Spec coverage.** This plan implements only the spec's *Naming* section + *Phase −1* — deliberately scoped. All other phases (0–4) are out of scope here and get their own plans as they are reached (per the spec's *Sequencing* and this project's slice-by-slice rhythm). No Phase −1 requirement is unaddressed: the four-token rename map matches the spec's Naming table exactly, and the counter methods (`generateNew*Id`) are correctly left untouched.

**2. Placeholder scan.** No TBD/TODO; every step has an exact command and expected output.

**3. Type consistency.** `serialId()` and `id()` share the same return type `std::optional<uint32_t>`; `setSerialId(uint32_t)`→`setId(uint32_t)`; `OwnerId::serial_id` and `ParamAddress::clip_serial` are `uint32_t` before and after. The rename preserves all signatures — only identifiers change.

## Notes for the next phase

Phase 0 (id minted at construction, `id_` made non-`optional`, `Song&` threaded into ctors) is the next plan. It depends on this rename having landed so its diff is semantic-only. Before writing it, re-read the renamed `id()` accessor and the "never saved" readers (`grep -rn 'id().has_value\|!.*->id()' src/`) — those `nullopt`-as-unsaved sites are Phase 0's real work.
