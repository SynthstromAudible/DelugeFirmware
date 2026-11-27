#pragma once

/// @file
/// @brief A param target's schema: its space as a word and its id as the number its owner's plugin pins it to.

#include "project/param_address.h"
#include "project/schema/row_fields.h"

#include <array>
#include <string_view>

namespace deluge::project::schema {

/// The words of a param target's space, by ParamSpace.
inline constexpr std::array<std::string_view, 4> kParamSpaceWords{"engine", "expression", "midiCc", "arp"};

namespace param_target_detail {

inline constexpr std::array kParamTargetFields{
    sch::scalar<&ParamTarget::space>("space", {.words = kParamSpaceWords}),
    sch::scalar_at<ParamTarget, ParamTarget, [](auto& target) -> auto& { return target.id.value; }>("id"),
};
static_assert(sch::keys_unique(kParamTargetFields));

} // namespace param_target_detail

/// @brief A param target: an identity, so its record is closed.
inline constexpr sch::RecordDesc kParamTargetDesc =
    sch::closed_record("paramTarget", param_target_detail::kParamTargetFields);

} // namespace deluge::project::schema
