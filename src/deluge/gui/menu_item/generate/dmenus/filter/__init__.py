from dmui.dsl import *
from . import lpf
from . import hpf

# TODO: remove?
routing = Menu(
    "FilterRoutingMenu",
    "filterRoutingMenu",
    ["{title}"],
    "filter/routing.md",
    title="STRING_FOR_FILTER_ROUTE",
)

sound_filters = Submenu(
    "submenu::Filter",
    "soundFiltersMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/sound_filters.md",
    [lpf.menu, hpf.menu, routing],
    name="STRING_FOR_HPF",
    title="STRING_FOR_FILTERS",
)
