#!/usr/bin/env bash
# Fetch a tiny public real-speech kit for manual codec / 3A demos.
#
# Source: Microsoft AEC-Challenge synthetic set (CC-BY-4.0 / upstream terms).
# One clip (~10 s, mono) covers speech + far-end + mic-with-echo; noisy/quiet
# are derived locally so ANS/AGC have matching material.
#
# Usage:
#   ./tools/fetch_real_assets.sh              # -> assets/real/ at 48 kHz
#   ./tools/fetch_real_assets.sh --rate 16000 # keep native rate
#   FILE_ID=100 ./tools/fetch_real_assets.sh  # pick another synthetic clip
#
# Requires: curl, sox (or ffmpeg as fallback for resample only).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/assets/real}"
RATE="${RATE:-48000}"
FILE_ID="${FILE_ID:-0}"
BASE_URL="https://media.githubusercontent.com/media/microsoft/AEC-Challenge/main/datasets/synthetic"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --rate) RATE="$2"; shift 2 ;;
    --file-id) FILE_ID="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

need() { command -v "$1" >/dev/null || { echo "missing dependency: $1" >&2; exit 1; }; }
need curl
need sox

mkdir -p "$OUT/_raw"

download() {
  local rel="$1" dest="$2"
  local url="$BASE_URL/$rel"
  echo "GET $url"
  curl -fsSL --retry 3 --retry-delay 1 -o "$dest" "$url"
}

# Paired synthetic scenario (same fileid): clean near-end, far-end ref, mic mix.
download "nearend_speech/nearend_speech_fileid_${FILE_ID}.wav" "$OUT/_raw/speech.wav"
download "farend_speech/farend_speech_fileid_${FILE_ID}.wav" "$OUT/_raw/far_end.wav"
download "nearend_mic_signal/nearend_mic_fileid_${FILE_ID}.wav" "$OUT/_raw/mic_echo.wav"

to_rate() {
  local src="$1" dst="$2"
  # Native synthetic rate is 16 kHz; project fixtures are usually 48 kHz.
  sox "$src" -r "$RATE" -b 16 -c 1 "$dst" rate -v
}

to_rate "$OUT/_raw/speech.wav" "$OUT/speech.wav"
to_rate "$OUT/_raw/far_end.wav" "$OUT/far_end.wav"
to_rate "$OUT/_raw/mic_echo.wav" "$OUT/mic_echo.wav"

# ANS: speech + low-level white noise (~-20 dB relative ballpark).
sox -m \
  "$OUT/speech.wav" \
  "|sox '$OUT/speech.wav' -p synth whitenoise vol 0.08" \
  "$OUT/noisy.wav" norm -1

# AGC: same utterance, strongly attenuated (needs seconds to ramp).
sox "$OUT/speech.wav" "$OUT/quiet.wav" vol -30dB

cat >"$OUT/ATTRIBUTION.txt" <<EOF
Real-speech demo assets for audio_codec_analyzer
================================================

Origin
  Microsoft AEC-Challenge synthetic dataset, fileid=${FILE_ID}
  https://github.com/microsoft/AEC-Challenge
  Paths:
    datasets/synthetic/nearend_speech/nearend_speech_fileid_${FILE_ID}.wav
    datasets/synthetic/farend_speech/farend_speech_fileid_${FILE_ID}.wav
    datasets/synthetic/nearend_mic_signal/nearend_mic_fileid_${FILE_ID}.wav

License
  See the AEC-Challenge repository. Upstream speech is largely LibriVox
  public-domain material; the challenge redistributes under its dataset terms
  (treat as CC-BY-4.0 style attribution for the packaged set).

Derived here (not from upstream)
  noisy.wav  — speech + synthesised white noise (for ANS)
  quiet.wav  — speech at -30 dB (for AGC)

Sample rate written: ${RATE} Hz (upstream native is 16000 Hz)

Example
  ./build/aca encode assets/real/speech.wav -o /tmp/s.opus-frames
  ./build/aca aec --mic assets/real/mic_echo.wav --ref assets/real/far_end.wav -o /tmp/aec.wav
  ./build/aca ans assets/real/noisy.wav -o /tmp/ans.wav
  ./build/aca agc assets/real/quiet.wav -o /tmp/agc.wav
  ./build/aca pipeline --mic assets/real/mic_echo.wav --ref assets/real/far_end.wav \\
      -o /tmp/pipe.wav --aec --codec
EOF

rm -rf "$OUT/_raw"

echo "wrote:"
ls -lh "$OUT"/*.wav "$OUT/ATTRIBUTION.txt"
soxi "$OUT/speech.wav" "$OUT/far_end.wav" "$OUT/mic_echo.wav" "$OUT/noisy.wav" "$OUT/quiet.wav"
