/// @file
/// @brief The sixth context-menu descriptor -- the audio-source selector -- against the real legacy
///        `context_menu::audioInputSelector`.
///
/// This is the descriptor that made `Descriptor` grow three hooks (`initial_option`, `on_move`,
/// `footer`), so the spec is shaped around proving each of them against the legacy override it
/// ports, and around proving that the five reset descriptors did **not** change: they are checked
/// hook-by-hook in `context_menu_scene_spec.cpp`'s descriptor group (all three null on all five) and
/// end-to-end by that spec's `cm_actions` group, which drives four of the five factory resets
/// against a witness. Both were re-run against this table; neither needed an edit beyond the count.
///
/// Every comparison below drives **both** sides from the same sentinel state:
///
///   1. write `audioOutput->inputChannel` (and, where it matters, the recording-from output),
///   2. run legacy (`setupAndCheckAvailability` / `selectEncoderAction` / `renderOLED`) and record,
///   3. write the same sentinel again,
///   4. drive the port through a real `Shell` and the real input path,
///
/// then compare. That matters more here than anywhere else in this task, because legacy and the port
/// address the *same* `AudioOutput` -- a comparison that forgot step 3 would be reading legacy's own
/// answer back.

#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/context_menu_items.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/context_menu/audio_input_selector.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "processing/audio_output.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "spec/support/spec_main.h"

/// `model/song/song.cpp:4896`. Both sides write it (`audio_input_selector.cpp:193`), so an example
/// that drives either half has to put it back.
extern AudioInputChannel defaultAudioOutputInputChannel;

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::context_menu::Descriptor;
using deluge::scenes::context_menu::DescriptorId;
using deluge::scenes::context_menu::Selection;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace cm = deluge::scenes::context_menu;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

constexpr const Node* kRoots[] = {&nodes::root_audio_clip};

/// `AudioInputSelector::Value`'s order (`gui/context_menu/audio_input_selector.cpp:30-40`),
/// transcribed here so the spec names rows by meaning rather than by number.
enum : uint8_t { kOff = 0, kLeft, kRight, kStereo, kBalanced, kMaster, kOutput, kTrack, kOptionCount };

/// @brief A 128x64 single-panel board (`menu_scene_spec.cpp:293-314`).
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
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief The audio-clip fixture plus a real `Shell` over it.
struct Rig {
	spec::AudioSongFixture song = spec::make_audio_clip_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext menu_context() {
		return menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false);
	}

	/// @brief Open the context menu scene straight onto the audio-source descriptor, the way
	///        `nodes::kAudioSourceItem`'s `on_focus` does.
	void open_selector() {
		(void)menu::assign_node_ids(kRoots);
		cm::context_menu_scene().open(menu_context());
		shell.init(SceneId{cm::kContextMenuSceneKind, static_cast<uint16_t>(DescriptorId::AudioSource)});
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	void press(uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 255}, at_ms);
	}
};

/// @brief Everything either side writes when the cursor moves -- the witness the comparisons use.
struct SourceWitness {
	AudioInputChannel channel{};
	Output* recording_from = nullptr;
	AudioInputChannel song_default{};
	uint8_t option = 0;
	uint8_t scroll = 0;

	friend bool operator==(const SourceWitness&, const SourceWitness&) = default;
};

/// @brief Saves and restores everything an example here can move.
struct SourceGuard {
	AudioOutput* out;
	AudioInputChannel saved_channel;
	Output* saved_from;
	AudioInputChannel saved_default = defaultAudioOutputInputChannel;

	explicit SourceGuard(AudioOutput* o)
	    : out{o}, saved_channel{o->inputChannel}, saved_from{o->getOutputRecordingFrom()} {}
	SourceGuard(const SourceGuard&) = delete;
	SourceGuard& operator=(const SourceGuard&) = delete;
	~SourceGuard() {
		out->inputChannel = saved_channel;
		out->setOutputRecordingFrom(saved_from);
		defaultAudioOutputInputChannel = saved_default;
	}
};

/// @brief Put @p out into a known state, the sentinel every comparison starts from.
void seed(AudioOutput* out, AudioInputChannel channel, Output* from) {
	out->inputChannel = channel;
	out->setOutputRecordingFrom(from);
	defaultAudioOutputInputChannel = AudioInputChannel::UNSET;
}

std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief Drop the five bezel rows the port's root frame cannot ink, from both sides
///        (`context_menu_scene_spec.cpp`'s own `below_bezel`).
std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	int newlines = 0;
	while (pos < pbm.size() && newlines < 2) {
		if (pbm[pos] == '\n') {
			newlines++;
		}
		pos++;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	out += pbm.substr(pos);
	return out;
}

