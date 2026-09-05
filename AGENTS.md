# AGENTS.md

C++20 CLI (`aca`) demonstrating the conferencing capture chain:
`mic -> AEC -> ANS -> AGC -> Opus encode -> [network] -> Opus decode`.

## Commands

```sh
make verify        # build + ctest + fixtures + end-to-end demo -- run before commit
make build         # cmake configure + compile into build/
make test          # cd build && ctest --output-on-failure  (62 tests)
make fixtures      # regenerate assets/*.wav (gitignored, not committed)
make fetch-real    # download tiny real-speech kit -> assets/real/ (curl+sox)
make asan          # ASan+UBSan build; currently clean, keep it that way
                   # ASAN_OPTIONS=detect_container_overflow=0:detect_stack_use_after_return=1
                   # (container overflow off: Homebrew libgtest is not instrumented,
                   # so mixing it with ASan-built code false-positives inside GTest;
                   # stack-use-after-return on: off by default in ASan, must enable)
                   # Also defines _LIBCPP_HARDENING_MODE_FAST / _GLIBCXX_ASSERTIONS
make tsan          # ThreadSanitizer (separate from ASan; cannot combine)
make demo-live     # needs a real mic; macOS prompts for permission
```
CI (`.github/workflows/ci.yml`) runs `make test` + fixtures/demo, `make asan`,
and `make tsan` on Ubuntu and macOS.


Run one test: `./build/aca_tests --gtest_filter='Aec.*'`.
`ctest` registers each case individually via `gtest_discover_tests`, so
`ctest -R Agc.RaisesQuietSignal` also works.

`make test` depends on `build`, but `demo`/`verify` also need `make fixtures`
first — `assets/*.wav` is gitignored and absent on a fresh clone.

## Library split (do not conflate)

- **libopus** = codec only. It has *no* AEC/ANS/AGC. Do not look for noise
  suppression or echo cancellation in the Opus API.
- **speexdsp** = all three processing stages (`speex_echo.h`,
  `speex_preprocess.h`).
- `webrtc-audio-processing` has no Homebrew formula; don't propose it as a
  drop-in.

## Toolchain gotchas

- Include is `<opus.h>`, **not** `<opus/opus.h>`. The `opus` pkg-config
  `--cflags` already ends in `/include/opus`.
- Deps resolve through `pkg_check_modules` against Homebrew. `CMakeLists.txt`
  injects `brew --prefix` into `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH`; keep
  that if you add a dependency.
- ANS and AGC share one `SpeexPreprocessState` in `CaptureChain`. When a
  preprocessor is wired to an echo state via `SPEEX_PREPROCESS_SET_ECHO_STATE`,
  **destroy the preprocessor before the echo state** — it dereferences the echo
  state during teardown.
- speexdsp prints `warning: The VAD has been replaced by a hack...` to stderr
  on every preprocess init. It's harmless library noise, not a bug to fix.
- `GET_AGC_GAIN` / `GET_AGC_LOUDNESS` always read `0` in this speexdsp build;
  don't build assertions on them. Measure output RMS instead.

## DSP invariants that break silently if violated

- **speexdsp ANS is a stationary-noise estimator.** A constant sine is
  classified as noise and attenuated ~7 dB, while speech-like audio loses
  <1 dB. Never validate ANS or AGC with `make_tone` — use
  `aca::test::make_speech_like()` (in `tests/test_helpers.h`), which is
  non-stationary. Two tests were initially wrong for exactly this reason.
- **AGC needs seconds to ramp.** ~+17 dB after 3 s, ~+26 dB after 5 s on a
  -48 dBFS input. Short test signals look like "AGC is broken".
- **Stage order is fixed: AEC -> ANS -> AGC.** AEC must see the raw mic so the
  mic stays a linear function of the reference; denoising or re-gaining first
  prevents the adaptive filter from converging. AGC last so it measures cleaned
  audio.
- **AEC needs a far-end reference**, not just the mic. In `aca live` the
  reference is the previous monitored output frame, so `--aec --no-monitor`
  has nothing to cancel (the CLI warns).
- Opus accepts only **8/12/16/24/48 kHz** and frames of **5/10/20/40/60 ms**.
  44.1 kHz and 15/30 ms are rejected with explicit errors — preserve that
  instead of silently resampling.

## Architecture

- `src/core/` — `AudioBuffer` (interleaved int16 everywhere; both speexdsp and
  the Opus fixed-point path are int16-native, so no float conversion), WAV I/O,
  metrics (RMS/peak/SNR/ERLE), and the frame container.
- `src/dsp/pipeline.cpp` — `CaptureChain` is the single streaming
  implementation shared by the offline `pipeline` command and the live
  PortAudio callback. Add stages here, not in two places.
- `src/live/` — compiled only when PortAudio is found; guarded by
  `ACA_HAVE_PORTAUDIO`. `src/cli/cmd_live.cpp` has a stub branch that throws a
  helpful error otherwise. Keep both branches compiling.
- `src/cli/` — hand-rolled `Args` parser in `src/cli/args.cpp` (declared in
  `include/aca/cli_args.h`, and part of `aca_core` so it is unit-testable);
  no CLI library. Every command calls `args.reject_unknown({...})`, so
  **adding a flag requires adding it to that allowlist** or it errors as
  unknown. The parser treats a leading `-` as a value only when a digit or `.`
  follows, so `--suppress-db -25` works without swallowing real options.
- `.opus-frames` is this repo's own length-prefixed container (magic `ACA1`),
  not Ogg. Raw Opus packets aren't self-delimiting; only `aca decode` reads it.

## Conventions

- Public headers in `include/aca/`, namespace `aca` (`aca::cli`, `aca::test`).
- Google style, 80 cols (`.clang-format`); `make fmt` needs clang-format
  installed.
- pimpl (`struct Impl`) wraps every C library handle so destruction order and
  cleanup are explicit; keep C types out of public headers.
- Errors are `std::runtime_error` with actionable messages; `main` catches and
  returns 1 (2 for usage). Preserve the "what to do about it" phrasing.
- Fixtures are synthesised with a fixed RNG seed — no binary audio in git.
- Assert on energy/level or relative deltas, not sample-exact output: Opus is
  lossy and adds delay, and the AEC/AGC adapt over time.
