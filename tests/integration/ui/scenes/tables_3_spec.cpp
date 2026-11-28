/// @file
/// @brief The third menu tables -- the settings root and the nine other roots -- checked against the reference rows
///        of upstream `main`'s menu (`tests/golden/ui/menu_oracle/tables_3.tsv`).
///
/// These tables declare everything a menu open can reach beyond the sound-editor root's subtrees: the kit-drum root,
/// the MIDI/CV root (both of its titles), the MIDI- and gate-drum roots, the audio-clip root, the performance-view
/// root, the song root, the kit global-FX root, and the settings root with its whole subtree.
///
/// The checks are `tables_2_spec`'s, generalised over **ten** roots rather than one -- each with its own fixture and
/// its own `MenuContext`, because a MIDI clip, an audio clip and the settings root resolve to genuinely different
/// contexts:
///
///   * every tree lists the children main's menu lists, under each submenu, and nothing else;
///   * each node's name and title are main's own;
///   * each node's `ParamRef`, learn target, AFFECT ENTIRE use, edit marking, slot count and range dependence are
///     main's;
///   * `bindings::read` equals what main's item reads on the same fixture;
///   * each `Kind::Enum`'s names are main's option strings;
///   * each node's `relevant` predicate agrees with main's `isRelevant()`;
///   * `bindings::write` leaves the model where main's `writeCurrentValue()` leaves it, for every node with a `set`
///     hook;
///
/// plus what only a complete tree can be asked: `MenuScene::roots()` resolves all ten, `validate_tree` over every root
/// under every context, and the cap on `Kind::Custom` nodes (`kMaxCustomNodes`).
///
/// @note The answers are rows of a reference table, not live calls: the spec includes no main-menu header, and the
///       last group asks that it asked every row (`OracleTable::unasked`).

#include "gui/ui/menu_view_of.h"
#include "support/main_numbering.h"
#include "support/sound_fixtures.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/dynamic.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/present.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/led/pad_leds.h"

#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/usb/usb_audio.h"

#include "processing/engines/cv_engine.h"

#include "effects/reverb.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/menu_entry.h"

#include "support/fake_note_bridge.h"
#include "support/host_flash.h"
#include "support/known_differences.h"
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/reset_witness.h"
#include "support/shortcut_layout.h"
#include "support/song_fixture.h"
#include "support/tables_3_fixtures.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/controller_mapping_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
namespace t3 = deluge::ui::scenes::spec::tables3;

namespace {

using deluge::test::matchers::address_of;

namespace nodes = deluge::ui::scenes::menu::nodes;
namespace bindings = deluge::ui::scenes::menu::bindings;

using t3::CableFixture;
using t3::CommandSlots;
using t3::cv_fixture;
using t3::device_name;
using t3::fake_cable;
using t3::follow_state;
using t3::FollowSaved;
using t3::install;
using t3::listed_devices;
using t3::roots;
using t3::shown;
using t3::TablesRoot;
using t3::text_of;

/// @brief Every node of every root, with the root it was reached from.
std::vector<std::pair<const TablesRoot*, spec::TreeEntry>> every_entry() {
	std::vector<std::pair<const TablesRoot*, spec::TreeEntry>> out;
	for (const TablesRoot& root : roots()) {
		for (spec::TreeEntry& e : spec::tree_entries(root.name, *root.node)) {
			out.emplace_back(&root, std::move(e));
		}
	}
	return out;
}

spec::OracleTable& table() {
	return spec::OracleTable::named("tables_3");
}

/// @brief The entries of the settings root, which most of the group-specific rows are keyed under.
const std::vector<spec::TreeEntry>& settings_entries() {
	static const std::vector<spec::TreeEntry> entries = spec::tree_entries("settings", nodes::root_settings);
	return entries;
}

/// @brief Reach `Output::activeClip`, which is `protected` (`model/output.h`) and whose only public setter,
///        `Output::setActiveClip`, also re-sends MIDI program changes and wants a timeline-counter model stack. The
///        pointer-to-member is formed inside a class derived from `Output` ([class.protected]); never instantiated.
struct OutputPeek : ::Output {
	static Clip*& active_clip_of(::Output& out) { return out.*(&OutputPeek::activeClip); }
};

/// @brief The root fixture named @p name (`roots()`'s own `TablesRoot::name`), installed and ready.
///
/// Aborts the example loudly rather than returning null: every caller below names a root this file declares, so a miss
/// is a typo, not a case to handle.
const TablesRoot& installed_root(std::string_view name) {
	for (const TablesRoot& r : roots()) {
		if (name == r.name) {
			install(r.ctx, r.song);
			return r;
		}
	}
	static const TablesRoot kNone{"<none>", nullptr, {}, nullptr};
	return kNone;
}

/// The six pad colour nodes, in `coloursSubmenu`'s order.
const Node* const kColourNodes[] = {&nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
                                    &nodes::pad_colour_soloed, &nodes::pad_colour_fill,    &nodes::pad_colour_once};
/// `Colour::Option`'s order: the nine names every pad colour item offers, as the options rows spell them.
enum PadColour : uint8_t { kRed, kGreen, kBlue, kYellow, kCyan, kMagenta, kAmber, kWhite, kPink, kNumPadColours };

/// Board encoder index (`kDelugeEncoders`, `ui/shell/profiles/deluge.cpp`): 3 = Select. Matrix
/// coordinates (`kMatrix`, same file) for the select-encoder button. Same spelling every other
/// spec in this suite uses.
constexpr uint8_t kSelectEncoderIndex = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
/// The BACK button's matrix position, the same pair `context_menu_scene_spec.cpp` and
/// `rename_scene_spec.cpp` use.
constexpr uint8_t kBackX = 7, kBackY = 1;

/// @brief A `Shell` over `FakeSurfaceSet` with the application's scene registry, for the goldens.
struct Rig {
	const deluge::ui::shell::BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

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
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	             const deluge::ui::shell::BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}

	/// @brief One button press/release at matrix (\p x, \p y) -- the select-encoder press an accept
	///        or a descent rides on, the same coordinates every other spec in this suite uses.
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One detent of the select encoder (2 edges/detent, `LoweringConfig::edges_per_detent`).
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoderIndex, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
};

constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const deluge::ui::shell::BoardProfile& profile_64() {
	static const deluge::ui::shell::BoardProfile p = [] {
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		deluge::ui::shell::BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief Shift+LEARN on the menu: unlearn the learn node.
void shift_learn() {
	using deluge::ui::shell::ButtonId;
	using deluge::ui::shell::UiEvent;
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const deluge::ui::shell::UiContext ui{held, pads, true, false, 0, deluge::ui::scenes::deluge_profile()};
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, ui);
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Learn}, ui);
}

/// @brief The last popup notice's string id, draining the queue; 0 for none.
uint16_t last_popup_string() {
	uint16_t last = 0;
	for (deluge::base::notify::Notice n; deluge::base::notify::notices().pop(n);) {
		last = n.string_id;
	}
	return last;
}

/// @brief Descend `Rig` @p r from the MIDI-commands submenu onto its @p row'th child.
///
/// The real descent the reset-item examples use: cursor down @p row times, then the select-encoder
/// press `enter_child` -> `enter_leaf` runs on.
void focus_command_row(Rig& r, const MenuContext& ctx, size_t row) {
	// Both global queues first. An undrained `Kind::Text` notice -- a `CANT_LEARN_PC` refusal from a
	// previous iteration, or a stray fact from an earlier describe block in this same process --
	// becomes a `PopupScene` on this Rig's very first tick, which is not `oled_transparent` for
	// `Main` and takes the input top with it (`ui/shell/notices.cpp`). The select press below
	// would then never reach the menu, and the descent would silently not happen.
	// `tables_3_reset_items` and `tables_3_pixels` drain for exactly this reason.
	deluge::base::notify::dirty().drain();
	{
		deluge::base::notify::Notice stray;
		while (deluge::base::notify::notices().pop(stray)) {}
	}
	// The turns below count from row 0, so the page must not land on the row an earlier walk left it on.
	menu_scene().reset_process_state();
	r.open(nodes::midi_commands, ctx);
	r.shell.tick(0);
	for (size_t t = 0; t < row; t++) {
		r.turn(1, static_cast<uint32_t>(10 + t * 10));
		r.shell.tick(static_cast<uint32_t>(15 + t * 10));
	}
	r.button(kSelectEncButtonX, kSelectEncButtonY, true, 200);
	r.button(kSelectEncButtonX, kSelectEncButtonY, false, 200);
	r.shell.tick(210);
}

/// @brief Open the MIDI-commands list on `Rig` @p r with the cursor on its @p row'th child, not entered.
void cursor_on_command_row(Rig& r, const MenuContext& ctx, size_t row) {
	deluge::base::notify::dirty().drain();
	{
		deluge::base::notify::Notice stray;
		while (deluge::base::notify::notices().pop(stray)) {}
	}
	menu_scene().reset_process_state();
	r.open(nodes::midi_commands, ctx);
	r.shell.tick(0);
	for (size_t t = 0; t < row; t++) {
		r.turn(1, static_cast<uint32_t>(10 + t * 10));
		r.shell.tick(static_cast<uint32_t>(15 + t * 10));
	}
}

/// @brief The handle the host gives `fake_cable()`.
deluge::midi::DeviceId spec_device() {
	return deluge::midi::id_of(fake_cable());
}

/// @brief The focused item's own learn hooks, for an example that reads whether the item learned (which
///        `offer_while_learning`'s "used" answer does not say) or that must not open learn mode around the learn.
/// @return The item, or null when nothing is focused or the focused node has no item.
const deluge::ui::scenes::menu::CustomItem* focused_item() {
	const deluge::ui::scenes::menu::Node* at = menu_scene().focused();
	return at == nullptr ? nullptr : at->custom;
}

/// @brief Offer the menu one MIDI message in learn mode: LEARN held for it, Shift up.
bool offer_while_learning(deluge::ui::shell::UiEvent::Kind kind, uint8_t channel, uint8_t data1, uint8_t data2) {
	using deluge::ui::shell::ButtonId;
	using deluge::ui::shell::UiEvent;
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const deluge::ui::shell::UiContext ui{held, pads, false, false, 0, deluge::ui::scenes::deluge_profile()};
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, ui);
	const bool used = menu_scene()
	                      .handle(UiEvent{.kind = kind,
	                                      .channel = channel,
	                                      .data1 = data1,
	                                      .data2 = data2,
	                                      .device = deluge::midi::id_of(fake_cable())},
	                              ui)
	                      .handled;
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Learn}, ui);
	return used;
}

/// @brief Is @p target named by some node's static `children` span, anywhere in `kAllRoots`?
///
/// `tables_2_spec.cpp` has the same helper for the arp half; the point of a chain-only page is that
/// the answer here is *no*, so both halves need to be able to ask.
bool reachable_by_children(const Node* target) {
	std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
	std::set<const Node*> seen;
	while (!stack.empty()) {
		const Node* n = stack.back();
		stack.pop_back();
		if (n == nullptr || !seen.insert(n).second) {
			continue;
		}
		for (const Node* c : n->children) {
			if (c == target) {
				return true;
			}
			stack.push_back(c);
		}
		for (const Node* t : n->chain_targets) {
			stack.push_back(t); // traversed, but never counted as a `children` hit
		}
	}
	return false;
}

/// Whether writing @p node's param makes the reverb recompute: the Edit raises it for a sidechain's shape (a sound's or
/// a global effectable's) and the global sidechain volume, the params legacy's two `UpdatingReverbParams` items edit.
bool raises_reverb(const Node& node) {
	namespace params = deluge::modulation::params;
	const auto& p = node.bind.param;
	return ((deluge::ui::scenes::spec::runtime_of(p).kind == params::Kind::UNPATCHED_SOUND
	         || deluge::ui::scenes::spec::runtime_of(p).kind == params::Kind::UNPATCHED_GLOBAL)
	        && deluge::ui::scenes::spec::runtime_of(p).index == params::UNPATCHED_SIDECHAIN_SHAPE)
	       || (deluge::ui::scenes::spec::runtime_of(p).kind == params::Kind::UNPATCHED_GLOBAL
	           && deluge::ui::scenes::spec::runtime_of(p).index == params::UNPATCHED_SIDECHAIN_VOLUME);
}

} // namespace

