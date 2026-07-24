from dmui.dsl import Menu, Submenu

_DOC = "oscillator/sample/round_robin.md"

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'


def build(osc: int) -> Submenu:
    """Builds the round-robin Variants submenu for one oscillator.

    Slot 1 (C++ slotIndex 0) is the primary sample; slots 2-4 are alternates. Each slot gets its
    own horizontal, icon-based menu (the same style used by the OSC1/OSC2 menu one level up):
    File / Strt / End / Transpose. Strt and End open the sample marker editor focused on that
    marker (loop points stay reachable inside that same editor screen); Transpose is a single
    combined transpose+cents value, mirroring how OSC-level pitch is one entry, not two.
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
        children.append(
            Submenu(
                "sample::RoundRobinSlot",
                f"sample{osc}RoundRobinSlot{slot + 1}Menu",
                ["{name}", "%%CHILDREN%%", f"{osc}", f"{slot}"],
                _DOC,
                [file_item, strt, end, transpose],
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
