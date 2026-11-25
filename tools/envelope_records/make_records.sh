#!/usr/bin/env bash
# Regenerates tests/unit/dsp/main_envelope_records.h from main's envelope at e223d19d5: takes main's
# modulation/envelope.{h,cpp} and envelope_curve.h, drops the two members that need main's Sound and Voice (the probe
# never calls them), builds them on the host against stub headers written here, and runs probe.cpp.
#
# The stubs are written into the work directory rather than tracked, so no second tracked file shares a project
# header's name. Each is reduced to what main's envelope uses; the q31 conversions, getSine and its table are main's own
# at the same commit.
#
# Usage: tools/envelope_records/make_records.sh [WORK_DIR]
set -euo pipefail

repo=$(git rev-parse --show-toplevel)
here="$repo/tools/envelope_records"
work=${1:-"$repo/.superpowers/estate/envelope-curves/main_envelope"}
commit=e223d19d5
show() { git -C "$repo" show "$commit:src/deluge/$1"; }

rm -rf "$work"
stubs="$work/stubs"
mkdir -p "$work/modulation" "$work/main" "$stubs/model/voice" "$stubs/processing/engines" "$stubs/processing/sound" \
  "$stubs/util"
# main's sources the stubs take parts of, as files: awk stops reading at the part it wants
show util/fixedpoint.h > "$work/main/fixedpoint.h"
show util/waves.h > "$work/main/waves.h"
show util/lookuptables/lookuptables.cpp > "$work/main/lookuptables.cpp"

cat > "$stubs/definitions_cxx.hpp" <<'EOF_STUB'
#pragma once
#include <cstdint>
enum class EnvelopeStage : uint8_t { ATTACK, HOLD, DECAY, SUSTAIN, RELEASE, FAST_RELEASE, OFF };
#define D_PRINTLN(...)
EOF_STUB
cat > "$stubs/model/voice/voice.h" <<'EOF_STUB'
#pragma once
namespace deluge::modulation::params {}
EOF_STUB
cat > "$stubs/processing/engines/audio_engine.h" <<'EOF_STUB'
#pragma once
#include <cstdint>
struct AudioEngine {
	static inline uint32_t nextVoiceState = 0;
};
EOF_STUB
cat > "$stubs/processing/sound/sound.h" <<'EOF_STUB'
#pragma once
#include "util/waves.h"
EOF_STUB
{
  printf '#pragma once\n#include <bit>\n#include <cstdint>\n#include <limits>\nusing q31_t = int32_t;\n'
  awk '/^\[\[gnu::always_inline\]\] constexpr q31_t q31_from_float/{p=1} p{print} p&&/^}/{exit}' "$work/main/fixedpoint.h"
  awk '/^static inline float q31_to_float/{n++} n==2{print} n==2&&/^}/{exit}' "$work/main/fixedpoint.h"
} > "$stubs/util/fixedpoint.h"
{
  printf '#pragma once\n#include <cstdint>\n'
  awk '/^const int16_t sineWaveSmall\[257\]/{p=1} p{print} p&&/};/{exit}' "$work/main/lookuptables.cpp" \
    | sed 's/^const int16_t/inline constexpr int16_t/'
  awk '/inline int32_t interpolateTableSigned/{p=1} p{print} p&&/^}/{exit}' "$work/main/waves.h"
  awk '/inline int32_t getSine\(/{p=1} p{print} p&&/^}/{exit}' "$work/main/waves.h"
} > "$stubs/util/waves.h"

show modulation/envelope.h > "$work/modulation/envelope.h"
show modulation/envelope_curve.h > "$work/modulation/envelope_curve.h"
# Drop noteOn(uint8_t, Sound*, Voice*) and noteOff(uint8_t, Sound*, ParamManagerForTimeline*): from each signature to
# the closing brace at the start of a line.
show modulation/envelope.cpp \
  | awk '/^int32_t Envelope::noteOn\(uint8_t envelopeIndex/ || /^void Envelope::noteOff\(uint8_t envelopeIndex/ {skip=1}
         skip && /^}/ {skip=0; next}
         !skip' > "$work/envelope.cpp"

c++ -std=c++23 -O2 -I"$work" -I"$stubs" -I"$repo/tests/unit/dsp" \
  "$work/envelope.cpp" "$here/probe.cpp" -o "$work/probe"

out="$repo/tests/unit/dsp/main_envelope_records.h"
"$work/probe" > "$out"
clang-format -i "$out"
echo "wrote $out"
