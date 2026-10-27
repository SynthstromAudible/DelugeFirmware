# OLED handshake regression tests

The native `oled_handshake_driver_tests` executable compiles the production OLED driver
against mocked registers. The host CI build runs it through CTest as
`OledHandshakeDriverTests`; no Python runner is required.

Coverage includes acknowledgements behind deferred pad input, preserved input
ordering, stale and duplicate acknowledgements, the existing 3 ms settling wait,
timer wraparound, prompt DESELECT dispatch, CV release after OLED deselection,
and wrapped DMA receive-ring scanning. CV queuing and gate-completion semantics
remain those of the base firmware.
