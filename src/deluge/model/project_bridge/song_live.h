#pragma once

#include "model/fx/master_channel.h"
#include "model/fx/song_stutter.h"
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
	/// The stutter playing on one of the song's owners, if any, and its tape. Declared before the owners here, so it
	/// outlives the master's knobs (global_effectable), whose destruction asks whether the master is the one
	/// stuttering.
	deluge::model::SongStutter stutter;
	/// The song's master channel: its chain around the reverb (master filters, FX, stutter, compressor), its settings
	/// and view.
	deluge::model::MasterChannel master_channel;
	/// The song's knobs and menus over the master channel.
	GlobalEffectableForSong global_effectable{master_channel};
	/// Snapshot of the global unpatched param values, captured while a param is held in automation mode so the live
	/// (overridden) value can be restored afterwards.
	std::array<int32_t, deluge::modulation::params::kMaxNumUnpatchedParams> unautomated_param_values{};
	/// Parked ParamManagers, keyed by (ModControllableAudio, Clip); for a given ModControllableAudio the null-Clip
	/// entry sorts first. Node-based, so a ParamManager never moves in memory.
	deluge::bridge::ParamBackup param_backup;
};
