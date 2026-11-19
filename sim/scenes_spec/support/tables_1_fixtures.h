#pragma once

/// @file
/// @brief The fixtures and port-side predicates of the first menu tables: a synth clip, the seventeen subtree roots
///        keyed as the reference rows key them, and the synth whose first source carries a chosen number of note
///        ranges. The spec and the menu oracle freezer share them.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"

#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/menu_engine.h"
#include "support/oracle_tree.h"
#include "support/song_fixture.h"
#include "support/tables_2_fixtures.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace deluge::scenes::spec::tables1 {

namespace nodes = deluge::scenes::menu::nodes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

/// The song and clip install every root's comparison starts from, and the text helpers around it, are
/// the sound root's.
using tables2::install;
using tables2::shown;
using tables2::text_of;

/// @brief Nodes whose name is built at render time, so no static name id can stand for it.
///
/// Two classes build theirs: a filter's MORPH item takes the filter mode's own morph name, and a source takes
/// "OSC1"/"OSC2" from its index. Keyed on the name id these nodes carry; a `FormattedTitle` name is compared as shown,
/// its `title_arg` filled in.
inline bool has_dynamic_name(const Node& node) {
	switch (node.name) {
	case static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_LPF_MORPH):
	case static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_HPF_MORPH):
	case static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_OSCILLATOR_1):
	case static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_OSCILLATOR_2):
		return true;
	default:
		return false;
	}
}

/// @brief The reference rows' label for the root at @p index of `nodes::kBatch1Roots`.
inline std::string root_label(size_t index) {
	return "b1/" + std::to_string(index);
}

/// @brief Every node under the seventeen roots, pre-order, root by root, as the reference rows key them.
inline const std::vector<TreeEntry>& entries() {
	static const std::vector<TreeEntry> list = [] {
		std::vector<TreeEntry> all;
		for (size_t i = 0; i < nodes::kBatch1Roots.size(); i++) {
			for (TreeEntry& e : tree_entries(root_label(i), *nodes::kBatch1Roots[i])) {
				all.push_back(std::move(e));
			}
		}
		return all;
	}();
	return list;
}

/// @brief The synth clip every shape and value example runs against.
struct Fixture {
	spec::SongFixture song = spec::make_synth_song();
	MenuContext ctx{};

	Fixture() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                        /*kit_global_fx=*/false);
	}
};

/// @brief The shared fixture, installed: an example that left another song current gets this one back.
inline Fixture& fixture() {
	static Fixture f;
	install(f.ctx, f.song.song);
	return f;
}

/// @brief A synth fixture whose source 0 carries @p n note ranges, installed.
///
/// Its own fixture rather than the shared one: inserting ranges changes what every other example reads off source 0
/// (`transpose_range`, `sample_start`, ...), and the shared one is a function-local static every group uses.
struct RangeFixture {
	spec::SongFixture song = spec::make_synth_song();
	MenuContext ctx{};

	explicit RangeFixture(int32_t n) {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		for (int32_t i = 0; i < n; i++) {
			(void)sound->sources[0].ranges.insertMultiRange(i);
		}
		if (n > 0) {
			deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them
		}
		ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                        /*kit_global_fx=*/false);
		install(ctx, song.song);
	}
	~RangeFixture() {
		// Put the shared fixture back: every other group reads the same fixture.
		(void)fixture();
	}
	RangeFixture(const RangeFixture&) = delete;
	RangeFixture& operator=(const RangeFixture&) = delete;

	/// @brief The four-range layout the picker examples share, or another: source 0's ranges end at @p tops.
	void set_tops(std::span<const int16_t> tops) {
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		for (size_t i = 0; i < tops.size(); i++) {
			sound->sources[0].ranges.getElement(static_cast<int32_t>(i))->topNote = tops[i];
		}
		deluge::edits::recordRanges(*sound, 0);
	}
};

/// @brief Ranges described by their top notes, as the picker rows record them: each one's span, the first from
///        `BOTTOM` and each later one from the note after the range below.
inline std::vector<std::string> spans_of(std::span<const int32_t> tops) {
	std::vector<std::string> out;
	for (size_t i = 0; i < tops.size(); i++) {
		out.push_back((i == 0 ? std::string{"BOTTOM"} : std::to_string(tops[i - 1] + 1)) + "-"
		              + std::to_string(tops[i]));
	}
	return out;
}

} // namespace deluge::scenes::spec::tables1
