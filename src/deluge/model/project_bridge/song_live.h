#pragma once

#include "model/project_bridge/clips.h"
#include "model/project_bridge/param_backup.h"

/// @brief The parts of a song the engine holds as live objects, beside its model (`::Song::project`), until later
/// work turns them into model data.
struct SongLive {
	deluge::bridge::Clips clips; ///< the session grid and the arrangement-only clips
	/// Parked ParamManagers, keyed by (ModControllableAudio, Clip); for a given ModControllableAudio the null-Clip
	/// entry sorts first. Node-based, so a ParamManager never moves in memory.
	deluge::bridge::ParamBackup param_backup;
};