// clang-format off
describe tables_3_shape("scenes::menu settings and other roots tables: shape", $ {
	it("lists, under every submenu of every root, the children main's menu lists, and nothing it does not", _ {
		size_t submenus = 0;
		for (const auto& [root, e] : every_entry()) {
			if (!t3::has_dynamic_name(*e.node)) {
				expect(e.path + (table().find(e.path, "name") != nullptr ? " has a name row" : " has none"))
				    .to_equal(e.path + " has a name row");
			}
			if (e.node->kind == menu::Kind::Submenu) {
				expect(e.path + " kids " + std::to_string(spec::main_children(*e.node)))
				    .to_equal(e.path + " kids " + table().value(e.path, "kids"));
				submenus++;
			}
		}
		expect(submenus).to_be_greater_than(size_t(120));
	});

	it("declares the settings root with the legacy child count", _ {
		// `origin/main:src/deluge/gui/ui/menus.cpp`'s `settingsRootMenu` literal: fifteen children.
		expect(nodes::root_settings.children.size()).to_equal(size_t(15));
		expect(std::to_string(nodes::root_settings.children.size())).to_equal(table().value("settings", "kids"));
	});

	it("names and titles the reverb width and room size as main does under each reverb model", _ {
		// `reverb::Width` and `reverb::RoomSize` answer Diffusion and Time under the MUTABLE and DIGITAL models, Width
		// and Room Size under FREEVERB; the nodes' name hooks answer the same (`column_hooks.cpp`).
		const deluge::effects::ReverbModel saved = AudioEngine::reverb.model();
		size_t asked = 0;
		for (const auto& [root, e] : every_entry()) {
			if (e.node != &nodes::reverb_width && e.node != &nodes::reverb_room_size) {
				continue;
			}
			install(root->ctx, root->song);
			size_t models_seen = 0;
			const int32_t was = menu::bindings::read(nodes::reverb_model, root->ctx);
			const menu::IntRange models = menu::bindings::range(nodes::reverb_model, root->ctx);
			for (int32_t v = models.min; v <= models.max; v++) {
				// The model as the menu sets it, through the song's own setting.
				menu::bindings::write(nodes::reverb_model, root->ctx, v);
				const deluge::effects::ReverbModel model = AudioEngine::reverb.model();
				const char* const suffix = model == deluge::effects::ReverbModel::FREEVERB  ? "@freeverb"
				                           : model == deluge::effects::ReverbModel::MUTABLE ? "@mutable"
				                                                                            : nullptr;
				if (suffix == nullptr) {
					continue;
				}
				models_seen++;
				menu::NodeText name_scratch;
				menu::NodeText title_scratch;
				expect(std::string{menu::display_text(*e.node, menu::ValueTextUse::Name, root->ctx, name_scratch)})
				    .to_equal(table().value(e.path, std::string{"name"} + suffix));
				expect(std::string{menu::display_text(*e.node, menu::ValueTextUse::Title, root->ctx, title_scratch)})
				    .to_equal(table().value(e.path, std::string{"title"} + suffix));
			}
			menu::bindings::write(nodes::reverb_model, root->ctx, was);
			expect(models_seen).to_equal(size_t(2));
			asked++;
		}
		expect(asked).to_be_greater_than(size_t(0));
		AudioEngine::setReverbModel(saved);
	});

	it("resolves each root from its own fixture's context", _ {
		// `context_for`'s part 2 is what chooses a root; this is the check that each fixture really is the clip shape
		// that root belongs to, so the walks above are not comparing a tree against a context it would never be
		// opened in.
		const menu::MenuContext::Root want[] = {
		    menu::MenuContext::Root::Sound,      menu::MenuContext::Root::Drum,
		    menu::MenuContext::Root::MidiOrCv,   menu::MenuContext::Root::MidiDrum,
		    menu::MenuContext::Root::GateDrum,   menu::MenuContext::Root::AudioClip,
		    menu::MenuContext::Root::Performance, menu::MenuContext::Root::Song,
		    menu::MenuContext::Root::KitGlobalFx, menu::MenuContext::Root::Settings,
		};
		expect(roots().size()).to_equal(std::size(want));
		for (size_t i = 0; i < roots().size(); i++) {
			expect(roots()[i].ctx.root).to_equal(want[i]);
			expect(roots()[i].ctx.resolved).to_be_true();
		}
	});

	it("fills MenuScene::roots() with one node per MenuContext::Root", _ {
		const std::span<const Node* const> r = menu::MenuScene::roots();
		expect(r.size()).to_equal(size_t(menu::MenuContext::Root::Count));
		for (const TablesRoot& root : roots()) {
			expect(r[size_t(root.ctx.root)]).to_equal(root.node);
		}
	});

	it("carries both of the MIDI/CV root's titles, chosen by output type", _ {
		// `SoundEditor::setup` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) rewrites one object's title; the port
		// has two nodes over one child list, and `midi_or_cv_root` is the branch.
		expect(text_of(nodes::root_midi_or_cv.title))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_MIDI_INST_MENU_TITLE)});
		expect(text_of(nodes::root_cv.title))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_CV_INSTRUMENT)});
		expect(static_cast<const void*>(nodes::root_cv.children.data()))
		    .to_equal(static_cast<const void*>(nodes::root_midi_or_cv.children.data()));

		const TablesRoot* midi = nullptr;
		for (const TablesRoot& root : roots()) {
			if (root.ctx.root == menu::MenuContext::Root::MidiOrCv) {
				midi = &root;
			}
		}
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(&nodes::midi_or_cv_root(midi->ctx)).to_equal(&nodes::root_midi_or_cv);
		}
		expect(&nodes::midi_or_cv_root(cv_fixture().ctx)).to_equal(&nodes::root_cv);
	});

	it("gives every node main's own name", _ {
		for (const auto& [root, e] : every_entry()) {
			if (t3::has_dynamic_name(*e.node)) {
				continue;
			}
			install(root->ctx, root->song);
			expect(e.path + " " + shown(e.node->name, *e.node, root->ctx))
			    .to_equal(e.path + " " + table().value(e.path, "name"));
		}
	});

	it("gives every node main's own title", _ {
		size_t compared = 0;
		for (const auto& [root, e] : every_entry()) {
			if (t3::has_dynamic_name(*e.node)) {
				continue;
			}
			install(root->ctx, root->song);
			const std::string title = shown(e.node->title, *e.node, root->ctx);
			// A placeholder left unfilled is `sample::Repeat`'s, a `FormattedTitle` main gives no argument
			// (`origin/main:src/deluge/gui/menu_item/sample/repeat.h`), so its title is empty until something formats
			// it.
			if (title.find('*') != std::string::npos) {
				expect(e.node->title_arg).to_equal(uint8_t{0});
				(void)table().find(e.path, "title");
				continue;
			}
			expect(e.path + " " + title).to_equal(e.path + " " + table().value(e.path, "title"));
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(800)); // 934 today, across the ten roots
	});

	it("gives every leaf main's own param, AFFECT ENTIRE use, edit marking, slot count and range dependence", _ {
		// Main keeps the arp's params in the unpatched sets: a sound's at runtime indices 11 to 25 (ids 101 to 115), a
		// kit's rate at the global index 30 (id 120). Here they are the arp's own ids.
		// The arp id of main's param at runtime index @p index of kind @p kind (support/main_numbering.h)
		const auto arp_id_of_main_index = [](std::string_view kind, int32_t index) -> std::optional<int32_t> {
			namespace numbering = spec::main_numbering;
			std::optional<uint32_t> arp;
			if (kind == std::to_string(static_cast<int32_t>(deluge::modulation::params::Kind::PATCHED))) {
				arp = numbering::arp_id_of_patched(static_cast<uint32_t>(index));
			}
			else {
				const bool chain =
				    kind == std::to_string(static_cast<int32_t>(deluge::modulation::params::Kind::UNPATCHED_GLOBAL));
				arp = numbering::arp_id_of_index(chain, index);
			}
			return arp ? std::optional<int32_t>{static_cast<int32_t>(*arp)} : std::nullopt;
		};
		size_t learned = 0;
		for (const auto& [root, e] : every_entry()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			const std::string at = e.path + " " + text_of(e.node->name);
			menu::MenuState st{};
			st.path[0] = e.node;
			install(root->ctx, root->song);
			const std::string ae = std::to_string(menu::node_uses_affect_entire(*e.node) ? 1 : 0);
			expect(at + " ae " + ae).to_equal(at + " ae " + table().value(e.path, "ae"));
			expect(at + " edits " + std::to_string(menu::select_turn_edits_instrument(root->ctx, st) ? 1 : 0))
			    .to_equal(at + " edits " + table().value(e.path, "edits"));
			expect(at + " slots " + std::to_string(e.node->slots))
			    .to_equal(at + " slots " + table().value(e.path, "slots"));
			expect(at + " range_dep " + std::to_string(e.node->range_dependent ? 1 : 0))
			    .to_equal(at + " range_dep " + table().value(e.path, "range_dep"));
			// Every leaf the freezer found a learner on (relevant here) learns what main's item learns.
			if (const std::vector<std::string>* want = table().find(e.path, "learn"); want != nullptr) {
				const std::optional<deluge::project::OwnerParamTarget> got =
				    menu::bindings::learn_target(*e.node, root->ctx);
				std::string wanted = want->empty() ? std::string{"<no value>"} : want->front();
				if (got && !want->empty()) {
					const int32_t main_id = std::stoi(want->front());
					const auto kind = table().find(e.path, "param_kind");
					const std::string main_kind = kind != nullptr && !kind->empty() ? kind->front() : std::string{};
					if (got->param.space == deluge::project::ParamSpace::ARP) {
						const bool patched =
						    main_kind == std::to_string(static_cast<int32_t>(deluge::modulation::params::Kind::PATCHED));
						if (const std::optional<int32_t> arp =
						        arp_id_of_main_index(main_kind, patched ? main_id : main_id - 90)) {
							wanted = std::to_string(*arp);
						}
					}
					// Past the arp's ids, main's numbers sit 15 higher (16 in a chain's table, above its arp rate).
					else if (main_id >= 116) {
						wanted = std::to_string(main_id > 120 ? main_id - 16 : main_id - 15);
					}
				}
				expect(at + " learns " + (got ? std::to_string(got->param.id.value) : std::string{"none"}))
				    .to_equal(at + " learns " + wanted);
				if (got) {
					expect(got->owner).to_equal(menu::bindings::controls_owner(root->ctx));
				}
				learned++;
			}
			// A leaf with its own param has main's index and kind; one with a hook or a non-param target owns its value,
			// its `ParamRef` is learn-only, and it is checked as the learn target above -- every such leaf shown here
			// has a learn row.
			if (!spec::param_comparable(*e.node)) {
				expect(table().find(e.path, "param_index")).to_be_null();
				if (e.node->kind != menu::Kind::Custom && e.node->bind.param.valid()
				    && (e.parent == nullptr || t3::relevant_here(*root, *e.parent)) && t3::relevant_here(*root, *e.node)) {
					expect(e.path + (table().find(e.path, "learn") != nullptr ? " has a learn row" : " has none"))
					    .to_equal(e.path + " has a learn row");
				}
				continue;
			}
			std::optional<int32_t> index = spec::int_of(table().find(e.path, "param_index"));
			const std::optional<int32_t> kind = spec::int_of(table().find(e.path, "param_kind"));
			const auto runtime = spec::runtime_of(e.node->bind.param);
			if (runtime.kind == deluge::plugin::ParamKind::ARP && index) {
				// An arp param: main's row names it in an unpatched set
				index = arp_id_of_main_index(kind ? std::to_string(*kind) : std::string{}, *index);
				expect(at + " is an arp param in main").to_equal(at + (index ? " is an arp param in main" : " is not"));
				expect(at + " index " + std::to_string(runtime.index))
				    .to_equal(at + " index " + (index ? std::to_string(*index) : std::string{"<no row>"}));
				continue;
			}
			// An unpatched index past main's arp block sits 15 lower here (16 past a chain's arp rate).
			const bool unpatched_kind = runtime.kind == deluge::plugin::ParamKind::UNPATCHED_SOUND
			                            || runtime.kind == deluge::plugin::ParamKind::UNPATCHED_GLOBAL;
			if (index && unpatched_kind && *index >= 26) {
				index = *index > 30 ? *index - 16 : *index - 15;
			}
			if (index && runtime.kind == deluge::plugin::ParamKind::PATCHED) {
				index = spec::main_numbering::patched_index_of(*index);
			}
			expect(at + " index " + std::to_string(runtime.index))
			    .to_equal(at + " index " + (index ? std::to_string(*index) : std::string{"<no row>"}));
			if (!kind || *kind == static_cast<int32_t>(deluge::modulation::params::Kind::NONE)) {
				continue;
			}
			const auto want = static_cast<deluge::modulation::params::Kind>(*kind);
			const bool unpatched = want == deluge::modulation::params::Kind::UNPATCHED_SOUND
			                       || want == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
			const auto got = spec::runtime_of(e.node->bind.param).kind;
			if (unpatched) {
				expect(got).to_satisfy([](auto kind) {
					using deluge::modulation::params::Kind;
					return kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL;
				});
			}
			else {
				expect(got).to_equal(want);
			}
		}
		expect(learned).to_be_greater_than(size_t(0));
	});
});

describe tables_3_values("scenes::menu settings and other roots tables: values", $ {
	it("reads what main's menu reads, for every node that holds a value", _ {
		size_t compared = 0;
		for (const auto& [root, e] : every_entry()) {
			const std::vector<std::string>* want = table().find(e.path, "read");
			if (want == nullptr) {
				continue;
			}
			install(root->ctx, root->song);
			const std::string at = e.path + " " + text_of(e.node->name) + " reads ";
			expect(at + std::to_string(menu::bindings::read(*e.node, root->ctx)))
			    .to_equal(at + (want->empty() ? std::string{"<no value>"} : want->front()));
			compared++;
		}
		// 581 today. A drop would mean the loop stopped reaching the leaves.
		expect(compared).to_be_greater_than(size_t(500));
	});

	it("names every enum option exactly as main's list does", _ {
		size_t compared = 0;
		for (const auto& [root, e] : every_entry()) {
			if (e.node->kind != menu::Kind::Enum) {
				continue;
			}
			install(root->ctx, root->song);
			const std::vector<std::string>* want = table().find(e.path, "options");
			if (want == nullptr) {
				// Only a node shown on a page the root opens has a row; a node shown where main hid it
				// (spec::kShownWhereMainHid) has none.
				const bool shown_here = (e.parent == nullptr || menu::bindings::relevant(*e.parent, root->ctx))
				                        && menu::bindings::relevant(*e.node, root->ctx);
				const std::vector<std::string>* main_rel = table().find(e.path, "rel");
				const bool main_hid_it = spec::shows_where_main_hid(
				    *e.node, shown_here, main_rel != nullptr && !main_rel->empty() && main_rel->front() == "1");
				expect(e.path + (shown_here && !main_hid_it ? " has none" : " has options"))
				    .to_equal(e.path + " has options");
				continue;
			}
			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t n = menu::bindings::names(*e.node, root->ctx, got, scratch);
			std::string port;
			for (size_t i = 0; i < n; i++) {
				port += std::string{got[i]} + "|";
			}
			std::string frozen;
			for (const std::string& o : spec::port_choices(*e.node, *want)) {
				frozen += o + "|";
			}
			expect(e.path + " " + port).to_equal(e.path + " " + frozen);
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(100)); // 129 today
	});

	it("shows each node exactly where main's menu shows it, in every root's own context", _ {
		size_t ever_true = 0;
		for (const auto& [root, e] : every_entry()) {
			install(root->ctx, root->song);
			if (t3::recorder_on_soundless_row(e, *root)) {
				// Main dereferences a null sound here; the port answers hidden.
				expect(menu::bindings::relevant(*e.node, root->ctx)).to_be_false();
				continue;
			}
			const std::vector<std::string>* want = table().find(e.path, "rel");
			if (want == nullptr) {
				continue; // a page this root never opens
			}
			const bool got = menu::bindings::relevant(*e.node, root->ctx);
			const std::string label = e.path + " " + text_of(e.node->name) + " relevant=";
			const bool named = spec::shows_where_main_hid(*e.node, got, want->front() == "1");
			expect(label + (got ? "1" : "0")).to_equal(label + (named ? "1" : want->front()));
			ever_true += got ? 1 : 0;
		}
		expect(ever_true).to_be_greater_than(size_t(700)); // 845 today -- the loop cannot go vacuous
	});

	it("hides the song root's two session-only items in the arranger and the automation view", _ {
		// One root (`soundEditorRootMenuSongView`) serves the session, arranger and song-automation views alike, and
		// `song_midi_learn` / `configure_song_macros` are the only items in it that tell the three apart. The port
		// takes the view from `context_for`'s `session_view_root` argument; main reads the root UI. The expected answer
		// is stated a third time, so the example still fails if the port and the rows are made to agree wrongly.
		struct Case {
			const char* name;
			bool session_view_root;
			bool expected;
		};
		static spec::SongFixture song = spec::make_synth_song();
		const Case cases[] = {
		    {"session", true, true},
		    {"arranger", false, false},
		    {"automation", false, false},
		};
		const Node* const guarded[] = {&nodes::configure_song_macros, &nodes::song_midi_learn};
		const std::vector<spec::TreeEntry> entries = spec::tree_entries("song", nodes::root_song);
		for (const Case& c : cases) {
			const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0,
			                                     /*performance_view=*/false, /*session_or_arranger_root=*/true,
			                                     c.session_view_root, false);
			// All three views take the same root: the port must differ on relevance, not on routing.
			expect(static_cast<int>(ctx.root)).to_equal(static_cast<int>(MenuContext::Root::Song));
			expect(ctx.is_song_view).to_be_true();
			expect(ctx.is_session_view).to_equal(c.session_view_root);

			install(ctx, song.song);
			for (const Node* node : guarded) {
				const std::string path = spec::first_path_of(entries, *node);
				expect(path.empty()).to_be_false();
				// The session view is the song root's own fixture, so its row is the generic one.
				const std::string want = table().value(path, c.session_view_root ? "rel" : std::string{"rel@"} + c.name);
				const bool got = node->relevant != nullptr && node->relevant(ctx);
				expect(want).to_equal(std::string{c.expected ? "1" : "0"});
				expect(got).to_equal(c.expected);
			}
		}
	});

	it("names relevance::is_session_view as the only nodes guarded by it, by identity", _ {
		// By identity in BOTH directions: the two nodes carry the predicate, and no third node anywhere in the tree
		// does. A count would pass while the pair silently drifted.
		for (const Node* n : {&nodes::configure_song_macros, &nodes::song_midi_learn}) {
			expect(address_of(n->relevant)).to_equal(address_of(&menu::relevance::is_session_view));
			expect(menu::relevance::name_of(n->relevant)).to_equal(std::string_view{"is_session_view"});
		}
		size_t others = 0;
		for (const auto& [root, e] : every_entry()) {
			if (e.node->relevant == &menu::relevance::is_session_view && !t3::relevance_reads_root_ui(*e.node)) {
				others++;
			}
		}
		expect(others).to_equal(size_t(0));
		// ... and the pair is actually reached by the walk, so the zero above is not vacuous.
		size_t reached = 0;
		for (const auto& [root, e] : every_entry()) {
			if (t3::relevance_reads_root_ui(*e.node)) {
				reached++;
				expect(root->node).to_equal(&nodes::root_song);
			}
		}
		expect(reached).to_equal(size_t(2));
	});

	it("refuses an unpatched param on a MIDI clip rather than reinterpreting its collection", _ {
		// `ParamManager::setupMIDI` and `setupUnpatched`
		// (`modulation/params/param_manager.cpp`) both leave `expressionParamSetOffset == 1`
		// and `summaries[0].paramCollection` non-null, and `getUnpatchedParamSet()` is a bare cast of
		// that pointer -- so on a MIDI or CV clip an unpatched `ParamRef` read would reinterpret a
		// `MIDIParamCollection`. That is reachable: the MIDI/CV root carries the
		// randomizer's and the arpeggiator's unpatched params so their `midi_cv` twins can be
		// siblings. `bindings::read` refuses it: the target resolves to no view (`target_view`), and it answers 0.
		//
		// `read()`'s "cannot serve" answer is a literal 0, which no *served* param can produce: a
		// stored 0 scales to `kMidMenuValue` (25) through `computeCurrentValueForStandardMenuItem`,
		// and the pan scaling's own zero point is 0 stored -> 0 shown but those params are patched or
		// global, neither of which is reachable here. So 0 is the signature of the refusal, and
		// reverting the hardening turns these reads into whatever lands at the reinterpreted
		// `params[]` offset.
		const TablesRoot* midi = nullptr;
		const TablesRoot* sound = nullptr;
		for (const TablesRoot& root : roots()) {
			if (root.ctx.root == menu::MenuContext::Root::MidiOrCv) {
				midi = &root;
			}
			if (root.ctx.root == menu::MenuContext::Root::Sound) {
				sound = &root;
			}
		}
		expect(midi).not_().to_be_null();
		expect(sound).not_().to_be_null();
		if (midi == nullptr || sound == nullptr) {
			return;
		}
		// The fixture really is a MIDI param manager, and it really does look servable structurally.
		expect(spec::menu_engine(midi->ctx, *midi->song).params).not_().to_be_null();
		expect(spec::menu_engine(midi->ctx, *midi->song).params->containsAnyParamCollectionsIncludingExpression()).to_be_true();

		// `summaries[1]` being non-null is NOT proof of a `PatchedParamSet`: `setupMIDI` leaves
		// `expressionParamSetOffset == 1` (`param_manager.cpp`), so `ensureExpressionParamSetExists` puts an
		// `ExpressionParamSet` there -- and `bend_range::Main::writeCurrentValue` calls
		// `getOrCreateExpressionParamSet()` (`origin/main:src/deluge/gui/menu_item/bend_range/main.h`) from `bendMenu`,
		// which is a child of this very root. So the scenario is reproduced here rather than assumed away: create the
		// expression set the way a user editing bend range would, then assert a PATCHED `ParamRef` is *still* refused.
		expect(spec::menu_engine(midi->ctx, *midi->song).params->getOrCreateExpressionParamSet()).not_().to_be_null();
		expect(spec::menu_engine(midi->ctx, *midi->song).params->summaries[1].paramCollection).not_().to_be_null();

		install(midi->ctx, midi->song);
		static constexpr Node kPatchedProbe =
		    menu::param(1, deluge::ui::scenes::menu::sound_param(deluge::plugin::ParamId{static_cast<uint32_t>(deluge::modulation::params::LOCAL_LPF_FREQ)}));
		expect(menu::bindings::read(kPatchedProbe, midi->ctx)).to_equal(0);
		menu::bindings::write(kPatchedProbe, midi->ctx, 42);
		expect(menu::bindings::read(kPatchedProbe, midi->ctx)).to_equal(0);

		std::set<const Node*> refused;
		for (const auto& [root, e] : every_entry()) {
			if (root != midi || !e.node->bind.param.valid() || e.node->bind.set != nullptr) {
				continue;
			}
			install(midi->ctx, midi->song);
			expect(menu::bindings::read(*e.node, midi->ctx)).to_equal(0);
			menu::bindings::write(*e.node, midi->ctx, 42);
			expect(menu::bindings::read(*e.node, midi->ctx)).to_equal(0);
			refused.insert(e.node);
		}
		// Every unpatched param node the MIDI/CV root reaches -- fourteen randomizer and arpeggiator
		// leaves. A drop to zero would make the example vacuous.
		expect(refused.size()).to_be_greater_than(size_t(10)); // 14 today

		// The positive control: the very same nodes DO read and write on a context whose
		// `summaries[0]` really is an `UnpatchedParamSet`, so the refusal above is about the MIDI
		// manager and not about the nodes.
		size_t served = 0;
		install(sound->ctx, sound->song);
		for (const auto& [root, e] : every_entry()) {
			if (root != sound || !refused.contains(e.node)) {
				continue;
			}
			menu::bindings::write(*e.node, sound->ctx, 42);
			expect(menu::bindings::read(*e.node, sound->ctx)).to_equal(42);
			served++;
		}
		expect(served).to_be_greater_than(size_t(10)); // the same 14
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const auto& [root, e] : every_entry()) {
			if (e.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(e.node->relevant)).not_().to_equal(std::string_view{"?"});
		}
	});
});

