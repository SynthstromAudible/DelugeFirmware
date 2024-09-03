from dmui.dsl import *

from . import hpf
from . import lpf

routing = Menu(
    "FilterRouting",
    "filterRoutingMenu",
    ["{title}"],
    "filter/routing.md",
    title="STRING_FOR_FILTER_ROUTE",
)

sound_filters = Submenu(
    "Submenu",
    "soundFiltersMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/sound_filters.md",
    [lpf.menu, hpf.menu, routing],
    name="STRING_FOR_HPF",
    title="STRING_FOR_FILTERS",
)
