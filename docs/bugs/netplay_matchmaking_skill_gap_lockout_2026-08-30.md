# "Wasn't discovering each other": forfeit-widened skill gap locked the 2-player queue

**Symptom:** after a rebuild + redeploy, the two clients queued and never matched. Both
logs healthy: queue POST 200, tickets live (`GET /v1/match/<ticket>` → 200 "queued"),
heartbeats OK. The guest's log was completely silent after "queued".

## Root cause (nothing to do with the rebuild)

Chain, fully verified against the live server DB (`/opt/battleship-server/battleship.db`):

1. The crash soak earlier the same day ended in the reconnect module's **first-ever
   posted match result**: `forfeit_timeout`, winner `1e764687` (linux), loser `44800042`
   (android), finalized 22:16 UTC.
2. Both players were in **placement** (8 placement games, high K): that single result
   moved them to mu **1674.2** and **1325.8** — a **348-point gap**.
3. The matcher's tier windows (`tier_interval_secs=15`) allow mu gaps of 50/100/200/400
   at 0/15/30/45 s of queue wait. A 348 gap pairs only after **45 s**. The players quit
   at ~25 s.
4. Client-side, the "still queued" log was verbose-gated, so the wait looked like a dead
   matchmaker.

Red herrings eliminated along the way: the `/v1/heartbeat` 404 (credential verify against
an unknown ticket — documented as auth-OK), the `/v1/match/<t>/ice` 404s (trickle before
match — tolerated by design), the checkout's stale May `battleship.db` (the debug server's
`.env` points DATABASE_URL at the live /opt db), and the server processes (the :9080 debug
instance is the LAN matchmaker; the /opt release instance binds 127.0.0.1:8899 only).

## Fixes

**Server (`BattleShip-Server`, unversioned checkout — note: this repo has no git!):**
lonely-queue relaxation in `matcher.rs`. When a searcher has waited
`lonely_pair_after_secs` (default 5, env `MATCH_LONELY_PAIR_AFTER_SECS`) and exactly ONE
distinct other player exists across its whole bucket chain, the skill window is skipped
(loosest RTT/loss caps still apply). Skill tiers exist to pick the best of several
candidates; with a population of two they only postpone the inevitable pairing.
**Requires a server restart to take effect** (in-memory queue drops — clients re-queue).

**Client:** the still-queued poll now logs unconditionally every ~15th poll with a running
count, so a long-but-alive wait is self-evident in any future log.

## Also worth knowing

- Instant rating reset if ever wanted (run against the live db):
  `sqlite3 /opt/battleship-server/battleship.db "UPDATE players SET mu=1500, phi=350, rating_display=1500;"`
- The forfeit posting itself worked as designed; whether a `forfeit_timeout` during a
  *crash* should carry full placement weight is a separate design question left open.
- `BattleShip-Server` at `~/Documents/GitHub/BattleShip-Server` is **not a git
  repository** — today's matcher change has no version control. Worth a `git init` +
  initial commit before it drifts further.
