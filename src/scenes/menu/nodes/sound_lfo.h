#pragma once

/// @brief The four LFO subtrees as data: `lfo1` .. `lfo4`, each {shape, sync, rate}.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/lfo/{lfo,__init__}.py` and `generate/g_menus.inc:203-238`
/// (at `ce4a79de0`):
///
// clang-format off
/// | node | legacy global | legacy class / param |
/// |---|---|---|
/// | `lfo<n>_type` | `lfo<n>TypeMenu` | `lfo::Type` (`gui/menu_item/lfo/type.h:27`) |
/// | `lfo<n>_sync` | `lfo<n>SyncMenu` | `lfo::Sync` (`gui/menu_item/lfo/sync.h:26`) |
/// | `lfo1_rate` | `lfo1RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_1` |
/// | `lfo2_rate` | `lfo2RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_1` |
/// | `lfo3_rate` | `lfo3RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_2` |
/// | `lfo4_rate` | `lfo4RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_2` |
/// | `lfo<n>` | `lfo<n>Menu` | `HorizontalMenu` |
// clang-format on
///
/// The odd/even alternation of global and local rate params is the Python spec's own
/// (`dmenus/lfo/lfo.py:29-33`: `isGlobal = i % 2 == 0`, `index = 1 if i < 2 else 2`), and it matches
/// `definitions_cxx.hpp:390-393`'s "LFO 1 (global), LFO 2 (local), LFO 3 (global), LFO 4 (local)".
///
/// @note **`Kind::SyncLevel` comes from `node.h`'s `sync_level_item()`.** Task 6 built these two by
///       hand and said the next task needing one should promote the builder; Task 7 needs four more
///       (`arpSyncMenu`, `delaySyncMenu`, `sidechainSyncMenu`, `reverbSidechainSyncMenu`) and did.
///       The value is legacy's own menu option index, `SyncLevel::syncTypeAndLevelToMenuOption`
///       (`gui/menu_item/sync_level.cpp:79-81`), over `SyncLevel::size() == NUM_SYNC_VALUES`
///       (`sync_level.h:31`, `model/sync.h:37`). The option *names* are `getNoteLengthName`, which
///       formats from the song's `insideWorldTickMagnitude` at render time (`sync_level.cpp:83-91`)
///       -- dynamic text this generation's interpreter does not draw yet, which is why the node
///       carries no `names`.
///
/// @note **`lfo::Type`'s title is a `FormattedTitle`** substituting the LFO number into
///       `STRING_FOR_LFO_TYPE` (`lfo/type.h:30,33`), as `lfo::Sync`'s is for `STRING_FOR_LFO_SYNC`
///       (`lfo/sync.h:29,32`). A `Node::title` is one static id, so all four carry the
///       unsubstituted one -- the same gap the envelope submenus have.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains

#include "model/sync.h" // NUM_SYNC_VALUES

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// `lfo::Type::getOptions` (`gui/menu_item/lfo/type.h:35-46`).
inline constexpr uint16_t kLfoShapeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_SINE),
    sid(deluge::l10n::String::STRING_FOR_TRIANGLE),
    sid(deluge::l10n::String::STRING_FOR_SQUARE),
    sid(deluge::l10n::String::STRING_FOR_SAW),
    sid(deluge::l10n::String::STRING_FOR_SAMPLE_AND_HOLD),
    sid(deluge::l10n::String::STRING_FOR_RANDOM_WALK),
    sid(deluge::l10n::String::STRING_FOR_WARBLE),
};

/// @brief The full sync-option range, `SyncLevel::size() == NUM_SYNC_VALUES` (`sync_level.h:31`,
///        `model/sync.h:37`) -- what every `SyncLevel` item but `sidechain::Sync` uses.
inline constexpr IntRange kSyncLevelRange{0, NUM_SYNC_VALUES - 1, 1};

/// @brief One LFO page. A macro for the same reason `sound_env.h` uses one -- and because the
///        `get`/`set` hooks are plain function pointers, so each LFO needs its own pair. The bodies
///        are in the `.cpp` (`DELUGE_LFO_HOOKS`); this declares them and builds the nodes.
#define DELUGE_LFO(n, rate_param, rate_title)                                                                          \
	int32_t get_lfo##n##_shape(const MenuContext& ctx);                                                                \
	void set_lfo##n##_shape(const MenuContext& ctx, int32_t v);                                                        \
	int32_t get_lfo##n##_sync(const MenuContext& ctx);                                                                 \
	void set_lfo##n##_sync(const MenuContext& ctx, int32_t v);                                                         \
	inline constexpr Node lfo##n##_type =                                                                              \
	    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_SHAPE), kLfoShapeNames, get_lfo##n##_shape,        \
	                           set_lfo##n##_shape),                                                                    \
	               sid(deluge::l10n::String::STRING_FOR_LFO_TYPE));                                                    \
	inline constexpr Node lfo##n##_sync =                                                                              \
	    sync_level_item(sid(deluge::l10n::String::STRING_FOR_SYNC), sid(deluge::l10n::String::STRING_FOR_LFO_SYNC),    \
	                    get_lfo##n##_sync, set_lfo##n##_sync, kSyncLevelRange);                                        \
	inline constexpr Node lfo##n##_rate =                                                                              \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_RATE),                                             \
	                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::rate_param},    \
	                           Hint::Knob, nullptr, sid(deluge::l10n::String::rate_title)));                           \
	inline constexpr const Node* kLfo##n##Children[] = {&lfo##n##_type, &lfo##n##_sync, &lfo##n##_rate};               \
	inline constexpr Node lfo##n =                                                                                     \
	    submenu(sid(deluge::l10n::String::STRING_FOR_LFO##n), kLfo##n##Children, Layout::Horizontal)

DELUGE_LFO(1, GLOBAL_LFO_FREQ_1, STRING_FOR_LFO1_RATE);
DELUGE_LFO(2, LOCAL_LFO_LOCAL_FREQ_1, STRING_FOR_LFO2_RATE);
DELUGE_LFO(3, GLOBAL_LFO_FREQ_2, STRING_FOR_LFO3_RATE);
DELUGE_LFO(4, LOCAL_LFO_LOCAL_FREQ_2, STRING_FOR_LFO4_RATE);

#undef DELUGE_LFO

/// @brief This file's four contributions to `kBatch1Roots`: `lfo1` .. `lfo4`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundLfoChildren;

} // namespace deluge::scenes::menu::nodes
