#pragma once

/// @brief The DX7 menu pages' tables and step rules: what the pages show and where a turn takes them.
///
/// A DX7 patch is 156 voice bytes. The pages name them by a *param* index: six operators of 21 params each, operator
/// `op` (0-5, where 0 is the DX7's operator 6) at `op * 21`, then the global params at 126..143, with -1 for the
/// patch's random detune, which is stored outside the voice bytes (`DxParam`, `gui/menu_item/dx/param.cpp`).
///
/// Everything here is a pure function of a param index, a row or a value, transcribed from legacy's own tables and
/// compared with them row for row in `dx_pages_spec`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::scenes::menu::dx {

/// @brief The param index of the patch's random detune.
inline constexpr int16_t kRandomDetune = -1;
/// @brief The first global param: the pitch envelope's first rate, just past the six operators' params.
inline constexpr int16_t kFirstGlobal = 6 * 21;
/// @brief The last param the pages reach.
inline constexpr int16_t kLastParam = 143;
/// @brief Params per operator.
inline constexpr int16_t kOperatorParams = 21;
/// @brief The voice byte holding the six operators' on/off bits, operator `op` at bit `op`.
inline constexpr size_t kOperatorSwitch = 155;

/// @brief The highest value @p param takes, inclusive (`DxParam::readValueAgain`, `param.cpp:73-127`): the per-operator
///        table for an operator param, the global table for a global one, 99 otherwise (the random detune included).
uint8_t upper_limit(int16_t param);

/// @brief The parameter page's title for @p param (`DxParam::getTitle`, `param.cpp:282-322`): "random detune", "op4
///        envelope" and the like for an operator param, "dx7 pitch env" for the pitch envelope, the global param's
///        long name otherwise.
/// @param param The param.
/// @param out   Where the title is written; 24 characters is enough for every title.
/// @return The title's length.
size_t title(int16_t param, std::span<char> out);

/// @brief How many rows "DX7 parameters" lists (`global_params.cpp:46-51`).
inline constexpr int32_t kGlobalRows = 11;
/// @brief How many rows "DX7 operator N" lists (`operator_params.cpp:45-53`).
inline constexpr int32_t kOperatorRows = 7;

/// @brief Row @p row's label on "DX7 parameters".
std::string_view global_row(int32_t row);
/// @brief Row @p row's label on "DX7 operator N".
std::string_view operator_row(int32_t row);

/// @brief Where SELECT on "DX7 parameters" row @p row goes (`DxGlobalParams::selectButtonPress`,
///        `global_params.cpp:78-90`): a param index for the parameter page, or `-N` for "DX7 operator N".
int16_t global_row_param(int32_t row);
/// @brief The param SELECT on "DX7 operator N" row @p row opens (`DxOperatorParams::selectButtonPress`,
///        `operator_params.cpp:80-84`).
/// @param op  The operator, 0-5 (`6 - N`).
/// @param row The row.
int16_t operator_row_param(int32_t op, int32_t row);

/// @brief A keyboard scaling curve's name (`curves`, `param.cpp:279`), values past the four curves named "????".
std::string_view curve_name(uint8_t value);
/// @brief An LFO waveform's long name (`shapes_long`, `param.cpp:280`), values past sample-and-hold named as it.
std::string_view lfo_shape(uint8_t value);

/// @brief The param a horizontal turn steps to (`DxParam::horizontalEncoderAction` without Shift, `param.cpp:214-222`):
///        @p param plus @p offset, below -1 wrapping to 143 and above 143 to -1.
int16_t step_param(int16_t param, int32_t offset);

/// @brief The param Shift + a horizontal turn steps to (`DxParam::horizontalEncoderAction`, `param.cpp:198-213`): the
///        same param on the next operator, in the turn's direction and wrapping, whose switch is on.
///
/// Nothing for the random detune and the globals past the pitch envelope's first rate, which legacy leaves alone. At
/// that first rate (126, which legacy counts as operator "6") with every operator switched off, legacy's search never
/// ends; here it answers nothing too.
/// @param param   The param shown.
/// @param offset  The turn; legacy steps one operator per unit of it.
/// @param params  The patch's voice bytes, for the operator switches.
/// @return The param to show, or nothing where the page stays as it is.
std::optional<int16_t> step_operator(int16_t param, int32_t offset, std::span<const uint8_t, 156> params);

/// @brief A shortcut pad, column and row.
struct Pad {
	uint8_t x;
	uint8_t y;
	bool operator==(const Pad&) const = default;
};

/// @brief The pad that blinks while the parameter page shows @p param (`DxParam::readValueAgain`, `param.cpp:100-145`):
///        an operator's row from the top, with its envelope, tuning and sensitivities across it; the pitch envelope,
///        algorithm, feedback and sync on row 1; the LFO and the random detune on row 0.
/// @return The pad, or nothing for the keyboard scaling params (8-12), which blink none.
std::optional<Pad> param_pad(int16_t param);

/// @brief The param a pad picks on the parameter page's own pad layout (`DxParam::potentialShortcutPadAction`,
///        `param.cpp:228-267`): an operator's row from the top, row 1 the pitch envelope, algorithm, feedback and sync,
///        row 0 the LFO and the random detune.
/// @return The param, or nothing for a pad that picks none (row 1 past column 10, row 0 past column 7).
std::optional<int16_t> pad_param(uint8_t x, uint8_t y);

} // namespace deluge::scenes::menu::dx
