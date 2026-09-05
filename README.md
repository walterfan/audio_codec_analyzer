# audio_codec_analyzer (`aca`)

A C++20 command-line tool that demonstrates — and **measures** — the audio
capture chain every voice/video conferencing client runs:

```
  mic  ->  AEC  ->  ANS  ->  AGC  ->  Opus encode  ->  [ network ]
                                                            |
speaker  <----------------------------  Opus decode  <------+
```

Every stage is a separate subcommand, so you can run one in isolation and see
exactly what it did to the signal. There is also a `pipeline` command that runs
the whole chain offline, and a `live` command that runs it on your real
microphone.

---

## Goal

Most "audio codec" examples stop at `encode()` / `decode()`. Real conferencing
audio is mostly the *other* work: removing the echo of the far end, suppressing
steady background noise, and normalising the talker's level — all before a
single byte reaches the encoder.

This project exists to make that pipeline concrete:

1. **Show the whole chain**, not just the codec, with the stages in the order a
   real client uses them.
2. **Quantify each stage.** Every command prints RMS/peak levels, and the
   relevant figure of merit (ERLE for echo cancellation, compression ratio and
   effective bitrate for the codec), so you can see the effect instead of
   trusting a description.
3. **Be reproducible.** Fixtures are synthesised from a fixed RNG seed, so no
   binary audio lives in the repo and everyone measures the same numbers.
