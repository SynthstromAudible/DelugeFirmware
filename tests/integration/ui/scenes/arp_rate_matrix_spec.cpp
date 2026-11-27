// A sound's arp rate is its arp's param: the matrix takes the arp-rate destination's base from the sound's arp set, not
// its patched set, and an edit of the arp's rate repatches the destination, at once with no cable or through the
// cable's sources with one.

#include "arp/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"
#include "model/edits/set_patch_cable.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_instrument.h"
#include "synth/matrix/param_format.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdint>
#include <optional>

namespace {

namespace arp = deluge::arp;
namespace classic = deluge::instruments::classic;
namespace params = deluge::modulation::params;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;

constexpr int32_t kArpWord = 1 << 29;
constexpr int32_t kPatchedWord = -(1 << 30);

/// The final arp rate the matrix last computed for @p sound, as its word.
int32_t final_arp_rate(const Sound& sound) {
	return deluge::synth::matrix::global_param_value<params::GLOBAL_ARP_RATE>(sound.params.param_final_values).raw();
}

/// Set @p synth's arp rate to @p word as a knob turn does.
Error turn_arp_rate(deluge::ui::scenes::spec::SongFixture& f, int32_t word) {
	const ParamAddress address{.owner = OwnerId::instrument(*f.instrument->id()),
	                           .space = ParamSpace::ARP,
	                           .id = arp::param::kRate,
	                           .clip_id = f.clip->id()};
	deluge::edits::Context ctx{*f.song, f.song->project};
	deluge::edits::SetParamValue turn{address, word};
	return deluge::edits::apply(ctx, turn);
}

/// A synth song whose clip the synth plays, so its param edits reach the matrix.
deluge::ui::scenes::spec::SongFixture make_playing_synth() {
	deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
	return f;
}

} // namespace

// clang-format off
describe arp_rate_matrix("a sound's arp rate, from its arp's set", ${
	it("is the base the matrix reads for the arp-rate destination", _ {
		deluge::ui::scenes::spec::SongFixture f = make_playing_synth();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		ParamManager& manager = f.clip->paramManager;
		manager.getArpParamSet()->set_word(arp::param::kRate, kArpWord);
		PatchedParamSet& patched = *manager.getPatchedParamSet();
		patched.params[params::GLOBAL_ARP_RATE].setBase(*patched.docForParam(params::GLOBAL_ARP_RATE), kPatchedWord);
		const deluge::synth::matrix::MatrixBlock block = synth->matrixBlock(manager, Sound::PresetGather::ALL);
		expect(block.preset_value(params::GLOBAL_ARP_RATE).raw()).to_equal(kArpWord);
		currentSong = before;
	});

	it("repatches at once when its rate is turned and nothing is patched to it", _ {
		deluge::ui::scenes::spec::SongFixture f = make_playing_synth();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		ParamManager& manager = f.clip->paramManager;
		synth->performInitialPatching(synth->matrixBlock(manager, Sound::PresetGather::ALL));
		const int32_t at_start = final_arp_rate(*synth);

		expect(turn_arp_rate(f, kArpWord)).to_equal(Error::NONE);
		expect(manager.getArpParamSet()->word(arp::param::kRate)).to_equal(kArpWord);
		const int32_t turned = final_arp_rate(*synth);
		expect(turned).not_().to_equal(at_start);

		synth->performInitialPatching(synth->matrixBlock(manager, Sound::PresetGather::ALL));
		expect(final_arp_rate(*synth)).to_equal(turned);
		currentSong = before;
	});

	it("marks the cable's sources changed when its rate is turned with a cable patched to it", _ {
		deluge::ui::scenes::spec::SongFixture f = make_playing_synth();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		const std::optional<deluge::plugin::ParamId> depth =
		    classic::cable_depth_id(classic::PatchSource::LFO_GLOBAL_1, classic::param::kArpRate);
		expect(depth.has_value()).to_be_true();
		if (depth) {
			const ParamAddress cable{.owner = OwnerId::instrument(*f.instrument->id()),
			                         .space = ParamSpace::ENGINE,
			                         .id = *depth,
			                         .clip_id = f.clip->id()};
			deluge::edits::Context ctx{*f.song, f.song->project};
			deluge::edits::SetPatchCable make{cable, 1 << 28, Polarity::BIPOLAR};
			expect(deluge::edits::apply(ctx, make)).to_equal(Error::NONE);
			const deluge::synth::RoutedDestination* destination =
			    f.clip->paramManager.getPatchCableSet()->getDestinationForParam(params::GLOBAL_ARP_RATE);
			expect(destination).not_().to_be_null();
			if (destination != nullptr) {
				synth->sourcesChanged.store(0);
				expect(turn_arp_rate(f, kArpWord)).to_equal(Error::NONE);
				expect(synth->sourcesChanged.load() & destination->sources).to_equal(destination->sources);
			}
		}
		currentSong = before;
	});
});
// clang-format on

DELUGE_SPEC(arp_rate_matrix_spec, arp_rate_matrix)
