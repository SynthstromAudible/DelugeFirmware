#pragma once

/// @brief The four LFO subtrees as data: `lfo1` .. `lfo4`, each {shape, sync, rate}.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `origin/main:src/deluge/gui/ui/menus.cpp`'s LFO block, from `lfo1TypeMenu`:
///
// clang-format off
/// | node | legacy global | legacy class / param |
/// |---|---|---|
/// | `lfo<n>_type` | `lfo<n>TypeMenu` | `lfo::Type` (`origin/main:src/deluge/gui/menu_item/lfo/type.h`) |
/// | `lfo<n>_sync` | `lfo<n>SyncMenu` | `lfo::Sync` (`origin/main:src/deluge/gui/menu_item/lfo/sync.h`) |
/// | `lfo1_rate` | `lfo1RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_1` |
/// | `lfo2_rate` | `lfo2RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_1` |
/// | `lfo3_rate` | `lfo3RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_2` |
/// | `lfo4_rate` | `lfo4RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_2` |
/// | `lfo<n>` | `lfo<n>Menu` | `HorizontalMenu` |
// clang-format on
///
/// The odd/even alternation of global and local rate params is main's own (LFO 1 and 3 take the global rate params, LFO
/// 2 and 4 the local ones), and it matches the `LFO_ID` enumerators' comments (`definitions_cxx.hpp`): "LFO 1 (global),
/// LFO 2 (local), LFO 3 (global), LFO 4 (local)".
///
/// @note **`Kind::SyncLevel` comes from `node.h`'s `sync_level_item()`.** The builder is shared by every `SyncLevel`
///       item: the two LFO syncs, `arpSyncMenu`, `delaySyncMenu`, `sidechainSyncMenu` and `reverbSidechainSyncMenu`.
///       The value is legacy's own menu option index, `SyncLevel::syncTypeAndLevelToMenuOption`
///       (`origin/main:src/deluge/gui/menu_item/sync_level.cpp`), over `SyncLevel::size() == NUM_SYNC_VALUES`
///       (`origin/main:src/deluge/gui/menu_item/sync_level.h`, `origin/main:src/deluge/model/sync.h`), here
///       `plugin::kNumCombinedSyncValues`. The option
///       *names* are `getNoteLengthName`, which formats from the song's `insideWorldTickMagnitude` at render time
///       (`origin/main:src/deluge/gui/menu_item/sync_level.cpp`) -- dynamic text this generation's interpreter does not
///       draw yet, which is why the node carries no `names`.
///
/// @note **`lfo::Type`'s title is a `FormattedTitle`** substituting the LFO number into `STRING_FOR_LFO_TYPE`
///       (`Type::getTitle` in `origin/main:src/deluge/gui/menu_item/lfo/type.h`), as `lfo::Sync`'s is
///       (`origin/main:src/deluge/gui/menu_item/lfo/sync.h`) for the `STRING_FOR_LFO_SYNC` its menus pass; both nodes
///       carry that number as their `title_arg`.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains

#include "plugin/types/display_mapping.h" // kMaxCombinedSync

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @brief The full sync-option range, `SyncLevel::size() == NUM_SYNC_VALUES`
///        (`origin/main:src/deluge/gui/menu_item/sync_level.h`, `origin/main:src/deluge/model/sync.h`), here
///        `plugin::kNumCombinedSyncValues` -- what every `SyncLevel` item but `sidechain::Sync` uses.
inline constexpr IntRange kSyncLevelRange{0, deluge::plugin::kMaxCombinedSync, 1};

/// @brief One LFO page. A macro for the same reason `sound_env.h` uses one -- and because the
///        `get`/`set` hooks are plain function pointers, so each LFO needs its own pair. The bodies
///        are in the `.cpp` (`DELUGE_LFO_HOOKS`); this declares them and builds the nodes.
#define DELUGE_LFO(n, rate_param, rate_title)                                                                          \
	inline constexpr Node lfo##n##_type = with_affect_entire(with_column_art(                                          \
	    without_column_label(with_title_arg(                                                                           \
	        with_title(                                                                                                \
	            setting_item(sid(deluge::l10n::String::STRING_FOR_SHAPE), Kind::Enum,                                  \
	                         {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND, .path = "lfo" #n "/type"}),       \
	            sid(deluge::l10n::String::STRING_FOR_LFO_TYPE)),                                                       \
	        (n))),                                                                                                     \
	    lfo_type_art));                                                                                                \
	inline constexpr Node lfo##n##_sync = with_affect_entire(with_title_arg(                                           \
	    with_title(                                                                                                    \
	        setting_item(sid(deluge::l10n::String::STRING_FOR_SYNC), Kind::SyncLevel,                                  \
	                     {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND, .path = "lfo" #n "/syncLevel"}),      \
	        sid(deluge::l10n::String::STRING_FOR_LFO_SYNC)),                                                           \
	    (n)));                                                                                                         \
	inline constexpr Node lfo##n##_rate =                                                                              \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_RATE), sound_param(rate_param), Hint::Knob,        \
	                           nullptr, sid(deluge::l10n::String::rate_title)));                                       \
	inline constexpr const Node* kLfo##n##Children[] = {&lfo##n##_type, &lfo##n##_sync, &lfo##n##_rate};               \
	inline constexpr Node lfo##n =                                                                                     \
	    submenu(sid(deluge::l10n::String::STRING_FOR_LFO##n), kLfo##n##Children, Layout::Horizontal)

DELUGE_LFO(1, deluge::instruments::classic::param::kLfo1Rate, STRING_FOR_LFO1_RATE);
DELUGE_LFO(2, deluge::instruments::classic::param::kLfo2Rate, STRING_FOR_LFO2_RATE);
DELUGE_LFO(3, deluge::instruments::classic::param::kLfo3Rate, STRING_FOR_LFO3_RATE);
DELUGE_LFO(4, deluge::instruments::classic::param::kLfo4Rate, STRING_FOR_LFO4_RATE);

#undef DELUGE_LFO

/// @brief This file's four contributions to `kBatch1Roots`: `lfo1` .. `lfo4`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundLfoChildren;

} // namespace deluge::ui::scenes::menu::nodes