4. **Be honest about the traps.** Several behaviours here silently produce
   plausible-but-wrong results (see [Gotchas](#gotchas-that-cost-real-time)).
   They are documented and covered by regression tests.

Non-goals: this is not a production audio engine, not a real-time-safe design
(the offline paths allocate freely), and the `.opus-frames` container is not
interoperable with anything else.

---

## Background knowledge

### The one thing people get wrong

> **Opus is a codec. It has no AEC, no noise suppression, and no AGC.**

Opus compresses audio. It does not clean it. AEC/ANS/AGC are *pre-processing*
stages that run on the microphone signal before the encoder ever sees it. If
you go looking for `opus_set_noise_suppression()`, you will not find it.

That is why this project uses two libraries:

| Concern | Library | Notes |
| --- | --- | --- |
| Encode / decode | **libopus** | The codec, and only the codec |
| AEC, ANS, AGC | **speexdsp** | `speex_echo.h`, `speex_preprocess.h` |
| WAV / FLAC I/O | **libsndfile** | Converts everything to interleaved int16 |
| Live mic + speaker | **PortAudio** | Optional; build works without it |

(The other common choice is `webrtc-audio-processing`, which has a stronger
AEC3/AGC2, but it has no Homebrew formula and needs a manual meson/gn build.)

### The stages

**AEC — Acoustic Echo Cancellation.** Your speaker plays the far end; your mic
picks it back up; without cancellation the far end hears themselves ~200 ms
later. AEC adaptively models the room's echo path and subtracts a filtered copy
of the far-end signal from the mic.

The terminology trips people up constantly:

| Term | Means |
| --- | --- |
| far-end / reference / "speaker" / `play` | what you **play out** |
| near-end / "mic" / `rec` | what the mic **picked up** (your voice + echo) |

So AEC needs **two** inputs. `aca aec --mic` alone is an error — with no
reference there is nothing to cancel.

Quality is measured as **ERLE** (Echo Return Loss Enhancement), the dB
reduction in echo energy. Higher is better. The adaptive filter needs seconds
of far-end audio to converge, and the filter tail (`--filter-ms`) must be
longer than the room's reverberation time.

**ANS — Noise Suppression.** Estimates the noise spectrum and attenuates it.
speexdsp uses a *stationary* noise estimator: it assumes noise is steady (fans,
hiss, hum) and speech is not. `--suppress-db` is a **negative** value.

**AGC — Automatic Gain Control.** Pushes a quiet or loud talker toward a target
level. It ramps gain slowly to avoid pumping, which means it needs **seconds**
to reach full gain.

**Why the order is AEC → ANS → AGC, and not anything else:**

- AEC must run **first**, on the untouched mic signal. The adaptive filter can
  only converge while the mic remains a roughly *linear* function of the
  reference. Denoising or re-gaining beforehand breaks that assumption and the
  filter stops converging.
- AGC runs **last**, so it measures already-cleaned audio. Put it first and it
  happily amplifies the noise floor during silences.

### Opus in one paragraph

Opus (RFC 6716) is a hybrid of SILK (speech, linear prediction) and CELT
(music, MDCT), covering 6 kbps narrowband speech to 510 kbps stereo. It is
mandatory-to-implement in WebRTC. Relevant constraints this tool enforces:

- Input sample rate must be **8, 12, 16, 24 or 48 kHz**. 44.1 kHz is illegal —
  a very common trap, since most music files are 44.1 kHz.
- Frame duration must be **2.5/5/10/20/40/60 ms**. This tool allows 5–60 ms.
  15 ms and 30 ms are not legal values.
- **PLC** (Packet Loss Concealment): pass a null packet and the decoder
  synthesises a replacement frame, so timing is preserved when a packet is lost.
- **FEC** adds redundant data for loss resilience (costs bitrate); **DTX**
  sends near-empty frames during silence (saves bitrate).

Raw Opus packets are **not self-delimiting** — you cannot concatenate them and
decode later. They need a container (normally Ogg). This project uses a
deliberately trivial one: see [`.opus-frames`](#the-opus-frames-container).

---

## Build

Requires a C++20 compiler, CMake ≥ 3.20 and pkg-config.

```sh
brew install opus speexdsp libsndfile portaudio googletest cmake pkg-config
make build
```

Verified against opus 1.6.1, speexdsp 1.2.1, libsndfile 1.2.2, PortAudio 19,
GoogleTest 1.18 on macOS/arm64 with Apple clang.

PortAudio is optional. Without it everything still builds; `aca live` and
`aca devices` then fail with an explanatory message instead of being missing.

---

## Usage

### Quick start

```sh
make verify     # build + 62 unit tests + end-to-end run   <- start here
make demo       # run every stage on the fixtures, into out/
make demo-live  # real microphone (macOS will prompt for permission)
```

`make fixtures` synthesises the test audio into `assets/` (gitignored, so it is
absent on a fresh clone — `verify` and `demo` generate it for you):

| File | Contents |
| --- | --- |
| `speech.wav` | speech-like: 140 Hz pitch + harmonics, 4 Hz syllable envelope |
| `noisy.wav` | `speech.wav` + white noise |
| `quiet.wav` | `speech.wav` at ~-45 dBFS, for AGC to pull up |
| `far_end.wav` | the signal "played to the speaker" |
| `mic_echo.wav` | near speech + a 3-tap delayed echo of `far_end.wav` |

### Codec

```sh
# encode (defaults: 24 kbps, 20 ms frames, VOIP mode, complexity 10, VBR)
aca encode assets/speech.wav -o speech.opus-frames --bitrate 24000 --frame-ms 20

# tuning knobs
aca encode in.wav -o out.opus-frames --mode audio --complexity 5 --vbr false
aca encode in.wav -o out.opus-frames --fec true --loss-pct 20   # loss resilience
aca encode in.wav -o out.opus-frames --dtx true                 # silence suppression

# decode
aca decode speech.opus-frames -o decoded.wav

# simulate network loss: drop every 5th packet, let Opus PLC fill the gap
aca decode speech.opus-frames -o lossy.wav --drop-every-nth 5
```

### One stage at a time

```sh
# AEC -- needs BOTH the mic and the far-end reference
aca aec --mic assets/mic_echo.wav --ref assets/far_end.wav -o aec.wav \
        --frame-ms 10 --filter-ms 100

# ANS -- note --suppress-db is negative
aca ans assets/noisy.wav -o ans.wav --suppress-db -25

# AGC -- give it several seconds of audio to ramp
aca agc assets/quiet.wav -o agc.wav --target-dbfs -3 --max-gain-db 30
```

### Whole chain, and measurement

```sh
# AEC + ANS + AGC + Opus round-trip in one pass
aca pipeline --mic assets/mic_echo.wav --ref assets/far_end.wav \
             -o out.wav --aec --codec

# stages are opt-out; ANS and AGC are on by default
aca pipeline --mic noisy.wav -o out.wav --no-agc

# level/duration/clipping report for any files
aca analyze assets/speech.wav out.wav
```

### Live audio

```sh
aca devices                                   # enumerate host devices
aca live --seconds 5 --aec --record live.wav  # process the real mic
aca live --in-device 3 --out-device 4 --no-monitor
```

> **Use headphones.** Monitoring is on by default, so laptop speakers will feed
> straight back into the mic.
>
> In the live demo the AEC reference is the previously monitored output frame,
> so `--aec --no-monitor` has nothing to cancel. The CLI warns about this.

### Measured results

All figures below are reproducible via `make demo` on the generated fixtures
(3 s, 48 kHz mono).

Codec, `speech.wav`:

| Target bitrate | Encoded size | Effective | Compression vs PCM |
| --- | --- | --- | --- |
| 8 kbps | 3 393 B | 9.0 kbps | 84.9 : 1 |
| 16 kbps | 6 372 B | 17.0 kbps | 45.2 : 1 |
| 24 kbps | 8 952 B | 23.9 kbps | 32.2 : 1 |
| 64 kbps | 24 150 B | 64.4 kbps | 11.9 : 1 |

Processing stages:

| Command | Input | Output | Result |
| --- | --- | --- | --- |
| `aec` on `mic_echo.wav` | -14.6 dBFS | -22.1 dBFS | **ERLE 7.5 dB** |
| `agc` on `quiet.wav` | -45.0 dBFS | -28.1 dBFS | **+16.9 dB** |
| `ans` on white noise | -26.0 dBFS | -32.5 dBFS | **-6.5 dB** noise |
| `ans` on speech | — | — | **< 1 dB** loss (speech preserved) |

Full pipeline (`--aec --codec`): ERLE 6.8 dB, 8 645 B at 23.1 kbps, 33.3 : 1.

Enabling `--fec true --loss-pct 20` grows the same clip from 8 952 → 9 166
bytes: that is the redundancy being paid for.

---

## Gotchas that cost real time

These are documented because each one produces *plausible but wrong* results
rather than an obvious failure. All are covered by regression tests.

- **speexdsp's ANS is a stationary-noise estimator.** A constant sine wave is
  classified as noise and attenuated ~7 dB, while speech-like audio loses
  < 1 dB. Validating noise suppression with a pure tone is meaningless — use a
  non-stationary signal (`aca::test::make_speech_like()`).
- **AGC needs seconds to ramp.** Roughly +17 dB after 3 s and +26 dB after 5 s
  on a -48 dBFS input. Test it with a 200 ms clip and it looks broken.
- **`--suppress-db` must be negative.** Passing a positive number to speexdsp
  gives *less* suppression, not more. The wrapper normalises the sign.
- **Opus rejects 44.1 kHz** and 15/30 ms frames, with an explicit error rather
  than silent resampling.
- **AEC needs a reference signal**, not just the mic.
- **Destruction order matters**: a `SpeexPreprocessState` wired to an echo state
  via `SPEEX_PREPROCESS_SET_ECHO_STATE` dereferences that echo state while
  tearing down, so the preprocessor must be destroyed **first**.
- speexdsp prints `warning: The VAD has been replaced by a hack...` to stderr
  on every init. Harmless library noise.

### The `.opus-frames` container

Raw Opus packets are not self-delimiting, so encoded output needs framing.
Rather than pull in libogg, this project defines a minimal length-prefixed
format with magic `ACA1`: a header (sample rate, channels, frame ms, frame
count) followed by `uint16` length + payload per packet.

It is **not** an `.opus`/Ogg file and only `aca decode` reads it. Feeding it a
WAV produces a clear error rather than garbage.

---

## Project layout

```
include/aca/     public headers (namespace aca)
src/core/        AudioBuffer, WAV I/O, metrics (RMS/peak/SNR/ERLE), container
src/codec/       Opus encode/decode wrappers
src/dsp/         AEC, ANS, AGC, and the shared CaptureChain
src/live/        PortAudio duplex session (compiled only if PortAudio found)
src/cli/         hand-rolled arg parser + subcommands
tests/           62 GoogleTest cases
tools/           deterministic fixture generator
```

`CaptureChain` (`src/dsp/pipeline.cpp`) is the single streaming implementation
shared by both the offline `pipeline` command and the live audio callback — new
stages belong there, not in two places.

Audio is interleaved **int16** throughout: both speexdsp and the Opus
fixed-point path are int16-native, so converting to float would only add
rounding noise.

Development notes for AI coding agents live in [`AGENTS.md`](AGENTS.md).

### Testing

```sh
make test                                    # all 62 tests
./build/aca_tests --gtest_filter='Aec.*'     # one suite
cd build && ctest -R Agc.RaisesQuietSignal   # one case
make asan                                    # ASan + UBSan (currently clean)
make tsan                                    # ThreadSanitizer (separate round)
make fetch-real                              # tiny real-speech WAVs -> assets/real/
```

`make fixtures` keeps CI reproducible (synthetic). `make fetch-real` pulls one
Microsoft AEC-Challenge synthetic clip plus derived noisy/quiet files for
manual codec/3A listening tests; see `assets/real/ATTRIBUTION.txt`.


Tests assert on **energy, levels and relative deltas**, never sample-exact
output: Opus is lossy and adds delay, and AEC/AGC adapt over time.

---

## References

### Standards

- **RFC 6716** — *Definition of the Opus Audio Codec* (Valin, Vos, Terriberry,
  2012). <https://www.rfc-editor.org/rfc/rfc6716>
- **RFC 8251** — *Updates to the Opus Audio Codec*.
  <https://www.rfc-editor.org/rfc/rfc8251>
- **RFC 7587** — *RTP Payload Format for the Opus Speech and Audio Codec*.
  <https://www.rfc-editor.org/rfc/rfc7587>
- **RFC 3550** — *RTP: A Transport Protocol for Real-Time Applications*.
  <https://www.rfc-editor.org/rfc/rfc3550>
- **RFC 6464** — *An RTP Header Extension for Client-to-Mixer Audio Level
  Indication*. <https://www.rfc-editor.org/rfc/rfc6464>
- ITU-T **G.167** (acoustic echo controllers), **P.340** (hands-free terminal
  characteristics) — the standards that define terms like ERLE and TCLW.
- ITU-T **P.862 (PESQ)** and **P.863 (POLQA)** — objective speech-quality
  metrics, the rigorous alternative to the simple energy metrics used here.

### Library documentation

- libopus API and docs — <https://opus-codec.org/docs/>
- Opus encoder/decoder CTL reference —
  <https://opus-codec.org/docs/opus_api-1.5/group__opus__encoderctls.html>
- speexdsp manual (AEC, preprocessor) — <https://www.speex.org/docs/>
- Jean-Marc Valin, *The Speex Codec Manual* — the appendix on the echo canceller
  and preprocessor documents exactly the behaviour this project relies on.
- PortAudio docs — <https://www.portaudio.com/docs.html>
- libsndfile API — <https://libsndfile.github.io/libsndfile/api.html>

### Papers

- J.-M. Valin, K. Vos, T. Terriberry, *High-Quality, Low-Delay Music Coding in
  the Opus Codec* (AES 135, 2013). <https://arxiv.org/abs/1602.04845>
- J.-M. Valin, *On Adjusting the Learning Rate in Frequency Domain Echo
  Cancellation With Double-Talk* (IEEE TASLP, 2007) — the MDF algorithm behind
  `speex_echo.h`. <https://arxiv.org/abs/0903.1156>
- Y. Ephraim & D. Malah, *Speech Enhancement Using a Minimum Mean-Square Error
  Short-Time Spectral Amplitude Estimator* (IEEE TASSP, 1984) — the classic
  MMSE-STSA basis for speexdsp's denoiser.
- S. Haykin, *Adaptive Filter Theory* — LMS/NLMS/frequency-domain adaptive
  filtering, the theory under any AEC.

### Books

- Eberhard Hänsler & Gerhard Schmidt, *Acoustic Echo and Noise Control: A
  Practical Approach* (Wiley, 2004) — the standard reference for AEC/ANS.
- Philipos C. Loizou, *Speech Enhancement: Theory and Practice*, 2nd ed. (CRC,
  2013) — noise suppression in depth, including evaluation methodology.
- Jacob Benesty et al., *Springer Handbook of Speech Processing* (2008).
- Udo Zölzer, *DAFX: Digital Audio Effects*, 2nd ed. (Wiley, 2011) — dynamics
  processing, i.e. the theory behind AGC/compressors.
- Alan Oppenheim & Ronald Schafer, *Discrete-Time Signal Processing*, 3rd ed.
- Marina Bosi & Richard Goldberg, *Introduction to Digital Audio Coding and
  Standards* (Springer, 2003) — perceptual coding fundamentals.
- Alan Johnston & Daniel Burnett, *WebRTC: APIs and RTCWEB Protocols* — where
  this pipeline sits in a real browser stack.

### Further reading

- WebRTC Audio Processing Module (AEC3, NS, AGC2) — the production-grade
  alternative to speexdsp.
  <https://webrtc.googlesource.com/src/+/main/modules/audio_processing/>
- Xiph.Org, *Opus Comparison* — codec quality vs bitrate.
  <https://opus-codec.org/comparison/>
- Mozilla Hacks, *Opus 1.1 / 1.3 release notes* — practical notes on DTX, FEC
  and complexity trade-offs. <https://hacks.mozilla.org/category/opus/>

---

## License

No license file yet — add one before publishing.
