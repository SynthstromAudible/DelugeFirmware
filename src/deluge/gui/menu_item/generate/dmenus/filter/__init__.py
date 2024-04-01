from dmui.dsl import *
from . import lpf
from . import hpf

routing = Menu(
    "FilterRouting",
    "filterRoutingMenu",
    ["{title}"],
    "STRING_FOR_FILTER_ROUTE",
    "filter/routing.md",
)

sound_filters = Submenu(
    "submenu::Filter",
    "soundFiltersMenu",
    ["{title}", "%%CHILDREN%%"],
    "STRING_FOR_FILTERS",
    "filter/sound_filters.md",
    [lpf.menu, hpf.menu, routing],
    name="STRING_FOR_HPF",
)
