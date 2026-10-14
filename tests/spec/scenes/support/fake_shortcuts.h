#pragma once

/// @file
/// @brief The host suite's stand-in for `src/scenes/menu/shortcuts.cpp`.
///
/// The real implementation (Task 9) is a transcription of the seven legacy tables as `Node`
/// pointers, so it odr-uses every node in `scenes/menu/nodes/*.h` and therefore drags in the whole
/// model-aware closure those nodes' `get`/`set`/`relevant` hooks live in. This host suite
/// deliberately never builds that closure (`tests/spec/CMakeLists.txt`), yet it *does* compile
/// `validate.cpp`, which calls `shortcut_node` for its `ShortcutToNothing` rule.
///
/// So the host suite links this fake instead: the same three entry points over one installable
/// table, which is exactly what `validate_spec.cpp` needs to drive that rule over fake trees. The
/// real tables are cross-checked against legacy in `sim/scenes_spec/shortcuts_spec.cpp`, where the
/// model closure exists.

#include "scenes/menu/shortcuts.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief Install @p table as the one table every `root` resolves through, addressed
///        `y * cols + x`. Pass an empty span to uninstall.
///
/// @param table Cells in row-major order; entries may be `nullptr` for "no shortcut here".
/// @param cols  Table width.
/// @param rows  Table height.
void set_shortcut_table_for_test(std::span<const Node* const> table, uint8_t cols, uint8_t rows);

} // namespace deluge::scenes::menu
