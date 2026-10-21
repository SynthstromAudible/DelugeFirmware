// The song's, a MIDI or CV track's, a clip's, a kit row's and an audio clip's menus edit their records by descriptor:
// each node shown on a root reads its group's record and writes it through the menu's sink, on the owner its root
// names. A bend range no clip holds shows the device's default, the reverb sidechain's top shape step is the
// descriptor's, and a voice priority or pitch/speed menu edits an audio clip's own row there.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/target.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/sound_bend.h"
#include "scenes/menu/nodes/sound_fx.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_voice.h"

#include "deluge-project/setting_group.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

namespace {

namespace nodes = menu::nodes;

MenuContext at(Song& song, Clip* clip) {
	return menu::context_for(song, clip, 0, false, false, false, false);
}

/// The owner @p group's records take at @p ctx.
std::optional<SettingAddress> owner_of(SettingGroupId group, const MenuContext& ctx) {
	switch (group) {
	case SettingGroupId::SONG:
		return SettingAddress{group, deluge::project::SongAddress{}};
	case SettingGroupId::INSTRUMENT_DEVICE:
		return ctx.addresses.track ? std::optional{SettingAddress{group, *ctx.addresses.track}} : std::nullopt;
	case SettingGroupId::NOTE_ROW:
		return ctx.addresses.note_row ? std::optional{SettingAddress{group, *ctx.addresses.note_row}} : std::nullopt;
	default:
		return ctx.addresses.clip ? std::optional{SettingAddress{group, *ctx.addresses.clip}} : std::nullopt;
	}
}

/// Which of @p shown (each with the group its records are in), where relevant at @p ctx, has no target, names another
/// owner, or does not read back what it wrote.
std::vector<std::string> misbound(const std::string& root, const MenuContext& ctx,
                                  std::initializer_list<std::pair<const Node*, SettingGroupId>> shown) {
	std::vector<std::string> found;
	for (const auto& [node, group] : shown) {
		const std::string name = root + " " + std::string(node->bind.target.path);
		if (!node->bind.target.valid()) {
			found.push_back(root + ": a node has no target");
			continue;
		}
		if (!menu::bindings::relevant(*node, ctx)) {
			continue;
		}
		if (bind::setting_address(node->bind.target, ctx.addresses) != owner_of(group, ctx)) {
			found.push_back(name + ": edits another owner");
			continue;
		}
		const menu::IntRange range = menu::bindings::range(*node, ctx);
		const int32_t wanted = menu::bindings::read(*node, ctx) == range.max ? range.min : range.max;
		menu::bindings::write(*node, ctx, wanted);
		if (menu::bindings::read(*node, ctx) != wanted) {
			found.push_back(name + ": reads back " + std::to_string(menu::bindings::read(*node, ctx)) + " for "
			                + std::to_string(wanted));
		}
	}
	return found;
}

std::string recorded(Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::project::read_setting(song.project, address, path);
	return value ? value->str() : "none";
}

} // namespace

