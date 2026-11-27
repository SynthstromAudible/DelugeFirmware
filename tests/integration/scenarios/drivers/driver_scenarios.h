/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "scenarios/support/scenario_declare.h"

/// @file
/// @brief The host-driver scenarios (scenarios/drivers/driver.h): each runs one driver's main once.
///
/// | scenario             | driver                                                                    |
/// | -------------------- | ------------------------------------------------------------------------- |
/// | audio_clip_edit_diff | an AudioClip descriptor edit + save/reload, printed for a baseline diff    |
/// | cable_decode         | the Deluge instrument's cable decoder against the app's reader, per element  |
/// | clip_clone_roundtrip | InstrumentClip::clone() on a loaded song, checked deep-independent         |
/// | edit_cost            | a settings Edit's host cost on a new synth, timed                          |
/// | source_memory        | a sample synth's heap across an oscillator switch away and back           |
/// | heap_footprint       | a loaded song's param managers, unpatched and arp sets, and heaps' use    |
/// | edit_substrate       | the Edit dispatcher and the SetParamValue edit, self-checking              |
/// | voice_pins           | which voice each cull, steal and admit takes, against its record           |
/// | edit_undo_redo       | reified Edits applied, undone and redone over a loaded song, self-checking |
/// | engine_settings      | a sound's settings through its slot: binding, every setting Edit, both equations |
/// | host_effect_settings | a host owner's effects through its chain: every effect setting Edit, coherence |
/// | instrument_codec_decode | the Deluge instrument's decode against the state the app's reader leaves     |
/// | instrument_codec_encode | the Deluge instrument's encode against the elements the app writes          |
/// | instrument_state_roundtrip | a sound's written element decodes and encodes back to itself            |
/// | json_roundtrip       | XML -> JSON -> JSON fixed point                                            |
/// | mapping_dispatch     | the mapping index against the dispatch's scans of learned bindings, seeded |
/// | model_decode_diff    | the model's decode of a saved song against the records the engine leaves   |
/// | param_clone          | ParamCollection clone deep independence, self-checking                     |
/// | patch_routing        | the cables' routing and save order over random topologies, pinned digest  |
/// | param_projection     | DocAutoParam <-> AutoParam projection, self-checking                       |
/// | param_record_diff    | live automation recording, printed for a baseline diff                     |
/// | param_snapshot_bench | ParamSnapshot::applyPatch against a full rebuild, timed                    |
/// | param_table_check    | the engines' param tables against the runtime's numbering and names        |
/// | param_target_check   | params named by target: the bridge's round trip, the target tables' cells  |
/// | preset_roundtrip     | preset load -> save -> reload -> save fixed point                          |
/// | serializer_check     | ValueSerializer against the JSON writer's parsed text, over a song's outputs |
/// | song_edit_roundtrip  | a DOM-first song-param edit persisting through save and reload             |
/// | song_roundtrip       | load -> save -> reload -> save fixed point                                 |
/// | xml_replay_diff      | ValueTreeDeserializer's XML replay against XMLDeserializer, token for token |

DELUGE_SCENARIO_DECLARE(audio_clip_edit_diff);
DELUGE_SCENARIO_DECLARE(cable_decode);
DELUGE_SCENARIO_DECLARE(clip_clone_roundtrip);
DELUGE_SCENARIO_DECLARE(document_version);
DELUGE_SCENARIO_DECLARE(edit_cost);
DELUGE_SCENARIO_DECLARE(source_memory);
DELUGE_SCENARIO_DECLARE(heap_footprint);
DELUGE_SCENARIO_DECLARE(edit_substrate);
DELUGE_SCENARIO_DECLARE(voice_pins);
DELUGE_SCENARIO_DECLARE(effect_owners);
DELUGE_SCENARIO_DECLARE(edit_undo_redo);
DELUGE_SCENARIO_DECLARE(engine_settings);
DELUGE_SCENARIO_DECLARE(song_settings);
DELUGE_SCENARIO_DECLARE(host_effect_settings);
DELUGE_SCENARIO_DECLARE(preset_parse);
DELUGE_SCENARIO_DECLARE(instrument_codec_decode);
DELUGE_SCENARIO_DECLARE(instrument_codec_encode);
DELUGE_SCENARIO_DECLARE(instrument_state_roundtrip);
DELUGE_SCENARIO_DECLARE(json_roundtrip);
DELUGE_SCENARIO_DECLARE(mapping_dispatch);
DELUGE_SCENARIO_DECLARE(model_decode_diff);
DELUGE_SCENARIO_DECLARE(param_clone);
DELUGE_SCENARIO_DECLARE(param_projection);
DELUGE_SCENARIO_DECLARE(param_record_diff);
DELUGE_SCENARIO_DECLARE(param_snapshot_bench);
DELUGE_SCENARIO_DECLARE(param_table_check);
DELUGE_SCENARIO_DECLARE(param_target_check);
DELUGE_SCENARIO_DECLARE(patch_routing);
DELUGE_SCENARIO_DECLARE(preset_roundtrip);
DELUGE_SCENARIO_DECLARE(serializer_check);
DELUGE_SCENARIO_DECLARE(song_edit_roundtrip);
DELUGE_SCENARIO_DECLARE(song_roundtrip);
DELUGE_SCENARIO_DECLARE(xml_replay_diff);
