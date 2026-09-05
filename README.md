# MNO

A CLAP synthesizer written directly against the CLAP API with `clap-helpers`,
with a `clap.webview/3` interface built from
[compost](https://github.com/charCulbert/compost) components. The
[`charCulbert/clap-wrapper`](https://github.com/charCulbert/clap-wrapper)
fork turns the CLAP into a macOS standalone, an AUv3 (with host app), an iOS
AUv3, and a WCLAP for the browser.

Everything it needs is a submodule under `external/`:

```sh
git clone --recurse-submodules https://github.com/charCulbert/mno.git
```

Build with the presets in `CMakePresets.json`:

```sh
cmake --preset xcode  && cmake --build --preset xcode    # CLAP, AUv3 + host app, standalone
cmake --preset native && cmake --build --preset native && ctest --preset native
WASI_SDK_ROOT=~/WASI_SDK/wasi-sdk-33.0-arm64-macos cmake --preset wclap && cmake --build --preset wclap
```

Outputs land in `build-*/artifacts/`. To build against a sibling checkout of a
dependency while working on it, override its root, for example
`-DCLAP_WRAPPER_ROOT=../../clap-wrapper` or `-DCOMPOST_ROOT=../../compost`.

iOS simulator:

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build-ios --config Release
```

Layout: `Plugin.cpp` is the CLAP plug-in (parameters, state, presets, UI
messaging), `MNOProcessor.*` and the `MNO*.h` headers are the DSP, `ui/` is
the web interface, `tests/` a source-level smoke test of the factory,
parameters, presets, processing, and telemetry.
