#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AURORA_BIN="$ROOT_DIR/build/src/aurora_cli/aurora"
OUT_ROOT="$ROOT_DIR/out/m4_test_runs"

if [[ ! -x "$AURORA_BIN" ]]; then
  echo "error: missing binary at $AURORA_BIN"
  echo "build first: cmake --build build -j4"
  exit 1
fi

hash_file() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print $1}'
  else
    shasum -a 256 "$f" | awk '{print $1}'
  fi
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
  echo "[M4] render: $file"
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/m4_${name}.log 2>&1
}

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

render_ok "cv_modules" "tests/m4_cv_modules.au"
render_ok "ring_mod" "tests/m4_ring_mod.au"
render_ok "ring_mod_variant" "tests/m4_ring_mod_variant.au"
render_ok "softclip" "tests/m4_softclip.au"
render_ok "audio_mix" "tests/m4_audio_mix.au"
render_ok "filter_vca_options" "tests/m4_filter_vca_options.au"
render_ok "vca_curve_variants" "tests/m4_vca_curve_variants.au"
render_ok "filter_drive_positions" "tests/m4_filter_drive_positions.au"
render_ok "filter_slope" "tests/m4_filter_slope.au"
render_ok "filter_keytrack" "tests/m4_filter_keytrack.au"
render_ok "filter_env_amount" "tests/m4_filter_env_amount.au"
render_ok "archetype_bank" "tests/m4_archetype_bank.au"

# Cross-phase regression.
render_ok "m3_regression" "tests/m3_rate_modes.au"

# Determinism check on M4 fixture.
DET_A="$OUT_ROOT/determinism_a"
DET_B="$OUT_ROOT/determinism_b"
SEED=77031
"$AURORA_BIN" render "$ROOT_DIR/tests/m4_cv_modules.au" --seed "$SEED" --out "$DET_A" >/tmp/m4_det_a.log 2>&1
"$AURORA_BIN" render "$ROOT_DIR/tests/m4_cv_modules.au" --seed "$SEED" --out "$DET_B" >/tmp/m4_det_b.log 2>&1

HASH_A="$(hash_file "$DET_A/mix/master.wav")"
HASH_B="$(hash_file "$DET_B/mix/master.wav")"
if [[ "$HASH_A" != "$HASH_B" ]]; then
  echo "error: M4 determinism hash mismatch"
  echo "a=$HASH_A"
  echo "b=$HASH_B"
  exit 1
fi

RING_STEM="$OUT_ROOT/ring_mod/stems/ring_voice.wav"
if [[ ! -f "$RING_STEM" ]]; then
  echo "error: missing ring-mod stem: $RING_STEM"
  cat /tmp/m4_ring_mod.log
  exit 1
fi
assert_non_silent_data "$RING_STEM"

RING_VAR_STEM="$OUT_ROOT/ring_mod_variant/stems/ring_variant.wav"
if [[ ! -f "$RING_VAR_STEM" ]]; then
  echo "error: missing ring-mod variant stem: $RING_VAR_STEM"
  cat /tmp/m4_ring_mod_variant.log
  exit 1
fi
assert_non_silent_data "$RING_VAR_STEM"

CLIP_STEM="$OUT_ROOT/softclip/stems/clip_voice.wav"
if [[ ! -f "$CLIP_STEM" ]]; then
  echo "error: missing softclip stem: $CLIP_STEM"
  cat /tmp/m4_softclip.log
  exit 1
fi
assert_non_silent_data "$CLIP_STEM"

MIX_STEM="$OUT_ROOT/audio_mix/stems/mix_util.wav"
if [[ ! -f "$MIX_STEM" ]]; then
  echo "error: missing audio-mix stem: $MIX_STEM"
  cat /tmp/m4_audio_mix.log
  exit 1
fi
assert_non_silent_data "$MIX_STEM"

DRIVE_LOW="$OUT_ROOT/filter_vca_options/stems/filter_drive_low.wav"
DRIVE_HIGH="$OUT_ROOT/filter_vca_options/stems/filter_drive_high.wav"
VCA_EXP="$OUT_ROOT/filter_vca_options/stems/vca_exp.wav"
for stem in "$DRIVE_LOW" "$DRIVE_HIGH" "$VCA_EXP"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing M4.C stem: $stem"
    cat /tmp/m4_filter_vca_options.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done

HASH_DRIVE_LOW="$(hash_file "$DRIVE_LOW")"
HASH_DRIVE_HIGH="$(hash_file "$DRIVE_HIGH")"
HASH_VCA_EXP="$(hash_file "$VCA_EXP")"
if [[ "$HASH_DRIVE_LOW" == "$HASH_DRIVE_HIGH" ]]; then
  echo "error: expected filter drive variants to differ, hashes matched"
  exit 1
fi
if [[ "$HASH_DRIVE_LOW" == "$HASH_VCA_EXP" ]]; then
  echo "error: expected VCA exp curve stem to differ from linear stem, hashes matched"
  exit 1
