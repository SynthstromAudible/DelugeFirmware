from dmui.dsl import Menu

byEnvelope = {}

for i in range(4):
    byEnvelope[i] = [
        Menu(
            "envelope::Segment",
            f"env{i + 1}AttackMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_ATTACK", f"{i}"],
            "envelope/attack.md",
            name="STRING_FOR_ATTACK",
            title="STRING_FOR_ENV_ATTACK_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}DecayMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_DECAY", f"{i}"],
            "envelope/decay.md",
            name="STRING_FOR_DECAY",
            title="STRING_FOR_ENV_DECAY_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}SustainMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_SUSTAIN", f"{i}"],
            "envelope/sustain.md",
            name="STRING_FOR_SUSTAIN",
            title="STRING_FOR_ENV_SUSTAIN_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}ReleaseMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_RELEASE", f"{i}"],
            "envelope/release.md",
            name="STRING_FOR_RELEASE",
            title="STRING_FOR_ENV_RELEASE_MENU_TITLE",
        ),
    ]