describe tables_3_validation("scenes::menu settings and other roots tables: validation", $ {
	it("validates every root but settings clean", _ {
		std::vector<MenuContext> contexts;
		std::vector<const Node*> without_settings;
		for (const TablesRoot& root : roots()) {
			contexts.push_back(root.ctx);
			if (root.node != &nodes::root_settings) {
				without_settings.push_back(root.node);
			}
		}
		without_settings.push_back(&nodes::root_cv);
		// `roots()` has no CV entry (`Root::MidiOrCv` covers both spellings),
		// so without this the CV context never reached the `EmptySubmenu` and `DuplicateName`
		// quantifiers -- and `root_cv`'s children are exactly the ones whose relevance differs
		// between a MIDI and a CV output (`cv2_source`, `is_midi_out`).
		contexts.push_back(cv_fixture().ctx);
		install(roots().front().ctx, roots().front().song);
		const auto issues = menu::validate_tree(
		    without_settings, contexts, nodes::kRangeSpecificNodes, nodes::kHalfPrecisionNodes,
		    menu::ValidateOptions{.skip_shortcut_sweep = true});
		size_t duplicates = 0;
		size_t others = 0;
		for (const menu::Issue& i : issues) {
			(i.kind == menu::Issue::Kind::DuplicateName ? duplicates : others)++;
		}
		// The `FormattedTitle` siblings, here and under the kit-drum root's second parent over the
		// same children, are told apart by their `title_arg`.
		expect(duplicates).to_equal(size_t(0));
		expect(others).to_equal(size_t(0));
	});

	it("validates the settings root clean, its sixteen follow-channel tracks included", _ {
		// Main names the sixteen tracks identically and formats the track number in (`FollowChannelTrack` in
		// `origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`); their `title_arg` tells them apart.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const Node* root[] = {&nodes::root_settings};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues =
		    menu::validate_tree(root, contexts, {}, {}, menu::ValidateOptions{.skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});

	it("assigns an id to every node in every root's tree", _ {
		std::vector<MenuContext> contexts;
		for (const TablesRoot& root : roots()) {
			contexts.push_back(root.ctx);
		}
		(void)menu::validate_tree(nodes::kAllRoots, contexts, {}, {},
		                          menu::ValidateOptions{});
		for (const auto& [root, e] : every_entry()) {
			expect(menu::node_id(*e.node)).not_().to_equal(uint16_t(0));
		}
	});

	it("uses every declared Kind at least three times", _ {
		// `node.h`'s `Kind` rule, applied with the whole tree in hand. A kind below three nodes is speculation, so the
		// enum declares none.
		// Sized from the enum's own last enumerator and swept in full: a
		// hand-written list of kinds would let a future enumerator escape the rule silently, and an
		// oversized array would report a phantom zero-count kind.
		constexpr size_t kNumKinds = static_cast<size_t>(menu::Kind::Custom) + 1;
		std::array<size_t, kNumKinds> counts{};
		std::set<const Node*> seen;
		for (const auto& [root, e] : every_entry()) {
			if (!seen.insert(e.node).second) {
				continue; // a shared node is one node
			}
			counts[static_cast<size_t>(e.node->kind)]++;
		}
		for (size_t i = 0; i < counts.size(); i++) {
			to_be_at_least(expect(counts[i]), size_t(3));
		}
	});

});
describe tables_3_writes("scenes::menu settings and other roots tables: writes", $ {
	it("leaves the model where main's menu leaves it, for every set hook", _ {
		// Every `set` hook is driven for the values the row names (the range's ends and its middle). Each starts from a
		// sentinel that is not the value, so a hook that stores nothing shows: the port's read after its own write is
		// compared with the frozen read after main's.
		size_t exercised = 0;
		std::set<const Node*> covered;
		for (const auto& [root, e] : every_entry()) {
			const std::vector<const spec::OracleRow*> rows = table().columns(e.path, "write/");
			if (rows.empty()) {
				continue;
			}
			install(root->ctx, root->song);
			const menu::IntRange r = menu::bindings::range(*e.node, root->ctx);
			std::string probed;
			std::string expected;
			for (const int32_t v : spec::distinct_probe_values(r)) {
				if (spec::sentinel_for(r, v)) {
					expected += std::to_string(v) + ",";
				}
			}
			for (const spec::OracleRow* row : rows) {
				const int32_t v = std::stoi(row->column.substr(std::string_view{"write/"}.size()));
				menu::bindings::write(*e.node, root->ctx, *spec::sentinel_for(r, v));
				menu::bindings::write(*e.node, root->ctx, v);
				const std::string at = e.path + " " + text_of(e.node->name) + " write " + std::to_string(v) + " reads ";
				expect(at + std::to_string(menu::bindings::read(*e.node, root->ctx))).to_equal(at + row->values.at(0));
				probed += std::to_string(v) + ",";
				exercised++;
				covered.insert(e.node);
			}
			expect(e.path + " probed " + probed).to_equal(e.path + " probed " + expected);
		}
		// 363 probes over 117 nodes, across the (root, path) pairs with a `set` hook -- a node reachable from three
		// roots is driven three times, on three different fixtures. main's DX7 engine feature (two probes) is gone: the
		// DX7 is built in. A node bound to a target has no `set` hook: the
		// binding library's specs drive it instead. A change in either number means the loop reaches other hooks.
		expect(exercised).to_equal(size_t(363));
		expect(covered.size()).to_equal(size_t(117));
	});

	it("drives the port hook of every node the write rows leave out", _ {
		// `t3::kWritesNeedingBootedUi` suppresses the **legacy** call only. Every one
		// of those fourteen port hooks is driven here on its own and its persisted effect asserted
		// directly -- model state, `playbackHandler`, `FlashStorage`, `runtimeFeatureSettings` --
		// because the read comparison does not reach any of it. The `covered` tally at the end is
		// the ratchet, and it holds node identities rather than a count: a fifteenth exclusion with
		// no arm here fails this example, and so does a second `insert` of a node already driven.
		//
		// The six `hid/`-side settings nodes are covered here in the only half `src/ui/scenes` owns:
		// the `FlashStorage` / `runtimeFeatureSettings` write. Their `hid/` halves are legacy's, as
		// `nodes/settings.h` records.
		std::set<const Node*> covered;

		// --- What this example perturbs, and puts back ------------------------------------------
		// These ten globals are process-wide, and the read comparisons in this file's other groups
		// observe some of them. Saved and restored so the example is order-independent.
		const bool saved_midi_out = playbackHandler.midiOutClockEnabled;
		const bool saved_midi_in = playbackHandler.midiInClockEnabled;
		const uint32_t saved_ppqn = playbackHandler.analogOutTicksPPQN;
		const uint8_t saved_brightness = FlashStorage::defaultPadBrightness;
		const MenuHighlighting saved_highlighting = FlashStorage::accessibilityMenuHighlighting;
		const ScreensaverMode saved_screensaver = FlashStorage::screensaverMode;
		const uint8_t saved_timeout = FlashStorage::screensaverTimeoutMinutes;
		const uint32_t saved_sticky = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);
		const uint32_t saved_light_shift = runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed);
		const uint32_t saved_corners = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners);

		{ // --- synth_mode: the mode Edit on the target, not just a stored int --------------------
			const TablesRoot& sound = installed_root("sound");
			const SynthMode before = spec::menu_engine(sound.ctx, *sound.song).sound->getSynthMode();
			// The model refuses RINGMOD unless both oscillators can be ring-modulated, as legacy's
			// relevance does; SAW on both, and the fixture's own types put back after.
			const OscType before_osc[] = {spec::menu_engine(sound.ctx, *sound.song).sound->oscType(0),
			                              spec::menu_engine(sound.ctx, *sound.song).sound->oscType(1)};
			deluge::test::setOscType(*spec::menu_engine(sound.ctx, *sound.song).sound, 0, OscType::SAW);
			deluge::test::setOscType(*spec::menu_engine(sound.ctx, *sound.song).sound, 1, OscType::SAW);
			menu::bindings::write(nodes::synth_mode, sound.ctx, static_cast<int32_t>(SynthMode::RINGMOD));
			expect(spec::menu_engine(sound.ctx, *sound.song).sound->getSynthMode()).to_equal(SynthMode::RINGMOD);
			menu::bindings::write(nodes::synth_mode, sound.ctx, static_cast<int32_t>(SynthMode::SUBTRACTIVE));
			expect(spec::menu_engine(sound.ctx, *sound.song).sound->getSynthMode()).to_equal(SynthMode::SUBTRACTIVE);
			menu::bindings::write(nodes::synth_mode, sound.ctx, static_cast<int32_t>(before));
			deluge::test::setOscType(*spec::menu_engine(sound.ctx, *sound.song).sound, 0, before_osc[0]);
			deluge::test::setOscType(*spec::menu_engine(sound.ctx, *sound.song).sound, 1, before_osc[1]);
			covered.insert(&nodes::synth_mode);
		}

		{ // --- arp_mode: the preset re-derivation AND the silence block --------------------------
			// Writing `arp_mode` is three effects, not one:
			// `silenceArp`, `arp.mode = mode`, `arp.updatePresetFromCurrentSettings()`.
			// `bindings::read` sees only the middle one, which is why deleting either of the others
			// would leave this suite green without a direct check.
			const TablesRoot& sound = installed_root("sound");
			ArpeggiatorSettings* arp = spec::menu_engine(sound.ctx, *sound.song).arp;
			expect(arp).not_().to_be_null();
			// The toggle's silencing only fires on a clip that is actually sounding (main
			// `origin/main:src/deluge/gui/menu_item/arpeggiator/mode.h`). `song_fixture.h` builds the clip without
			// making it its output's active one -- nothing in the fixture plays -- so this is the one bit of state the
			// example has to arrange for itself.
			expect(spec::menu_engine(sound.ctx, *sound.song).clip).not_().to_be_null();
			Clip* const saved_active = OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output);
			OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output) = spec::menu_engine(sound.ctx, *sound.song).clip;
			expect(spec::menu_engine(sound.ctx, *sound.song).clip->isActiveOnOutput()).to_be_true();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::DOWN;
			arp->noteMode = ArpNoteMode::DOWN;
			// `Sound::allNotesOff` clears this first thing -- the one effect
			// of `silenceArp`'s synth arm that survives in model state a spec can read.
			spec::menu_engine(sound.ctx, *sound.song).sound->invertReversed = true;

			menu::bindings::write(nodes::arp_mode, sound.ctx, static_cast<int32_t>(ArpMode::ARP));

			expect(arp->mode).to_equal(ArpMode::ARP);
			// DOWN/DOWN maps to `ArpPreset::DOWN` (`arpeggiator.cpp`) -- so this is false
			// unless `updatePresetFromCurrentSettings()` actually ran.
			expect(arp->preset).to_equal(ArpPreset::DOWN);
			// ... and this is true only if `silence_for_arp_toggle` reached the synth arm.
			expect(spec::menu_engine(sound.ctx, *sound.song).sound->invertReversed).to_be_false();
			OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output) = saved_active;
			covered.insert(&nodes::arp_mode);
		}

		{ // --- arp_preset: the settings re-derivation, the restart flag, the silence block -------
			const TablesRoot& sound = installed_root("sound");
			ArpeggiatorSettings* arp = spec::menu_engine(sound.ctx, *sound.song).arp;
			Clip* const saved_active = OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output);
			OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output) = spec::menu_engine(sound.ctx, *sound.song).clip;

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::RANDOM;
			arp->noteMode = ArpNoteMode::RANDOM;
			arp->flagForceArpRestart = false;
			spec::menu_engine(sound.ctx, *sound.song).sound->invertReversed = true;

			menu::bindings::write(nodes::arp_preset, sound.ctx, static_cast<int32_t>(ArpPreset::DOWN));

			expect(arp->preset).to_equal(ArpPreset::DOWN);
			// `ArpeggiatorSettings::updateSettingsFromCurrentPreset()` is the only thing
			// that moves these three; delete its call from `set_arp_preset` and all three fail.
			expect(arp->mode).to_equal(ArpMode::ARP);
			expect(arp->octaveMode).to_equal(ArpOctaveMode::DOWN);
			expect(arp->noteMode).to_equal(ArpNoteMode::DOWN);
			expect(arp->flagForceArpRestart).to_be_true();
			expect(spec::menu_engine(sound.ctx, *sound.song).sound->invertReversed).to_be_false();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->flagForceArpRestart = false;
			OutputPeek::active_clip_of(*spec::menu_engine(sound.ctx, *sound.song).output) = saved_active;
			covered.insert(&nodes::arp_preset);
		}

		{ // --- mod_fx_type: the type set on the target directly ------------------------------------
			const TablesRoot& sound = installed_root("sound");
			const ModFXType before = deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(sound.ctx, *sound.song).mod)->binding->renderEffects().mod_fx.type;
			menu::bindings::write(nodes::mod_fx_type, sound.ctx, static_cast<int32_t>(ModFXType::NONE));
			expect(deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(sound.ctx, *sound.song).mod)->binding->renderEffects().mod_fx.type)
			    .to_equal(ModFXType::NONE);
			menu::bindings::write(nodes::mod_fx_type, sound.ctx, static_cast<int32_t>(ModFXType::FLANGER));
			expect(deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(sound.ctx, *sound.song).mod)->binding->renderEffects().mod_fx.type)
			    .to_equal(ModFXType::FLANGER);
			deluge::model::changeEffects(*deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(sound.ctx, *sound.song).mod), [&](deluge::effects::EffectStates& e) { e.mod_fx.type = before; });
			covered.insert(&nodes::mod_fx_type);
		}

		{ // --- audio_clip_reverse: the clip's own sample controls --------------------------------
			const TablesRoot& audio = installed_root("audio clip");
			auto* clip = static_cast<AudioClip*>(spec::menu_engine(audio.ctx, *audio.song).clip);
			expect(clip).not_().to_be_null();
			const bool before = clip->sampleControls.reversed;
			menu::bindings::write(nodes::audio_clip_reverse, audio.ctx, 1);
			expect(clip->sampleControls.reversed).to_be_true();
			menu::bindings::write(nodes::audio_clip_reverse, audio.ctx, 0);
			expect(clip->sampleControls.reversed).to_be_false();
			clip->sampleControls.reversed = before;
			covered.insert(&nodes::audio_clip_reverse);
		}

		// --- The four `playbackHandler` / settings-root nodes ------------------------------------
		// These hooks ignore the context entirely (`nodes/settings.cpp`'s signatures take an unnamed
		// `const MenuContext&`), so the settings root's own context is the honest one to pass.
		const TablesRoot& settings = installed_root("settings");

		{ // midi_clock_out -- `setMidiOutClockMode` (the MIDI send is the part legacy cannot do here)
			menu::bindings::write(nodes::midi_clock_out, settings.ctx, 1);
			expect(playbackHandler.midiOutClockEnabled).to_be_true();
			menu::bindings::write(nodes::midi_clock_out, settings.ctx, 0);
			expect(playbackHandler.midiOutClockEnabled).to_be_false();
			covered.insert(&nodes::midi_clock_out);
		}
		{ // midi_clock_in
			menu::bindings::write(nodes::midi_clock_in, settings.ctx, 1);
			expect(playbackHandler.midiInClockEnabled).to_be_true();
			menu::bindings::write(nodes::midi_clock_in, settings.ctx, 0);
			expect(playbackHandler.midiInClockEnabled).to_be_false();
			covered.insert(&nodes::midi_clock_in);
		}
		{ // trigger_out_ppqn
			menu::bindings::write(nodes::trigger_out_ppqn, settings.ctx, 24);
			expect(playbackHandler.analogOutTicksPPQN).to_equal(uint32_t(24));
			menu::bindings::write(nodes::trigger_out_ppqn, settings.ctx, 48);
			expect(playbackHandler.analogOutTicksPPQN).to_equal(uint32_t(48));
			covered.insert(&nodes::trigger_out_ppqn);
		}
		{ // default_pad_brightness -- the `>> 2` the node's step-of-4 range implies
			menu::bindings::write(nodes::default_pad_brightness, settings.ctx, 32);
			expect(FlashStorage::defaultPadBrightness).to_equal(uint8_t(8));
			menu::bindings::write(nodes::default_pad_brightness, settings.ctx, 64);
			expect(FlashStorage::defaultPadBrightness).to_equal(uint8_t(16));
			expect(menu::bindings::read(nodes::default_pad_brightness, settings.ctx)).to_equal(int32_t(64));
			covered.insert(&nodes::default_pad_brightness);
		}
		{ // default_menu_highlighting
			menu::bindings::write(nodes::default_menu_highlighting, settings.ctx,
			                      static_cast<int32_t>(MenuHighlighting::PARTIAL_INVERSION));
			expect(FlashStorage::accessibilityMenuHighlighting).to_equal(MenuHighlighting::PARTIAL_INVERSION);
			menu::bindings::write(nodes::default_menu_highlighting, settings.ctx,
			                      static_cast<int32_t>(MenuHighlighting::NO_INVERSION));
			expect(FlashStorage::accessibilityMenuHighlighting).to_equal(MenuHighlighting::NO_INVERSION);
			covered.insert(&nodes::default_menu_highlighting);
		}
		{ // screensaver_mode
			menu::bindings::write(nodes::screensaver_mode, settings.ctx, static_cast<int32_t>(ScreensaverMode::OFF));
			expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::OFF);
			menu::bindings::write(nodes::screensaver_mode, settings.ctx, static_cast<int32_t>(ScreensaverMode::STARSCAPE));
			expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::STARSCAPE);
			covered.insert(&nodes::screensaver_mode);
		}
		{ // screensaver_timeout
			menu::bindings::write(nodes::screensaver_timeout, settings.ctx, 7);
			expect(FlashStorage::screensaverTimeoutMinutes).to_equal(uint8_t(7));
			menu::bindings::write(nodes::screensaver_timeout, settings.ctx, 21);
			expect(FlashStorage::screensaverTimeoutMinutes).to_equal(uint8_t(21));
			covered.insert(&nodes::screensaver_timeout);
		}
		{ // feature_sticky_shift -- plus the LightShiftLed poke `ShiftIsSticky` carries with it
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed,
			                           util::to_underlying(RuntimeFeatureStateToggle::Off));
			menu::bindings::write(nodes::feature_sticky_shift, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			menu::bindings::write(nodes::feature_sticky_shift, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::Off));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::Off));
			covered.insert(&nodes::feature_sticky_shift);
		}
		{ // feature_rounded_corners
			menu::bindings::write(nodes::feature_rounded_corners, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			menu::bindings::write(nodes::feature_rounded_corners, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::Off));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::Off));
			covered.insert(&nodes::feature_rounded_corners);
		}

		// The ratchet.
		expect(covered).to_equal(
		    std::set<const Node*>(std::begin(t3::kWritesNeedingBootedUi), std::end(t3::kWritesNeedingBootedUi)));

		playbackHandler.midiOutClockEnabled = saved_midi_out;
		playbackHandler.midiInClockEnabled = saved_midi_in;
		playbackHandler.analogOutTicksPPQN = saved_ppqn;
		FlashStorage::defaultPadBrightness = saved_brightness;
		FlashStorage::accessibilityMenuHighlighting = saved_highlighting;
		FlashStorage::screensaverMode = saved_screensaver;
		FlashStorage::screensaverTimeoutMinutes = saved_timeout;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, saved_sticky);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, saved_light_shift);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, saved_corners);
	});

	it("round-trips every param node, and raises the reverb flag on exactly the two that should", _ {
		size_t exercised = 0;
		size_t flagged = 0;
		for (const auto& [root, e] : every_entry()) {
			if (!e.node->bind.param.valid() || e.node->bind.set != nullptr || e.node->kind == menu::Kind::Custom
			    || !t3::relevant_here(*root, *e.node)) {
				continue;
			}
			// A param node is only round-trippable where the context can actually serve it; the settings root has no
			// param manager at all.
			if (spec::menu_engine(root->ctx, *root->song).params == nullptr) {
				continue;
			}
			install(root->ctx, root->song);
			const menu::IntRange r = menu::bindings::range(*e.node, root->ctx);
			for (int32_t v : spec::probe_values(r)) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
				menu::bindings::write(*e.node, root->ctx, v);
				// `relevant_here` already restricts the loop to nodes their own root's context shows, and a relevant
				// param node is servable by construction (that is what `serves_for` decides), so the round trip is
				// asserted outright.
				expect(menu::bindings::read(*e.node, root->ctx)).to_equal(v);
				expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_equal(raises_reverb(*e.node));
				exercised++;
			}
		}
		expect(exercised).to_be_greater_than(size_t(400)); // 462 today

		std::set<const Node*> reverb;
		for (const auto& [root, e] : every_entry()) {
			if (raises_reverb(*e.node)) {
				reverb.insert(e.node);
			}
		}
		flagged = reverb.size();
		// `sidechain::Shape` (`tables_2_spec.cpp`) and `sidechain::GlobalVolume` (this file) -- the only two
		// `unpatched_param::UpdatingReverbParams` subclasses in the whole of main's menu.
		expect(flagged).to_equal(size_t(2));
		expect(reverb.contains(&nodes::sidechain_shape)).to_be_true();
		expect(reverb.contains(&nodes::global_sidechain_volume)).to_be_true();
	});
});

