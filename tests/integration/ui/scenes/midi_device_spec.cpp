/// @file
/// @brief The per-device MIDI page (`nodes/midi_device.h`) against the reference rows
///        `tests/golden/ui/menu_oracle/midi_device.tsv` holds for main's `midi::Device midiDeviceMenu`'s children and
///        the three-page MPE arm under it.
///
/// The port is **driven**: through `bindings::read`/`write` and a real `Shell` over the real input path, and its
/// result is compared with the frozen answer main gave for the same start.
///
/// ## The trap this spec is shaped against
///
/// Six near-identical per-cable leaves is exactly the shape where a positional test pins nothing:
/// `sendClock`, `receiveClock`, `is_relative` and `midi_thru` are four `bool`s on the same object, and a
/// `get`/`set` pair wired to the wrong one of them reads back perfectly from its own hook. So:
///
///   * **the mapping is proved by mutation, and asserted by identity.** Every comparison captures
///     the *whole* observable state of *both* cables as one string (`CableState::capture`) -- all
///     five per-cable fields and all four MPE member-channel numbers, on both -- so a swapped pair
///     of hooks changes the string even though every individual read still round-trips.
///   * **the fixture has two cables**, and every write is checked to leave the other one exactly
///     where it was. `MenuContext::midi_device` is the first binding input that is a *selection*
///     rather than a global, and a hook that reached for `MIDIDeviceManager::dinMIDIPorts` (or for
///     the wrong `MIDIPort` of the right cable) would pass a one-cable spec.
///   * **each example starts from the asymmetric baseline**, drives the port with a value, and compares the captured
///     state string with the frozen one: the port is compared with main's answer, not with a restatement of its own
///     rule.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/midi_device.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/sound_menu_open.h"

#include "support/global_guest_menu.h"
#include "support/midi_device_fixtures.h"
#include "support/oracle_table.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/gfx/support/pbm.h"

#include "fakes.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

using deluge::test::matchers::address_of;

namespace nodes = deluge::ui::scenes::menu::nodes;
namespace bindings = deluge::ui::scenes::menu::bindings;
using spec::mididev::baseline;
using spec::mididev::CableState;
using spec::mididev::chain_name;
using spec::mididev::Fixture;
using spec::mididev::fixture;
using spec::mididev::SpecCable;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
	deluge::base::notify::dirty().drain();
}

/// Board encoder index and the select-encoder button's matrix position (`ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

spec::OracleTable& table() {
	return spec::OracleTable::named("midi_device");
}

/// @brief Point the app's current song at the fixture's, as a booted app's is while the page is open.
void install_song(Song* song) {
	currentSong = song;
}

/// @brief The frozen values of (@p path, @p column) as a list, or one entry saying where a miss was looked for.
std::vector<std::string> frozen_list(const std::string& path, std::string_view column) {
	const std::vector<std::string>* v = table().find(path, column);
	return v == nullptr ? std::vector<std::string>{path + " <no row>"} : *v;
}

