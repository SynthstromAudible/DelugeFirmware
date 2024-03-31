from dmui.dsl import *

threshold = Menu(
    "audio_compressor::CompParam",
    "threshold",
    ["{name}", "{title}", "params::UNPATCHED_COMPRESSOR_THRESHOLD"],
    "STRING_FOR_THRESHOLD",
    "compressor/threshold.md",
    name="STRING_FOR_THRESHOLD",
)

attack = Menu(
    "audio_compressor::Attack",
    "compAttack",
    ["{name}", "{title}"],
    "STRING_FOR_ATTACK",
    "compressor/attack.md",
)

release = Menu(
    "audio_compressor::Release",
    "compRelease",
    ["{name}", "{title}"],
    "STRING_FOR_RELEASE",
    "compressor/release.md",
)

ratio = Menu(
    "audio_compressor::Ratio",
    "compRatio",
    ["{name}", "{title}"],
    "STRING_FOR_RATIO",
    "compressor/ratio.md",
)

hpf = Menu(
    "audio_compressor::SideHPF",
    "compHPF",
    ["{name}", "{title}"],
    "STRING_FOR_HPF",
    "compressor/hpf.md",
)

menu = Submenu(
    "Submenu",
    "audioCompMenu",
    ["{name}", "%%CHILDREN%%"],
    "STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR",
    "compressor/index.md",
    [threshold, ratio, attack, release, hpf],
)