describe tables_3_chaining("scenes::menu settings and other roots tables: selectButtonPress chaining", $ {
	it("answers what cv::Selection and gate::Selection answer, output by output", _ {
		// Both selectors read the output the cursor names -- the port's `MenuContext::source_index` -- so the sweep is
		// over every value main distinguishes, the `NUM_GATE_CHANNELS` sentinel included.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const std::string cv_path = spec::first_path_of(settings_entries(), nodes::cv_selection);
		const std::string gate_path = spec::first_path_of(settings_entries(), nodes::gate_selection);
		expect(cv_path.empty()).to_be_false();
		expect(gate_path.empty()).to_be_false();

		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			const std::string column = "chain/" + std::to_string(which);
			expect(t3::chain_name(nodes::cv_selection.select_chain(ctx, menu_scene().state())))
			    .to_equal(table().value(cv_path, column));
			expect(t3::chain_name(nodes::gate_selection.select_chain(ctx, menu_scene().state())))
			    .to_equal(table().value(gate_path, column));
		}
	});

	it("declares the three chain-only pages with the legacy items' own shape and values", _ {
		// `cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` are children of nothing, so no tree walk can see them: their
		// shape and their reads are rows of their own.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);

		expect(nodes::cv_submenu.children.size()).to_equal(size_t(2));
		expect(std::to_string(nodes::cv_submenu.children.size())).to_equal(table().value("chain/cv_submenu", "kids"));
		expect(nodes::cv_submenu.kind).to_equal(menu::Kind::Submenu);
		for (size_t i = 0; i < nodes::cv_submenu.children.size(); i++) {
			expect(text_of(nodes::cv_submenu.children[i]->name))
			    .to_equal(table().value("chain/cv_submenu/" + std::to_string(i), "name"));
		}

		// Values, on each CV output in turn: `cv_volts` and `cv_transpose` both index the output's own channel, so a
		// port that ignored `source_index` would pass on output 0 alone.
		for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			const std::string column = "read/" + std::to_string(which);
			expect(std::to_string(bindings::read(nodes::cv_volts, ctx))).to_equal(table().value("chain/cv_volts", column));
			expect(std::to_string(bindings::read(nodes::cv_transpose, ctx)))
			    .to_equal(table().value("chain/cv_transpose", column));

			// A write goes to that output and no other.
			bindings::write(nodes::cv_volts, ctx, 100 + which);
			expect(int32_t(cvEngine.cvChannels[which].voltsPerOctave)).to_equal(100 + which);
		}

		for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			expect(std::to_string(bindings::read(nodes::gate_mode, ctx)))
			    .to_equal(table().value("chain/gate_mode", "read/" + std::to_string(which)));

			// The two trigger polarities always, plus CLOCK or RUN SIGNAL on the two outputs that carry them.
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
			const size_t n = bindings::names(nodes::gate_mode, ctx, names, scratch);
			const std::vector<std::string>* want = table().find("chain/gate_mode", "options/" + std::to_string(which));
			expect(want).not_().to_be_null();
			if (want == nullptr) {
				continue;
			}
			expect(n).to_equal(want->size());
			for (size_t i = 0; i < n && i < want->size(); i++) {
				expect(std::string{names[i]}).to_equal((*want)[i]);
			}
		}

		expect(std::to_string(bindings::read(nodes::gate_off_time, settings.ctx)))
		    .to_equal(table().value("chain/gate_off_time", "read"));
		expect(text_of(nodes::gate_off_time.title)).to_equal(table().value("chain/gate_off_time", "title"));
		expect(nodes::gate_off_time.bind.decimal_places).to_equal(uint8_t(1));
		expect(bindings::range(nodes::gate_off_time, settings.ctx).min).to_equal(int32_t(1));
		expect(bindings::range(nodes::gate_off_time, settings.ctx).max).to_equal(int32_t(100));
	});

	it("numbers the three chain-only settings pages, which no children span reaches", _ {
		// The reachability rule, on the settings half: `assign_node_ids` follows `chain_targets`,
		// so `SceneId::arg` can name one of these pages and `MenuScene::on_enter` can restore it.
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (const Node* n : {&nodes::cv_submenu, &nodes::cv_volts, &nodes::cv_transpose, &nodes::gate_mode,
		                      &nodes::gate_off_time}) {
			const uint16_t id = menu::node_id(*n);
			expect(id).not_().to_equal(uint16_t(0));
			expect(menu::node_by_id(id)).to_equal(n);
		}
		// And they really are chain-only: the settings root's thirteen children do not name them,
		// and neither does anything else's `children`.
		for (const Node* n : {&nodes::cv_submenu, &nodes::gate_mode, &nodes::gate_off_time}) {
			expect(reachable_by_children(n)).to_be_false();
		}
		// `cv_volts` / `cv_transpose` are ordinary children -- of a page that is itself chain-only.
		expect(reachable_by_children(&nodes::cv_volts)).to_be_true();
	});

	it("never lets a select_chain hook name a node outside its own chain_targets, anywhere in the tree", _ {
		// The containment half of the chain contract, whole-tree. `validate_tree`'s
		// `Flag::ChainTargetsMismatch` can only test both-set-or-neither: a `SelectChainFn` is
		// opaque to a static walk, so nothing structural can prove `range(select_chain)` is a subset
		// of `chain_targets`. A hook that escaped its span would name a page with no node id --
		// unaddressable by `SceneId::arg`, so `on_enter` could never restore it, absent from
		// `validate_tree`'s walk and from the dump -- and every one of those failures is silent.
		//
		// Not provable, but exhaustively *exercised*: every chaining node in `kAllRoots` is asked
		// under every one of the ten root fixtures' contexts (plus the CV one), which between them
		// vary kit-row-ness, output type and `source_index` -- the three facts the shipped hooks
		// actually branch on. Cheap enough to be whole-tree rather than per-example.
		// The note-row hooks ask the note bridge, and the real one reads the current clip, which these
		// contexts do not install; without a bridge they answer their defaults.
		const spec::NoNoteBridge no_bridge;
		std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
		std::set<const Node*> seen;
		std::vector<MenuContext> contexts;
		for (const TablesRoot& root : roots()) {
			contexts.push_back(root.ctx);
		}
		contexts.push_back(cv_fixture().ctx);
		// `gate_selection_chain` is the one hook that branches on `source_index`, and no fixture
		// carries the `NUM_GATE_CHANNELS` sentinel; add both sides of that branch explicitly rather
		// than leave the interesting value unswept.
		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = roots().back().ctx;
			ctx.source_index = static_cast<int8_t>(which);
			contexts.push_back(ctx);
		}

		size_t chaining_nodes = 0;
		size_t answers = 0;
		while (!stack.empty()) {
			const Node* n = stack.back();
			stack.pop_back();
			if (n == nullptr || !seen.insert(n).second) {
				continue;
			}
			for (const Node* c : n->children) {
				stack.push_back(c);
			}
			for (const Node* t : n->chain_targets) {
				stack.push_back(t);
			}
			if (n->select_chain == nullptr) {
				continue;
			}
			chaining_nodes++;
			for (const MenuContext& ctx : contexts) {
				const Node* got = n->select_chain(ctx, menu_scene().state());
				if (got == nullptr) {
					continue; // "ascend" -- always legal, and names no page
				}
				answers++;
				const bool contained =
				    std::find(n->chain_targets.begin(), n->chain_targets.end(), got) != n->chain_targets.end();
				// Verdict on the left so a failure reads "expected in / got ESCAPED", and names the
				// page that escaped rather than just reporting a false boolean.
				expect(std::string{contained ? "in chain_targets: " : "ESCAPED chain_targets: "} + text_of(got->name))
				    .to_equal(std::string{"in chain_targets: "} + text_of(got->name));
				// And the consequence the containment exists to guarantee.
				expect(menu::node_id(*got)).not_().to_equal(uint16_t(0));
			}
		}
		// The chaining nodes of the shipped tree; a chain added without a spec fails here.
		//
		// **80** = 7 + 56 + 4 + 3 + 1 + 3 + 2 + 4. The seven original chain nodes; 56 `PatchedParam` nodes
		// (`with_patch_chain`, every node with `bind.param.kind == PATCHED`: main's 53 and the DX7's transpose, amp mod
		// and level); the four patch-cable pages
		// (`source_selection_{regular,range}`, `patch_cable_strength_{regular,range}`); the three items that open one
		// (`patch_cables`, `vibrato`, `sidechain_volume`); the range picker; `midi_devices`, which chains to the
		// per-device page (`midi::Devices::selectButtonPress`,
		// `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), with the two MPE selectors under it chaining onward
		// -- `mpe_direction` (`DirectionSelector`, `origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`)
		// -> `mpe_zone` (`ZoneSelector`, `origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`) ->
		// `mpe_zone_num_member_channels`, all three unconditional, so all three are swept here under every context and
		// every one lands inside its own one-element span; and `nodes::note_iterance` and `nodes::note_row_iterance`,
		// which each chain to their own custom-iterance root on `kCustomIterancePreset`
		// (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`,
		// `origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`); and the four DX7 chains: the synth mode
		// into "DX7 source" on the master page and as the DX7 page's first column (main's oscillator 1 type,
		// `origin/main:src/deluge/gui/menu_item/osc/type.h`), "DX7 parameters" and "DX7 operator N" (`nodes/dx.h`).
		expect(chaining_nodes).to_equal(size_t(80));
		expect(answers).to_be_greater_than(size_t(0));
	});

	it("has exactly one chain edge that closes a cycle, and it is the legacy one", _ {
		// The dump emits a cycle as a `{"ref": true}` stub, so the only net the dump itself has is
		// "the JSON changed", which says nothing about why. This names the edge instead.
		//
		// A **path-aware** walk, not the seen-set one above: a cycle is a target that is already
		// open on the current path, which is exactly `scenarios/menu_dump.cpp`'s `g_path` test and
		// therefore exactly what makes the dump emit a stub.
		// A *set* of edges: the same edge closes a cycle on every path that reaches it (the dump
		// emits a stub at each such occurrence -- 118 of them), and what is being pinned here is
		// which edges can do it at all, not how many times the walk meets one.
		std::set<std::pair<const Node*, const Node*>> back_edges;
		std::vector<const Node*> path;
		std::set<std::pair<const Node*, const Node*>> visited_edges;
		const auto walk = [&](auto&& self, const Node* n) -> void {
			if (n == nullptr) {
				return;
			}
			path.push_back(n);
			for (const Node* c : n->children) {
				if (c != nullptr && visited_edges.insert({n, c}).second) {
					self(self, c);
				}
			}
			for (const Node* t : n->chain_targets) {
				if (t == nullptr) {
					continue;
				}
				if (std::find(path.begin(), path.end(), t) != path.end()) {
					back_edges.insert({n, t});
					continue; // the dump stops here too -- it emits the stub and does not recurse
				}
				if (visited_edges.insert({n, t}).second) {
					self(self, t);
				}
			}
			path.pop_back();
		};
		for (const Node* root : nodes::kAllRoots) {
			walk(walk, root);
		}

		// **Both directions of one two-node loop**, and nothing else. `patch_cable_strength::rangeMenu`'s select press
		// opens `source_selection::rangeMenu` (`PatchCableStrength::selectButtonPress` in
		// `origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`) and that page's opens the strength page
		// straight back (`Range::selectButtonPress` in
		// `origin/main:src/deluge/gui/menu_item/source_selection/range.cpp`) -- main's own loop, and how a user toggles
		// between "which source modulates this cable's depth" and "by how much". Which of the two shows up as the back
		// edge depends on which the walk enters first, and the tree reaches each of them first from somewhere
		// (`patch_cables` chains straight to the strength page, every `PatchedParam` reaches the picker first), so both
		// do.
		//
		// Named by identity, not by `name`: the two share the string "Modulate depth".
		const auto name_of = [](const Node* n) -> std::string {
			if (n == &nodes::source_selection_range) {
				return "source_selection_range";
			}
			if (n == &nodes::patch_cable_strength_range) {
				return "patch_cable_strength_range";
			}
			return "UNEXPECTED:" + text_of(n->name);
		};
		// The set orders by pointer, which is link-order-dependent, so the names are sorted before
		// they are joined -- and compared as one string, so an unintended cycle prints the edge that
		// caused it rather than a bare `false`.
		std::vector<std::string> names;
		for (const auto& [from, to] : back_edges) {
			names.push_back(name_of(from) + " -> " + name_of(to));
		}
		std::sort(names.begin(), names.end());
		std::string named;
		for (const std::string& one : names) {
			named += one + "; ";
		}
		expect(named).to_equal(std::string{"patch_cable_strength_range -> source_selection_range; "
		                                   "source_selection_range -> patch_cable_strength_range; "});
	});

	it("validates the three chain-only pages along with the settings root", _ {
		// They are only in the walk because `collect` follows `chain_targets` too; without that the
		// `Unreachable` rule would fire on every one of them (`assign_node_ids` would have numbered
		// them while this walk did not, or the reverse).
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const Node* subtree[] = {&nodes::cv_selection, &nodes::gate_selection};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues =
		    menu::validate_tree(subtree, contexts, {}, {},
		                        menu::ValidateOptions{.skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});
});