/// @brief The eight channels `initial_option` maps, plus the two it folds into OFF.
const std::array<AudioInputChannel, 9> kAllChannels{
    AudioInputChannel::NONE,     AudioInputChannel::LEFT, AudioInputChannel::RIGHT,  AudioInputChannel::STEREO,
    AudioInputChannel::BALANCED, AudioInputChannel::MIX,  AudioInputChannel::OUTPUT, AudioInputChannel::SPECIFIC_OUTPUT,
    AudioInputChannel::UNSET,
};

} // namespace

// clang-format off
describe audio_source_table("scenes::context_menu the audio-source descriptor", $ {
	it("offers exactly the options the real legacy AudioInputSelector offers, in its order", _ {
		select_language();
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		SourceGuard guard{f.output};

		// Legacy is driven: `getOptions()` is a real call on the shipped singleton.
		deluge::gui::context_menu::audioInputSelector.audioOutput = f.output;
		const std::span<const char*> legacy =
		    deluge::gui::context_menu::audioInputSelector.getOptions();
		std::vector<std::string> legacy_names;
		for (const char* v : legacy) {
			legacy_names.emplace_back(v);
		}

		const Descriptor& d = cm::descriptor(DescriptorId::AudioSource);
		std::vector<std::string> port_names;
		for (const uint16_t id : d.options) {
			deluge::ui::shell::TextScratch scratch;
			port_names.emplace_back(deluge_profile().text_or_digits(id, scratch));
		}
		expect(port_names).to_equal(legacy_names);
		expect(static_cast<int>(d.options.size())).to_equal(kOptionCount);
	});

	it("carries the legacy heading, and all three of the new hooks", _ {
		select_language();
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		deluge::gui::context_menu::audioInputSelector.audioOutput = f.output;
		const Descriptor& d = cm::descriptor(DescriptorId::AudioSource);

		deluge::ui::shell::TextScratch scratch;
		expect(std::string(deluge_profile().text_or_digits(d.title, scratch)))
		    .to_equal(std::string(deluge::gui::context_menu::audioInputSelector.getTitle()));

		// The three the task-6 note said this descriptor would need. Asserted present here and
		// asserted *absent* on all five resets in `context_menu_scene_spec.cpp`, so "the extension
		// changed nothing for the existing table" is a fact rather than a claim.
		expect(d.initial_option != nullptr).to_be_true();
		expect(d.on_move != nullptr).to_be_true();
		expect(d.footer != nullptr).to_be_true();
	});

	it("is what the audio_source_selector menu item opens", _ {
		const auto* item = dynamic_cast<const nodes::ContextMenuItem*>(nodes::audio_source_selector.custom);
		expect(item != nullptr).to_be_true();
		expect(static_cast<int>(item->descriptor_id())).to_equal(static_cast<int>(DescriptorId::AudioSource));
		expect(nodes::audio_source_selector.custom->is_placeholder()).to_be_false();
		// ... and the five reset items still open theirs, unchanged by the class rename.
		const std::array<std::pair<const Node*, DescriptorId>, 5> resets{{
		    {&nodes::reset_flash, DescriptorId::ResetFlash},
		    {&nodes::reset_community_features, DescriptorId::ResetCommunityFeatures},
		    {&nodes::reset_midi_follow, DescriptorId::ResetMidiFollow},
		    {&nodes::reset_midi_devices, DescriptorId::ResetMidiDevices},
		    {&nodes::reset_all, DescriptorId::ResetAll},
		}};
		std::vector<int> observed;
		for (const auto& [node, id] : resets) {
			const auto* reset_item = dynamic_cast<const nodes::ContextMenuItem*>(node->custom);
			expect(reset_item != nullptr).to_be_true();
			observed.push_back(reset_item != nullptr ? static_cast<int>(reset_item->descriptor_id()) : -1);
		}
		const std::vector<int> expected{0, 1, 2, 3, 4};
		expect(observed).to_equal(expected);
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[4]);
		expect(swapped == expected).to_be_false();
	});
});

