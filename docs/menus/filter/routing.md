Filter routing.

TODO: use fancy 7seg/oled rendering in list below
TODO: diagrams showing the filter routing?

Can be one of:
  - "HPF2LPF", the default, where the output of the high-pass filter is routed to the input of the low-pass filter.
  - "LPF2HPF", where the output of the low-pass filter is routed to the input of the high-pass filter.
  - <string-for string="STRING_FOR_PARALLEL">Parallel</string-for>, where both filters are run in parallel and their output is summed.