describe tables_3_reset_items("scenes::menu settings and other roots tables: the five reset_* items", $ {
	// `ContextMenuScene`, its `Descriptor` table and all five accept hooks are driven directly by `DescriptorId`
	// and checked against main's frozen resets (`context_menu_scene_spec.cpp`'s `cm_actions`). What that spec does
	// NOT drive is the settings tree itself: it opens `ContextMenuScene` straight off a `DescriptorId`, never through
	// `nodes::reset_flash` .. `nodes::reset_all`. This is the missing half -- the mapping from menu
	// node to descriptor -- and it is the one thing a swapped pair of `ResetItem` ids would break
	// silently if it were untested.
	namespace context_menu = deluge::ui::scenes::context_menu;

	it("opens the reset context menu on the matching descriptor, rather than resetting on the spot", _ {
		// Real descent from `reset_settings` (the "Reset Settings" submenu, `nodes/settings.h`'s
		// `kResetSettingsChildren`) -- cursor down to row `i`, then the select-encoder press that
		// `enter_child` -> `enter_leaf` -> `CustomItem::on_focus` runs on, the same path
		// `rename_scene_spec.cpp`'s "opens on the drum target" test drives `EditNameItem` through.
		deluge::base::notify::dirty().drain();
		// A stray notice left in the global queue by an earlier describe block in this same process
		// would otherwise push a `ConsoleScene` overlay on this test's very first tick, one deeper
		// than the plain menu-then-context-menu stack this counts -- `tables_3_pixels`' own
		// `before_each` drains both queues for exactly this reason.
		{
			deluge::base::notify::Notice stray;
			while (deluge::base::notify::notices().pop(stray)) {}
		}
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);

		const context_menu::DescriptorId kOrder[] = {
		    context_menu::DescriptorId::ResetFlash,
		    context_menu::DescriptorId::ResetCommunityFeatures,
		    context_menu::DescriptorId::ResetMidiFollow,
		    context_menu::DescriptorId::ResetMidiDevices,
		    context_menu::DescriptorId::ResetAll,
		};
		for (size_t i = 0; i < std::size(kOrder); i++) {
			Rig r;
			r.open(nodes::reset_settings, settings.ctx);
			r.shell.tick(0);
			for (size_t t = 0; t < i; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
			r.shell.tick(110);

			// `CustomItem::on_focus` fired from `enter_leaf`, and this tick's `after_render` turned
			// the resulting `MenuWalkState::pending_scene` into a `Push` -- the context menu is on top.
			expect(&r.shell.stack().input_top()).to_equal(&context_menu::context_menu_scene());
			expect(r.shell.stack().depth()).to_equal(size_t(2));
			expect(static_cast<int>(context_menu::context_menu_scene().descriptor_id()))
			    .to_equal(static_cast<int>(kOrder[i]));
		}
	});

	it("resets exactly what main resets, driven through the real node, the real push and the real "
	   "accept button -- not just through a DescriptorId",
	   _ {
		   deluge::base::notify::dirty().drain();
		   {
			   deluge::base::notify::Notice stray;
			   while (deluge::base::notify::notices().pop(stray)) {}
		   }
		   const TablesRoot& settings = roots().back();
		   install(settings.ctx, settings.song);

		   // The community-features reset's field of the reset witness, moved where `kPerturbed` moves it, so the
		   // port starts here, through the menu node, where main's frozen reset started.
		   runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer,
		                              spec::reset::kPerturbed.drum_randomizer);

		   Rig r;
		   r.open(nodes::reset_settings, settings.ctx);
		   r.shell.tick(0);
		   // Row 1 is `reset_community_features` (`kResetSettingsChildren`'s own order).
		   r.turn(1, 10);
		   r.shell.tick(15);
		   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		   r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
		   r.shell.tick(110);
		   expect(&r.shell.stack().input_top()).to_equal(&context_menu::context_menu_scene());
		   expect(static_cast<int>(context_menu::context_menu_scene().descriptor_id()))
		       .to_equal(static_cast<int>(context_menu::DescriptorId::ResetCommunityFeatures));

		   // The one option ("OK") is already the cursor's position; the select-encoder press is
		   // `ContextMenu::buttonAction`'s `probablyAcceptCurrentOption` arm.
		   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
		   r.button(kSelectEncButtonX, kSelectEncButtonY, false, 130);
		   r.shell.tick(140);
		   const uint32_t ported = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer);

		   // `acceptCurrentOption` returns false for every shipped reset -> the menu removes itself,
		   // and `go_up_on_resume` (set alongside the push, `EditNameItem`'s own mechanism) steps the
		   // menu back out of the item it does not linger on -- back to the row it was entered from.
		   expect(r.shell.stack().depth()).to_equal(size_t(1));
		   expect(menu_scene().focused()).to_equal(&nodes::reset_settings);

		   // Main's community-features reset from the same sentinel, as `context_menu_scene.tsv` holds it
		   // (`context_menu_scene_spec` asks every row of that table; this example reads one).
		   expect(std::to_string(ported))
		       .to_equal(spec::OracleTable::named("context_menu_scene")
		                     .value("reset/community_features", "drum_randomizer"));
		   expect(ported).not_().to_equal(spec::reset::kPerturbed.drum_randomizer); // the reset ran
	   });
});

describe tables_3_midi_commands("scenes::menu settings and other roots tables: the twelve midi_command_* items", $ {
	// `midi::Command` is a MIDI *learn* item: focus it, send a message, and the message is bound to one
	// `GlobalMIDICommand`. The port is one `nodes::MidiCommandItem` parameterised by that command. A message reaches it
	// in the menu's learn mode through `MenuScene::handle` (`MenuLearn::offer`), which `offer_while_learning` drives;
	// an example that reads whether the item itself learned calls its learn hooks (`focused_item`). The rows hold the
	// whole `globalMIDICommands` array main's item leaves, which catches an item that learned onto the wrong command
	// as well as one that learned nothing.

	it("binds each node to the same GlobalMIDICommand as the legacy item in the same position", _ {
		expect(std::size(nodes::kMidiCommandsChildren)).to_equal(size_t(12));
		for (const Node* node : nodes::kMidiCommandsChildren) {
			const auto* port = dynamic_cast<const nodes::MidiCommandItem*>(node->custom);
			expect(port).not_().to_be_null(); // the node really is a MidiCommandItem now
			if (port == nullptr) {
				continue;
			}
			expect(std::to_string(static_cast<int>(port->command())))
			    .to_equal(table().value(spec::first_path_of(settings_entries(), *node), "command"));
		}
	});

	it("learns a CC onto exactly the slot the legacy item learns it onto, driven through the real "
	   "descent",
	   _ {
		   const TablesRoot& settings = roots().back();
		   install(settings.ctx, settings.song);
		   const CommandSlots saved = CommandSlots::capture();
		   for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
			   const int32_t cc = static_cast<int32_t>(40 + i);
			   CommandSlots::clear_all();
			   Rig r;
			   focus_command_row(r, settings.ctx, i);
			   expect(menu_scene().focused()).to_equal(nodes::kMidiCommandsChildren[i]);
			   expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 3, static_cast<uint8_t>(cc), 127))
			       .to_be_true();
			   const std::string got = CommandSlots::capture().describe();
			   expect(got).to_equal(table().value("learn_cc/" + std::to_string(i), "slot"));
			   expect(got).not_().to_equal(std::string{"(all clear)"}); // a no-op would match vacuously
		   }
		   saved.restore();
	   });

	it("from the list, a note-on and a program change learn the row under the cursor as legacy's Submenu does", _ {
		// `Submenu::learnNoteOn` / `learnProgramChange` (`origin/main:src/deluge/gui/menu_item/submenu.cpp`) forward to
		// the current child when the list is the current item.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();
		const size_t play = 0;

		CommandSlots::clear_all();
		Rig note_rig;
		cursor_on_command_row(note_rig, settings.ctx, play);
		expect(menu_scene().focused()).to_equal(&nodes::midi_commands);
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 5, 61, 100)).to_be_true();
		const std::string note_got = CommandSlots::capture().describe();
		expect(note_got).to_equal(table().value("learn_list/0/note", "slot"));
		expect(note_got).not_().to_equal(std::string{"(all clear)"});

		CommandSlots::clear_all();
		Rig pc_rig;
		cursor_on_command_row(pc_rig, settings.ctx, play);
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiProgramChange, 2, 9, 0)).to_be_true();
		expect(CommandSlots::capture().describe()).to_equal(table().value("learn_list/0/pgm", "slot"));
		saved.restore();
	});

	it("from the list, a CC learns the row under the cursor as the item itself does", _ {
		// Main's `Submenu` has no `learnCC`, so main's list drops a CC a command would learn; the port forwards it to
		// the row, as entering the row and learning there does (an accepted difference, `parity/DIFFERENCES.md`). The
		// row is what the item itself learns.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();
		const size_t play = 0;

		CommandSlots::clear_all();
		Rig r;
		cursor_on_command_row(r, settings.ctx, play);
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 3, 42, 127)).to_be_true();
		const std::string got = CommandSlots::capture().describe();
		expect(got).to_equal(table().value("learn_list/0/cc", "slot"));
		expect(got).not_().to_equal(std::string{"(all clear)"});
		saved.restore();
	});

	it("learns a note-on and a program change the way the legacy item does, including FILL's refusal",
	   _ {
		   const TablesRoot& settings = roots().back();
		   install(settings.ctx, settings.song);
		   const CommandSlots saved = CommandSlots::capture();

		   for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
			   // Note-on: the raw channel is written.
			   CommandSlots::clear_all();
			   Rig note_rig;
			   focus_command_row(note_rig, settings.ctx, i);
			   expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 5,
			                               static_cast<uint8_t>(60 + i), 100))
			       .to_be_true();
			   expect(CommandSlots::capture().describe())
			       .to_equal(table().value("learn_own/" + std::to_string(i) + "/note", "slot"));

			   // Program change: `IS_A_PC` is added to the channel -- except on FILL, which refuses with the
			   // `CANT_LEARN_PC` popup and writes nothing. The MIDI routing still reports the message as used in both
			   // cases, and so does the port.
			   CommandSlots::clear_all();
			   Rig pc_rig;
			   focus_command_row(pc_rig, settings.ctx, i);
			   const std::string want = table().value("learn_own/" + std::to_string(i) + "/pgm", "slot");
			   const bool is_fill = want == "refused";
			   const deluge::ui::scenes::menu::CustomItem* item = focused_item();
			   expect(item).not_().to_be_null();
			   if (item != nullptr) {
				   expect(item->learn_program_change(settings.ctx, spec_device(), 2, static_cast<int32_t>(7 + i)))
				       .to_equal(!is_fill); // FILL refuses
			   }
			   const std::string pc_got = CommandSlots::capture().describe();
			   // The port's own half of the refusal: the same `CANT_LEARN_PC` string main pops, and only on FILL.
			   size_t refusals = 0;
			   for (deluge::base::notify::Notice n; deluge::base::notify::notices().pop(n);) {
				   refusals += (n.string_id == uint16_t(String::STRING_FOR_CANT_LEARN_PC)) ? 1 : 0;
			   }
			   expect(is_fill ? std::string{"(all clear)"} : want).to_equal(pc_got);
			   expect(refusals).to_equal(size_t(is_fill ? 1 : 0));
		   }
		   saved.restore();
	   });

	it("stores an MPE zone the way legacy does, and renders it as a zone rather than a channel", _ {
		// This arm is live, not defensive: the MIDI routing resolves the incoming channel through
		// `MIDIPort::channelToZone` *before* dispatching to the learn, so a note-on learn from an MPE-configured input
		// port really does arrive as `MIDI_CHANNEL_MPE_LOWER_ZONE`/`UPPER_ZONE` and really does take the item's zone
		// branch. (The CC path is unaffected -- it is handed the raw channel.)
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();

		const int32_t kZones[] = {MIDI_CHANNEL_MPE_LOWER_ZONE, MIDI_CHANNEL_MPE_UPPER_ZONE};
		for (const int32_t zone : kZones) {
			CommandSlots::clear_all();
			Rig r;
			focus_command_row(r, settings.ctx, 0);
			expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, static_cast<uint8_t>(zone), 64, 100))
			    .to_be_true();
			expect(CommandSlots::capture().describe()).to_equal(table().value("learn_mpe/" + std::to_string(zone), "slot"));
			// A zone, not a channel: `channelOrZone >= 16` is `LearnedMIDI::isForMPEZone()`, and it is below `IS_A_CC`
			// (18), so the third row still reads "Note".
			expect(midiEngine.globalMIDICommands[1].channelOrZone).to_equal(uint8_t(zone));
		}

		// The zone *readout* is pinned by a golden in `tables_3_pixels` below -- an inequality against the plain-channel
		// frame would not have caught a deleted zone arm, since the fallback prints "CHANNEL 17" and that differs from
		// "CHANNEL 4" too. Confirmed by mutation.
		saved.restore();
	});

	it("ignores a zero-valued CC, which the menu still uses", _ {
		// `Command::learnCC`: the release half of a momentary controller binds nothing. `SoundEditor` uses the
		// message regardless, and so does the menu.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();

		Rig r;
		focus_command_row(r, settings.ctx, 0);
		const deluge::ui::scenes::menu::CustomItem* item = focused_item();
		expect(item).not_().to_be_null();
		if (item != nullptr) {
			expect(item->learn_cc(settings.ctx, spec_device(), 3, 42, 0)).to_be_false();
		}
		expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 3, 42, 0)).to_be_true();
		expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		saved.restore();
	});

	it("unlearns exactly the focused item's slot, and clears it on any select-encoder motion", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();

		// Row 0 is `midi_command_play`; row 2 is `midi_command_record` (`kMidiCommandsChildren`).
		CommandSlots::clear_all();
		{
			Rig r;
			focus_command_row(r, settings.ctx, 2);
			expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 3, 42, 127)).to_be_true();
		}
		const std::string learned = CommandSlots::capture().describe();
		{
			Rig r;
			focus_command_row(r, settings.ctx, 0);
			expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 4, 43, 127)).to_be_true();
			// `MenuScene::unlearn` -> `Command::unlearnAction`
			// (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`): only the focused item forgets, so RECORD's
			// binding survives PLAY's unlearn.
			menu_scene().unlearn();
		}
		expect(CommandSlots::capture().describe()).to_equal(learned);

		// `Command::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`): a detent in either
		// direction clears the binding. Driven through the real encoder, not by calling the item.
		{
			Rig r;
			focus_command_row(r, settings.ctx, 2);
			r.turn(-1, 300);
			r.shell.tick(310);
		}
		expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		saved.restore();
	});

	it("asks for the learn blink on these twelve and on nothing else, and lights the LEARN LED for "
	   "them",
	   _ {
		   // `MenuItem::shouldBlinkLearnLed` is true only on `midi::Command` (the base returns false), and the editor's
		   // screen change turns that into `blinkLed(IndicatorLED::LEARN)` or `setLedState(LEARN, false)` on every
		   // screen change (`SoundEditor::updatePadLightsFor` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
		   // Board LED 7 is LEARN (`ui/shell/profiles/deluge.cpp`).
		   constexpr uint8_t kLearnBoardLed = 7;
		   const TablesRoot& settings = roots().back();
		   install(settings.ctx, settings.song);

		   for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
			   Rig r;
			   focus_command_row(r, settings.ctx, i);
			   expect(menu_scene().wants_learn_blink()).to_be_true();
			   // A blink is a phase, so sample across more than one half-period (110 ms) before
			   // concluding the LED never came on.
			   bool lit = false;
			   for (uint32_t t = 220; t <= 660; t += 55) {
				   r.shell.tick(t);
				   for (const auto& w : r.fake.led_writes) {
					   lit = lit || (w.first == kLearnBoardLed && w.second);
				   }
			   }
			   expect(lit).to_be_true();
		   }

		   // A plain value item under the same root: no blink, and the LED is never lit.
		   Rig plain;
		   plain.open(nodes::default_pad_brightness, settings.ctx);
		   plain.shell.tick(0);
		   expect(menu_scene().wants_learn_blink()).to_be_false();
		   for (uint32_t t = 55; t <= 660; t += 55) {
			   plain.shell.tick(t);
		   }
		   for (const auto& w : plain.fake.led_writes) {
			   if (w.first == kLearnBoardLed) {
				   expect(w.second).to_be_false();
			   }
		   }

		   // And the submenu the twelve hang off does not blink LEARN itself; its learn goes to the row under the
		   // cursor, which the list examples above pin.
		   Rig sub;
		   sub.open(nodes::midi_commands, settings.ctx);
		   sub.shell.tick(0);
		   expect(menu_scene().wants_learn_blink()).to_be_false();
	   });

	it("has no param learn target: this is global-command learn, not param learn", _ {
		// `bindings::learn_target` is `PatchedParam`/`UnpatchedParam::getLearningThing()`
		// (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`,
		// `origin/main:src/deluge/gui/menu_item/unpatched_param.cpp`) -- "which automatable parameter would a CC bind
		// to". These twelve bind a whole message to a transport action instead and carry no `ParamRef`, so there is no
		// target. The two learn paths share a name and nothing else.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().learn_target().has_value()).to_be_false();
	});
});

