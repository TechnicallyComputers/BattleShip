#!/usr/bin/env bash
# Paired-host rollback regression env presets (run two BattleShip instances manually or via your harness).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build/BattleShip"

if [[ ! -x "${BIN}" ]]; then
  echo "Build BattleShip first: cmake --build ${ROOT}/build --target ssb64 -j 4" >&2
  exit 1
fi

export SSB64_NETPLAY_ROLLBACK=1
export SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0
export SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1
export SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY=1
export SSB64_NETPLAY_INPUT_PREDICTION=1

case "${1:-help}" in
  forced-mismatch)
    export SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1
  ;;
  synctest)
    export SSB64_NETPLAY_ROLLBACK_SYNCTEST=1
  ;;
  prediction-storm)
    export SSB64_NETPLAY_INPUT_PREDICTION=1
    export SSB64_NETPLAY_PHASE_LOCK_PREDICTION_WINDOW=8
  ;;
  help|*)
    cat <<EOF
Usage: $0 <preset>

Presets:
  forced-mismatch   — inject published/remote divergence (debug)
  synctest          — periodic load/verify synctest logs
  prediction-storm  — wide prediction window stress

Pass/fail: no LOAD_HASH_DRIFT, ROLLBACK_IDENTITY_DRIFT, REMOTE_CONFIRMED_CONFLICT, or rollback storms in logs.
Launch: ${BIN} (from a directory containing BattleShip.o2r assets)
EOF
    exit 0
  ;;
esac

echo "Preset active: ${1:-help}"
echo "Binary: ${BIN}"
exec "${BIN}" "${@:2}"
