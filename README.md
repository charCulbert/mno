# MNO

A monophonic synthesizer with two oscillators, envelopes, modulation, a filter,
and a waveform display. Available as CLAP, AUv3, a macOS standalone app, and
WCLAP for the browser.

## Build

Requires CMake 3.24+, a C++17 compiler, and Ninja or Xcode. Dependencies are
pinned submodules under `external/`.

```sh
git clone --recurse-submodules https://github.com/charCulbert/mno.git
cd mno
cmake --preset native
cmake --build --preset native
ctest --preset native
```

For WCLAP, install WASI SDK 33.0 with pthread support:

```sh
export WASI_SDK_ROOT=/path/to/wasi-sdk
cmake --preset wclap
cmake --build --preset wclap
```

For macOS AUv3:

```sh
cmake --preset xcode
cmake --build --preset xcode --config Release
```

Outputs are written to `build-*/artifacts/`. The WCLAP archive is
`build-wclap/artifacts/MNO.wclap.tar.gz`.

## Credits

Built with these projects. Thanks to their authors and contributors:

- [CLAP](https://github.com/free-audio/clap) and [clap-helpers](https://github.com/free-audio/clap-helpers) — Alexandre Bique and contributors.
- [clap-wrapper](https://github.com/free-audio/clap-wrapper) — defiantnerd and contributors; this project uses my fork.
- [CHOC](https://github.com/Tracktion/choc) — Tracktion Corporation and contributors.
- [Compost](https://github.com/charCulbert/compost) and [char-clap-utils](https://github.com/charCulbert/char-clap-utils) — Charlie Culbert.
- [chardsp](https://github.com/charCulbert/chardsp) — Charlie Culbert, with [elliptic-blep](https://github.com/Signalsmith-Audio/elliptic-blep) and [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp) by Geraint Luff / Signalsmith Audio.
- [WASI SDK](https://github.com/WebAssembly/wasi-sdk), [LLVM](https://github.com/llvm/llvm-project), and [wasi-libc](https://github.com/WebAssembly/wasi-libc) — their contributors, including the musl and dlmalloc authors.

## License

ISC. See [LICENSE](LICENSE). Each WCLAP archive contains this license and
[third-party notices](THIRD_PARTY_NOTICES.md), with the full dependency license
texts in `THIRD_PARTY_LICENSES/`.

## Source layout

`Plugin.cpp` handles the CLAP interface, parameters, state, presets, and UI
messages. `MNOProcessor.*` and the `MNO*.h` headers contain the DSP. `ui/`
contains the web interface, and `tests/` contains the native checks.
