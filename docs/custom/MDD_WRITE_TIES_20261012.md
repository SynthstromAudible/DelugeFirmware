# MDD WRITE automation, exclusive targets, Tie and Overlap

This adds the agreed MDD features on top of the existing 12 October beta + Plaits + Reverb #4990 custom build. Plaits DSP and upstream overload protection are unchanged.

## WRITE with Custom automation

WRITE now prepares Notes, Gates, Velocity and four Custom automation curves before replacing the Normal grid. It returns to Normal after confirmation. Undo/Redo restores or reapplies the complete old/new note rows, row expression, direction, Custom automation and written-source activation state together. Existing undo history is retained. Allocation/action failure leaves the current grid and automation intact.

The implementation records **native source-automation channels**, labelled Custom 1 Auto through Custom 4 Auto. The original target parameters and mod-matrix cables remain in place. This preserves base values, signed Depth, destination scaling/bounds and existing LFO/envelope connections; it also preserves Plaits Engine mapping. It does not freeze an envelope or LFO into a sampled final cutoff value. The target's own automation lane is not overwritten with an approximate converted value.

In Normal mode the recorded curves drive Custom 1–4 once. In MDD mode the live lanes drive them instead. There is no additional multiplication by the live step value. A single Custom can still control several different targets at their own Depths.

Open **MDD Automation** in the normal synth clip's OLED sound menu to access the four written channels with native parameter/automation editing and learning. This menu is shown after a successful synth WRITE while Normal is selected. Custom source values are displayed as 0–100%; Plaits Engine targets still select discrete engines via the existing mapping.

The written curves capture one clip loop from tick zero, including each lane's speed, traversal, shift, range values and held mute output. Random traversal becomes the recorded pattern for that loop. They follow native tick timing and repeat with the Normal clip. MIDI/CV WRITE still writes note/gate/velocity; these outputs have no internal synth Custom patch destinations.

Work and memory limits remain explicit: 65,536 emitted notes, 65,536 total automation nodes and 1,048,576 preparation ticks/iterations. Tie/Overlap note baking warms/captures/drains three cycles and therefore requires three times the loop length to fit the tick bound. Audio is serviced during preparation; no hardware timing guarantee is claimed.

## One Custom per destination

A target already assigned to another Custom is excluded from the Map selection. The restriction is also checked in the clip mapping bridge and native synth patch acceptance. Existing LFO/envelope sources remain permitted. A Custom may own several different destinations.

Legacy projects that already contain conflicting Custom cables may show an unavailable target. Remove the conflicting cable/mapping before reassigning it; the firmware does not silently rewrite the user's saved cables.

## Gates editor: Tie, Legato, Overlap

The first Gates page remains Step / Step Value / Steps / Shift. Use the normal MDD page button (SCALE or CROSS SCREEN) for the second page:

| Control | Meaning |
| --- | --- |
| Tie | 0/1 for the selected Gates cell. A tied trigger extends the most recent held note without retriggering it. |
| Overlap | 0–100% of a Notes interval beyond the following note boundary. Nonzero values hold through that boundary even at a short Gate setting. |
| Legato | 0/1. With Overlap at zero, enabling this retains the old note until one native tick after the new note starts. |

Tie uses the selected Gates cell, so independent Gates traversal/rate can determine which Notes triggers tie. While a tie continues, its new Notes-cell pitch and Velocity do not retrigger the previous note. A tied step without a held note starts one. Muted/zero-gate/zero-velocity triggers remain suppressed. Reset Gates/Reset All clears Tie, Legato and Overlap along with the gate values.

Tie chains can continue across MDD cycles. Legato/Overlap sends a new pitch before releasing the old one, allowing native strict-legato presets to glide; the preset still controls mono/poly behavior and portamento. Overlap can temporarily increase the number of sounding notes and CPU load. This adds no per-step chord editor.

Repeated identical pitches are released before retriggering unless tied. This avoids a delayed MIDI/CV/synth note-off cutting the newly triggered instance of the same pitch. Open pitches are tracked in a fixed 16-entry list; at capacity the earliest-ending note is released. Stop, seek, clip cleanup and mode changes release tracked notes without transport-side heap allocation.

WRITE captures wrapping ties as single notes across the loop boundary and overlap as overlapping stock notes. An all-tie cycle becomes one sustained loop-length note. Normal playback then uses Deluge's existing note, envelope, mono/legato and portamento behavior.

## Storage and validation

- Existing mddSnakeV1 clips load with Tie/Legato/Overlap disabled. New clips save mddSnakeV2, including validated tie bits, overlap and legato state.
- Written-source activation is saved per clip. Four appended native parameter IDs store Custom automation; existing parameter IDs retain their values.
- A new feature regression checks ties, wrapping WRITE, short-gate legato, same-pitch cleanup, 20,000 changing transport frames and V1/V2 data.
- Production WRITE and source-publication probes check four independent curves, native Undo/Redo, rollback at 38 allocation points and no double modulation.
- Target acceptance covers all four Custom sources and 35,136 envelope-destination comparisons, with an explicit cross-Custom conflict check.
- Existing MDD, Plaits and eight effect/menu regression groups remain required; ARM Release and GitHub formatting checks are required for publication.

Physical Deluge testing is still pending. Back up songs/presets: older firmware does not implement V2 articulation or these four automation channels. The earlier PDF and audit describe their dated snapshots; this document describes the new behavior.

## Publication: 13 October 2026

Source `03556b92957d4cef776b7940f08cd60d196d0ff9` was rebuilt with DBT v22 in ARM Release configuration. ELF32/ARM identification and exact ELF-to-BIN conversion passed. The source GitHub Format Check and feature regression suite passed in the preceding validation session. See [build record](../../firmware/BUILD_INFO.json) and [installation/download](../../firmware/INSTALL.md). Physical-device testing remains pending.
