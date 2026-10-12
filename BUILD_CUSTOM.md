# Building this custom source

This source uses the upstream Deluge build system and DBT toolchain version 22, pinned in toolchain/REQUIRED_VERSION. The toolchain is downloaded separately; it is not vendored here.

From a Git checkout on a supported host:

```sh
git clone https://github.com/mileda/DelugeFirmware.git
cd DelugeFirmware
./dbt build Release
```

On Windows use `dbt.cmd build Release`. Consult the retained upstream development documentation for host dependencies and toolchain setup. Build output is generated under build/. The published BIN was built in ARM Release configuration from source commit `a13bf0fba59f13217129a90c20b714223deef6a5` using toolchain v22. Physical-device testing is still pending; compilation and host tests do not replace it. See firmware/BUILD_INFO.json for the exact build commands and checksum.

Host regressions (Python 3, g++ with C++23 and sanitizers):

```sh
bash tests/regression/mdd/run.sh
bash tests/regression/plaits/run.sh
python3 tests/regression/plaits/menu_probe.py
python3 tests/regression/audio_audit/run.py
g++ -std=c++23 -O2 -fsanitize=undefined -Isrc/deluge tests/regression/audio_audit/bake_service.cpp -o /tmp/bake-service
/tmp/bake-service
g++ -std=c++23 -O3 -ffast-math -Isrc/deluge -Isrc/plaits_vendor tests/regression/audio_audit/finite_output.cpp -o /tmp/finite-output
/tmp/finite-output
```

Use the exact source commit when labeling any built binary. The imported historical snapshot IDs in the notes are provenance records; the publication commit has its own Git hash. Firmware installation guidance remains in the upstream documentation.

Toolchain v22 on this Linux host needed its LTO plugin registered with binutils before linking. If the bundle has no `arm-none-eabi-gcc/lib/bfd-plugins/liblto_plugin.so`, create a symlink there to `../../libexec/gcc/arm-none-eabi/14.2.1/liblto_plugin.so`. Reindex existing build archives with `arm-none-eabi-gcc-ranlib` or rebuild after registering the plugin. This changes the local toolchain setup, not firmware source.
