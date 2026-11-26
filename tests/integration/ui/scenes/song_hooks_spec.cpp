// The song-level items a menu builds from hooks -- the swing interval, the reverb sidechain's volume and sync level, a
// song scale's toggle -- write the song's record through the menu's sink, as SONG requests.

#include "fakes.h"
#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/roots.h"
#include "ui/scenes/menu/nodes/scales.h"
#include "ui/scenes/menu/nodes/sound_fx.h"

#include "model/song/song.h"
#include "project/setting_group.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <string>

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuContext;
namespace nodes = deluge::ui::scenes::menu::nodes;

namespace {

/// @brief A song-level menu context over @p f's song.
MenuContext song_context(spec::SongFixture& f) {
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(nullptr), 0, false, true, true, false);
}

/// @brief Whether @p recording holds exactly one SONG request, for @p path, of @p value.
bool one_song_request(const bind::test::RecordingWrites& recording, std::string_view path, int32_t value) {
	return recording.requests.size() == 1 && recording.requests[0].target.kind == bind::Target::Kind::MODEL
	       && recording.requests[0].target.group == deluge::project::SettingGroupId::SONG
	       && recording.requests[0].target.path == path && recording.requests[0].menu_value == value;
}

} // namespace

// clang-format off
describe song_hooks("the song's hook-built items, by address", ${
	it("hands the swing interval to the sink as a SONG request, and the record holds it", _ {
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = song_context(f);
		bind::test::RecordingWrites recording;
		MenuContext recorded = ctx;
		recorded.ports.writes = &recording;
		menu::bindings::write(nodes::swing_interval, recorded, 3);
		expect(one_song_request(recording, "swingInterval", 3)).to_be_true();
		menu::bindings::write(nodes::swing_interval, ctx, 3);
		expect(static_cast<int32_t>(f.song->project.groove.swing_interval)).to_equal(3);
		expect(menu::bindings::read(nodes::swing_interval, ctx)).to_equal(3);
	});

	it("hands the reverb sidechain's volume and sync level to the sink as SONG requests", _ {
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = song_context(f);
		bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		menu::bindings::write(nodes::reverb_sidechain_volume, ctx, 10);
		expect(one_song_request(recording, "reverb/compressor/volume", 10 * 21474836)).to_be_true();
		recording.requests.clear();
		menu::bindings::write(nodes::reverb_sidechain_sync, ctx, 2);
		expect(recording.requests.size()).to_equal(1);
		if (recording.requests.size() == 1) {
			expect(recording.requests[0].target.path).to_equal("reverb/compressor/syncLevel");
		}
	});

	it("hands a song scale's toggle to the sink as a SONG request, and the record holds it", _ {
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = song_context(f);
		bind::test::RecordingWrites recording;
		MenuContext recorded = ctx;
		recorded.ports.writes = &recording;
		nodes::kSongActiveScalesItem.set_disabled(recorded, 2, true);
		expect(one_song_request(recording, "scales/disabled/2", 1)).to_be_true();
		nodes::kSongActiveScalesItem.set_disabled(ctx, 2, true);
		expect(static_cast<bool>(f.song->project.scale.disabled_preset_scales[2])).to_be_true();
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 2)).to_be_true();
	});
});

CPPSPEC_SPEC(song_hooks)
