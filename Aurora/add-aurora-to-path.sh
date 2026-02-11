#!/usr/bin/env bash

# Temporarily adds Aurora CLI build output directory to PATH for the current shell.
# Usage:
#   source ./add-aurora-to-path.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AURORA_BIN_DIR="${SCRIPT_DIR}/build-linux/src/aurora_cli"

if [[ ! -d "${AURORA_BIN_DIR}" ]]; then
  echo "Aurora binary directory not found: ${AURORA_BIN_DIR}" >&2
  echo "Build first: cmake -S . -B build-linux && cmake --build build-linux -j" >&2
  return 1 2>/dev/null || exit 1
fi

# Require sourcing so PATH changes affect the current shell.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "Run this script with: source ./add-aurora-to-path.sh" >&2
  exit 1
fi

case ":${PATH:-}:" in
  *":${AURORA_BIN_DIR}:"*)
    echo "Aurora CLI path already present for this session."
    ;;
  *)
    export PATH="${AURORA_BIN_DIR}:${PATH}"
    echo "Added to PATH for this shell session: ${AURORA_BIN_DIR}"
    ;;
esac
