from dmui.dsl import *

attack = Menu(
    "envelope::Segment",
    "envAttackMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_ATTACK"],
    "STRING_FOR_ENV_ATTACK_MENU_TITLE",
    "envelope/attack.md",
    name="STRING_FOR_ATTACK",
)

decay = Menu(
    "envelope::Segment",
    "envDecayMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_DECAY"],
    "STRING_FOR_ENV_DECAY_MENU_TITLE",
    "envelope/decay.md",
    name="STRING_FOR_DECAY",
)

sustain = Menu(
    "envelope::Segment",
    "envSustainMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_SUSTAIN"],
    "STRING_FOR_ENV_SUSTAIN_MENU_TITLE",
    "envelope/sustain.md",
    name="STRING_FOR_SUSTAIN",
)

release = Menu(
    "envelope::Segment",
    "envReleaseMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_RELEASE"],
    "STRING_FOR_ENV_RELEASE_MENU_TITLE",
    "envelope/release.md",
    name="STRING_FOR_RELEASE",
)

menus = [
    Submenu(
        "submenu::Envelope",
        f"env{i}Menu",
        ["{name}", "%%CHILDREN%%", f"{i}"],
        f"STRING_FOR_ENVELOPE_{i+1}",
        "envelope/index.md",
        [attack, decay, sustain, release],
    )
    for i in range(2)
]
