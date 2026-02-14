#!/usr/bin/env bash

# Temporarily adds Aurora CLI build output directory to PATH for the current shell.
# Usage:
#   source ./add-aurora-to-path.sh

set -u

is_sourced=0
if (return 0 2>/dev/null); then
  is_sourced=1
fi

if [[ ${is_sourced} -ne 1 ]]; then
  echo "Run this script with: source ./add-aurora-to-path.sh" >&2
  exit 1
fi

if [[ -n "${BASH_SOURCE:-}" ]]; then
  SCRIPT_PATH="${BASH_SOURCE[0]}"
elif [[ -n "${ZSH_VERSION:-}" ]]; then
  SCRIPT_PATH="${(%):-%N}"
else
  SCRIPT_PATH="./add-aurora-to-path.sh"
fi

SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"

CANDIDATE_DIRS=(
  "${SCRIPT_DIR}/build/src/aurora_cli"
  "${SCRIPT_DIR}/build-linux/src/aurora_cli"
)

AURORA_BIN_DIR=""
for dir in "${CANDIDATE_DIRS[@]}"; do
  if [[ -x "${dir}/aurora" ]]; then
    AURORA_BIN_DIR="${dir}"
    break
  fi
done

if [[ -z "${AURORA_BIN_DIR}" ]]; then
  echo "Aurora CLI binary not found." >&2
  echo "Checked:" >&2
  for dir in "${CANDIDATE_DIRS[@]}"; do
    echo "  - ${dir}/aurora" >&2
  done
  echo "Build first: cmake -S . -B build && cmake --build build -j" >&2
  return 1
fi

case ":${PATH:-}:" in
  *":${AURORA_BIN_DIR}:"*)
    echo "Aurora CLI path already present for this session: ${AURORA_BIN_DIR}"
    ;;
  *)
    export PATH="${AURORA_BIN_DIR}:${PATH:-}"
    echo "Added to PATH for this shell session: ${AURORA_BIN_DIR}"
    ;;
esac
