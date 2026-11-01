// One param written across a kit's sound drums as one Edit, as legacy's affect-entire loops write it: every sound drum
// with a row in the clip takes the value, a sound drum with no row and the MIDI and gate drums are left alone, and one
// undo step restores every drum it wrote.

#include "deluge-project/param_address.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_kit_param_value.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

using namespace deluge::scenes;
namespace params = deluge::modulation::params;

namespace {

const deluge::plugin::ParamId kLpf = deluge::bridge::paramIdOf(params::Kind::PATCHED, params::LOCAL_LPF_FREQ);

/// The value @p drum's LPF frequency plays in @p clip, or nullopt where it has no row there.
std::optional<int32_t> lpf_of(Song& song, Clip& clip, Drum& drum) {
	const deluge::project::ParamAddress address{.owner = deluge::project::OwnerId::drum(*drum.id()),
	                                            .space = deluge::project::ParamSpace::ENGINE,
	                                            .id = kLpf,
	                                            .clip_id = clip.id()};
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
}

/// A kit with three sound drums, one of them with no row in the clip, a MIDI drum and a gate drum.
struct Rows {
	spec::SongFixture f = spec::make_kit_song();
	SoundDrum* first = spec::add_drum<SoundDrum>(f);
	SoundDrum* second = spec::add_drum<SoundDrum>(f);
	MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
	GateDrum* gate = spec::add_drum<GateDrum>(f);
	SoundDrum* rowless = nullptr;
	Rows() {
		rowless = static_cast<SoundDrum*>(StorageManager::createNewDrum(DrumType::SOUND));
		static_cast<Kit*>(f.instrument)->addDrum(rowless);
	}
	[[nodiscard]] uint32_t kit() const { return *f.instrument->id(); }
};

} // namespace

// clang-format off
describe set_kit_param_value("a param written across a kit's sound drums", ${
	it("reaches every sound drum with a row, and no other drum", _ {
		Rows r;
		const std::optional<int32_t> before = lpf_of(*r.f.song, *r.f.clip, *r.first);
		const int32_t wanted = before == 0x11111111 ? 0x22222222 : 0x11111111;
		deluge::edits::Context ctx{*r.f.song, r.f.song->project};
		deluge::edits::SetKitParamValue edit{r.kit(), r.f.clip->id(), kLpf, wanted};
		expect(deluge::edits::apply(ctx, edit)).to_equal(Error::NONE);
		expect(edit.drums_written()).to_equal(2u);
		expect(lpf_of(*r.f.song, *r.f.clip, *r.first)).to_equal(std::optional{wanted});
		expect(lpf_of(*r.f.song, *r.f.clip, *r.second)).to_equal(std::optional{wanted});
		expect(lpf_of(*r.f.song, *r.f.clip, *r.rowless).has_value()).to_be_false();
	});

	it("is one undo step, which restores every drum it wrote", _ {
		Rows r;
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		const std::optional<int32_t> first = lpf_of(*r.f.song, *r.f.clip, *r.first);
		const std::optional<int32_t> second = lpf_of(*r.f.song, *r.f.clip, *r.second);
		deluge::edits::Context ctx{*r.f.song, r.f.song->project};
		deluge::edits::SetKitParamValue edit{r.kit(), r.f.clip->id(), kLpf, 0x33333333};
		expect(deluge::edits::apply(ctx, edit)).to_equal(Error::NONE);
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action != nullptr && action->nextAction == nullptr).to_be_true();
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(lpf_of(*r.f.song, *r.f.clip, *r.first)).to_equal(first);
		expect(lpf_of(*r.f.song, *r.f.clip, *r.second)).to_equal(second);
		actionLogger.deleteAllLogs();
		nullifyUIs();
	});

	it("refuses a kit the song does not hold", _ {
		Rows r;
		deluge::edits::Context ctx{*r.f.song, r.f.song->project};
		deluge::edits::SetKitParamValue edit{r.kit() + 1000, r.f.clip->id(), kLpf, 0x11111111};
		expect(deluge::edits::apply(ctx, edit)).to_equal(Error::UNSPECIFIED);
		expect(edit.drums_written()).to_equal(0u);
	});
});

CPPSPEC_SPEC(set_kit_param_value)
