#pragma once

/// @file
/// @brief The fixtures and port-side predicates of the third menu tables: the ten roots, each with the context it is
///        opened over, and the facts the spec and the menu oracle freezer both need to decide what to ask.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/present.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/profile.h"

#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/menu_context.h"
#include "support/menu_engine.h"
#include "support/oracle_tree.h"
#include "support/song_fixture.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::ui::scenes::spec::tables3 {

namespace nodes = deluge::ui::scenes::menu::nodes;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;

/// @brief Make the song, clip and UI mode those @p ctx is read over. A write leaves the model where it wrote, so every
///        comparison installs the fixture it reads.
inline void install(const MenuContext& ctx, Song* song) {
	currentSong = song;
	song->setCurrentClip(menu_engine(ctx, *song).clip);
	currentUIMode = ctx.affect_entire_held ? UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR : 0;
}

/// @brief One root: its node, and the fixture a `MenuContext` was resolved from.
struct TablesRoot {
	const char* name;
	const Node* node;
	MenuContext ctx;
	Song* song;

	/// @brief The root UI the menu would have been opened over, for the one pair of items whose relevance reads it
	///        (`song_midi_learn`, `configure_song_macros`). Null on every other root.
	UI* root_ui = nullptr;
};

/// @brief Every root the third tables declare, plus the sound root the second tables did (so the `Kind` count and the
///        whole-tree validation see the complete tree).
///
/// The fixtures are built once and never torn down (`song_fixture.h`'s ownership note). Each is the
/// clip shape a menu open would have been opened over, so `context_for` picks the arm the
/// root belongs to -- which the `resolves each root from its own fixture's context` example asserts
/// directly rather than assuming.
inline std::vector<TablesRoot>& roots() {
	static std::vector<TablesRoot> list = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		// `deluge_boot()` calls this (`deluge/deluge.cpp`); nothing else in this spec's world does, and the
		// community-features page is exactly the part of the settings root that reads it: it gives every feature its
		// factory value.
		runtimeFeatureSettings.init();
		std::vector<TablesRoot> r;

		{ // sound (the second tables' root, re-checked here as part of the whole tree)
			static spec::SongFixture synth = spec::make_synth_song();
			r.push_back({"sound", &nodes::sound_root,
			             spec::ctx_of(*synth.song, deluge::gui::menuViewOf(synth.clip), 0, false, false, false, false),
			             synth.song});
		}
		{ // kit row with a SoundDrum
			static spec::SongFixture kit = spec::make_kit_song();
			static SoundDrum* drum = spec::add_drum<SoundDrum>(kit);
			static_cast<Kit*>(kit.instrument)->selectedDrum = drum;
			r.push_back({"drum", &nodes::root_drum,
			             spec::ctx_of(*kit.song, deluge::gui::menuViewOf(kit.clip), 0, false, false, false, false),
			             kit.song});
		}
		{ // MIDI clip
			static spec::SongFixture midi = spec::make_midi_song();
			r.push_back({"midi/cv", &nodes::root_midi_or_cv,
			             spec::ctx_of(*midi.song, deluge::gui::menuViewOf(midi.clip), 0, false, false, false, false),
			             midi.song});
		}
		{ // kit row with a MIDIDrum, and one with a GateDrum
			static spec::SongFixture midi_kit = spec::make_kit_song();
			static MIDIDrum* midi_drum = spec::add_drum<MIDIDrum>(midi_kit);
			static_cast<Kit*>(midi_kit.instrument)->selectedDrum = midi_drum;
			r.push_back(
			    {"midi drum", &nodes::root_midi_drum,
			     spec::ctx_of(*midi_kit.song, deluge::gui::menuViewOf(midi_kit.clip), 0, false, false, false, false),
			     midi_kit.song});

			static spec::SongFixture gate_kit = spec::make_kit_song();
			static GateDrum* gate_drum = spec::add_drum<GateDrum>(gate_kit);
			static_cast<Kit*>(gate_kit.instrument)->selectedDrum = gate_drum;
			r.push_back(
			    {"gate drum", &nodes::root_gate_drum,
			     spec::ctx_of(*gate_kit.song, deluge::gui::menuViewOf(gate_kit.clip), 0, false, false, false, false),
			     gate_kit.song});
		}
		{ // audio clip
			static spec::AudioSongFixture audio = spec::make_audio_clip_song();
			r.push_back({"audio clip", &nodes::root_audio_clip,
			             spec::ctx_of(*audio.song, deluge::gui::menuViewOf(audio.clip), 0, false, false, false, false),
			             audio.song});
		}
		{ // performance view and the session/arranger root: no clip
			static spec::SongFixture song_view = spec::make_synth_song();
			r.push_back({"performance", &nodes::root_performance,
			             spec::ctx_of(*song_view.song, deluge::gui::menuViewOf(nullptr), 0,
			                          /*performance_view=*/true,
			                          /*session_or_arranger_root=*/true, /*session_view_root=*/false, false),
			             song_view.song, &performanceView});
			// The song root as the SESSION view -- one of the three root UIs `SoundEditor::setup` funnels
			// into it, and the only one where `song_midi_learn` / `configure_song_macros` are shown.
			// The arranger and song-automation readings of this same root are the dedicated example
			// below, which drives all three from one table.
			r.push_back({"song", &nodes::root_song,
			             spec::ctx_of(*song_view.song, deluge::gui::menuViewOf(nullptr), 0, false,
			                          /*session_or_arranger_root=*/true,
			                          /*session_view_root=*/true, false),
			             song_view.song, &sessionView});
		}
		{ // a kit on affect-entire
			static spec::SongFixture kit_fx = spec::make_kit_song();
			(void)spec::add_drum<SoundDrum>(kit_fx);
			kit_fx.clip->affectEntire = true;
			r.push_back({"kit global fx", &nodes::root_kit_global_fx,
			             spec::ctx_of(*kit_fx.song, deluge::gui::menuViewOf(kit_fx.clip), 0, false, false, false,
			                          /*kit_global_fx=*/true),
			             kit_fx.song});
		}
		{ // settings: no clip, not a session root
			static spec::SongFixture settings = spec::make_synth_song();
			r.push_back({"settings", &nodes::root_settings,
			             spec::ctx_of(*settings.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false),
			             settings.song});
		}
		return r;
	}();
	return list;
}

