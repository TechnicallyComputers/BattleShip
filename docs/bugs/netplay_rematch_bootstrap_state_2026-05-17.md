# Netplay Rematch Bootstrap State

## Summary

After a successful automatch battle, returning to character select and starting another automatch could leave peers stuck in the staging / execution gate path. Logs showed the client entering a second VS session at the previous battle's local sim tick while the host never reached VS start for the same matchmaking session.

## Root Cause

Automatch bootstrap success flags were not cleared on normal VS teardown or at the beginning of a fresh bootstrap attempt. A client could therefore carry `START` / metadata state from the prior match into the next session and believe bootstrap had completed even though the host was still waiting for the new automatch offer exchange.

The staging scene also started `syNetPeerStartVSSession()` before netinput reset its VS-local tick, so rematch staging could run exec-sync diagnostics and gate state at the previous battle tick instead of tick zero.

## Fix

- Reset automatch bootstrap attempt state before every automatch bootstrap run.
- Clear bootstrap, staging rendezvous, input-bind, exec-sync, UDP-link, and peer-offer state during normal VS stop.
- Reset netinput before the automatch staging scene starts the peer VS session.

## Verification

Built `ssb64` successfully after the patch.