describe audio_source_seed("scenes::context_menu the audio-source descriptor: opening", $ {
	it("opens on the option legacy's setupAndCheckAvailability opens on, for every channel", _ {
		select_language();
		for (const AudioInputChannel channel : kAllChannels) {
			Rig r;
			SourceGuard guard{r.song.output};

			// --- legacy, driven from the sentinel
			seed(r.song.output, channel, nullptr);
			deluge::gui::context_menu::audioInputSelector.audioOutput = r.song.output;
			(void)deluge::gui::context_menu::audioInputSelector.setupAndCheckAvailability();
			const int32_t legacy_option = deluge::gui::context_menu::audioInputSelector.currentOption;
			const int32_t legacy_scroll = deluge::gui::context_menu::audioInputSelector.scrollPos;

			// --- port, same sentinel, through a real scene entry
			seed(r.song.output, channel, nullptr);
			r.open_selector();
			r.shell.tick(5);

			expect(static_cast<int>(cm::context_menu_scene().selection().option)).to_equal(legacy_option);
			expect(static_cast<int>(cm::context_menu_scene().selection().scroll)).to_equal(legacy_scroll);
		}
	});

	it("actually distinguishes the channels, so the loop above is not vacuous", _ {
		// If every channel mapped to the same option the loop would pass with the mapping deleted.
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		SourceGuard guard{f.output};
		const Descriptor& d = cm::descriptor(DescriptorId::AudioSource);
		std::vector<int> seen;
		for (const AudioInputChannel channel : kAllChannels) {
			seed(f.output, channel, nullptr);
			const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false);
			seen.push_back(static_cast<int>(d.initial_option(ctx)));
		}
		// NONE and UNSET both fold into OFF (legacy's `default:` arm), so eight distinct answers over
		// nine channels.
		const std::vector<int> expected{kOff, kLeft, kRight, kStereo, kBalanced, kMaster, kOutput, kTrack, kOff};
		expect(seen).to_equal(expected);
	});
});

describe audio_source_move("scenes::context_menu the audio-source descriptor: committing on the turn", $ {
	it("writes what legacy's selectEncoderAction writes, over every start and every turn", _ {
		select_language();
		struct Case {
			AudioInputChannel start;
			int32_t offset;
		};
		const Case cases[] = {
		    {AudioInputChannel::NONE, 1},            // OFF -> LEFT
		    {AudioInputChannel::LEFT, 1},            // LEFT -> RIGHT
		    {AudioInputChannel::LEFT, -1},           // LEFT -> OFF
		    {AudioInputChannel::NONE, -1},           // off the top: refused, and still re-commits
		    {AudioInputChannel::OUTPUT, 1},          // OUTPUT -> TRACK, which picks a source
		    {AudioInputChannel::SPECIFIC_OUTPUT, -1},// TRACK -> OUTPUT, which clears the source
		    {AudioInputChannel::SPECIFIC_OUTPUT, 1}, // off the bottom: refused
		    {AudioInputChannel::MIX, 2},             // two detents at once
		};
		for (const Case& c : cases) {
			Rig r;
			SourceGuard guard{r.song.output};

			// --- legacy, driven from the sentinel
			seed(r.song.output, c.start, nullptr);
			deluge::gui::context_menu::audioInputSelector.audioOutput = r.song.output;
			(void)deluge::gui::context_menu::audioInputSelector.setupAndCheckAvailability();
			deluge::gui::context_menu::audioInputSelector.selectEncoderAction(static_cast<int8_t>(c.offset));
			const SourceWitness want{
			    .channel = r.song.output->inputChannel,
			    .recording_from = r.song.output->getOutputRecordingFrom(),
			    .song_default = defaultAudioOutputInputChannel,
			    .option = static_cast<uint8_t>(deluge::gui::context_menu::audioInputSelector.currentOption),
			    .scroll = static_cast<uint8_t>(deluge::gui::context_menu::audioInputSelector.scrollPos),
			};

			// --- port, same sentinel, through the real select encoder
			seed(r.song.output, c.start, nullptr);
			r.open_selector();
			r.shell.tick(5);
			r.turn(c.offset, 10);
			r.shell.tick(15);
			const SourceWitness got{
			    .channel = r.song.output->inputChannel,
			    .recording_from = r.song.output->getOutputRecordingFrom(),
			    .song_default = defaultAudioOutputInputChannel,
			    .option = cm::context_menu_scene().selection().option,
			    .scroll = cm::context_menu_scene().selection().scroll,
			};

			expect(got == want).to_be_true();
			// The song-wide default is written on every turn, refused ones included -- that is
			// legacy's shape (`audio_input_selector.cpp:193` is outside every guard), and asserting it
			// is what stops a "tidy" guard being added later.
			expect(got.song_default == AudioInputChannel::UNSET).to_be_false();
		}
	});

	it("keeps TRACK on the first visible row, which is what makes room for the footer", _ {
		select_language();
		Rig r;
		SourceGuard guard{r.song.output};
		seed(r.song.output, AudioInputChannel::OUTPUT, nullptr);
		r.open_selector();
		r.shell.tick(5);
		expect(static_cast<int>(cm::context_menu_scene().selection().option)).to_equal(kOutput);
		r.turn(1, 10);
		r.shell.tick(15);
		// `audio_input_selector.cpp:142-145`: `scrollPos = currentOption` on TRACK, so TRACK is the
		// top of the two-row window rather than the bottom the base rule would leave it on.
		expect(static_cast<int>(cm::context_menu_scene().selection().option)).to_equal(kTrack);
		expect(static_cast<int>(cm::context_menu_scene().selection().scroll)).to_equal(kTrack);
	});

	it("closes on a select press without changing anything: the value is already committed", _ {
		select_language();
		Rig r;
		SourceGuard guard{r.song.output};
		seed(r.song.output, AudioInputChannel::NONE, nullptr);
		r.open_selector();
		r.shell.tick(5);
		r.turn(1, 10);
		r.shell.tick(15);
		expect(r.song.output->inputChannel == AudioInputChannel::LEFT).to_be_true();
		r.press(20);
		r.shell.tick(25);
		// `AudioInputSelector` has no `acceptCurrentOption()` override, so the base's `return false`
		// closes the menu; the channel is whatever the last turn set.
		expect(r.song.output->inputChannel == AudioInputChannel::LEFT).to_be_true();
	});
});