/// @brief The CV fixture, used only by the two examples about the MIDI/CV root's second title and
///        the CV2 source item -- it is not a root of its own (`Root::MidiOrCv` covers both).
struct CvRig {
	spec::SongFixture song;
	MenuContext ctx{};
};
inline CvRig& cv_fixture() {
	static CvRig c = [] {
		CvRig k;
		k.song = spec::make_cv_song();
		k.ctx = spec::ctx_of(*k.song.song, deluge::gui::menuViewOf(k.song.clip), 0, false, false, false, false);
		return k;
	}();
	return c;
}

inline std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief @p id as @p node shows it: its placeholder filled (`menu::node_text`).
inline std::string shown(uint16_t id, const Node& node, const MenuContext& ctx) {
	menu::NodeText scratch;
	return std::string{menu::node_text(id, node, ctx, scratch)};
}

/// @brief Nodes whose name or title is built at render time rather than being the l10n id the node was given, so one
///        static `Node::name` cannot match it.
inline bool has_dynamic_name(const Node& node) {
	static const std::set<const Node*> kDynamic = {
	    &nodes::lpf_morph,    &nodes::hpf_morph,           &nodes::source0,          &nodes::source1,
	    &nodes::mod_fx_depth, &nodes::mod_fx_feedback,     &nodes::mod_fx_offset,    &nodes::reverb_room_size,
	    &nodes::reverb_width, &nodes::global_mod_fx_depth, &nodes::global_lpf_morph, &nodes::global_hpf_morph,
	};
	return kDynamic.contains(&node);
}

