from dmui.dsl import Menu, Submenu

_DOC = "oscillator/sample/round_robin.md"

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'


def build(osc: int) -> Submenu:
    """Builds the round-robin Variants submenu for one oscillator.

    Slot 1 (C++ slotIndex 0) is the primary sample; slots 2-4 are alternates.
    All four slots get the same submenu: File / Markers / Transpose / Cents. "Markers" opens
    the sample marker editor, where start, end and loop points are all editable in one screen.
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
        markers = Menu(
            "sample::VariantMarkers",
            f"sample{osc}RRMarkers{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_MARKERS",
        )
        transpose = Menu(
            "sample::VariantTranspose",
            f"sample{osc}RRTranspose{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_TRANSPOSE",
        )
        cents = Menu(
            "sample::VariantCents",
            f"sample{osc}RRCents{slot + 1}",
            ["{name}", f"{osc}", f"{slot}"],
            _DOC,
            name="STRING_FOR_CENTS",
        )
        children.append(
            Submenu(
                "sample::RoundRobinSlot",
                f"sample{osc}RoundRobinSlot{slot + 1}Menu",
                ["{name}", "%%CHILDREN%%", f"{osc}", f"{slot}"],
                _DOC,
                [file_item, markers, transpose, cents],
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