describe audio_source_pixels("scenes::context_menu the audio-source descriptor: pixels", $ {
	it("draws exactly what the real legacy AudioInputSelector::renderOLED draws, footer included", _ {
		select_language();
		for (const AudioInputChannel channel :
		     {AudioInputChannel::NONE, AudioInputChannel::STEREO, AudioInputChannel::OUTPUT,
		      AudioInputChannel::SPECIFIC_OUTPUT}) {
			Rig r;
			SourceGuard guard{r.song.output};

			// --- legacy
			seed(r.song.output, channel, nullptr);
			deluge::gui::context_menu::audioInputSelector.audioOutput = r.song.output;
			(void)deluge::gui::context_menu::audioInputSelector.setupAndCheckAvailability();
			deluge::hid::display::OLED::main.clear();
			deluge::gui::context_menu::audioInputSelector.renderOLED(deluge::hid::display::OLED::main);
			const std::string legacy = below_bezel(legacy_panel_pbm());

			// --- port, same sentinel
			seed(r.song.output, channel, nullptr);
			r.open_selector();
			r.shell.tick(5);
			const std::string port = below_bezel(r.fake.pbm(0));

			expect(port).to_equal(legacy);
		}
	});

	it("shows the footer only on SPECIFIC_OUTPUT, and says No track when there is none", _ {
		select_language();
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		SourceGuard guard{f.output};
		const Descriptor& d = cm::descriptor(DescriptorId::AudioSource);
		std::array<char, cm::kFooterScratch> scratch{};

		seed(f.output, AudioInputChannel::LEFT, nullptr);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false);
		expect(d.footer(ctx, std::span{scratch}).empty()).to_be_true();

		seed(f.output, AudioInputChannel::SPECIFIC_OUTPUT, nullptr);
		// The fixture's only output is the `AudioOutput` itself, which `canRecordFrom` refuses
		// (`processing/audio_output.h:109-111`), so there is nothing to record from.
		expect(std::string(d.footer(ctx, std::span{scratch}))).to_equal(std::string("No track"));
	});

	it("records the goldens at both panel geometries", _ {
		select_language();
		{
			Rig r;
			SourceGuard guard{r.song.output};
			seed(r.song.output, AudioInputChannel::STEREO, nullptr);
			r.open_selector();
			r.shell.tick(5);
			expect_golden(self, "../ui/scenes/context_menu_audio_source_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			SourceGuard guard{r64.song.output};
			seed(r64.song.output, AudioInputChannel::SPECIFIC_OUTPUT, nullptr);
			r64.open_selector();
			r64.shell.tick(5);
			expect_golden(self, "../ui/scenes/context_menu_audio_source_64.pbm", r64.fake.pbm(0));
		}
	});

	it("is byte-identical after twenty quiet ticks, and commits nothing", _ {
		select_language();
		Rig r;
		SourceGuard guard{r.song.output};
		seed(r.song.output, AudioInputChannel::STEREO, nullptr);
		r.open_selector();
		r.shell.tick(5);
		const std::string first = r.fake.pbm(0);
		for (uint32_t t = 1; t <= 20; t++) {
			r.shell.tick(5 + t * 15);
			expect(r.fake.pbm(0)).to_equal(first);
		}
		expect(r.song.output->inputChannel == AudioInputChannel::STEREO).to_be_true();
		// `on_move` runs on a turn, not on a tick, so the song-wide default is still untouched.
		expect(defaultAudioOutputInputChannel == AudioInputChannel::UNSET).to_be_true();
		// Every shipped label fits its box, so the side scroller never arms.
		expect(cm::context_menu_scene().next_timer().has_value()).to_be_false();
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` is spelled out with `add_specs`.
int audio_source_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(audio_source_table, audio_source_seed, audio_source_move, audio_source_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