/// @brief The two nodes whose relevance is a root-UI fact rather than a model one: `song_midi_learn` and
///        `configure_song_macros` show in the session view only. By identity, so a third node acquiring
///        `relevance::is_session_view` is a visible edit.
inline bool relevance_reads_root_ui(const Node& node) {
	return &node == &nodes::configure_song_macros || &node == &nodes::song_midi_learn;
}

/// @brief A sample recorder on a root with no sound (a MIDI or gate drum row): the item the menu this replaces would
///        have asked a null sound; the port answers hidden.
inline bool recorder_on_soundless_row(const TreeEntry& e, const TablesRoot& root) {
	return menu_engine(root.ctx, *root.song).sound == nullptr
	       && (e.node == &nodes::sample0_recorder || e.node == &nodes::sample1_recorder);
}

/// @brief Nodes whose write is not driven through a reference item, because each reaches a `gui/` or `hid/` global that
///        only a booted app has. Every one has its port hook driven on its own in the writes group.
inline constexpr const Node* kWritesNeedingBootedUi[] = {
    &nodes::synth_mode,
    &nodes::arp_mode,
    &nodes::arp_preset,
    &nodes::mod_fx_type,
    &nodes::audio_clip_reverse,
    &nodes::midi_clock_out,
    &nodes::midi_clock_in,
    &nodes::trigger_out_ppqn,
    &nodes::default_pad_brightness,
    &nodes::default_menu_highlighting,
    &nodes::screensaver_mode,
    &nodes::screensaver_timeout,
    &nodes::feature_sticky_shift,
    &nodes::feature_rounded_corners,
};

inline bool write_needs_booted_ui(const Node& node) {
	return std::ranges::find(kWritesNeedingBootedUi, &node) != std::end(kWritesNeedingBootedUi);
}

/// @brief The one node whose value does not live in shared storage: the shortcuts version is the port's own copy.
inline bool value_is_port_owned(const Node& node) {
	return &node == &nodes::shortcuts_version;
}

/// @brief The nodes whose value is a pair, so the one-integer comparisons have nothing to compare.
inline bool value_is_two_ended(const Node& node) {
	return node.kind == menu::Kind::Range;
}

/// @brief Is @p node on screen in @p root's own context? A menu is on the current song, so the fixture is installed.
inline bool relevant_here(const TablesRoot& root, const Node& node) {
	install(root.ctx, root.song);
	return menu::bindings::relevant(node, root.ctx);
}

/// @brief A node pointer as a name the failure message can print: the three chain-only settings pages by identity.
inline std::string chain_name(const Node* n) {
	if (n == nullptr) {
		return "<ascend>";
	}
	if (n == &nodes::cv_submenu) {
		return "cv_submenu";
	}
	if (n == &nodes::gate_mode) {
		return "gate_mode";
	}
	if (n == &nodes::gate_off_time) {
		return "gate_off_time";
	}
	return "UNMAPPED:" + text_of(n->name);
}

/// @brief A concrete `MIDICable` to learn against: only its address and display name are ever read.
struct FakeCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "SPEC CABLE"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

inline FakeCable& fake_cable() {
	static FakeCable c;
	return c;
}

/// @brief Every `midiEngine.globalMIDICommands` entry, saved so a learn example can put them back.
struct CommandSlots {
	LearnedMIDI slots[kNumGlobalMIDICommands];

	static CommandSlots capture() {
		CommandSlots c;
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			c.slots[i] = midiEngine.globalMIDICommands[i];
		}
		return c;
	}
	void restore() const {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i] = slots[i];
		}
	}
	static void clear_all() {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i].clear();
		}
	}
	/// @brief The whole array as one comparable string: `index=cable/channelOrZone/noteOrCC`, the empty slots elided.
	std::string describe() const {
		std::string out;
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			const LearnedMIDI& s = slots[i];
			if (s.channelOrZone == MIDI_CHANNEL_NONE) {
				continue;
			}
			out += std::to_string(i) + "=" + (s.cable != nullptr ? s.cable->getDisplayName() : "none") + "/"
			       + std::to_string(s.channelOrZone) + "/" + std::to_string(s.noteOrCC) + " ";
		}
		return out.empty() ? std::string{"(all clear)"} : out;
	}
};

