# Parameter unit tests

The CppUTest target compiles the same AutoParam value and storage source files
(`auto_param_value.cpp`, `auto_param_storage.cpp`) as firmware, with host adapters for allocation, attribute IO and fatal errors.
Run `ParameterTests` through CTest, or select a case with CppUTest arguments.

AutoParam privately inherits the production AutoParamData implementation. Tests use
standalone data for loading and playback operations, and the explicit automation
interface for live parameters. Compile-time checks verify restricted role
constructors, immutable live data views, and hidden mutable node storage. Setup,
ownership, playback and live-input access changes do not alter parameter
behavior. Control callers which set live values do so through the restricted
live-input role. Compile-time checks also reject direct live setters on AutoParam and
on the other roles.
The existing lazy-vector tests also run in ParameterTests using the same source
as the 32-bit target.
