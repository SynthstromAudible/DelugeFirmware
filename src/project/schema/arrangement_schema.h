#pragma once

/// @file
/// @brief The arrangement's schema: every section's repeats and every session macro.

#include "project/arrangement.h"
#include "project/schema/row_fields.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace deluge::project::schema {

/// The words of a session macro's kind, by SessionMacroKind.
inline constexpr std::array<std::string_view, 4> kSessionMacroWords{"none", "clipLaunch", "outputCycle", "section"};
static_assert(kSessionMacroWords.size() == static_cast<std::size_t>(SessionMacroKind::NUM_KINDS));

namespace arrangement_detail {

inline constexpr std::array kSectionFields{
    sch::scalar<&Section::num_repetitions>("numRepetitions", {.min = -2, .max = 9999}),
};

inline constexpr sch::FieldDesc kId = sch::value_scalar<uint32_t>();

inline constexpr std::array kMacroFields{
    sch::scalar<&SessionMacro::kind>("kind", {.words = kSessionMacroWords}),
    sch::optional<&SessionMacro::clip_id>("clip", kId),
    sch::optional<&SessionMacro::output_id>("output", kId),
    sch::scalar<&SessionMacro::section>("section", {.min = 0, .max = kMaxNumSections - 1}),
};

} // namespace arrangement_detail

inline constexpr sch::RecordDesc kSectionDesc =
    sch::open_record<&Section::extras>("section", arrangement_detail::kSectionFields);
inline constexpr sch::RecordDesc kSessionMacroDesc =
    sch::open_record<&SessionMacro::extras>("sessionMacro", arrangement_detail::kMacroFields);

namespace arrangement_detail {

inline constexpr sch::FieldDesc kSection = sch::value_record(kSectionDesc);
inline constexpr sch::FieldDesc kMacro = sch::value_record(kSessionMacroDesc);

inline constexpr std::array kArrangementFields{
    sch::list<&Arrangement::sections>("sections", kSection),
    sch::list<&Arrangement::session_macros>("sessionMacros", kMacro),
};

} // namespace arrangement_detail

inline constexpr sch::RecordDesc kArrangementDesc =
    sch::open_record<&Arrangement::extras>("arrangement", arrangement_detail::kArrangementFields);
inline constexpr sch::Schema<Arrangement> kArrangementSchema{&kArrangementDesc};

} // namespace deluge::project::schema