/// @brief A 128x64 single-panel board (`menu_scene_spec.cpp`'s `board_64`).
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `MenuScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	}
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One complete select press: down and up, then the tick that acts on the release.
	void press(uint32_t& at) {
		button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		shell.tick(at + 5);
		at += 10;
		at += 10;
	}
	/// @brief Walk the cursor @p rows down through the real input path, one detent per tick.
	void walk(int32_t rows, uint32_t& at) {
		for (int32_t i = 0; i < rows; i++) {
			turn(1, at);
			shell.tick(at + 5);
			at += 10;
		}
	}
};

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Whether @p pbm has a full-width rule with nothing lit above it: a title band holding only the separator.
bool title_band_blank(const std::string& pbm) {
	std::istringstream in(pbm);
	std::string magic;
	int w = 0;
	int h = 0;
	in >> magic >> w >> h;
	for (int y = 0; y < h; y++) {
		int lit = 0;
		for (int x = 0; x < w; x++) {
			int bit = 0;
			in >> bit;
			lit += bit;
		}
		if (lit == w) {
			return true;
		}
		if (lit != 0) {
			return false;
		}
	}
	return false;
}

/// @brief The per-device page on the GLOBAL guest over the clip view, as the device list opens it for @p ctx's cable.
bool open_device_page_on_guest(Fixture& f, const MenuContext& ctx) {
	install_song(f.song.song);
	setRootUILowLevel(&instrumentClipView);
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(nodes::midi_device, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::midi_device)});
	if (!openUI(&shellHostUI)) {
		return false;
	}
	(void)shellHostUI.timerCallback();
	return true;
}

/// @brief One SELECT press through the guest, and the ticks that act on its release.
void guest_select() {
	(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
	(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
	(void)shellHostUI.timerCallback();
	(void)shellHostUI.timerCallback();
}

/// @brief What the member-channel page reads for @p ctx at (@p direction, @p zone), off the scene.
int32_t member_channels(const MenuContext& ctx, uint8_t direction, uint8_t zone) {
	menu::MenuState st{};
	st.walk.mpe_direction = direction;
	st.walk.mpe_zone = zone;
	nodes::kMpeZoneChannelsItem.on_focus(ctx, st);
	return st.edit_int;
}

} // namespace

// clang-format off
describe midi_device_shape("scenes::menu: the per-device MIDI page's shape and chain", $ {

	it("declares main's six children, in its order and with its names", _ {
		select_language();
		expect(table().value("device", "kids")).to_equal(std::string{"6"});
		expect(std::to_string(nodes::midi_device.children.size())).to_equal(table().value("device", "kids"));
		expect(nodes::midi_device.kind).to_equal(menu::Kind::Submenu);

		for (size_t i = 0; i < nodes::midi_device.children.size(); i++) {
			const Node* n = nodes::midi_device.children[i];
			const std::string path = "device/" + std::to_string(i);
			expect(path + ": " + text_of(n->name)).to_equal(path + ": " + table().value(path, "name"));
		}
	});

	it("points each of the six at the item it is, and at no other -- by identity", _ {
		// The positional check above would pass with two leaves swapped, because a swap moves both
		// names together. This names each node's own identity: which `CustomItem` (or which pair of
		// hooks) it carries. A hook table shuffled between two of the three `Kind::Toggle` leaves is
		// caught here and again, behaviourally, in the write comparison below.
		expect(nodes::mpe_direction.custom).to_equal(&nodes::kMpeDirectionItem);
		expect(nodes::mpe_zone.custom).to_equal(&nodes::kMpeZoneItem);
		expect(nodes::mpe_zone_num_member_channels.custom).to_equal(&nodes::kMpeZoneChannelsItem);
		expect(nodes::kMpeDirectionItem.is_placeholder()).to_be_false();
		expect(nodes::kMpeZoneItem.is_placeholder()).to_be_false();
		expect(nodes::kMpeZoneChannelsItem.is_placeholder()).to_be_false();

		expect(address_of(nodes::device_default_velocity_to_level.bind.get))
		    .to_equal(address_of(&nodes::get_device_velocity_to_level));
		expect(address_of(nodes::device_default_velocity_to_level.bind.set))
		    .to_equal(address_of(&nodes::set_device_velocity_to_level));
		expect(address_of(nodes::device_send_clock.bind.get)).to_equal(address_of(&nodes::get_device_send_clock));
		expect(address_of(nodes::device_send_clock.bind.set)).to_equal(address_of(&nodes::set_device_send_clock));
		expect(address_of(nodes::device_receive_clock.bind.get)).to_equal(address_of(&nodes::get_device_receive_clock));
		expect(address_of(nodes::device_receive_clock.bind.set)).to_equal(address_of(&nodes::set_device_receive_clock));
		expect(address_of(nodes::device_is_relative.bind.get)).to_equal(address_of(&nodes::get_device_is_relative));
		expect(address_of(nodes::device_is_relative.bind.set)).to_equal(address_of(&nodes::set_device_is_relative));
		expect(address_of(nodes::device_send_thru.bind.get)).to_equal(address_of(&nodes::get_device_send_thru));
		expect(address_of(nodes::device_send_thru.bind.set)).to_equal(address_of(&nodes::set_device_send_thru));

		// The three toggles are `Kind::Toggle` because their legacy items are `Toggle`s
		// (`origin/main:src/deluge/gui/menu_item/midi/device_send_clock.h`,
		// `origin/main:src/deluge/gui/menu_item/midi/device_receive_clock.h`,
		// `origin/main:src/deluge/gui/menu_item/midi/device_is_relative.h`); the velocity one is an `IntegerWithOff`
		// (`origin/main:src/deluge/gui/menu_item/midi/default_velocity_to_level.h`), so it is `Kind::Int` with
		// `off_at_zero` and `kMaxMenuValue` bounds.
		expect(nodes::device_send_clock.kind).to_equal(menu::Kind::Toggle);
		expect(nodes::device_receive_clock.kind).to_equal(menu::Kind::Toggle);
		expect(nodes::device_is_relative.kind).to_equal(menu::Kind::Toggle);
		expect(nodes::device_send_thru.kind).to_equal(menu::Kind::Toggle);
		expect(nodes::device_default_velocity_to_level.kind).to_equal(menu::Kind::Int);
		expect(nodes::device_default_velocity_to_level.bind.style.off_at_zero).to_be_true();
		expect(bindings::range(nodes::device_default_velocity_to_level, fixture().ctx).max).to_equal(kMaxMenuValue);
	});

	it("chains from the device list onto the page, where main chains, driven through the scene", _ {
		// Main's `Devices::selectButtonPress` returns `&midiDeviceMenu` unconditionally; driven through the real scene
		// and the real input path.
		select_language();
		Fixture& f = fixture();
		install_song(f.song.song);

		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		Rig r;
		r.open(nodes::midi_devices, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at);
		expect(menu_scene().focused()).to_equal(&nodes::midi_device);
		expect(chain_name(menu_scene().focused())).to_equal(table().value("devices", "chain"));
	});

	it("chains onward through the MPE arm exactly as main's two selectors do", _ {
		// Main's direction selector chains to the zone selector, and the zone selector to the member-channel page.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // row 0 is `mpe_direction`
		expect(menu_scene().focused()).to_equal(&nodes::mpe_direction);
		r.press(at);
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone);
		expect(chain_name(menu_scene().focused())).to_equal(table().value("mpe_direction", "chain"));
		r.press(at);
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone_num_member_channels);
		expect(chain_name(menu_scene().focused())).to_equal(table().value("mpe_zone", "chain"));
	});

	it("declares the page and its three chain-only MPE nodes with ids, and validates them clean", _ {
		// The reachability half: `assign_node_ids` and `validate_tree` follow `chain_targets`, so a
		// page reachable only through a chain is still numbered (addressable by `SceneId::arg`) and
		// still checked. Without `midi_devices`' `select_chain` none of these nine had an id at all.
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (const Node* n : {&nodes::midi_device, &nodes::mpe_direction, &nodes::mpe_zone,
		                      &nodes::mpe_zone_num_member_channels, &nodes::device_default_velocity_to_level,
		                      &nodes::device_send_clock, &nodes::device_receive_clock, &nodes::device_is_relative,
		                      &nodes::device_send_thru}) {
			expect(text_of(n->name) + " id != 0").to_equal(text_of(n->name) + (menu::node_id(*n) != 0 ? " id != 0"
			                                                                                          : " UNNUMBERED"));
		}

		const Node* subtree[] = {&nodes::midi_devices};
		const MenuContext contexts[] = {fixture().ctx};
		const auto issues = menu::validate_tree(subtree, contexts, {}, {},
		                                        menu::ValidateOptions{.skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});
});

describe midi_device_per_cable("scenes::menu: the per-cable binding family", $ {
	// Each example: baseline both cables, drive the PORT hook, capture both cables; baseline again,
	// drive the LEGACY item from its own value, capture again; compare the two strings. The capture
	// is every field on both cables, so "wrote the right field on the right cable" and "left
	// everything else alone" are the same assertion.

	it("writes what main's DefaultVelocityToLevel writes, on the selected cable and no other", _ {
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		for (const int32_t value : {0, 1, 25, kMaxMenuValue}) {
			baseline(*f.a, *f.b);
			install_song(f.song.song);
			bindings::write(nodes::device_default_velocity_to_level, ctx, value);
			const std::string path = "velocity/" + std::to_string(value);
			expect(path + " " + CableState::capture(*f.a, *f.b)).to_equal(path + " " + table().value(path, "state"));
			expect(path + " a " + std::to_string(f.a->defaultVelocityToLevel))
			    .to_equal(path + " a " + table().value(path, "cable/0"));
			expect(path + " b " + std::to_string(f.b->defaultVelocityToLevel))
			    .to_equal(path + " b " + table().value(path, "cable/1"));

			// ... and the port reads back what main reads back, from the same state.
			expect(path + " " + std::to_string(bindings::read(nodes::device_default_velocity_to_level, ctx)))
			    .to_equal(path + " " + table().value(path, "read"));
		}
	});

	it("writes every synth's and sound row's velocity cable in every clip, in the record, with no undo step", _ {
		// `DefaultVelocityToLevel::writeCurrentValue`'s tail,
		// `Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything`: every synth and sound row whose input
		// is the cable takes its default in every clip.
		spec::SongFixture f = spec::make_kit_song();
		Song* const before = currentSong;
		currentSong = f.song;
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		InstrumentClip* synth_clip = spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->dirPath = "SYNTHS";
		synth->name = "VEL";
		synth->setupAsDefaultSynth(&params);
		spec::detail::attach(f.song, synth_clip, synth, params);

		SpecCable cable{"CABLE V"};
		cable.defaultVelocityToLevel = 0;
		for (::LearnedMIDI* input : {&synth->midiInput, &drum->midiInput}) {
			input->cable = &cable;
			input->channelOrZone = 0;
		}
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false,
		                               false);
		ctx.midi_device = deluge::midi::id_of(cable);
		actionLogger.deleteAllLogs();

		bindings::write(nodes::device_default_velocity_to_level, ctx, 25);

		const int32_t want = cable.defaultVelocityToLevel;
		expect(want).not_().to_equal(0);
		deluge::synth::CableTarget volume = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_VOLUME);
		const deluge::plugin::ParamId depth = deluge::bridge::cable_depth(PatchSource::VELOCITY, volume);
		const auto recorded = [&](deluge::project::OwnerId owner, const Clip& clip) -> std::optional<int32_t> {
			const deluge::project::DocAutoParam* param = f.song->project.resolveParam(
			    {.owner = owner, .space = deluge::project::ParamSpace::ENGINE, .id = depth, .clip_id = clip.id()});
			return param != nullptr ? std::optional{param->current_value} : std::nullopt;
		};
		expect(recorded(deluge::project::OwnerId::instrument(*synth->id()), *synth_clip)).to_equal(std::optional{want});
		expect(recorded(deluge::project::OwnerId::drum(*drum->id()), *f.clip)).to_equal(std::optional{want});
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
		currentSong = before;
	});

	it("writes each of the four per-device clock/relative/thru toggles to its own field, and only it", _ {
		// The mutation the identity table exists for: four `bool`s on the same object. A `set` hook
		// pointed at the wrong one round-trips perfectly through its own `get`, and is caught here
		// only because the capture covers all four on both cables.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		struct Row {
			const Node* node;
			const char* what;
		};
		const Row rows[] = {
		    {&nodes::device_send_clock, "sendClock"},
		    {&nodes::device_receive_clock, "receiveClock"},
		    {&nodes::device_is_relative, "is_relative"},
		    {&nodes::device_send_thru, "midi_thru"},
		};

		for (const Row& row : rows) {
			for (const int32_t value : {0, 1}) {
				baseline(*f.a, *f.b);
				install_song(f.song.song);
				bindings::write(*row.node, ctx, value);
				const std::string path = std::string{"toggle/"} + row.what + "/" + std::to_string(value);
				expect(path + " " + CableState::capture(*f.a, *f.b))
				    .to_equal(path + " " + table().value(path, "state"));
				const std::vector<std::string> fields{std::to_string(int(f.a->sendClock)),
				                                      std::to_string(int(f.a->receiveClock)),
				                                      std::to_string(int(f.a->is_relative)),
				                                      std::to_string(int(f.a->midi_thru))};
				expect(frozen_list(path, "fields")).to_equal(fields);
				expect(path + " " + std::to_string(bindings::read(*row.node, ctx)))
				    .to_equal(path + " " + table().value(path, "read"));
			}
		}
	});

	it("follows the selection: the same node writes cable B once the context names cable B", _ {
		// The wrong-cable catch, stated directly. Nothing about the node changes between the two
		// halves -- only `MenuContext::midi_device` -- so a hook that reached for anything other than
		// that field (a global, the first hosted device, the DIN ports) fails here.
		select_language();
		Fixture& f = fixture();
		MenuContext on_a = f.ctx;
		on_a.midi_device = deluge::midi::id_of(*f.a);
		MenuContext on_b = f.ctx;
		on_b.midi_device = deluge::midi::id_of(*f.b);

		// Each write is deliberately a **no-op on the cable it addresses** and would be a *change*
		// on the other one, because `baseline()` gives the two cables opposite values, so a write of the value
		// the neighbour already held would pass whichever cable the hook reached. The four assertions below can therefore only hold if
		// the write landed where it was addressed.
		baseline(*f.a, *f.b);
		bindings::write(nodes::device_is_relative, on_a, 0); // A is already false, B is true
		bindings::write(nodes::device_send_clock, on_b, 0);  // B is already false, A is true
		expect(f.a->is_relative).to_be_false();
		expect(f.b->is_relative).to_be_true(); // a write that reached B would have cleared it
		expect(f.b->sendClock).to_be_false();
		expect(f.a->sendClock).to_be_true(); // a write that reached A would have cleared it

		baseline(*f.a, *f.b);
		bindings::write(nodes::device_send_clock, on_b, 1);
		expect(f.b->sendClock).to_be_true();
		expect(f.a->sendClock).to_be_true();     // A's baseline is true and stays true ...
		expect(f.a->receiveClock).to_be_false(); // ... and nothing else on A moved either
		expect(f.a->is_relative).to_be_false();

		// And reads follow the selection too, on a field whose two cables differ.
		baseline(*f.a, *f.b);
		expect(bindings::read(nodes::device_is_relative, on_a)).to_equal(0);
		expect(bindings::read(nodes::device_is_relative, on_b)).to_equal(1);
	});

	it("raises MIDIDeviceManager::anyChangesToSave on every one of the five, as legacy does", _ {
		// Assert-then-clear, not defensive-clear: the flag is a process global shared with every
		// other example in this binary, so each iteration asserts the state it needs *before*
		// setting up, rather than quietly forcing it.
		//
		// Each write changes its field, as every write the page makes does (a toggle's turn flips it, and an int's turn
		// moves it or is not committed): the host marks the devices on a change.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		const bool saved = MIDIDeviceManager::anyChangesToSave;

		for (const Node* n : {&nodes::device_default_velocity_to_level, &nodes::device_send_clock,
		                      &nodes::device_receive_clock, &nodes::device_is_relative, &nodes::device_send_thru}) {
			baseline(*f.a, *f.b);
			MIDIDeviceManager::anyChangesToSave = false;
			expect(MIDIDeviceManager::anyChangesToSave).to_be_false();
			bindings::write(*n, ctx, bindings::read(*n, ctx) == 0 ? 1 : 0);
			expect(text_of(n->name) + " marked dirty")
			    .to_equal(text_of(n->name) + (MIDIDeviceManager::anyChangesToSave ? " marked dirty" : " LEFT CLEAN"));
		}
		MIDIDeviceManager::anyChangesToSave = saved;
	});

	it("reads 0 and writes nothing when the context names no cable", _ {
		// The port's own guard (`nodes/midi_device.h`): legacy cannot reach the page without
		// `Devices::beginSession` having assigned the pointer, this port can be pointed straight at
		// the node by a `SceneId` deep link.
		select_language();
		Fixture& f = fixture();
		MenuContext none = f.ctx;
		none.midi_device = {};
		baseline(*f.a, *f.b);
		const std::string before = CableState::capture(*f.a, *f.b);
		for (const Node* n : {&nodes::device_default_velocity_to_level, &nodes::device_send_clock,
		                      &nodes::device_receive_clock, &nodes::device_is_relative, &nodes::device_send_thru}) {
			expect(bindings::read(*n, none)).to_equal(0);
			bindings::write(*n, none, 1);
		}
		expect(CableState::capture(*f.a, *f.b)).to_equal(before);
	});
});

describe midi_device_host_writes("scenes::menu: the host's MIDI devices port applies what the page's items applied", $ {
	// `MidiDevices::write` takes a device's whole settings; the host applies what changed, in the order legacy's items
	// did: the fields, the recount for a zone change, the save mark, then an output zone's MCM.

	it("sends an output zone change to the device as its MCM, and an input zone change not at all", _ {
		select_language();
		Fixture& f = fixture();
		bind::MidiDevices& midi = *deluge::gui::shell_host::host_ports().midi;
		const deluge::midi::DeviceId a = deluge::midi::id_of(*f.a);

		baseline(*f.a, *f.b);
		std::optional<bind::MidiDeviceSettings> s = midi.settings(a);
		expect(s.has_value()).to_be_true();
		if (!s) {
			return;
		}
		s->zones[std::to_underlying(bind::MidiDirection::OUT)].lower_last_member = 7;
		expect(midi.write(a, *s)).to_be_true();
		expect(int(f.a->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel)).to_equal(7);
		// `sendRPN(0, 0, 6, 7)`: RPN LSB, RPN MSB, the value, then the two null-RPN CCs.
		expect(f.a->sent).to_equal(std::vector<std::string>{"0:100=6", "0:101=0", "0:6=7", "0:100=127", "0:101=127"});

		f.a->sent.clear();
		s->zones[std::to_underlying(bind::MidiDirection::OUT)].upper_last_member = 12;
		expect(midi.write(a, *s)).to_be_true();
		// `sendRPN(15, 0, 6, 15 - 12)`, on the upper zone's master channel.
		expect(f.a->sent).to_equal(
		    std::vector<std::string>{"15:100=6", "15:101=0", "15:6=3", "15:100=127", "15:101=127"});

		f.a->sent.clear();
		s->zones[std::to_underlying(bind::MidiDirection::IN)].lower_last_member = 9;
		expect(midi.write(a, *s)).to_be_true();
		expect(int(f.a->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel)).to_equal(9);
		expect(f.a->sent.empty()).to_be_true();
	});

	it("recounts the smallest zones on any zone change, and marks the devices on any change", _ {
		select_language();
		bind::MidiDevices& midi = *deluge::gui::shell_host::host_ports().midi;
		MIDICable& din = MIDIDeviceManager::dinMIDIPorts;
		const uint8_t saved_flags = din.connectionFlags;
		const MIDIPort saved_out = din.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE];
		const MIDIPort saved_in = din.ports[MIDI_DIRECTION_INPUT_TO_DELUGE];
		const bool saved_dirty = MIDIDeviceManager::anyChangesToSave;

		// DIN connected, with an output lower zone the recount has not seen yet.
		din.connectionFlags = 1;
		din.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel = 6;
		MIDIDeviceManager::recountSmallestMPEZones();
		din.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel = 4;
		expect(int(MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput)).to_equal(6);

		// An unchanged write applies nothing.
		std::optional<bind::MidiDeviceSettings> s = midi.settings(deluge::midi::kDin);
		expect(s.has_value()).to_be_true();
		if (s) {
			MIDIDeviceManager::anyChangesToSave = false;
			expect(midi.write(deluge::midi::kDin, *s)).to_be_true();
			expect(MIDIDeviceManager::anyChangesToSave).to_be_false();
			expect(int(MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput)).to_equal(6);

			// An input zone change recounts (and so sees the output's 4) and marks.
			s->zones[std::to_underlying(bind::MidiDirection::IN)].upper_last_member = 13;
			expect(midi.write(deluge::midi::kDin, *s)).to_be_true();
			expect(int(MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput)).to_equal(4);
			expect(MIDIDeviceManager::anyChangesToSave).to_be_true();

			// A clock change marks and does not recount.
			din.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel = 2;
			MIDIDeviceManager::anyChangesToSave = false;
			s = midi.settings(deluge::midi::kDin);
			if (s) {
				s->receive_clock = !s->receive_clock;
				expect(midi.write(deluge::midi::kDin, *s)).to_be_true();
			}
			expect(MIDIDeviceManager::anyChangesToSave).to_be_true();
			expect(int(MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput)).to_equal(4);
			din.receiveClock = !din.receiveClock;
		}

		din.connectionFlags = saved_flags;
		din.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE] = saved_out;
		din.ports[MIDI_DIRECTION_INPUT_TO_DELUGE] = saved_in;
		MIDIDeviceManager::recountSmallestMPEZones();
		MIDIDeviceManager::anyChangesToSave = saved_dirty;
	});

	it("writes the velocity default, then has every input on the device take it", _ {
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		bind::test::RecordingSoundWrites writes;
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		ctx.ports.sound_writes = &writes;

		bindings::write(nodes::device_default_velocity_to_level, ctx, 25);
		expect(f.a->defaultVelocityToLevel).to_equal(int32_t{25 * (2147483648 / (kMaxMenuValue * 2))});
		expect(writes.calls.size()).to_equal(size_t{1});
		if (writes.calls.size() == 1) {
			expect(writes.calls[0].kind).to_equal(bind::test::RecordingSoundWrites::Call::Kind::VELOCITY_EVERYWHERE);
			expect(writes.calls[0].device.value).to_equal(ctx.midi_device.value);
		}

		// Legacy re-patches on every write, a value the device already holds included.
		bindings::write(nodes::device_default_velocity_to_level, ctx, 25);
		expect(writes.calls.size()).to_equal(size_t{2});

		// No device: nothing written, nothing re-patched.
		ctx.midi_device = {};
		bindings::write(nodes::device_default_velocity_to_level, ctx, 10);
		expect(writes.calls.size()).to_equal(size_t{2});
	});
});

describe midi_device_mpe("scenes::menu: the MPE direction, zone and member-channel pages", $ {

	it("resets the direction and the zone on a forward entry, and keeps them on the way back", _ {
		// `DirectionSelector::beginSession` / `ZoneSelector::beginSession` assign their member only when
		// `navigatedBackwardFrom == nullptr` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`,
		// `origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`); `CustomItem::on_focus` fires only on a
		// descent, which is that condition one hop earlier.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // into `mpe_direction`
		expect(int(menu_scene().state().walk.mpe_direction)).to_equal(MIDI_DIRECTION_INPUT_TO_DELUGE);

		r.walk(1, at); // OUT
		expect(int(menu_scene().state().walk.mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);

		r.press(at); // into `mpe_zone`; the direction stays where the user left it
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone);
		expect(int(menu_scene().state().walk.mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(MPE_ZONE_LOWER_NUMBERED_FROM_0);

		r.walk(1, at); // UPPER ZONE
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
		r.press(at); // into the member-channel page: both selections survive the descent
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone_num_member_channels);
		expect(int(menu_scene().state().walk.mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
	});

	it("a re-read of the zone page keeps the zone the user is on, as ZoneSelector::readValueAgain does", _ {
		// `ZoneSelector::readCurrentValue` is `setValue(whichZone)` and only `beginSession` zeroes `whichZone`.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // into `mpe_direction`
		r.press(at); // into `mpe_zone`
		r.walk(1, at); // UPPER ZONE
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
		menu_scene().read_value_again();
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
		expect(int(menu_scene().state().list[menu_scene().state().depth].cursor)).to_equal(1);
	});

	it("a re-read of the member-channel page takes the port's value again, as readCurrentValue does", _ {
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // into `mpe_direction`
		r.press(at); // into `mpe_zone`
		r.press(at); // into the member-channel page
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone_num_member_channels);
		menu_scene().state_for_spec().edit_int = 99;
		menu_scene().read_value_again();
		expect(menu_scene().state().edit_int)
		    .to_equal(nodes::mpe_zone_channels_value(ctx, menu_scene().state()));
	});

	it("reads and bounds the member-channel page exactly as main's item does, over all four "
	   "(direction, zone) pairs and both cables",
	   _ {
		   // Main's `ZoneNumMemberChannels::readCurrentValue` and `getMaxValue`; the port reads from `MenuState`.
		   select_language();
		   Fixture& f = fixture();
		   for (SpecCable* cable : {f.a.get(), f.b.get()}) {
			   baseline(*f.a, *f.b);
			   MenuContext ctx = f.ctx;
			   ctx.midi_device = deluge::midi::id_of(*cable);
			   install_song(f.song.song);
			   const std::string which = cable == f.a.get() ? "a" : "b";

			   for (uint8_t direction = 0; direction < 2; direction++) {
				   for (uint8_t zone = 0; zone < 2; zone++) {
					   // A `MenuState` of this example's own rather than the scene singleton's:
					   // `MenuScene::state()` is const, and the item reads nothing else.
					   menu::MenuState st{};
					   st.walk.mpe_direction = direction;
					   st.walk.mpe_zone = zone;
					   nodes::kMpeZoneChannelsItem.on_focus(ctx, st);

					   const std::string path = "mpe/" + std::to_string(direction) + "/" + std::to_string(zone);
					   const std::string label = std::string{cable->getDisplayName()} + " " + path + ": ";
					   expect(label + std::to_string(st.edit_int))
					       .to_equal(label + table().value(path, "read@" + which));
					   expect(label + "max " + std::to_string(nodes::mpe_zone_channels_range(ctx, st).max))
					       .to_equal(label + "max " + table().value(path, "max@" + which));
					   expect(label + "min " + std::to_string(nodes::mpe_zone_channels_range(ctx, st).min))
					       .to_equal(label + "min 0"); // `Number::getMinValue`, `origin/main:src/deluge/gui/menu_item/number.h`
				   }
			   }
		   }
	   });

	it("writes the member channels onto the port the two selectors name, and onto no other", _ {
		// The 2x2 mutation: four (direction, zone) pairs address four distinct `uint8_t`s on one
		// cable, and a port index or a zone branch taken the wrong way round still round-trips
		// through its own read. Every write is compared against what main's item left from the same
		// pair, with the *whole* two-cable state as the verdict.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);

		for (uint8_t direction = 0; direction < 2; direction++) {
			for (uint8_t zone = 0; zone < 2; zone++) {
				for (const int32_t value : {0, 4, 7}) {
					baseline(*f.a, *f.b);
					install_song(f.song.song);
					menu::MenuState st{};
					st.walk.mpe_direction = direction;
					st.walk.mpe_zone = zone;
					st.edit_int = value;
					nodes::kMpeZoneChannelsItem.commit(ctx, st);

					const std::string path =
					    "mpe/" + std::to_string(direction) + "/" + std::to_string(zone) + "/" + std::to_string(value);
					expect(path + " " + CableState::capture(*f.a, *f.b))
					    .to_equal(path + " " + table().value(path, "state"));
					const std::vector<std::string> ports{std::to_string(f.a->ports[0].mpeLowerZoneLastMemberChannel),
					                                     std::to_string(f.a->ports[0].mpeUpperZoneLastMemberChannel),
					                                     std::to_string(f.a->ports[1].mpeLowerZoneLastMemberChannel),
					                                     std::to_string(f.a->ports[1].mpeUpperZoneLastMemberChannel)};
					expect(frozen_list(path, "ports")).to_equal(ports);
					// The MCM tail: five CCs on the master channel, but only for an OUT port.
					expect(frozen_list(path, "sent")).to_equal(f.a->sent);
					if (direction == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) {
						expect(path + " " + std::to_string(f.a->sent.size())).to_equal(path + " 5");
					}
					else {
						expect(path + " " + std::to_string(f.a->sent.size())).to_equal(path + " 0");
					}
				}
			}
		}
	});

	it("titles the direction page from its own value, as main's DirectionSelector::getTitle does", _ {
		// The runtime title. `Node::title` is one static id, which is half of why this item is bespoke.
		//
		// **What this pins, and what it does not.** The frozen titles are main's; the port's side is its mapping from
		// the direction to an l10n id, so swapping the two ids inside `MpeDirectionItem::render` still passes here. What
		// catches that swap is the pair of goldens in `midi_device_pixels` below, which render the item itself one
		// detent apart.
		select_language();
		for (uint8_t direction = 0; direction < 2; direction++) {
			const std::string want = table().value("mpe/" + std::to_string(direction), "title");
			const std::string got = text_of(static_cast<uint16_t>(direction != 0 ? String::STRING_FOR_MPE_OUTPUT
			                                                                    : String::STRING_FOR_MPE_INPUT));
			expect("direction " + std::to_string(direction) + ": " + got)
			    .to_equal("direction " + std::to_string(direction) + ": " + want);
		}
		// ... and the two answers really are different, so the example above is not vacuous.
		expect(text_of(static_cast<uint16_t>(String::STRING_FOR_MPE_INPUT)))
		    .not_().to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_MPE_OUTPUT)));
		// The static fallback the dump and the parent row carry is `Node::title`.
		expect(text_of(nodes::mpe_direction.name)).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_MPE)));
	});

	it("draws the zone page's title band empty but for its rule, as main's nameless ZoneSelector does", _ {
		// `zoneSelectorMenu` is default-constructed: no name, no `getTitle()`, so `MenuItem::renderOLED` draws an empty
		// title. The direction page, titled MPE INPUT, is the control: the helper sees its text.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		{
			Rig r;
			r.open(nodes::mpe_zone, ctx);
			r.shell.tick(0);
			expect(title_band_blank(r.fake.pbm(0))).to_be_true();
		}
		{
			Rig r;
			r.open(nodes::mpe_direction, ctx);
			r.shell.tick(0);
			expect(title_band_blank(r.fake.pbm(0))).to_be_false();
		}
	});

	it("offers the same options as main's two Selections", _ {
		select_language();
		const std::vector<std::string> directions{text_of(static_cast<uint16_t>(String::STRING_FOR_IN)),
		                                          text_of(static_cast<uint16_t>(String::STRING_FOR_OUT))};
		expect(frozen_list("mpe_direction", "options")).to_equal(directions);

		const std::vector<std::string> zones{text_of(static_cast<uint16_t>(String::STRING_FOR_LOWER_ZONE)),
		                                     text_of(static_cast<uint16_t>(String::STRING_FOR_UPPER_ZONE))};
		expect(frozen_list("mpe_zone", "options")).to_equal(zones);
	});

	it("the member-channel page re-reads after an MPE configuration message, as mpeZonesPotentiallyUpdated re-reads "
	   "zoneNumMemberChannelsMenu", _ {
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		expect(open_device_page_on_guest(f, ctx)).to_be_true();
		guest_select(); // into the direction (IN)
		guest_select(); // into the zone (LOWER)
		guest_select(); // into the member channels
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone_num_member_channels);
		const int32_t before = member_channels(ctx, MIDI_DIRECTION_INPUT_TO_DELUGE, MPE_ZONE_LOWER_NUMBERED_FROM_0);
		expect(menu_scene().state().edit_int).to_equal(before);

		f.a->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel = 7;
		const int32_t after = member_channels(ctx, MIDI_DIRECTION_INPUT_TO_DELUGE, MPE_ZONE_LOWER_NUMBERED_FROM_0);
		expect(after).not_().to_equal(before);
		deluge::gui::ui::mpeZonesPotentiallyUpdated();
		expect(menu_scene().state().edit_int).to_equal(after);
	});

	it("leaves every other page alone: the zone page is neither re-read nor moved off the zone the user turned to", _ {
		// `mpeZonesPotentiallyUpdated` re-reads only `zoneNumMemberChannelsMenu`; a re-read of any other page would
		// raise a redraw.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		expect(open_device_page_on_guest(f, ctx)).to_be_true();
		guest_select(); // into the direction
		guest_select(); // into the zone
		shellHostUI.selectEncoderAction(1); // UPPER ZONE
		(void)shellHostUI.timerCallback();
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
		(void)deluge::base::notify::dirty().drain();
		deluge::gui::ui::mpeZonesPotentiallyUpdated();
		expect(int(menu_scene().state().walk.mpe_zone)).to_equal(1);
		expect(deluge::base::notify::dirty().drain().has(deluge::base::notify::Dirty::Oled)).to_be_false();
	});
});