describe tables_3_midi_follow_learn("scenes::menu settings and other roots tables: MIDI follow learns its channel and kit root note", $ {
	it("each follow channel learns a note-on and a CC as legacy's FollowChannel does, and Shift+LEARN clears it", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		size_t which = 0;
		for (const Node* node : nodes::kMidiFollowChannelChildren) {
			for (const auto kind : {deluge::ui::shell::UiEvent::Kind::MidiNoteOn, deluge::ui::shell::UiEvent::Kind::MidiCc}) {
				midiEngine.midiFollowChannelType[which].clear();
				Rig r;
				r.open(*node, settings.ctx);
				r.shell.tick(0);
				expect(offer_while_learning(kind, 4, 60, 100)).to_be_true();
				const std::string got = follow_state(which);
				const std::string want = table().value("follow/" + std::to_string(which),
				                                       kind == deluge::ui::shell::UiEvent::Kind::MidiNoteOn
				                                           ? "learned/note"
				                                           : "learned/cc");
				expect(got).to_equal(want);
				expect(want).not_().to_equal(std::string{"-/"} + std::to_string(MIDI_CHANNEL_NONE));

				shift_learn();
				expect(follow_state(which)).to_equal(std::string{"-/"} + std::to_string(MIDI_CHANNEL_NONE));
			}
			which++;
		}
		expect(which).to_equal(size_t(19));
	});

	it("the kit root note learns a note-on as legacy's FollowKitRootNote does", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		midiEngine.midiFollowKitRootNote = 36;
		Rig r;
		r.open(nodes::midi_follow_kit_root_note, settings.ctx);
		r.shell.tick(0);
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 4, 61, 100)).to_be_true();
		expect(std::to_string(int(midiEngine.midiFollowKitRootNote))).to_equal(table().value("follow/kit_root", "learned"));
		expect(int(midiEngine.midiFollowKitRootNote)).to_equal(61);
	});

	it("from the follow-channel list, a note-on learns the row under the cursor and pops LEARNED, as legacy does", _ {
		// Legacy's item is not the current one when the list forwards to it, so it pops LEARNED instead of redrawing.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		midiEngine.midiFollowChannelType[0].clear();
		(void)last_popup_string();
		Rig r;
		r.open(nodes::midi_follow_channel, settings.ctx);
		r.shell.tick(0);
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 4, 60, 100)).to_be_true();
		expect(follow_state(0)).to_equal(std::string{fake_cable().getDisplayName()} + "/4");
		expect(last_popup_string()).to_equal(uint16_t(String::STRING_FOR_LEARNED));
	});

	it("a MIDI command learned from the list pops LEARNED, and one learned on its own screen does not", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig list;
		cursor_on_command_row(list, settings.ctx, 0);
		(void)last_popup_string();
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 5, 61, 100)).to_be_true();
		expect(last_popup_string()).to_equal(uint16_t(String::STRING_FOR_LEARNED));
		Rig leaf;
		focus_command_row(leaf, settings.ctx, 0);
		(void)last_popup_string();
		expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 5, 62, 100)).to_be_true();
		expect(last_popup_string()).to_equal(uint16_t(0));
		saved.restore();
	});

	it("a follow channel's turn wraps through unassigned as legacy's FollowChannel::selectEncoderAction does", _ {
		// Past either end the channel becomes unassigned and its learned device is forgotten (`midiInput.clear()`); from
		// unassigned a turn up lands on channel 1 and a turn down on the MPE upper zone.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		midiEngine.midiFollowChannelType[0].clear();
		midiEngine.midiFollowChannelType[0].cable = &fake_cable();
		midiEngine.midiFollowChannelType[0].channelOrZone = 0;
		const std::string unassigned = std::string{"-/"} + std::to_string(MIDI_CHANNEL_NONE);
		Rig r;
		r.open(nodes::midi_follow_channel_a, settings.ctx);
		r.shell.tick(0);

		r.turn(-1, 10);
		r.shell.tick(20);
		expect(follow_state(0)).to_equal(unassigned);
		r.turn(-1, 30);
		r.shell.tick(40);
		expect(follow_state(0)).to_equal(std::string{"-/"} + std::to_string(MIDI_CHANNEL_MPE_UPPER_ZONE));
		r.turn(1, 50);
		r.shell.tick(60);
		expect(follow_state(0)).to_equal(unassigned);
		r.turn(1, 70);
		r.shell.tick(80);
		expect(follow_state(0)).to_equal(std::string{"-/0"});
		r.turn(3, 90);
		r.shell.tick(100);
		expect(follow_state(0)).to_equal(std::string{"-/3"});
	});

	it("a learn or unlearn on a follow channel's own screen shows the channel it leaves, as setValue does", _ {
		// `FollowChannel::learnCC`/`learnNoteOn` and `unlearnAction` set the item's value as well as the learned input,
		// and redraw when the item is the current one; `FollowKitRootNote::learnNoteOn`
		// (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_kit_root_note.h`) the same.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		midiEngine.midiFollowChannelType[0].clear();
		midiEngine.midiFollowChannelType[0].channelOrZone = 0;
		{
			Rig r;
			r.open(nodes::midi_follow_channel_a, settings.ctx);
			r.shell.tick(0);
			expect(menu_scene().state().edit_int).to_equal(0);
			expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiCc, 4, 20, 64)).to_be_true();
			expect(menu_scene().state().edit_int).to_equal(4);
			shift_learn();
			expect(menu_scene().state().edit_int).to_equal(int32_t{MIDI_CHANNEL_NONE});
		}
		{
			Rig k;
			k.open(nodes::midi_follow_kit_root_note, settings.ctx);
			k.shell.tick(0);
			expect(offer_while_learning(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 4, 61, 100)).to_be_true();
			expect(menu_scene().state().edit_int).to_equal(61);
		}
	});

	it("every follow channel draws its own screen and turns its own way", _ {
		for (const Node* node : nodes::kMidiFollowChannelChildren) {
			expect(node->bind.leaf_draw).not_().to_be_null();
			expect(node->bind.turn).not_().to_be_null();
		}
	});
});

