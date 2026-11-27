# Parked until the DX7 engine's pages

The DX7 is a sound-level engine (`"mode": "dx7"`), and no oscillator can hold one, so the DX7 menu pages, the
cartridge browser and the keyboard's DX column cannot be reached until plan E points them at the engine. Their specs
wait here, outside every build glob:

- `dx_pages_spec.cpp`, `dx_cartridge_spec.cpp` and their rig `dx_rig.h` (from `tests/integration/ui/scenes/`);
- `menu_entry_dx7_cases.inc`: `menu_entry_spec`'s four DX7 cases;
- `dx_cartridge_spec`'s card, `tests/fixtures/scenes_spec/dx_cartridge`, named in `tests/integration/checks/scenes_spec_cards.txt`;
- song-preview-ab's DX stage (`SP-DX`, `SP-DX-STALL`), skipped in `song_preview_ab_scenario.cpp`'s stage order.

Plan E brings each back on a DX7-mode sound, and deletes this folder.

Until E, `ui-scenes-spec` also fails where a menu lists the oscillator types, since no oscillator's list offers DX7
any more: `descriptor_oracle_spec`, `setting_names_spec`, `tables_1_spec`, `tables_2_spec`, and the two specs whose
example of a stored choice that no longer applies was a DX7 on an oscillator (`stale_choice_scene_spec`,
`scenes_edit_sink_spec`). E re-records those tables with DX7 in the mode menu and gives the two specs another
example. The branch merges into `next` only after E.