describe midi_device_pixels("scenes::menu: the per-device page's pixels", $ {
	// Both panel geometries for every screen on the page, because a row or a value-band constant
	// that is right on one panel and wrong on the other is exactly what a single golden cannot see
	// (`tables_3_spec.cpp`'s own note on the same point). Nothing here hand-computes a rect: the two
	// selectors lay out through `ui::widgets::oled::menu_rect(f)` and the member-channel editor through
	// `f.bounds()`, like `MenuView::render_leaf_int`.
	//
	// Every screen is reached by *driving* the scene rather than by poking `MenuState`
	// (`MenuScene::state()` is const, deliberately), so a golden here is a screen the user can
	// actually get to.

	/// @brief Open @p node, walk @p rows detents, tick, and golden both panels under @p name.
	auto shoot = [&](auto& self_ref, const Node& node, const char* name, int32_t rows) {
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_device = deluge::midi::id_of(*f.a);
		{
			Rig r;
			r.open(node, ctx);
			r.shell.tick(0);
			uint32_t at = 10;
			r.walk(rows, at);
			r.shell.tick(at + 5);
			expect_golden(self_ref, std::string{"ui/menu/"} + name + "_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(node, ctx);
			r.shell.tick(0);
			uint32_t at = 10;
			r.walk(rows, at);
			r.shell.tick(at + 5);
			expect_golden(self_ref, std::string{"ui/menu/"} + name + "_64.pbm", r.fake.pbm(0));
		}
	};

	it("renders the per-device page's six rows", _ { shoot(self, nodes::midi_device, "menu_midi_device_page", 0); });

	it("renders the MPE direction selector, titled MPE INPUT on IN and MPE OUTPUT on OUT", _ {
		// The pair that shows the runtime title (`DirectionSelector::getTitle`,
		// `origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`) actually reaching the panel: same node, same
		// context, one detent apart, and the two goldens differ in the title band as well as in the cursor.
		shoot(self, nodes::mpe_direction, "menu_mpe_direction_in", 0);
		shoot(self, nodes::mpe_direction, "menu_mpe_direction_out", 1);
	});

	it("renders the MPE zone selector", _ { shoot(self, nodes::mpe_zone, "menu_mpe_zone", 0); });

	it("renders the member-channel editor over the baseline cable's IN port lower zone", _ {
		// `baseline()` puts 3 there, so the huge digit is a value this example chose rather than a
		// default -- and `off_at_zero` is not what is being drawn.
		shoot(self, nodes::mpe_zone_num_member_channels, "menu_mpe_member_channels", 0);
	});
});

describe midi_device_reference_rows("scenes::menu: the per-device page: the reference rows", $ {
	it("parses midi_device.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{40});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(midi_device_spec, midi_device_shape, midi_device_per_cable, midi_device_host_writes, midi_device_mpe,
            midi_device_pixels, midi_device_reference_rows)