describe tables_3_dynamic("scenes::menu settings and other roots tables: the CV/gate selectors and the MIDI device list", $ {
	// The last three settings items main implements as bespoke `MenuItem` subclasses rather than as data:
	// `cv::Selection` (`origin/main:src/deluge/gui/menu_item/cv/selection.h`), `gate::Selection`
	// (`origin/main:src/deluge/gui/menu_item/gate/selection.h`) and `midi::Devices`
	// (`origin/main:src/deluge/gui/menu_item/midi/devices.h`, `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`).
	// The port is three `CustomItem`s (`menu/nodes/dynamic.h`) plus the `MenuWalkState::source_choice` /
	// `midi_device_choice` write-back `nodes/settings.h` recorded as owed.
	//
	// **These are NOT `Node::dynamic_children` nodes**, as one might expect them to be. A submenu row is labelled by
	// `display_text` (`MenuView::render_vertical`; `present.cpp`) -- a `Binding::name_text` hook with no row index, or
	// the `Node::name` l10n id -- and `Devices` labels its rows with
	// `MIDICable::getDisplayName()` (`Devices::drawPixelsForOled` in
	// `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`) -- a runtime string with no id at all -- while the two
	// selectors' option lists are static in main (`getOptions` in
	// `origin/main:src/deluge/gui/menu_item/cv/selection.h` and
	// `origin/main:src/deluge/gui/menu_item/gate/selection.h`) over the compile-time `NUM_PHYSICAL_CV_CHANNELS` /
	// `NUM_GATE_CHANNELS`. See `nodes/dynamic.h`'s own note.

	it("lists exactly the cables legacy lists, in legacy's order, with DIN first", _ {
		CableFixture cables;
		cables.set(/*din=*/1, /*up1=*/1, /*up2=*/1, {"ALPHA", "BETA"});

		const std::vector<deluge::midi::DeviceId> got = listed_devices();
		const size_t n = got.size();
		const std::vector<std::string>* want = table().find("devices/all", "list");
		expect(want).not_().to_be_null();

		expect(n).to_equal(size_t(5)); // DIN, upstream 1, upstream 2, ALPHA, BETA
		expect(want == nullptr ? size_t(0) : want->size()).to_equal(n);
		for (size_t i = 0; i < n && want != nullptr && i < want->size(); i++) {
			expect(device_name(got[i])).to_equal((*want)[i]);
		}
		// DIN really is first, and it is first because `lowestDeviceNum` is -3.
		expect(n).to_be_greater_than(0);
		if (n > 0) {
			expect(got[0].value).to_equal(deluge::midi::kDin.value);
			expect(device_name(got[0])).to_equal(std::string{MIDIDeviceManager::dinMIDIPorts.getDisplayName()});
		}
	});

	it("drops the disconnected ones, and yields DIN alone when nothing else is connected", _ {
		CableFixture cables;
		cables.set(/*din=*/1, /*up1=*/0, /*up2=*/0, {});

		std::vector<deluge::midi::DeviceId> got = listed_devices();
		size_t n = got.size();
		const std::vector<std::string>* din_only = table().find("devices/din_only", "list");
		expect(din_only).not_().to_be_null();
		if (din_only != nullptr) {
			expect(din_only->size()).to_equal(n);
		}
		expect(n).to_equal(size_t(1));
		if (n > 0) {
			expect(got[0].value).to_equal(deluge::midi::kDin.value);
		}

		// A disconnected *hosted* device is dropped too -- main's own "Disconnected devices aren't drawn, so they
		// don't take up a row". Adding one must not move the count.
		cables.set(/*din=*/1, /*up1=*/0, /*up2=*/0, {});
		auto ghost = std::make_unique<MIDICableUSBHosted>();
		ghost->name = "GHOST";
		ghost->connectionFlags = 0;
		MIDIDeviceManager::hostedMIDIDevices.push_back(ghost.get());
		n = listed_devices().size();
		const std::vector<std::string>* with_ghost = table().find("devices/ghost", "list");
		expect(with_ghost).not_().to_be_null();
		if (with_ghost != nullptr) {
			expect(with_ghost->size()).to_equal(n);
		}
		expect(n).to_equal(size_t(1));
		MIDIDeviceManager::hostedMIDIDevices.clear();
	});

	it("seeds the cursor on the cable the context already holds -- and on DIN when it holds none", _ {
		// `Devices::beginSession` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`): the previously-current
		// cable if it is still listed, otherwise `lowestDeviceNum` -- "Start on DIN. That's the only one that'll always
		// be there."
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);

		const std::vector<deluge::midi::DeviceId> listed = listed_devices();
		const size_t n = listed.size();
		expect(n).to_equal(size_t(5));

		for (size_t seed = 0; seed < n; seed++) {
			MenuContext ctx = settings.ctx;
			ctx.midi_device = listed[seed];
			Rig r;
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect(size_t(menu_scene().state().list[0].cursor)).to_equal(seed);
		}

		MenuContext none = settings.ctx;
		none.midi_device = {};
		Rig r;
		r.open(nodes::midi_devices, none);
		r.shell.tick(0);
		expect(int(menu_scene().state().list[0].cursor)).to_equal(0);
	});

	it("remembers the cable the user moved onto, through the context, on the next visit", _ {
		// The write-back half: `Devices::selectEncoderAction` assigns `soundEditor.currentMIDICable` on every move
		// (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), and the next `beginSession` reads it back to
		// reselect that device. The port routes the same fact through `MenuWalkState::midi_device_choice` ->
		// `MenuScene::after_render` -> `MenuContext::midi_device` -> `MidiDevicesItem::on_focus`, so the round trip is
		// what proves it: descend, move, leave, come back, and the cursor is where it was left.
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const TablesRoot& settings = roots().back();
		MenuContext ctx = settings.ctx;
		ctx.midi_device = {};
		install(ctx, settings.song);

		// Which row `midi_devices` is on: its position among the children `relevant` admits here,
		// rather than a hard-coded number that a hidden sibling would silently shift.
		int32_t row = 0;
		for (const Node* kid : nodes::midi_settings.children) {
			if (kid == &nodes::midi_devices) {
				break;
			}
			if (kid != nullptr && (menu::bindings::relevant(*kid, ctx))) {
				row++;
			}
		}

		Rig r;
		r.open(nodes::midi_settings, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		for (int32_t t = 0; t < row; t++) {
			r.turn(1, at);
			r.shell.tick(at + 5);
			at += 10;
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		r.shell.tick(at + 5);
		at += 10;
		expect(menu_scene().focused()).to_equal(&nodes::midi_devices);
		expect(int(menu_scene().state().list[1].cursor)).to_equal(0);

		// Two detents down the device list, then out and back in.
		at += 10;
		r.turn(2, at);
		r.shell.tick(at + 5);
		at += 10;
		expect(int(menu_scene().state().list[1].cursor)).to_equal(2);

		// This press does not ascend. `midi_devices` chains to the per-device page
		// (`Devices::selectButtonPress`, `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), so the way back
		// up to the MIDI page is two BACK presses rather than one select press -- which is a
		// *stronger* round trip for what this example is about, because `MenuScene::ascend` never
		// runs `on_focus` and only the re-descent below can put the cursor back.
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		r.shell.tick(at + 5);
		at += 10;
		expect(menu_scene().focused()).to_equal(&nodes::midi_device);
		at += 10;
		r.button(kBackX, kBackY, true, at);
		r.shell.tick(at + 5);
		at += 10;
		r.button(kBackX, kBackY, false, at);
		at += 10;
		expect(menu_scene().focused()).to_equal(&nodes::midi_devices);
		r.button(kBackX, kBackY, true, at);
		r.shell.tick(at + 5);
		at += 10;
		r.button(kBackX, kBackY, false, at);
		at += 10;
		expect(menu_scene().focused()).to_equal(&nodes::midi_settings);

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		r.shell.tick(at + 5);
		expect(menu_scene().focused()).to_equal(&nodes::midi_devices);
		// `on_focus` found the cable the move published, not DIN.
		expect(int(menu_scene().state().list[1].cursor)).to_equal(2);
	});

	it("lands on DIN when nothing at all is connected, rather than leaving a stale cable", _ {
		// `Devices::beginSession` assigns unconditionally (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`),
		// and `getCable(lowestDeviceNum)` is the DIN cable whatever its `connectionFlags` say --
		// the guard in `Devices::getCable` rejects out-of-range *indices*, not disconnection. So an empty
		// row list still resolves to DIN; it must not leave `MenuContext::midi_device` naming
		// the previous visit's cable.
		//
		// Read before the first tick on purpose: `MenuScene::open` -> `retarget` -> `enter_leaf`
		// runs `on_focus`, and `after_render` (which the first tick would run) drains the field.
		CableFixture cables;
		cables.set(/*din=*/0, /*up1=*/0, /*up2=*/0, {});
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);

		expect(listed_devices().size()).to_equal(size_t(0));

		MenuContext ctx = settings.ctx;
		ctx.midi_device = deluge::midi::kUpstreamUsb2; // the stale one
		Rig r;
		r.open(nodes::midi_devices, ctx);
		expect(menu_scene().state().walk.midi_device_choice.value).to_equal(deluge::midi::kDin.value);
	});

	it("does not move the source index just because the selector was opened", _ {
		// Main's `beginSession` moves the *item's* value and leaves `soundEditor.currentSourceIndex` alone; only
		// `selectButtonPress` writes it (`origin/main:src/deluge/gui/menu_item/cv/selection.h` and
		// `origin/main:src/deluge/gui/menu_item/gate/selection.h`). So `on_focus` publishes no `source_choice`, and the
		// sentinel is still -1 after the descent that ran it.
		//
		// Read before the first tick, for the same reason as the example above. (The other half of
		// the same fix -- that *scrolling* publishes nothing either -- has no observable effect
		// through the scene, because every page that reads `source_index` is reached only through
		// the press that republishes it; it is a code-shape fix, and this pins the half that shows.)
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		for (const Node* selector : {&nodes::cv_selection, &nodes::gate_selection}) {
			Rig r;
			r.open(*selector, settings.ctx);
			expect(int(menu_scene().state().walk.source_choice)).to_equal(-1);
		}
	});

	it("offers cv::Selection's own two options and chains onto the output the cursor names", _ {
		// The option rows are main's `getOptions()`; the *mapping* is proved by a sentinel per output, because "CV
		// OUTPUT 1" and "CV OUTPUT 2" are otherwise indistinguishable in a golden.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const std::vector<std::string>* options = table().find("cv_selection", "options");
		expect(options).not_().to_be_null();
		if (options != nullptr) {
			expect(options->size()).to_equal(size_t(NUM_PHYSICAL_CV_CHANNELS));
		}

		const uint8_t saved0 = cvEngine.cvChannels[0].voltsPerOctave;
		const uint8_t saved1 = cvEngine.cvChannels[1].voltsPerOctave;
		cvEngine.cvChannels[0].voltsPerOctave = 11;
		cvEngine.cvChannels[1].voltsPerOctave = 77;

		for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
			Rig r;
			r.open(nodes::cv_selection, settings.ctx);
			r.shell.tick(0);
			for (int32_t t = 0; t < which; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
			r.shell.tick(110);
			// `cv::Selection::selectButtonPress` returns the CV page unconditionally.
			expect(t3::chain_name(menu_scene().focused()))
			    .to_equal(table().value("cv_selection", "chain/" + std::to_string(which)));
			expect(menu_scene().focused()).to_equal(&nodes::cv_submenu);

			// ... and it wrote the output on the way. The proof is which output `cv_volts` now reads: `enter_leaf`
			// loads `bindings::read` into `MenuState::edit_int`.
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 130);
			r.shell.tick(140);
			expect(menu_scene().focused()).to_equal(&nodes::cv_volts);
			expect(menu_scene().state().edit_int).to_equal(which == 0 ? 11 : 77);
		}

		cvEngine.cvChannels[0].voltsPerOctave = saved0;
		cvEngine.cvChannels[1].voltsPerOctave = saved1;
	});

	it("offers gate::Selection's five options and chains where legacy chains, output by output", _ {
		// `gate::Selection::selectButtonPress`: the one-past-the-last option is the minimum-off-time page, every other is
		// that output's mode page. Driven through the real scene, and checked against main's answer on every value.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const std::vector<std::string>* options = table().find("gate_selection", "options");
		expect(options).not_().to_be_null();
		if (options != nullptr) {
			expect(options->size()).to_equal(size_t(NUM_GATE_CHANNELS + 1));
		}

		GateType saved[NUM_GATE_CHANNELS];
		for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
			saved[g] = cvEngine.gateChannels[g].mode;
			// One distinguishable value per output: V_TRIG on the even ones, S_TRIG on the odd, so "the mode page
			// opened on output N" is checkable rather than merely plausible.
			cvEngine.gateChannels[g].mode = (g % 2 == 0) ? GateType::V_TRIG : GateType::S_TRIG;
		}

		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			Rig r;
			r.open(nodes::gate_selection, settings.ctx);
			r.shell.tick(0);
			for (int32_t t = 0; t < which; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
			r.shell.tick(110);

			const std::string want = table().value("gate_selection", "chain/" + std::to_string(which));
			expect(want).to_satisfy([](const auto& w) { return w == "gate_off_time" || w == "gate_mode"; });
			expect(t3::chain_name(menu_scene().focused())).to_equal(want);

			if (which < NUM_GATE_CHANNELS) {
				// `enter_leaf` loaded the mode of the output the selector picked.
				expect(menu_scene().state().enum_index)
				    .to_equal(static_cast<int32_t>((which % 2 == 0) ? GateType::V_TRIG : GateType::S_TRIG));
			}
		}

		for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
			cvEngine.gateChannels[g].mode = saved[g];
		}
	});

	it("drains the selector's output index BEFORE the navigation arm, not after", _ {
		// The load-bearing case of `MenuState`'s drain-order contract (`menu_scene.h`, "The
		// one-shot mailboxes, and the order `after_render` must drain them in"):
		// `MenuScene::after_render` writes `ctx_.source_index` from `MenuWalkState::source_choice`
		// *above* the `render_resp.entered` arm, because `gate_selection_chain` picks its target
		// from `ctx_.source_index` and must see the output the user just picked -- legacy assigns
		// `soundEditor.currentSourceIndex` on the way through `selectButtonPress`
		// (`origin/main:src/deluge/gui/menu_item/gate/selection.h` `selectButtonPress`).
		//
		// The example above covers the same write from a context that already holds 0, so its
		// `which == 0` case passes either way. This one is built so that BOTH directions of the
		// mapping disagree with the incoming context: it opens the selector with a `source_index`
		// that names the *other* page, and moves the cursor to the one that names this page. Drain
		// the mailbox below the navigation arm and the chain reads the stale value, so both halves
		// land on the wrong page and both assertions fail.
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);

		// Incoming context says "minimum off time"; the cursor is left on output 0, whose page is
		// the mode page. Fresh value must win.
		{
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(NUM_GATE_CHANNELS);
			Rig r;
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
			r.shell.tick(110);
			expect(menu_scene().focused()).to_equal(&nodes::gate_mode);
			expect(int(menu_scene().state().walk.source_choice)).to_equal(-1); // drained, not merely read
		}

		// ... and the reverse: incoming context names output 0, the cursor is walked to the
		// one-past-the-last sentinel, and the off-time page must open.
		{
			MenuContext ctx = settings.ctx;
			ctx.source_index = 0;
			Rig r;
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			for (int32_t t = 0; t < NUM_GATE_CHANNELS; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 300);
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 300);
			r.shell.tick(310);
			expect(menu_scene().focused()).to_equal(&nodes::gate_off_time);
			expect(int(menu_scene().state().walk.source_choice)).to_equal(-1);
		}
	});

	it("offers CLOCK or RUN SIGNAL only on the outputs that carry them, once a selection has been "
	   "made through the scene",
	   _ {
		   // The port's counterpart of the options main rebuilds on the way through the selector is the mode page's
		   // per-output row, chosen by `MenuContext::source_index`, so this is the second, independent proof that the
		   // selector's cursor really reached the context -- a different mechanism from the `enum_index` check above, on
		   // the same write.
		   const TablesRoot& settings = roots().back();
		   install(settings.ctx, settings.song);

		   for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
			   Rig r;
			   r.open(nodes::gate_selection, settings.ctx);
			   r.shell.tick(0);
			   for (int32_t t = 0; t < which; t++) {
				   r.turn(1, static_cast<uint32_t>(10 + t * 10));
				   r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			   }
			   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			   r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
			   r.shell.tick(110);
			   expect(menu_scene().focused()).to_equal(&nodes::gate_mode);

			   const std::vector<std::string>* want = table().find("gate_mode/" + std::to_string(which), "options");
			   expect(want).not_().to_be_null();

			   MenuContext ctx = settings.ctx;
			   ctx.source_index = static_cast<int8_t>(which);
			   std::string_view names[menu::kMaxDynamicNames];
			   deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			   const size_t n = menu::bindings::names(nodes::gate_mode, ctx, names, scratch);
			   expect(want == nullptr ? size_t(0) : want->size()).to_equal(n);
			   for (size_t i = 0; i < n && want != nullptr && i < want->size(); i++) {
				   expect(std::string{names[i]}).to_equal((*want)[i]);
			   }
			   expect(n).to_equal(size_t(which == WHICH_GATE_OUTPUT_IS_CLOCK || which == WHICH_GATE_OUTPUT_IS_RUN ? 3
			                                                                                                     : 2));
		   }
	   });

	it("renders the device list and both output selectors, on both panel geometries", _ {
		// Both geometries, because a row constant that is right on one panel and wrong on the other
		// is exactly the class of mistake a single golden cannot see. Nothing here hand-computes a
		// rect: both pages lay out through `ui::widgets::oled::menu_rect(f)`, like every other menu screen.
		deluge::base::notify::dirty().drain();
		{
			deluge::base::notify::Notice stray;
			while (deluge::base::notify::notices().pop(stray)) {}
		}
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const TablesRoot& settings = roots().back();
		MenuContext ctx = settings.ctx;
		ctx.midi_device = {};
		install(ctx, settings.song);

		{
			Rig r;
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_midi_devices_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_midi_devices_64.pbm", r.fake.pbm(0));
		}
		{
			Rig r;
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_gate_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_gate_selection_64.pbm", r.fake.pbm(0));
		}
		{
			Rig r;
			r.open(nodes::cv_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_cv_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::cv_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_cv_selection_64.pbm", r.fake.pbm(0));
		}
	});

	it("is no placeholder, and reports nothing under a strict validation", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		expect(nodes::cv_selection.custom).to_equal(&nodes::kCvSelectionItem);
		expect(nodes::gate_selection.custom).to_equal(&nodes::kGateSelectionItem);
		expect(nodes::midi_devices.custom).to_equal(&nodes::kMidiDevicesItem);
		expect(nodes::kCvSelectionItem.is_placeholder()).to_be_false();
		expect(nodes::kGateSelectionItem.is_placeholder()).to_be_false();
		expect(nodes::kMidiDevicesItem.is_placeholder()).to_be_false();
		// No `dynamic_children` anywhere in the tree -- the reason `deluge_menu_dump`'s fail-closed arm still has no case
		// to be designed against.
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		size_t dynamic = 0;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->dynamic_children != nullptr) {
				dynamic++;
			}
		}
		expect(dynamic).to_equal(size_t(0));

		const Node* subtree[] = {&nodes::cv_selection, &nodes::gate_selection, &nodes::midi_devices};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues = menu::validate_tree(subtree, contexts, {}, {},
		                                        menu::ValidateOptions{.skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});

	it("lists the two upstream USB ports once a host configures the Deluge, and drops them when it goes", _ {
		// main's USB stack calls `configuredAsPeripheral` on SET_CONFIGURATION and `detachedAsPeripheral` on detach;
		// here the MIDI routine reports the BSP's configured state through `notePeripheralConnected`.
		CableFixture cables;
		cables.set(1, 0, 0, {});
		expect(listed_devices().size()).to_equal(size_t{1});

		MIDIDeviceManager::notePeripheralConnected(0, true);
		const std::vector<deluge::midi::DeviceId> rows = listed_devices();
		expect(rows.size()).to_equal(size_t{3});
		if (rows.size() == 3) {
			expect(rows[1].value).to_equal(deluge::midi::kUpstreamUsb1.value);
			expect(rows[2].value).to_equal(deluge::midi::kUpstreamUsb2.value);
		}
		expect(connectedUSBMIDIDevices[0][0].cable[0]).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1);

		MIDIDeviceManager::notePeripheralConnected(0, false);
		expect(listed_devices().size()).to_equal(size_t{1});
		expect(connectedUSBMIDIDevices[0][0].cable[0]).to_be_null();
	});

	it("acts on a change of the configured state only, as the USB stack's two callbacks did", _ {
		// A repeated `true` must not run `configuredAsPeripheral` again (it would re-send the capability messages);
		// a disconnect the cables already know of must not run `detachedAsPeripheral`.
		CableFixture cables;
		cables.set(1, 0, 0, {});
		MIDIDeviceManager::notePeripheralConnected(0, true);
		MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags = 0; // a marker: a second configure would set it again
		MIDIDeviceManager::notePeripheralConnected(0, true);
		expect(int(MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags)).to_equal(0);
		MIDIDeviceManager::notePeripheralConnected(0, false);
		MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = 1; // a marker: a second detach would clear it
		MIDIDeviceManager::notePeripheralConnected(0, false);
		expect(int(MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags)).to_equal(1);
	});

	it("opening the USB device opens the MIDI routine's USB reads, as openUSBPeripheral did", _ {
		const uint8_t saved = usbCurrentlyInitialized;
		usbCurrentlyInitialized = false;
		deluge::usb_audio::startDevice();
		expect(usbCurrentlyInitialized).not_().to_equal(0);
		usbCurrentlyInitialized = saved;
	});
});

