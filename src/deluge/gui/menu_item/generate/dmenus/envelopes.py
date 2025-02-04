from dmui.dsl import *

attack = Menu(
    "envelope::Segment",
    "envAttackMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_ATTACK"],
    "envelope/attack.md",
    name="STRING_FOR_ATTACK",
    title="STRING_FOR_ENV_ATTACK_MENU_TITLE",
)

decay = Menu(
    "envelope::Segment",
    "envDecayMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_DECAY"],
    "envelope/decay.md",
    name="STRING_FOR_DECAY",
    title="STRING_FOR_ENV_DECAY_MENU_TITLE",
)

sustain = Menu(
    "envelope::Segment",
    "envSustainMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_SUSTAIN"],
    "envelope/sustain.md",
    name="STRING_FOR_SUSTAIN",
    title="STRING_FOR_ENV_SUSTAIN_MENU_TITLE",
)

release = Menu(
    "envelope::Segment",
    "envReleaseMenu",
    ["{name}", "{title}", "params::LOCAL_ENV_0_RELEASE"],
    "envelope/release.md",
    name="STRING_FOR_RELEASE",
    title="STRING_FOR_ENV_RELEASE_MENU_TITLE",
)

menus = [
    Submenu(
        "submenu::Envelope",
        f"env{i}Menu",
        ["{name}", "%%CHILDREN%%", f"{i}"],
        "envelope/index.md",
        [attack, decay, sustain, release],
        name=f"STRING_FOR_ENVELOPE_{i + 1}",
    )
    for i in range(4)
]
