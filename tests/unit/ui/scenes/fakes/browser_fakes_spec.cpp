/// @file
/// @brief The browser, the context menu and the rename scene over the ports' fakes, with no app: the browser lists and
///        walks a catalogue fake's entries, each context-menu reset lands on the device actions' recorder, and the
///        rename keyboard types by the text keyboard's DEVICE row.

#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"

#include "gui/l10n/l10n.h"
#include "libdeluge/control_surface.h"
#include "project/device_settings.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/grid/keyboard.h"

#include "fakes.h"
#include "support/mocks/fake_surfaces.h"
#include "unit/ui/scenes/bind/support/model_fixture.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

#include "support/spec_main.h"

using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserId;
using deluge::ui::scenes::browser::kBrowserSceneKind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::rename::kRenameSceneKind;
using deluge::ui::scenes::rename::rename_scene;
using deluge::ui::scenes::rename::TargetId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

/// The select encoder's index on the board.
constexpr uint8_t kSelectEncoder = 3;

/// @brief The browser's listing work, parked until the spec runs it, as the host's card channel would.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	static bool dispatch(void (*f)(void*), void* c) {
		fn = f;
		ctx = c;
		return true;
	}
	static void run() {
		if (fn != nullptr) {
			auto* f = fn;
			fn = nullptr;
			f(ctx);
		}
	}
};

/// @brief A `Shell` over fake surfaces with the scenes' own registry: the whole stack a scene runs on, with no app.
struct Rig {
	deluge::ui::shell::SceneRegistry reg = deluge::ui::scenes::scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge::ui::scenes::deluge_profile()};
	Shell shell{fake, deluge::ui::scenes::deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	Rig() { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void step() {
		clock += 10;
		shell.tick(clock);
	}
	void turn(int32_t detents) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, clock + 5);
		step();
	}
	void pad(int32_t x, int32_t y, uint8_t velocity) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y), velocity}, clock + 5);
		step();
	}
};

/// @brief The QWERTY pad that types @p c, as the text keyboard lays it out.
std::array<int32_t, 2> qwerty_pad(char c) {
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < deluge::ui::widgets::grid::kTextKeyboardCols; x++) {
			if (deluge::ui::widgets::grid::text_keyboard_char_at(deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty,
			                                                     x, y, deluge::ui::widgets::grid::kTextKeyboardHomeRow,
			                                                     false)
			    == c) {
				return {x, y};
			}
		}
	}
	return {-1, -1};
}

} // namespace

// clang-format off
describe browser_fakes("the browser, the context menu and the rename scene over the fakes", ${
	it("lists a catalogue's entries and walks them with the select encoder", _ {
		// Declared first: the scene exits through the catalogue it was opened with as the rig tears down. The MIDI
		// browser opens on a MIDI track, which the fixture's song holds.
		deluge::ui::scenes::bind::test::ModelFixture f;
		f.host.catalogue.entries = {{"A.XML", false}, {"B", true}, {"C.XML", false}};
		f.host.catalogue.folder = "MIDI_DEVICES/DEFINITION";
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.track = deluge::ui::scenes::bind::test::kMidiTrack;
		ctx.addresses.instrument = deluge::plugin::SettingContext::Instrument::MIDI;
		ctx.ports = f.ports();
		Rig r;
		browser_scene().set_dispatch(&Deferred::dispatch);
		browser_scene().open(ctx);
		r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::MidiDeviceDefinition)});
		r.shell.tick(0);
		Deferred::run();
		r.step();
		r.step();
		expect(f.host.catalogue.listed.size()).to_equal(std::size_t{1});
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"A"});
		r.turn(1);
		expect(browser_scene().file_index()).to_equal(1);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"B"});
	});

	it("runs each reset on the device actions port", _ {
		using Kind = deluge::ui::scenes::bind::test::RecordingDeviceActions::Kind;
		using deluge::ui::scenes::context_menu::DescriptorId;
		const std::array<Kind, 5> kinds{Kind::RESET_SETTINGS, Kind::RESET_COMMUNITY_FEATURES, Kind::RESET_MIDI_FOLLOW,
		                                Kind::RESET_MIDI_DEVICES, Kind::FACTORY_RESET};
		for (int32_t i = 0; i <= static_cast<int32_t>(DescriptorId::ResetAll); i++) {
			deluge::ui::scenes::bind::test::RecordingDeviceActions actions;
			MenuContext ctx{};
			ctx.ports.device_actions = &actions;
			static_cast<void>(deluge::ui::scenes::context_menu::descriptor(static_cast<DescriptorId>(i)).accept(ctx, 0));
			expect(actions.calls.size()).to_equal(std::size_t{1});
			if (!actions.calls.empty()) {
				expect(static_cast<int32_t>(actions.calls.front()))
				    .to_equal(static_cast<int32_t>(kinds.at(static_cast<std::size_t>(i))));
			}
		}
	});

	it("types by the text keyboard's DEVICE row: the QWERTY Q pad types A on AZERTY", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		static_cast<void>(f.host.device.write(deluge::project::device_path::kTextKeyboardLayout, 1)); // AZERTY
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.clip = deluge::ui::scenes::bind::test::kSynthClip;
		ctx.ports = f.ports();
		Rig r;
		rename_scene().open(ctx);
		r.shell.init(SceneId{kRenameSceneKind, static_cast<uint16_t>(TargetId::Clip)});
		r.shell.tick(0);
		const std::array<int32_t, 2> q = qwerty_pad('Q');
		r.pad(q[0], q[1], 64);
		r.pad(q[0], q[1], 0);
		const std::string typed{rename_scene().entered_text()};
		expect(typed.size()).to_be_greater_than(std::size_t{0});
		if (!typed.empty()) {
			expect(static_cast<char>(std::tolower(static_cast<unsigned char>(typed.back())))).to_equal('a');
		}
	});
});
// clang-format on

DELUGE_SPEC(browser_fakes_spec, browser_fakes)
