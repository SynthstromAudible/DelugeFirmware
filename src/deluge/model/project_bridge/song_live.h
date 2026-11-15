#pragma once

#include "model/fx/master_channel.h"
#include "model/fx/song_stutter.h"
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
	/// The stutter playing on one of the song's owners, if any, and its tape. Declared before the master channel, so it
	/// outlives it: the channel's destruction asks whether the master is the one stuttering.
	deluge::model::SongStutter stutter;
	/// The song's master channel: its chain around the reverb (master filters, FX, stutter, compressor), its settings
	/// and view; the song's ModelStack owner slot.
	deluge::model::MasterChannel master_channel;
	/// Snapshot of the global unpatched param values, captured while a param is held in automation mode so the live
	/// (overridden) value can be restored afterwards.
	std::array<int32_t, deluge::modulation::params::kMaxNumUnpatchedParams> unautomated_param_values{};
	/// Parked ParamManagers, keyed by their owner's address and clip id; for a given owner the null-clip entry sorts
	/// first. Node-based, so a ParamManager never moves in memory.
	deluge::bridge::ParamBackup param_backup;
};
