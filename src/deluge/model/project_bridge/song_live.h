#pragma once

#include "model/global_effectable/global_effectable_for_song.h"
#include "model/project_bridge/clips.h"
#include "model/project_bridge/media_pool.h"
#include "model/project_bridge/param_backup.h"
#include "modulation/params/param.h"

#include <array>
#include <cstdint>

/// @brief The parts of a song the engine holds as live objects, beside its model (`::Song::project`), until later
/// work turns them into model data.
struct SongLive {
	deluge::bridge::Clips clips;     ///< the session grid and the arrangement-only clips
	deluge::bridge::MediaPool media; ///< the output list and the hibernating instruments
	/// Clip whose loop length scales external input ticks (the sync master).
	Clip* sync_scaling_clip{nullptr};
	/// The song-global effect chain (master filters, FX, stutter, compressor).
	GlobalEffectableForSong global_effectable;
	/// Snapshot of the global unpatched param values, captured while a param is held in automation mode so the live
	/// (overridden) value can be restored afterwards.
	std::array<int32_t, deluge::modulation::params::kMaxNumUnpatchedParams> unautomated_param_values{};
	/// Parked ParamManagers, keyed by (ModControllableAudio, Clip); for a given ModControllableAudio the null-Clip
	/// entry sorts first. Node-based, so a ParamManager never moves in memory.
	deluge::bridge::ParamBackup param_backup;
};