/// @brief The MIDI follow channels and kit root note, saved for one example and put back after it.
struct FollowSaved {
	::LearnedMIDI channels[kNumMIDIFollowChannelTypesIncludingTracks];
	uint8_t root = midiEngine.midiFollowKitRootNote;
	FollowSaved() { std::ranges::copy(midiEngine.midiFollowChannelType, channels); }
	~FollowSaved() {
		std::ranges::copy(channels, midiEngine.midiFollowChannelType);
		midiEngine.midiFollowKitRootNote = root;
	}
	FollowSaved(const FollowSaved&) = delete;
	FollowSaved& operator=(const FollowSaved&) = delete;
};

/// @brief Follow channel @p which as text: the cable's name and the channel or zone.
inline std::string follow_state(size_t which) {
	const ::LearnedMIDI& m = midiEngine.midiFollowChannelType[which];
	return std::string{m.cable != nullptr ? m.cable->getDisplayName() : "-"} + "/" + std::to_string(m.channelOrZone);
}

/// @brief Connect a chosen set of cables for the scope, and put the manager back afterwards.
struct CableFixture {
	std::vector<MIDICableUSBHosted*> saved_hosted;
	uint8_t saved_din, saved_up1, saved_up2;
	std::vector<std::unique_ptr<MIDICableUSBHosted>> owned;

	CableFixture() {
		saved_hosted.assign(MIDIDeviceManager::hostedMIDIDevices.begin(), MIDIDeviceManager::hostedMIDIDevices.end());
		saved_din = MIDIDeviceManager::dinMIDIPorts.connectionFlags;
		saved_up1 = MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags;
		saved_up2 = MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags;
	}
	~CableFixture() {
		MIDIDeviceManager::hostedMIDIDevices.clear();
		for (MIDICableUSBHosted* c : saved_hosted) {
			MIDIDeviceManager::hostedMIDIDevices.push_back(c);
		}
		MIDIDeviceManager::dinMIDIPorts.connectionFlags = saved_din;
		MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags = saved_up1;
		MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = saved_up2;
	}
	CableFixture(const CableFixture&) = delete;
	CableFixture& operator=(const CableFixture&) = delete;

	/// @brief `flags` on the three fixed cables, and one hosted cable per name in @p hosted.
	void set(uint8_t din, uint8_t up1, uint8_t up2, std::initializer_list<const char*> hosted) {
		MIDIDeviceManager::dinMIDIPorts.connectionFlags = din;
		MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags = up1;
		MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = up2;
		MIDIDeviceManager::hostedMIDIDevices.clear();
		for (const char* n : hosted) {
			auto cable = std::make_unique<MIDICableUSBHosted>();
			cable->name = n;
			cable->connectionFlags = 1;
			MIDIDeviceManager::hostedMIDIDevices.push_back(cable.get());
			owned.push_back(std::move(cable));
		}
	}
};

/// @brief The host's connected MIDI devices (`bind::MidiDevices::connected`), copied out of the port's answer.
inline std::vector<deluge::midi::DeviceId> listed_devices() {
	const std::span<const deluge::midi::DeviceId> got = deluge::gui::shell_host::host_ports().midi->connected();
	return {got.begin(), got.end()};
}

/// @brief @p device's name, as the host's MIDI devices port answers it.
inline std::string device_name(deluge::midi::DeviceId device) {
	return std::string{deluge::gui::shell_host::host_ports().midi->name(device)};
}

} // namespace deluge::ui::scenes::spec::tables3
