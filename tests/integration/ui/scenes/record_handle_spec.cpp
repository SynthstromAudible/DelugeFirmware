/// @file
/// @brief A recorded write finds its lane's working copy by the handle it was made with, never a later copy's.

#include "model/clip/instrument_clip.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/record_events.h"
#include "modulation/automation/working_copies.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_instrument.h"

#include <optional>

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace bridge = deluge::bridge;
namespace wc = deluge::modulation;

namespace {

/// @brief The synth fixture's filter-frequency lane in its clip.
std::optional<bridge::LaneTarget> filter_lane(spec::SongFixture& f) {
	const deluge::project::ParamAddress address =
	    bridge::addressOf(deluge::project::OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
	                      params::LOCAL_LPF_FREQ, f.clip->id());
	return bridge::lane_exact(*f.song, address);
}

} // namespace

// clang-format off
describe record_handle("a working copy's handle", ${
	it("names its copy and the lane's AutoParam", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const wc::WorkingCopy made = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(made.copy).not_().to_be_null();
			expect(made.handle.generation).not_().to_equal(0u, "a made copy has a generation");
			expect(wc::copy_at(made.handle).first).to_equal(made.copy);
			expect(wc::copy_at(made.handle).second).to_equal(lane->auto_param);
			const wc::WorkingCopy again = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(again.copy).to_equal(made.copy, "a second make finds it");
			expect(again.handle.generation).to_equal(made.handle.generation, "under the same handle");
			wc::releaseWorkingCopies();
		}
		spec::make_current(nullptr);
	});

	it("names nothing once its copy is released, and never the slot's next copy", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const wc::WorkingCopy first = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(first.copy).not_().to_be_null();
			wc::releaseWorkingCopies();
			expect(wc::copy_at(first.handle).first).to_be_null();
			const wc::WorkingCopy second = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(int(second.handle.index)).to_equal(int(first.handle.index), "the slot is reused");
			expect(wc::copy_at(first.handle).first).to_be_null();
			expect(wc::copy_at(second.handle).first).to_equal(second.copy);
			wc::releaseWorkingCopies();
		}
		spec::make_current(nullptr);
	});

	it("names nothing when it names no copy", _ {
		expect(wc::copy_at(wc::CopyHandle{}).first).to_be_null();
		expect(wc::copy_at(wc::CopyHandle{.index = 200, .generation = 1}).first).to_be_null();
	});
});
// clang-format on

CPPSPEC_SPEC(record_handle)
