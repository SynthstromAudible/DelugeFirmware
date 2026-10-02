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

#include <cstdint>

/// @file
/// @brief The host-driver scenarios (scenarios/drivers/driver.h): each runs one driver's main once.
///
/// | scenario             | driver                                                                    |
/// | -------------------- | ------------------------------------------------------------------------- |
/// | audio_clip_edit_diff | an AudioClip descriptor edit + save/reload, printed for a baseline diff    |
/// | clip_clone_roundtrip | InstrumentClip::clone() on a loaded song, checked deep-independent         |
/// | edit_substrate       | the Edit dispatcher and the SetParamValue pilot, self-checking             |
/// | edit_undo_redo       | reified Edits applied, undone and redone over a loaded song, self-checking |
/// | json_roundtrip       | XML -> JSON -> JSON fixed point                                            |
/// | param_clone          | ParamCollection clone deep independence, self-checking                     |
/// | param_projection     | DocAutoParam <-> AutoParam projection, self-checking                       |
/// | param_record_diff    | live automation recording, printed for a baseline diff                     |
/// | param_snapshot_bench | ParamSnapshot::applyPatch against a full rebuild, timed                    |
/// | song_edit_roundtrip  | a DOM-first song-param edit persisting through save and reload             |
/// | song_roundtrip       | load -> save -> reload -> save fixed point                                 |
/// | xml_replay_diff      | ValueTreeDeserializer's XML replay against XMLDeserializer, token for token |

#define DELUGE_DRIVER_SCENARIO_DECLARE(name)                                                                           \
	void deluge_scenario_##name##_begin(void);                                                                         \
	bool deluge_scenario_##name##_complete(void);                                                                      \
	void deluge_scenario_##name##_dispatch_step(void);                                                                 \
	bool deluge_scenario_##name##_step_done(void);                                                                     \
	int32_t deluge_scenario_##name##_failures(void);

extern "C" {
DELUGE_DRIVER_SCENARIO_DECLARE(audio_clip_edit_diff)
DELUGE_DRIVER_SCENARIO_DECLARE(clip_clone_roundtrip)
DELUGE_DRIVER_SCENARIO_DECLARE(edit_substrate)
DELUGE_DRIVER_SCENARIO_DECLARE(edit_undo_redo)
DELUGE_DRIVER_SCENARIO_DECLARE(json_roundtrip)
DELUGE_DRIVER_SCENARIO_DECLARE(param_clone)
DELUGE_DRIVER_SCENARIO_DECLARE(param_projection)
DELUGE_DRIVER_SCENARIO_DECLARE(param_record_diff)
DELUGE_DRIVER_SCENARIO_DECLARE(param_snapshot_bench)
DELUGE_DRIVER_SCENARIO_DECLARE(song_edit_roundtrip)
DELUGE_DRIVER_SCENARIO_DECLARE(song_roundtrip)
DELUGE_DRIVER_SCENARIO_DECLARE(xml_replay_diff)
} // extern "C"

#undef DELUGE_DRIVER_SCENARIO_DECLARE
