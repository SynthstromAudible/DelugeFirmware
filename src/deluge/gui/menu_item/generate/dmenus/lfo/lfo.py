from dmui.dsl import Menu

byLfo = {}

for i in range(4):
    isGlobal = i % 2 == 0
    index = 1 if i < 2 else 2

    byLfo[i] = [
        Menu(
            "lfo::Type",
            f"lfo{i + 1}TypeMenu",
            ["{name}", "{title}", f"LFO{i + 1}_ID"],
            "lfo/type.md",
            name="STRING_FOR_SHAPE",
            title="STRING_FOR_LFO_TYPE",
        ),
        Menu(
            "lfo::Sync",
            f"lfo{i + 1}SyncMenu",
            ["{name}", "{title}", f"LFO{i + 1}_ID"],
            "lfo/sync.md",
            name="STRING_FOR_SYNC",
            title="STRING_FOR_LFO_SYNC",
        ),
        Menu(
            "patched_param::Integer",
            f"lfo{i + 1}RateMenu",
            [
                "{name}",
                "{title}",
                f"params::GLOBAL_LFO_FREQ_{index}"
                if isGlobal
                else f"params::LOCAL_LFO_LOCAL_FREQ_{index}",
            ],
            "lfo/rate.md",
            name="STRING_FOR_RATE",
            title=f"STRING_FOR_LFO{i + 1}_RATE",
        ),
    ]
