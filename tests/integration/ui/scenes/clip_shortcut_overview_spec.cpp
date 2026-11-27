/// @file
/// @brief The clip view's SHIFT shortcut overview against main's, for every kind of clip and kit row, and a kit's own
///        effects with the kit-global latch set and clear: every main-grid pad's colour and occupancy, as
///        `tests/golden/ui/menu_oracle/clip_shortcut_overview.tsv` holds them.

#include "util/menu_entry.h"

#include "support/clip_overview_cases.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"

#include "cppspec.hpp"

#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace clip_overview = deluge::ui::scenes::spec::clip_overview;

namespace {

spec::OracleTable& table() {
	return spec::OracleTable::named("clip_shortcut_overview");
}

/// @brief Every main-grid cell where the overview of @p c differs from main's, as
///        "(x,y) main <r,g,b>/<occupancy> ours <r,g,b>/<occupancy>" lines; and how many cells main lit, through @p lit.
std::string differences(clip_overview::Case c, int32_t& lit) {
	clip_overview::RootGuard root;
	(void)clip_overview::set_up(c);
	RGB got[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	uint8_t gotOccupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	deluge::gui::ui::renderClipShortcutsOverview(got, gotOccupancy);
	const std::string path = "overview/" + std::string{clip_overview::name_of(c)};
	std::string bad;
	lit = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		const std::vector<std::string>* colours = table().find(path, "rgb/" + std::to_string(y));
		const std::vector<std::string>* occupied = table().find(path, "occupancy/" + std::to_string(y));
		constexpr size_t kCells = static_cast<size_t>(kDisplayWidth);
		if (colours == nullptr || occupied == nullptr || colours->size() != kCells || occupied->size() != kCells) {
			bad += path + " row " + std::to_string(y) + ": no frozen row of sixteen cells\n";
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			const std::string& want = (*colours)[static_cast<size_t>(x)];
			const std::string& wantOccupancy = (*occupied)[static_cast<size_t>(x)];
			const std::string ours = clip_overview::rgb_text(got[y][x]);
			const std::string oursOccupancy = std::to_string(gotOccupancy[y][x]);
			lit += want == "0,0,0" ? 0 : 1;
			// A shortcut to a node the port shows where main hid it (spec::kShownWhereMainHid) lights where main's was
			// dark.
			const bool named = want == "0,0,0" && ours != "0,0,0"
			                   && spec::pad_shown_where_main_hid(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
			if (!named && (want != ours || wantOccupancy != oursOccupancy)) {
				bad += "(" + std::to_string(x) + "," + std::to_string(y) + ") main " + want + "/" + wantOccupancy
				       + " ours " + ours + "/" + oursOccupancy + "\n";
			}
		}
	}
	return bad;
}

} // namespace

// clang-format off
describe clip_shortcut_overview("the clip view's SHIFT shortcut overview paints what main's paints", $ {
	it("for a synth clip", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::Synth, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on, latched by a press: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntire, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on, the latch clear: the kit's own table, asked of its selected row", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireUnlatched, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on and no row selected, the latch clear: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireNoRowUnlatched, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on and a gate drum selected, the latch clear: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireGateDrumUnlatched, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on and a gate drum selected, latched by a press: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireGateDrum, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on and a MIDI drum selected, the latch clear: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireMidiDrumUnlatched, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with affect-entire on and a MIDI drum selected, latched by a press: the kit's own table", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitAffectEntireMidiDrum, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("lights the arp rate pad on a kit's own table right after a menu close and darkens it once a press latches", _ {
		// Main's `arpeggiator::Rate` asks `editingKitAffectEntire()`, which reads the latch, not the clip.
		const auto arp_rate_pad = [](clip_overview::Case c) {
			clip_overview::RootGuard root;
			(void)clip_overview::set_up(c);
			RGB got[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			uint8_t gotOccupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			deluge::gui::ui::renderClipShortcutsOverview(got, gotOccupancy);
			return clip_overview::rgb_text(got[0][11]);
		};
		expect(arp_rate_pad(clip_overview::Case::KitAffectEntireUnlatched)).not_().to_equal(std::string{"0,0,0"});
		expect(arp_rate_pad(clip_overview::Case::KitAffectEntire)).to_equal(std::string{"0,0,0"});
	});

	it("for a kit row with a sound drum under a latch a press set, not since cleared: the row's table, asked as on the kit's own effects", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitSoundDrumLatched, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit row with a sound drum", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitSoundDrum, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit row with a MIDI drum", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitMidiDrum, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit row with a gate drum", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitGateDrum, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a kit with neither affect-entire nor a selected drum: nothing", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::KitNothing, lit)).to_equal(std::string{});
		expect(lit).to_equal(0);
	});

	it("for a MIDI clip", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::Midi, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for a CV clip", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::Cv, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});

	it("for an audio clip", _ {
		int32_t lit = 0;
		expect(differences(clip_overview::Case::Audio, lit)).to_equal(std::string{});
		expect(lit).to_be_greater_than(0);
	});
});

describe clip_shortcut_overview_reference_rows("the clip view's shortcut overview: the reference rows", $ {
	it("parses clip_shortcut_overview.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		// Sixteen rows per case: eight of colours, eight of occupancy.
		expect(t.size()).to_equal(std::size(clip_overview::kCases) * size_t{16});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(clip_shortcut_overview_spec, clip_shortcut_overview, clip_shortcut_overview_reference_rows)
