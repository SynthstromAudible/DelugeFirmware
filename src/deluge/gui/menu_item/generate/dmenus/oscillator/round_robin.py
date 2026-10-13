from dmui.dsl import Menu, Submenu

_DOC = "oscillator/sample/round_robin.md"

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'


def build(osc: int) -> Submenu:
    """Builds the round-robin Variants submenu for one oscillator.

    Slot 1 (C++ slotIndex 0) is the primary sample; slots 2-4 are alternates. Each slot gets its
    own horizontal, icon-based menu (the same style used by the OSC1/OSC2 menu one level up):
    File / Strt / End / Transpose / VMin / VMax. Strt and End open the sample marker editor focused
    on that marker (loop points stay reachable inside that same editor screen); Transpose is a
    single combined transpose+cents value, mirroring how OSC-level pitch is one entry, not two.
    VMin and VMax are the two ends of the slot's velocity range (1-127) used by RRMode::Velocity to
    pick a slot by note velocity - two adjacent columns, each edited in place, since a horizontal
    menu has no gesture for choosing which end of a single two-ended item you are editing. They
    only appear while the zone's mode is Velocity. Vol is a per-slot level trim (0-50,
    50 = unity, attenuation only) for balancing takes against each other.
    """
    mode = Menu(
        "sample::RoundRobinMode",
        f"sample{osc}RoundRobinModeMenu",
        ["{name}", f"{osc}"],
        _DOC,
        name="STRING_FOR_MODE",
        available_when="At least one alternate variant slot is loaded",
    )

    children = [mode]

    for slot in range(4):
        file_item = Menu(
            "sample::RoundRobinFileItem",
            f"sample{osc}RRFile{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_FILE",
        )
        strt = Menu(
            "sample::VariantStart",
            f"sample{osc}RRStart{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_START_POINT",
        )
        end = Menu(
            "sample::VariantEnd",
            f"sample{osc}RREnd{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_END_POINT",
        )
        transpose = Menu(
            "sample::VariantTranspose",
            f"sample{osc}RRTranspose{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_TRANSPOSE",
        )
        velocity_min = Menu(
            "sample::VariantVelocityEdge",
            f"sample{osc}RRVelocityMin{slot + 1}",
            [
                "{name}",
                f"{osc}",
                f"{slot}",
                "sample::VariantVelocityEdge::Edge::Min",
            ],
            _DOC,
            name="STRING_FOR_VELOCITY_MIN",
            available_when="Mode is set to Velocity",
        )
        velocity_max = Menu(
            "sample::VariantVelocityEdge",
            f"sample{osc}RRVelocityMax{slot + 1}",
            [
                "{name}",
                f"{osc}",
                f"{slot}",
                "sample::VariantVelocityEdge::Edge::Max",
            ],
            _DOC,
            name="STRING_FOR_VELOCITY_MAX",
            available_when="Mode is set to Velocity",
        )
        volume = Menu(
            "sample::VariantVolume",
            f"sample{osc}RRVolume{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_VARIANT_VOLUME",
        )
        children.append(
            Submenu(
                "sample::RoundRobinSlot",
                f"sample{osc}RoundRobinSlot{slot + 1}Menu",
                ["{name}", "%%CHILDREN%%", f"{osc}", f"{slot}"],
                _DOC,
                [file_item, strt, end, transpose, velocity_min, velocity_max, volume],
                name=f"STRING_FOR_VARIANT_SLOT_{slot + 1}",
                available_when="Slot 1 is always available; alternate slots when loaded, or the next empty slot",
            )
        )

    return Submenu(
        "sample::RoundRobinSubmenu",
        f"sample{osc}RoundRobinMenu",
        ["{name}", "%%CHILDREN%%", f"{osc}"],
        _DOC,
        children,
        name="STRING_FOR_VARIANTS",
        available_when=_available_txt,
    )
