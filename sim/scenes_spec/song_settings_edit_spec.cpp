/// @file
/// @brief `SetSetting` over a booted song: each setting group's Edit writes its record, then projects it onto the
///        engine object, and the `*SettingFor` helpers reach an owner outside the song through its setter alone.

#include "deluge-project/setting_group.h"
#include "model/edits/set_setting.h"
#include "model/edits/setting_edit.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::project::SongAddress;
using deluge::serialization::Value;

namespace {

const SettingAddress kSong{SettingGroupId::SONG, SongAddress{}};

/// @brief The record's value at @p path, as text.
std::string recorded(::Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::project::read_setting(song.project, address, path);
	return value ? value->str() : std::string{"<none>"};
}

/// @brief The engine copy's value at @p path, as text.
std::string projected(::Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::bridge::engineSettingValue(song, address, path);
	return value ? value->str() : std::string{"<none>"};
}

} // namespace

// clang-format off
describe song_settings_edit("SetSetting", $ {
	it("writes the record and projects it onto the song", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::songSettingFor("swingInterval", Value::makeNumber(5));
		expect(recorded(*f.song, kSong, "swingInterval")).to_equal(std::string{"5"});
		expect(projected(*f.song, kSong, "swingInterval")).to_equal(std::string{"5"});
	});

	it("refuses a value outside the row and leaves the record and the song as they were", _ {
		spec::SongFixture f = spec::make_synth_song();
		const deluge::project::Groove before = f.song->project.groove;
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{kSong, "swingInterval", Value::makeNumber(99)};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.song->project.groove == before).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(song_settings_edit_spec, song_settings_edit)