fi

VCA_LINEAR="$OUT_ROOT/vca_curve_variants/stems/vca_linear.wav"
VCA_EXP_AMT="$OUT_ROOT/vca_curve_variants/stems/vca_exp_amt.wav"
VCA_LOG_AMT="$OUT_ROOT/vca_curve_variants/stems/vca_log_amt.wav"
for stem in "$VCA_LINEAR" "$VCA_EXP_AMT" "$VCA_LOG_AMT"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing VCA curve stem: $stem"
    cat /tmp/m4_vca_curve_variants.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done

HASH_VCA_LINEAR="$(hash_file "$VCA_LINEAR")"
HASH_VCA_EXP_AMT="$(hash_file "$VCA_EXP_AMT")"
HASH_VCA_LOG_AMT="$(hash_file "$VCA_LOG_AMT")"
if [[ "$HASH_VCA_LINEAR" == "$HASH_VCA_EXP_AMT" || "$HASH_VCA_LINEAR" == "$HASH_VCA_LOG_AMT" || "$HASH_VCA_EXP_AMT" == "$HASH_VCA_LOG_AMT" ]]; then
  echo "error: expected VCA curve variants to produce distinct stems"
  exit 1
fi

DRIVE_PRE="$OUT_ROOT/filter_drive_positions/stems/drive_pre.wav"
DRIVE_POST="$OUT_ROOT/filter_drive_positions/stems/drive_post.wav"
for stem in "$DRIVE_PRE" "$DRIVE_POST"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing filter drive position stem: $stem"
    cat /tmp/m4_filter_drive_positions.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done
HASH_DRIVE_PRE="$(hash_file "$DRIVE_PRE")"
HASH_DRIVE_POST="$(hash_file "$DRIVE_POST")"
if [[ "$HASH_DRIVE_PRE" == "$HASH_DRIVE_POST" ]]; then
  echo "error: expected pre/post filter drive stems to differ, hashes matched"
  exit 1
fi

SLOPE12="$OUT_ROOT/filter_slope/stems/slope12.wav"
SLOPE24="$OUT_ROOT/filter_slope/stems/slope24.wav"
for stem in "$SLOPE12" "$SLOPE24"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing filter slope stem: $stem"
    cat /tmp/m4_filter_slope.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done
HASH_SLOPE12="$(hash_file "$SLOPE12")"
HASH_SLOPE24="$(hash_file "$SLOPE24")"
if [[ "$HASH_SLOPE12" == "$HASH_SLOPE24" ]]; then
  echo "error: expected slope12/slope24 stems to differ, hashes matched"
  exit 1
fi

KEYTRACK_OFF="$OUT_ROOT/filter_keytrack/stems/keytrack_off.wav"
KEYTRACK_ON="$OUT_ROOT/filter_keytrack/stems/keytrack_on.wav"
for stem in "$KEYTRACK_OFF" "$KEYTRACK_ON"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing filter keytrack stem: $stem"
    cat /tmp/m4_filter_keytrack.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done
HASH_KEYTRACK_OFF="$(hash_file "$KEYTRACK_OFF")"
HASH_KEYTRACK_ON="$(hash_file "$KEYTRACK_ON")"
if [[ "$HASH_KEYTRACK_OFF" == "$HASH_KEYTRACK_ON" ]]; then
  echo "error: expected keytrack off/on stems to differ, hashes matched"
  exit 1
fi

ENV_AMT_OFF="$OUT_ROOT/filter_env_amount/stems/env_amt_off.wav"
ENV_AMT_ON="$OUT_ROOT/filter_env_amount/stems/env_amt_on.wav"
for stem in "$ENV_AMT_OFF" "$ENV_AMT_ON"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing filter env amount stem: $stem"
    cat /tmp/m4_filter_env_amount.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done
HASH_ENV_AMT_OFF="$(hash_file "$ENV_AMT_OFF")"
HASH_ENV_AMT_ON="$(hash_file "$ENV_AMT_ON")"
if [[ "$HASH_ENV_AMT_OFF" == "$HASH_ENV_AMT_ON" ]]; then
  echo "error: expected env_amt off/on stems to differ, hashes matched"
  exit 1
fi

ARCH_RING="$OUT_ROOT/archetype_bank/stems/arch_ring_perc.wav"
ARCH_SH="$OUT_ROOT/archetype_bank/stems/arch_sh_motion.wav"
ARCH_LOGIC="$OUT_ROOT/archetype_bank/stems/arch_logic_gate.wav"
for stem in "$ARCH_RING" "$ARCH_SH" "$ARCH_LOGIC"; do
  if [[ ! -f "$stem" ]]; then
    echo "error: missing M4 archetype stem: $stem"
    cat /tmp/m4_archetype_bank.log
    exit 1
  fi
  assert_non_silent_data "$stem"
done

echo "[M4] all tests passed"
echo "[M4] determinism hash: $HASH_A"