// clang-format off
describe model_nodes("the model groups' menus edit their records by descriptor", ${
	it("edits the song's reverb from a synth's menu", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound("synth", ctx,
		                {{&nodes::reverb_room_size, SettingGroupId::SONG}, {&nodes::reverb_damping, SettingGroupId::SONG},
		                 {&nodes::reverb_width, SettingGroupId::SONG}, {&nodes::reverb_model, SettingGroupId::SONG},
		                 {&nodes::reverb_pan, SettingGroupId::SONG}, {&nodes::reverb_hpf, SettingGroupId::SONG},
		                 {&nodes::reverb_lpf, SettingGroupId::SONG},
		                 {&nodes::reverb_sidechain_shape, SettingGroupId::SONG},
		                 {&nodes::reverb_sidechain_attack, SettingGroupId::SONG},
		                 {&nodes::reverb_sidechain_release, SettingGroupId::SONG},
		                 {&nodes::bend_range_main, SettingGroupId::CLIP_VALUES},
		                 {&nodes::bend_range_per_finger, SettingGroupId::CLIP_VALUES}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("edits a MIDI track's device and its clip's program and bend", _ {
		spec::SongFixture f = spec::make_midi_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound("midi", ctx,
		                {{&nodes::midi_hide_unlabeled, SettingGroupId::INSTRUMENT_DEVICE},
		                 {&nodes::mpe_y_to_mod_wheel, SettingGroupId::INSTRUMENT_DEVICE},
		                 {&nodes::midi_aftertouch_collapse, SettingGroupId::INSTRUMENT_DEVICE},
		                 {&nodes::midi_mpe_collapse, SettingGroupId::INSTRUMENT_DEVICE},
		                 {&nodes::midi_bank, SettingGroupId::CLIP_VALUES},
		                 {&nodes::midi_sub_bank, SettingGroupId::CLIP_VALUES},
		                 {&nodes::midi_pgm, SettingGroupId::CLIP_VALUES},
		                 {&nodes::bend_range_main, SettingGroupId::CLIP_VALUES},
		                 {&nodes::bend_range_per_finger, SettingGroupId::CLIP_VALUES}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("edits a CV track's second source", _ {
		spec::SongFixture f = spec::make_cv_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound("cv", ctx, {{&nodes::cv2_source, SettingGroupId::INSTRUMENT_DEVICE}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("edits a kit row's bend range on its note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound("kit row", ctx, {{&nodes::drum_bend_range, SettingGroupId::NOTE_ROW}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("edits an audio clip's own rows, its voice priority and pitch/speed included", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound("audio clip", ctx,
		                {{&nodes::audio_clip_transpose, SettingGroupId::AUDIO_CLIP},
		                 {&nodes::audio_clip_reverse, SettingGroupId::AUDIO_CLIP},
		                 {&nodes::audio_clip_interpolation, SettingGroupId::AUDIO_CLIP},
		                 {&nodes::audio_clip_attack, SettingGroupId::AUDIO_CLIP},
		                 {&nodes::voice_priority, SettingGroupId::AUDIO_CLIP},
		                 {&nodes::sample0_pitch_speed, SettingGroupId::AUDIO_CLIP}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("shows the device's default bend range for a clip that holds none, and keeps a written one", _ {
		spec::SongFixture f = spec::make_midi_song();
		const MenuContext ctx = at(*f.song, f.clip);
		const SettingAddress values{SettingGroupId::CLIP_VALUES, *ctx.addresses.clip};
		expect(deluge::project::holds_setting(f.song->project, values, "bendRange")).to_be_false();
		expect(menu::bindings::read(nodes::bend_range_main, ctx))
		    .to_equal(static_cast<int32_t>(FlashStorage::defaultBendRange[BEND_RANGE_MAIN]));
		menu::bindings::write(nodes::bend_range_main, ctx, 12);
		expect(recorded(*f.song, values, "bendRange")).to_equal("12");
		expect(menu::bindings::read(nodes::bend_range_main, ctx)).to_equal(12);
	});

	it("shows the bend range a clip's expression set plays, made after load, not the device's later default", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto* clip = static_cast<InstrumentClip*>(f.clip);
		clip->docClip_->values.bend_range.reset();
		clip->docClip_->values.bend_range_mpe.reset();
		ExpressionParamSet* expression = clip->paramManager.getOrCreateExpressionParamSet();
		expect(expression != nullptr).to_be_true();
		expression->bendRanges[BEND_RANGE_MAIN] = 5;
		expression->bendRanges[BEND_RANGE_FINGER_LEVEL] = 40;
		const uint8_t defaults[] = {FlashStorage::defaultBendRange[0], FlashStorage::defaultBendRange[1]};
		FlashStorage::defaultBendRange[0] = 9;
		FlashStorage::defaultBendRange[1] = 20;
		const MenuContext ctx = at(*f.song, f.clip);
		const int32_t main = menu::bindings::read(nodes::bend_range_main, ctx);
		const int32_t finger = menu::bindings::read(nodes::bend_range_per_finger, ctx);
		FlashStorage::defaultBendRange[0] = defaults[0];
		FlashStorage::defaultBendRange[1] = defaults[1];
		expect(main).to_equal(5);
		expect(finger).to_equal(40);
	});

	it("shows the finger bend range a kit row's expression set plays", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = at(*f.song, f.clip);
		NoteRow* row = static_cast<InstrumentClip*>(f.clip)->noteRows.tryGet(0);
		expect(row != nullptr && row->docNoteRow_ != nullptr).to_be_true();
		row->docNoteRow_->bend_range.reset();
		ExpressionParamSet* expression = row->paramManager.getOrCreateExpressionParamSet(true);
		expect(expression != nullptr).to_be_true();
		expression->bendRanges[BEND_RANGE_FINGER_LEVEL] = 17;
		expect(menu::bindings::read(nodes::drum_bend_range, ctx)).to_equal(17);
	});

	it("stores the descriptor's top step for the reverb sidechain's shape", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		menu::bindings::write(nodes::reverb_sidechain_shape, ctx, 50);
		expect(recorded(*f.song, {SettingGroupId::SONG, deluge::project::SongAddress{}}, "reverb/compressor/shape"))
		    .to_equal("2147483647");
	});

	it("repaints the grid when an audio clip's reverse changes", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = at(*f.song, f.clip);
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::audio_clip_reverse, ctx, 1);
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::GridMain)).to_be_true();
	});

	it("edits a synth's voice priority on its sound, and an audio clip's on the clip", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const MenuContext on_synth = at(*synth.song, synth.clip);
		expect(bind::setting_address(nodes::voice_priority.bind.target, on_synth.addresses).has_value()).to_be_false();
		menu::bindings::write(nodes::voice_priority, on_synth, 2);
		expect(static_cast<int>(static_cast<Sound*>(static_cast<SoundInstrument*>(synth.instrument))
		                            ->boundState()->voice_priority))
		    .to_equal(2);
	});
});

CPPSPEC_SPEC(model_nodes)
