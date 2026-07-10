#!/usr/bin/env bash
# Audit port/net forward-sim mutators for rollback-semantics gates.
# Snapshot apply/load paths are exempt (they run only during rollback load/resim).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail=0

check_fn_has_gate() {
	local file="$1"
	local fn="$2"
	local body
	body="$(awk "/${fn}\\(/,/^}/" "$file" 2>/dev/null || true)"
	if [[ -z "$body" ]]; then
		echo "WARN: could not find function ${fn} in ${file}" >&2
		return 0
	fi
	if echo "$body" | rg -q 'syNetplayRollback(LiveForwardSimEligible|SemanticsActive)'; then
		return 0
	fi
	echo "FAIL: ${file}:${fn} missing syNetplayRollbackSemanticsActive/LiveForwardSimEligible gate"
	fail=1
}

# Live forward reconcile / catch-up entry points (must gate).
check_fn_has_gate port/net/sys/netrollbacksnapshot.c syNetRbSnapReconcileGuardShieldEffectsLive
check_fn_has_gate port/net/sys/netrollbacksnapshot.c syNetRbSnapReconcileYoshiEggLayEffectsLive
check_fn_has_gate port/net/sys/netplay_ness_pkthunder_gate.c syNetplayNessRunLiveJibakuCatchUpAll
check_fn_has_gate port/net/sys/netrollbacksnapshot.c syNetRbSnapTryAdoptLiveYoshiShieldForEscapeEnd

# AfterBattleUpdate must use live-forward eligibility for netmenu hooks.
if ! rg -q 'syNetplayRollbackLiveForwardSimEligible' port/net/sys/netrollback.c; then
	echo "FAIL: port/net/sys/netrollback.c missing syNetplayRollbackLiveForwardSimEligible usage"
	fail=1
fi

# Sector Z arwing rollback stepping must use semantics, not syNetRollbackIsActive alone.
if rg -q 'syNetRollbackIsActive\(\)' decomp/src/gr/grcommon/grsector.c; then
	echo "FAIL: grsector.c still uses syNetRollbackIsActive for rollback forward policy"
	fail=1
fi

if [[ "$fail" -ne 0 ]]; then
	exit 1
fi

echo "check_netplay_rollback_gates: OK"
