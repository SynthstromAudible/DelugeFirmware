# Parameter unit tests

ParameterTests runs the existing lazy-vector tests against the production parameter
vector and container sources on the native host, including 64-bit macOS. The test
source is shared with SmallPointerTests. Host adapters provide allocation and fatal-error services.

Run `ctest --test-dir build/tests -R '^ParameterTests$' --output-on-failure`,
or run `build/tests/unit/parameters/ParameterTests` directly.

This suite covers vector allocation, cloning, and ownership; it does not directly
exercise ParamManager or AutoParam interpolation and serialization.