describe tables_3_pixels("scenes::menu settings and other roots tables: pixels", $ {
	// Shared setup, so a pixel test added here later gets it for free rather than needing to
	// remember to copy it in.
	// `tables_3_writes` above drives every set hook across the whole tree, including params an
	// alpha/beta build's `bindings::write` cannot serve in the probe context it uses -- which raises
	// a real `Kind::Console` "MENU PARAM UNBOUND" notice (`bindings.cpp`'s `notice_unservable_param`).
	// That notice sat harmlessly undrained in the global queue until `ui/shell/notices.cpp`
	// composites a drained Console notice into whatever next calls `shell.tick()` -- one of the
	// tests below, in the same process. Drain both global
	// queues before each `it` here so a stray fact from an earlier describe block, or from the
	// *other* test in this one, never bleeds into a golden.
	before_each([] {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		deluge::base::notify::dirty().drain();
		deluge::base::notify::Notice stray;
		while (deluge::base::notify::notices().pop(stray)) {}
	});

	// The golden names are `menu_settings_tree_*`, not `menu_settings_root_*`: those belong to
	// `menu_scene_spec`, whose own `settings_root` is a hand-built three-item tree that exercises
	// the interpreter. This one is the real thirteen-item settings root.
	it("renders the settings root list", _ {
		const TablesRoot& settings = roots().back();
		Rig r;
		r.open(nodes::root_settings, settings.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/menu_settings_tree_48.pbm", r.fake.pbm(0));
	});

	// `midi::Command::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`) has exactly two
	// shapes, and both are recorded here: the one-line "COMMAND UNASSIGNED", and the three-line
	// device / channel / message-kind readout, stepping by the row pitch. The number columns are
	// legacy's `kTextSpacingX * 8` (channel) and `* 5` (note).
	//
	// The rows land on legacy's literal *canvas* rows 20 / 29 / 38 on both panels, which the port
	// reaches through two corrections rather than one literal (`nodes/midi_command.cpp`'s
	// `command_row0_y`, and `ui/widgets/core/metrics.h`): the frame origin already carries the panel's own
	// `visible_top`, which is 5 on the 48-row board and **0** on the 64-row one -- hence
	// `kMidiCommandRowY48` = 15 and `kMidiCommandRowY64` = 20, a pair, not a constant -- and
	// `kApple7Height7YAdjust` = -1 cancels the one-row drop `FontId::Apple7` (the height-9 Apple
	// face) has against legacy's own height-7 call. A single 48-row-derived
	// constant put the 64-row panel five rows high, and only a 64-row golden could see it, so the
	// learned readout is recorded on both.
	it("renders an unassigned MIDI command item", _ {
		const TablesRoot& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().focused()).to_equal(&nodes::midi_command_play);
		r.shell.tick(300);
		expect_golden(self, "ui/menu/menu_midi_command_unassigned_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item with a learned CC", _ {
		const TablesRoot& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		// Channel 3 + `IS_A_CC`, CC 42: the readout is the cable's display name, "CHANNEL 4"
		// (1-based, with the CC band subtracted back off) and "CC 42".
		const deluge::ui::scenes::menu::CustomItem* item = focused_item();
		expect(item).not_().to_be_null();
		if (item != nullptr) {
			expect(item->learn_cc(settings.ctx, spec_device(), 3, 42, 127)).to_be_true();
		}
		r.shell.tick(300);
		expect_golden(self, "ui/menu/menu_midi_command_learned_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item learned to an MPE zone", _ {
		// `Command::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`): an MPE zone prints
		// "MPE LOWER ZONE" on the channel row instead of "CHANNEL n", and -- being below `IS_A_CC` -- still prints
		// "Note" on the third. A golden, not an inequality: deleting the zone arm falls through to "CHANNEL 17", which
		// is different from every other frame in this file and so passes any inequality check.
		const TablesRoot& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		const deluge::ui::scenes::menu::CustomItem* item = focused_item();
		expect(item).not_().to_be_null();
		if (item != nullptr) {
			expect(item->learn_note_on(settings.ctx, spec_device(), MIDI_CHANNEL_MPE_LOWER_ZONE, 64)).to_be_true();
		}
		r.shell.tick(300);
		expect_golden(self, "ui/menu/menu_midi_command_mpe_zone_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item with a learned CC on a 64-row panel", _ {
		const TablesRoot& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r{board_64(), profile_64()};
		focus_command_row(r, settings.ctx, 0);
		const deluge::ui::scenes::menu::CustomItem* item = focused_item();
		expect(item).not_().to_be_null();
		if (item != nullptr) {
			expect(item->learn_cc(settings.ctx, spec_device(), 3, 42, 127)).to_be_true();
		}
		r.shell.tick(300);
		expect_golden(self, "ui/menu/menu_midi_command_learned_64.pbm", r.fake.pbm(0));
		saved.restore();
	});

	// `FollowChannel::drawInteger` draws MIDI-command-shaped rows on canvas rows 20 / 29 / 38: the input-differentiation
	// line, the device (or DEVICE UNASSIGNED), and CHANNEL n, an MPE zone or CHANNEL UNASSIGNED. sm26-follow-channel
	// holds the same screens against main.
	it("renders a follow channel's own screen: a channel, an MPE zone, unassigned, and a learned device", _ {
		const TablesRoot& settings = roots().back();
		install(settings.ctx, settings.song);
		const FollowSaved saved;
		struct Case {
			int32_t channel;
			bool cable;
			const char* golden;
		};
		const Case kCases[] = {
		    {0, false, "menu_follow_channel_1"},
		    {MIDI_CHANNEL_MPE_LOWER_ZONE, false, "menu_follow_channel_lower_zone"},
		    {MIDI_CHANNEL_NONE, false, "menu_follow_channel_unassigned"},
		    {3, true, "menu_follow_channel_learned"},
		};
		for (const Case& c : kCases) {
			midiEngine.midiFollowChannelType[0].clear();
			midiEngine.midiFollowChannelType[0].channelOrZone = c.channel;
			midiEngine.midiFollowChannelType[0].cable = c.cable ? &fake_cable() : nullptr;
			Rig r;
			r.open(nodes::midi_follow_channel_a, settings.ctx);
			r.shell.tick(0);
			expect_golden(self, std::string{"ui/menu/"} + c.golden + "_48.pbm", r.fake.pbm(0));
		}
		midiEngine.midiFollowChannelType[0].clear();
		midiEngine.midiFollowChannelType[0].channelOrZone = 0;
		Rig r{board_64(), profile_64()};
		r.open(nodes::midi_follow_channel_a, settings.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/menu_follow_channel_1_64.pbm", r.fake.pbm(0));
	});

	it("renders the settings root list on a 64-row panel", _ {
		const TablesRoot& settings = roots().back();
		Rig r{board_64(), profile_64()};
		r.open(nodes::root_settings, settings.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/menu_settings_tree_64.pbm", r.fake.pbm(0));
	});
});

// clang-format on

// clang-format off
describe tables_4_items("scenes::menu settings items: the pad colours, the cursor flash and the shortcuts version", $ {
	/// The group's eight items. Named once, here, and every example below inserts into `covered`
	/// so the closing example can assert the set by identity -- a node dropped from a root, or an
	/// example that quietly stopped driving one, fails rather than shrinking a count with slack in it.
	static const std::set<const Node*> kBatch4 = {
	    &nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
	    &nodes::pad_colour_soloed, &nodes::pad_colour_fill,    &nodes::pad_colour_once,
	    &nodes::flash_status,      &nodes::shortcuts_version,
	};
	static std::set<const Node*> covered;

	it("gives all six pad colours legacy's own nine options", _ {
		const TablesRoot& settings = installed_root("settings");
		for (const Node* n : kColourNodes) {
			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t count = menu::bindings::names(*n, settings.ctx, got, scratch);
			expect(count).to_equal(size_t(kNumPadColours));
			// The same nine for every item, so each path's row is the oracle.
			const std::vector<std::string>* want = table().find(spec::first_path_of(settings_entries(), *n), "options");
			expect(want).not_().to_be_null();
			if (want == nullptr) {
				continue;
			}
			expect(count).to_equal(want->size());
			for (size_t i = 0; i < count && i < want->size(); i++) {
				expect(std::string{got[i]}).to_equal((*want)[i]);
			}
			// The local enum's order is main's: each option row spells the colour its enumerator names.
			const String ids[] = {String::STRING_FOR_RED,   String::STRING_FOR_GREEN, String::STRING_FOR_BLUE,
			                      String::STRING_FOR_YELLOW, String::STRING_FOR_CYAN,  String::STRING_FOR_MAGENTA,
			                      String::STRING_FOR_AMBER, String::STRING_FOR_WHITE, String::STRING_FOR_PINK};
			for (size_t i = 0; i < std::size(ids) && i < want->size(); i++) {
				expect((*want)[i]).to_equal(std::string{deluge::l10n::get(ids[i])});
			}
			// `bindings::range` derives 0..8 from the option count (`bindings.cpp`, `Kind::Enum`), which is what
			// `Enumeration::selectEncoderAction` clamps against.
			const menu::IntRange r = menu::bindings::range(*n, settings.ctx);
			expect(r.min).to_equal(int32_t(0));
			expect(r.max).to_equal(int32_t(kNumPadColours - 1));
		}
	});

	it("maps each pad colour node onto its own legacy item, and onto no other", _ {
		// All six items look alike -- same nine options, same kind, adjacent template arguments -- so "each one writes
		// something" is worth nothing. This drives one node at a time from a common base and records *which* byte of
		// `FlashStorage::padColours` moved; the observed permutation must be the one main's items write.
		const TablesRoot& settings = installed_root("settings");
		uint8_t saved[FlashStorage::NUM_PAD_COLOUR_SLOTS];
		std::copy(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), saved);

		std::vector<int32_t> observed;
		std::vector<int32_t> frozen;
		for (size_t k = 0; k < std::size(kColourNodes); k++) {
			const int32_t storage = spec::int_of(table().find("colour/" + std::to_string(k), "storage")).value_or(-1);
			frozen.push_back(storage);
			// A common base that is not the value about to be written, so a hook that stores nothing is visible.
			std::fill(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), uint8_t{kRed});
			menu::bindings::write(*kColourNodes[k], settings.ctx, int32_t{kPink});
			int32_t moved = -1;
			int32_t moved_count = 0;
			for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
				if (FlashStorage::padColours[i] != kRed) {
					moved = static_cast<int32_t>(i);
					moved_count++;
				}
			}
			expect(moved_count).to_equal(int32_t(1)); // exactly one byte moved, never two, never none
			observed.push_back(moved);
			covered.insert(kColourNodes[k]);

			// And the reverse direction: the port reads back what main's own write left behind, on the same byte.
			std::fill(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), uint8_t{kRed});
			if (storage >= 0) {
				FlashStorage::padColours[storage] = kCyan;
			}
			expect(menu::bindings::read(*kColourNodes[k], settings.ctx)).to_equal(int32_t{kCyan});
		}

		expect(observed).to_equal(frozen);
		// The six bytes are six different ones, so a transposition of any two would be visible in the line above.
		expect(std::set<int32_t>(frozen.begin(), frozen.end()).size()).to_equal(size_t(6));

		std::copy(std::begin(saved), std::end(saved), FlashStorage::padColours);
	});

	it("edits PadLEDs::flashCursor, in legacy's own option order", _ {
		const TablesRoot& settings = installed_root("settings");
		const uint8_t saved = PadLEDs::flashCursor;

		// FAST / DISABLED / SLOW is `FLASH_CURSOR_FAST` 0, `_OFF` 1, `_SLOW` 2 (`hid/led/pad_leds.h`) -- a value order,
		// not a reading order, so getting it wrong would silently mean "off" where the user asked for "slow". The
		// option names are the generic options row; each value is driven from a sentinel that is not itself.
		const std::vector<std::string>* options =
		    table().find(spec::first_path_of(settings_entries(), nodes::flash_status), "options");
		expect(options).not_().to_be_null();
		if (options != nullptr) {
			expect(options->size()).to_equal(size_t(3));
		}
		for (int32_t v = 0; v <= 2; v++) {
			PadLEDs::flashCursor = static_cast<uint8_t>(v == 0 ? 2 : 0);
			menu::bindings::write(nodes::flash_status, settings.ctx, v);
			expect(int32_t(PadLEDs::flashCursor)).to_equal(v);

			PadLEDs::flashCursor = static_cast<uint8_t>(v);
			expect(menu::bindings::read(nodes::flash_status, settings.ctx)).to_equal(v);
		}
		covered.insert(&nodes::flash_status);
		PadLEDs::flashCursor = saved;
	});

	it("round-trips the shortcuts version through the port's own copy", _ {
		const TablesRoot& settings = installed_root("settings");
		const spec::ShortcutLayout layout{deluge::project::ShortcutsVersion::V3};

		// Main's two options are the generic options row of the shortcuts-version path, checked by the options example;
		// the port's own copy is what this node reads, so it is excluded from the value rows and round-tripped here.
		std::string_view got[menu::kMaxDynamicNames];
		deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
		const size_t count = menu::bindings::names(nodes::shortcuts_version, settings.ctx, got, scratch);
		expect(count).to_equal(size_t(2));

		// The round trip, each direction driven from the other value. The layer it selects is checked cell for cell in
		// `shortcuts_spec.cpp`, which is where the tables live.
		spec::set_shortcut_layout(deluge::project::ShortcutsVersion::V3);
		menu::bindings::write(nodes::shortcuts_version, settings.ctx, 0);
		expect(menu::bindings::read(nodes::shortcuts_version, settings.ctx)).to_equal(int32_t(0));
		expect(spec::shortcut_layout()).to_equal(deluge::project::ShortcutsVersion::V1);

		spec::set_shortcut_layout(deluge::project::ShortcutsVersion::V1);
		menu::bindings::write(nodes::shortcuts_version, settings.ctx, 1);
		expect(menu::bindings::read(nodes::shortcuts_version, settings.ctx)).to_equal(int32_t(1));
		expect(spec::shortcut_layout()).to_equal(deluge::project::ShortcutsVersion::V3);

		covered.insert(&nodes::shortcuts_version);
	});

	it("round-trips all six pad colours through writeSettings and readSettings", _ {
		// The only exercise of `FlashStorage::readSettings` / `writeSettings`: without it a
		// byte-166 bug (`flash_storage.cpp`, the ONCE colour read back into FILL) could return in
		// silence. It is exactly the kind of bug a
		// value comparison cannot see: both the port and legacy read `padColours` correctly, and the
		// loader between flash and that array was the broken part.
		//
		// The host BSP backs the settings region with an in-memory NOR model (`peripherals/flash.rs`), so this is a
		// real program/erase/read cycle rather than a stub -- and the buffer is process-global,
		// which is why the guard below is here as well as the save/restore of `padColours`. See
		// `support/host_flash.h`.
		spec::ErasedFlash flash;
		uint8_t saved[FlashStorage::NUM_PAD_COLOUR_SLOTS];
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			saved[i] = FlashStorage::padColours[i];
		}

		// Six DISTINCT colours, none of them a slot's own default and none repeated -- so a loader
		// that crosses two slots, drops one, or leaves one at its default is visible. Avoids kRed in
		// both FILL and ONCE, which would trip the deliberate both-red guard
		// (`flash_storage.cpp`, immediately below the byte-166 branch) and mask the result.
		const uint8_t written[FlashStorage::NUM_PAD_COLOUR_SLOTS] = {
		    kCyan,  kPink,  kWhite,
		    kAmber, kGreen, kBlue,
		};
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = written[i];
		}

		FlashStorage::writeSettings();
		// Scribble over every slot with a value none of them was written as, so a `readSettings`
		// that simply fails to touch a slot cannot pass by leaving the pre-write value in place.
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = kMagenta;
		}
		FlashStorage::readSettings();

		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			expect(FlashStorage::padColours[i]).to_equal(written[i]);
		}
		// And name the two the bug was actually about, so a failure reads as itself rather than as
		// "index 5 differs": byte 165 is FILL and byte 166 is ONCE, and each must come back its own.
		expect(FlashStorage::padColours[FlashStorage::PAD_COLOUR_FILL]).to_equal(uint8_t(kGreen));
		expect(FlashStorage::padColours[FlashStorage::PAD_COLOUR_ONCE]).to_equal(uint8_t(kBlue));

		// The port reads the restored bytes, which is the point of the round trip: a colour the user
		// set survives a power cycle and the menu shows it again.
		const TablesRoot& settings = installed_root("settings");
		expect(menu::bindings::read(nodes::pad_colour_fill, settings.ctx))
		    .to_equal(static_cast<int32_t>(kGreen));
		expect(menu::bindings::read(nodes::pad_colour_once, settings.ctx))
		    .to_equal(static_cast<int32_t>(kBlue));

		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = saved[i];
		}
	});

	it("has no placeholder left among the nine, and drove every one of them", _ {
		// By identity, not by count: this is the whole point of `covered`.
		expect(covered).to_equal(kBatch4);
		for (const Node* n : kBatch4) {
			expect(n->kind).to_equal(menu::Kind::Enum);
			expect(n->custom).to_be_null();
			// A DEVICE row's node binds through its target, which the device port reads and writes; the rest through
			// their hooks.
			const bool bound = n->bind.target.on_device() || (n->bind.get != nullptr && n->bind.set != nullptr);
			expect(bound).to_be_true();
		}
	});
});
// clang-format on

// clang-format off
describe tables_3_reference_rows("tables_3: the reference rows", $ {
	it("parses tables_3.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("tables_3");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{9000});
		// main's rows for a child the port drops (spec::kMainOnlyChildren) answer for a node no longer here: each is held,
		// and set aside.
		for (const std::string& path : spec::main_only_paths("settings", nodes::root_settings)) {
			expect(path + " rows " + std::to_string(t.under(path).size()))
			    .not_()
			    .to_equal(path + " rows 0");
		}
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(tables_3_spec, tables_3_shape, tables_3_values, tables_3_validation, tables_3_writes, tables_3_chaining,
            tables_3_reset_items, tables_3_midi_commands, tables_3_midi_follow_learn, tables_3_dynamic, tables_3_pixels,
            tables_4_items, tables_3_reference_rows)
