#pragma once

/// @file
/// @brief What a scene reads of a sound that its record does not hold: what the card has loaded into it, its
///        engine's typed state, its patch cables, its ranges' identities, and the DX7 bank last read.

#include "instruments/classic/cables.h"
#include "instruments/classic/sound_state.h"
#include "project/clip_address.h"
#include "project/instrument_slot.h"
#include "project/param_address.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::scenes::bind {

/// @brief One of a sound's patch cables, as the engine holds it.
struct CableFact {
	plugin::ParamId depth;                     ///< the cable's depth id, which names its source and its destination
	instruments::classic::Polarity polarity{}; ///< its polarity
};

/// @brief The most patch cables a sound holds.
inline constexpr size_t kMaxCables = 32;

/// @brief A sound's patch cables, as the engine holds them, in its order.
struct CableList {
	std::array<CableFact, kMaxCables> items{}; ///< the cables; only the first @ref count hold one
	size_t count{0};                           ///< how many cables the sound holds
	/// @brief The first cable.
	[[nodiscard]] const CableFact* begin() const { return items.data(); }
	/// @brief One past the last cable.
	[[nodiscard]] const CableFact* end() const { return items.data() + count; }
	/// @brief How many cables the sound holds.
	[[nodiscard]] size_t size() const { return count; }
	/// @brief Whether the sound holds no cable.
	[[nodiscard]] bool empty() const { return count == 0; }
	/// @brief The cable at @p i, below @ref count.
	[[nodiscard]] const CableFact& operator[](size_t i) const { return items[i]; }
};

/// @brief The DX7 bank a `.syx` read loaded: its programs' names, as `DX7Cartridge::getProgramNames` gives them.
struct DxCartridgeNames {
	std::array<std::array<char, 11>, 32> names{}; ///< each program's name, NUL-terminated
	int32_t count = 0;                            ///< how many programs the bank holds
};

/// @brief What a recording is taken for, each part by the identity that names that object alone: the take attaches
/// there, or nowhere.
struct TakeTarget {
	std::optional<project::ClipAddress> clip; ///< the clip the take was begun over
	project::SlotAddress slot{};              ///< the sound: a synth track's output, or a kit row's drum
	uint8_t source{0};                        ///< which of the sound's two sources
	std::optional<uint64_t> range;            ///< the range's identity (`SoundFacts::range_identity`)
	bool operator==(const TakeTarget&) const = default;
};

/// @brief A sound's facts, answered by the host.
class SoundFacts {
public:
	virtual ~SoundFacts() = default;
	/// @brief Storage: which sources of the sound at @p slot have an audio file loaded (bit i for source i). A file
	/// loads after the song that names it, so whether it has is a fact of the card, not of the song.
	[[nodiscard]] virtual uint8_t loaded_sources(project::SlotAddress slot) const = 0;
	/// @brief The instrument's typed state for the sound at @p slot, or nullptr when there is no such sound. It is what
	/// the slot decodes to, kept equal to it by every Edit, plus what no file can hold (an FM sound's oscillator types,
	/// a non-FM sound's modulators): a read of a row a menu does not show, which a condition may still read, needs it.
	[[nodiscard]] virtual const instruments::classic::SoundState* sound_state(project::SlotAddress slot) const = 0;
	/// @brief Whether the sound at @p slot is a kit's only drum, on its clip's first note row: the kit a whole folder
	/// of samples may be loaded into, one drum per file (`SampleBrowser::canImportWholeKit` in
	/// `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`, which also asks that the menu edits source 0 -- the
	/// caller's to check).
	[[nodiscard]] virtual bool kit_takes_whole_folder(project::SlotAddress slot) const = 0;
	/// @brief The range source @p source of the sound at @p slot remembers: `Source::defaultRangeI`, which the
	/// note-range picker opens on and every note played moves (`Source::getRange`). None when it names no range.
	[[nodiscard]] virtual std::optional<uint16_t> default_range(project::SlotAddress slot, uint8_t source) const = 0;
	/// @brief The identity of range @p range of source @p source of the sound at @p slot (`MultiRange::identity`). It
	/// names the range itself: it moves with the range as ranges are inserted or deleted before it, and stays while
	/// another file is loaded into it. Never 0: identities count from 1, so 0 names no range. None where the address
	/// names no range.
	[[nodiscard]] virtual std::optional<uint64_t> range_identity(project::SlotAddress slot, uint8_t source,
	                                                             uint16_t range) const = 0;
	/// @brief Where the take @p target names may still land: the index its range now has among its source's ranges,
	/// while the clip is still in the current song, still playing the sound. Range identities are run-unique and never
	/// saved, so a range in another song never matches. None otherwise.
	[[nodiscard]] virtual std::optional<uint16_t> take_range(const TakeTarget& target) const = 0;
	/// @brief The DX7 bank the last `.syx` read loaded, which the cartridge page lists; none before one has loaded.
	[[nodiscard]] virtual std::optional<DxCartridgeNames> dx_cartridge() const = 0;
	/// @brief The patch cables of the sound @p owner names (a synth or a sound drum, with its clip; the id is not
	/// read), in the order the engine holds them; empty where there is no such sound. The cables are its engine's: one
	/// made this session is one before the song is saved.
	[[nodiscard]] virtual CableList cables(const project::ParamAddress& owner) const = 0;
};

} // namespace deluge::scenes::bind
