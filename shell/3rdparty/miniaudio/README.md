# miniaudio

Vendored single-header audio library, used for playing audio from the shell.

- Upstream: https://github.com/mackron/miniaudio
- Version: 0.11.26, tag `0.11.26` (2026-10-31), files `miniaudio.h` and `LICENSE` unchanged.
- `miniaudio.h` sha256: `6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e`
- License: public domain (Unlicense) or MIT-0, at our choice (`LICENSE`).

`miniaudio.c` (ours) compiles the implementation with only what we use: playback devices,
no decoding/encoding/engine. Backends are loaded at runtime (PulseAudio, ALSA, …); on
SteamOS that's PipeWire through its PulseAudio interface.
