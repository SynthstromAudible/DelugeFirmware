#pragma once

/// @file
/// @brief `FakeOps`: a `SliceOps` that records rather than acts, for the slicer's specs over the app and over fakes.

#include "scenes/slicing/slicer_scene.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::scenes::slicing::test {

/// @brief A `SliceOps` that records rather than acts.
///
/// The calls behind `SliceOps` are the ones this layer may not make (see its docstring); a fake
/// is how the spec sees *that they were made, with what, and in what order* without a booted audio
/// engine, a kit, or a card. Everything the scene decides on its own -- the slice table, the modes,
/// the popup, the gate, the coalescer, the dispatch -- is real.
///
/// `play_position` is the one that answers back: it stands for the voice walk in
/// main's `Slicer::padAction` do-slice arm, and `nullopt` (its default) is main's
/// `voiceSample == nullptr`.
struct FakeOps final : SliceOps {
	int32_t stop_previewing_calls = 0;           ///< how many times `stop_previewing` was called
	int32_t kill_voices_calls = 0;               ///< how many times `kill_voices` was called
	int32_t restore_calls = 0;                   ///< how many times `restore_full_range` was called
	std::vector<PreviewRequest> auditions;       ///< every audition, in order
	std::vector<SliceCommit> commits;            ///< every commit begun, in order
	std::optional<int32_t> next_play_position{}; ///< what `play_position` answers
	int32_t play_position_calls = 0;             ///< how many times `play_position` was called
	int32_t cancel_audition_calls = 0;           ///< how many times `cancel_audition` was called
	int32_t closed_calls = 0;                    ///< how many times `closed` was called
	bool accept_commit = true;                   ///< what `begin_commit` answers

	void stop_previewing() override { stop_previewing_calls++; }
	void cancel_audition() override { cancel_audition_calls++; }
	void closed() override { closed_calls++; }
	void kill_voices() override { kill_voices_calls++; }
	void restore_full_range() override { restore_calls++; }
	void audition(const PreviewRequest& r) override { auditions.push_back(r); }
	bool begin_commit(const SliceCommit& c) override {
		commits.push_back(c);
		return accept_commit;
	}
	std::optional<int32_t> play_position() override {
		play_position_calls++;
		return next_play_position;
	}

	/// @brief Element @p i of a recorded vector, or a zeroed value if it was never recorded.
	///
	/// cppspec's `expect` does not stop the example, so a bare `auditions[0]` on a mutant that
	/// recorded nothing aborts the whole process inside libstdc++ and hides every other result.
	/// This accessor makes such a mutant fail loudly on the value.
	/// @{
	PreviewRequest audition_at(size_t i) const { return i < auditions.size() ? auditions[i] : PreviewRequest{}; }
	SliceCommit commit_at(size_t i) const { return i < commits.size() ? commits[i] : SliceCommit{}; }
	/// @}
};

} // namespace deluge::scenes::slicing::test
