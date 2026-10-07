# Third-party software

This plugin is built with, and its binaries include, the following. CMake downloads them (pinned in libs/*/*.cmake and CMakeLists.txt); keep their notices with anything you distribute.

- CLAP SDK: MIT. https://github.com/free-audio/clap
- clap-wrapper, and the VST3 and AudioUnit SDKs it downloads: MIT (see each SDK's own licence). https://github.com/free-audio/clap-wrapper
- CHOC: ISC. https://github.com/Tracktion/choc
- chardsp (`libs/chardsp`, a git submodule): MIT. https://github.com/charCulbert/chardsp
  - elliptic-blep, its oscillators' anti-aliasing: MIT. https://github.com/Signalsmith-Audio/elliptic-blep
  - Signalsmith DSP: MIT. https://github.com/Signalsmith-Audio/dsp
- compost (the files in `resources/page/compost/`, with its LICENSE there): MIT. https://github.com/charCulbert/compost
- Barlow Semi Condensed and IBM Plex Mono (the fonts in `resources/page/fonts/`, with their licences there): SIL Open Font License 1.1. https://github.com/jpt/barlow, https://github.com/IBM/plex
