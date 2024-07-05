from dmui.dsl import *

threshold = Menu(
    "audio_compressor::CompParam",
    "threshold",
    ["{name}", "{title}", "params::UNPATCHED_COMPRESSOR_THRESHOLD"],
    "compressor/threshold.md",
    name="STRING_FOR_THRESHOLD",
)

attack = Menu(
    "audio_compressor::Attack",
    "compAttack",
    ["{name}", "{title}"],
    "compressor/attack.md",
    name="STRING_FOR_ATTACK",
)

release = Menu(
    "audio_compressor::Release",
    "compRelease",
    ["{name}", "{title}"],
    "compressor/release.md",
    name="STRING_FOR_RELEASE",
)

ratio = Menu(
    "audio_compressor::Ratio",
    "compRatio",
    ["{name}", "{title}"],
    "compressor/ratio.md",
    name="STRING_FOR_RATIO",
)

hpf = Menu(
    "audio_compressor::SideHPF",
    "compHPF",
    ["{name}", "{title}"],
    "compressor/hpf.md",
    name="STRING_FOR_HPF",
)

blend = Menu(
    "audio_compressor::Blend",
    "compBlend",
    ["{name}", "{title}"],
    "compressor/blend.md",
    name="STRING_FOR_BLEND",
)

menu = Submenu(
    "Submenu",
    "audioCompMenu",
    ["{name}", "%%CHILDREN%%"],
    "compressor/index.md",
    [threshold, ratio, attack, release, hpf, blend],
    name="STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR",
)
