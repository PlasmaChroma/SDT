#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AURORA_BIN="$ROOT_DIR/build/src/aurora_cli/aurora"
OUT_ROOT="$ROOT_DIR/out/fx_test_runs"

if [[ ! -x "$AURORA_BIN" ]]; then
  echo "error: missing binary at $AURORA_BIN"
  echo "build first: cmake --build build -j4"
  exit 1
fi

wav_channels() {
  local wav="$1"
  od -An -j22 -N2 -tu2 "$wav" | tr -d '[:space:]'
}

assert_non_silent_data() {
  local wav="$1"
  local hex
  hex="$(od -An -j44 -N32768 -tx1 "$wav" | tr -d '[:space:]')"
  if [[ -z "$hex" || "$hex" =~ ^0+$ ]]; then
    echo "error: expected non-silent audio data in $wav"
    exit 1
  fi
}

render_ok() {
  local name="$1"
  local file="$2"
  local out="$OUT_ROOT/$name"
  echo "[FX] render: $file"
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/fx_${name}.log 2>&1
}

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

render_ok "stereo_bus" "tests/fx_stereo_bus.au"

FX_WAV="$OUT_ROOT/stereo_bus/stems/fx_stereo.wav"
MASTER_WAV="$OUT_ROOT/stereo_bus/mix/master.wav"

if [[ ! -f "$FX_WAV" ]]; then
  echo "error: missing FX stem: $FX_WAV"
  cat /tmp/fx_stereo_bus.log
  exit 1
fi
if [[ ! -f "$MASTER_WAV" ]]; then
  echo "error: missing master file: $MASTER_WAV"
  cat /tmp/fx_stereo_bus.log
  exit 1
fi

FX_CH="$(wav_channels "$FX_WAV")"
MASTER_CH="$(wav_channels "$MASTER_WAV")"
if [[ "$FX_CH" != "2" ]]; then
  echo "error: expected stereo FX stem, got channels=$FX_CH"
  exit 1
fi
if [[ "$MASTER_CH" != "2" ]]; then
  echo "error: expected stereo master, got channels=$MASTER_CH"
  exit 1
fi

assert_non_silent_data "$FX_WAV"

echo "[FX] all tests passed"
